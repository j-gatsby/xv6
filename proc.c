#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


struct {
	struct spinlock lock;
	struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
	initlock(&ptable.lock, "ptable");
}


// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise, return 0.
// Must hold ptable.lock.
static struct proc*
allocproc(void)
{
	struct proc *p;
	char *sp;

	acquire(&ptable.lock);

	// Scan the proc table for a slot with state UNUSED
	// TODO:	a real os would find free proc structs with
	//			an explicit free list, in constant time,
	//			instead of a linear-time search like this.
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if (p->state == UNUSED)
			goto found;

	release(&ptable.lock);
	// If none found, return 0 to signal failure
	return 0;

found:
	// When an unused slot is found, set the state to EMBRYO
	p->state = EMBRYO;
	// Give the process a unique pid
	p->pid = nextpid++;

	release(&ptable.lock);

	// Allocate kernel stack for process' kernel thread
	if ((p->kstack = kalloc()) == 0)
	{
		// If allocation fails, change state back to UNUSED
		p->state = UNUSED;
		// Return 0 to signal failure
		return 0;
	}
	// Specially prepare kernel stack and set of kernel registers
	sp = p->kstack + KSTACKSIZE;

	// Leave room for trap frame
	sp -= sizeof *p->tf;
	p->tf = (struct trapframe*)sp;

	// Set up new context to start executing at forkret,
	// which returns to trapret.
	sp -= 4;
	*(uint*)sp = (uint)trapret;
	sp -= sizeof *p->context;
	p->context = (struct context*)sp;
	memset(p->context, 0, sizeof *p->context);
	p->context->eip = (uint)forkret;
	// One p->context is popped off, the top word on
	// the stack will be trapret, which will have
	// %esp set to p->tf

	return p;
}


// Set up first user process
void
userinit(void)
{
	struct proc *p;
	extern char _binary_initcode_start[], _binary_initcode_size[];

	// Allocate a slot in the process table and initialize the parts
	// of the process' state required for it's kernel thread to execute.
	p = allocproc();
	initproc = p;
	// Create a page table for the first process, initially with
	// mappings only for memory that the kernel uses.
	if ((p->pgdir = setupkvm()) == 0)
		panic("userinit: out of memory?");

	// The inital contents of the first process' user-space memory are the
	// compiled form of initcode.S; as part of the kernel build process, the
	// linker embeds that binary in the kernel and defines two special
	// symbols indicating the location and size of the binary:
	//	_binary_initcode_start and _binary_initcode_size

	// Copy that binary into the new process' memory by calling inituvm(),
	// which allocates one page of physical memory, maps virtual address
	// zero to that memory, and copies the binary to that page.
	inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
	p->sz = PGSIZE;

	// Set up the trap frame with the inital user mode state
	memset(p->tf, 0, sizeof(*p->tf));
	// Set up the low bits of %cs to run the process' user code
	// at CPL=3. Consequently, the user code can only use pages
	// with PTE_U set, and cannot modify sensitive hardware
	// registers, such as %cr3. This constrains the process to
	// using only it's own memory.
	p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
	// %ds, %es, and %ss use SEG_UDATA with privilege level DPL_USER
	p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
	p->tf->es = p->tf->ds;
	p->tf->ss = p->tf->ds;
	// %eflags FL_IF bit is set to allow hardware interrupts
	p->tf->eflags = FL_IF;
	// Set stack pointer to the process' largest valid virtual address
	p->tf->esp = PGSIZE;
	// Set instruction pointer to address zero
	p->tf->eip = 0;			// beginning of initcode.S

	// At this point, %eip holds 0, and %esp holds 4096 (PGSIZE).
	// These are the virtual addresses int the process' address space.
	// The processor's paging hardware translates them into physical
	// addresses. allocuvm() has set up the process' page table so
	// that virtual address 0 refers to the physical memory allocated
	// for this process, and set a flag (PTE_U) that tells the paging
	// hardware to allow user code to access that memory.

	// Set p->name to initcode, mainly for debugging
	safestrcpy(p->name, "initcode", sizeof(p->name));
	// Set the process' current working directory
	p->cwd = namei("/");

	// The following assignment to p->state (after acquire) lets
	// other cores run this process. The acquire forces the above
	// writes to be visible, and the lock is also needed because
	// the assignment might not be atomic.
	// TODO: if this is the same situation as elsewhere, a real OS
	//			would import and use an atomic C function.
	acquire(&ptable.lock);
	// The process is now initialized, so we
	// can now mark it available for scheduling.
	p->state = RUNNABLE;

	release(&ptable.lock);
}


// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
	uint sz;

	sz = proc->sz;
	if (n > 0)
	{
		if ((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
			return -1;
	}
	else if (n < 0)
	{
		if ((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
			return  -1;
	}

	proc->sz = sz;
	switchuvm(proc);
	return 0;
}


// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
	int i, pid;
	struct proc *np;

	// Allocate process
	if ((np = allocproc()) == 0)
	{
		return -1;
	}

	// Copy process state from p
	if ((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0)
	{
		kfree(np->kstack);
		np->kstack = 0;
		np->state = UNUSED;
		return -1;
	}

	np->sz = proc->sz;
	np->parent = proc;
	*np->tf = *proc->tf;

	// Clear %eax so that fork returns 0 in the child
	np->tf->eax = 0;

	for (i = 0; i < NOFILE; i++)
	{
		if (proc->ofile[i])
			np->ofile[i] = filedup(proc->ofile[i]);
	}
	np->cwd = idup(proc->cwd);

	safestrcpy(np->name, proc->name, sizeof(proc->name));

	pid = np->pid;

	acquire(&ptable.lock);

	np->state = RUNNABLE;

	release(&ptable.lock);

	return pid;
}


// Exit the current process. Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
	struct proc *p;
	int fd;

	if (proc == initproc)
		panic("init exiting");

	// Close all open files
	for (fd = 0; fd < NOFILE; fd++)
	{
		if (proc->ofile[fd])
		{
			fileclose(proc->ofile[fd]);
			proc->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(proc->cwd);
	end_op();
	proc->cwd = 0;

	acquire(&ptable.lock);

	// Parent might be sleeping in wait()
	wakeup1(proc->parent);

	// Pass abandoned children to init
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if (p->parent == proc)
		{
			p->parent = initproc;
			if (p->state == ZOMBIE)
				wakeup1(initproc);
		}
	}

	// Jump into the scheduler, never to return.
	proc->state = ZOMBIE;
	sched();
	panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
	struct proc *p;
	int havekids, pid;

	acquire(&ptable.lock);
	for ( ; ; )
	{
		// Scan through table looking for zombie children
		havekids = 0;
		for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		{
			if (p->parent != proc)
				continue;
			havekids = 1;
			if (p->state == ZOMBIE)
			{
				// Found one
				pid = p->pid;
				kfree(p->kstack);
				p->kstack = 0;
				freevm(p->pgdir);
				p->pid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED;
				release(&ptable.lock);
				return pid;
			}
		}

		// No point waiting if we don't have any children
		if (!havekids || proc->killed)
		{
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit
		//	(see wakeup1 call in proc_exit)
		sleep(proc, &ptable.lock);
	}
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns. It loops, doing:
// - choose a process to run
// - swtch() to start running that process
// - eventually that process transfers control via
//		swtch() back to the scheduler
void
scheduler(void)
{
	// Per-CPU variable
	struct proc *p;

	for ( ; ; )
	{
		// Enable interrupts on this processor
		sti();

		// Loop over process table looking for process to run;
		// one with p->state set to RUNNABLE. Initially there
		// is only one: initproc.
		// TODO:	although I have not seen anything about it,
		//			I would imagine that this should be more along
		//			the lines of an explicit free list. Something
		//			O(1), not O(n). At least it should be after
		//			the initial run.
		acquire(&ptable.lock);
		for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		{
			if (p->state != RUNNABLE)
				continue;

			// Switch to chosen process. It is the process's job
			// to release ptable.lock and then reacquire it
			// before jumping back to us.

			// Set proc to the process found
			proc = p;
			// Tell the hardware to start using the target
			// process' page table. Also set up a task state
			// segment, SEG_TSS, that instructs the hardware
			// to execute system calls and interrupts on the
			// process' kernel stack.
			switchuvm(p);

			// Set state, then perform a context switch to the target
			// process' kernel thread.
			p->state = RUNNING;
			// swtch() first saves the current registers. The current
			// context is not a process but rather a special per-cpu
			// scheduler context, so we save the current registers in
			// per-CPU storage (cpu->scheduler) rather than in any
			// process' kernel thread context.
			// swtch() then loads the saved registers of the target
			// kernel thread (p->context) into the x86 hardware
			// registers, including the stack and instruction pointers.
			// The final ret instruction pops the target process' %eip
			// from the stack, finishing the context switch.
			swtch(&cpu->scheduler, p->context);
			// Now the processor is running on the kernel stack of process p,
			// which will start executing forkret().

			// Switch hardware page table register to the kernel-only
			// page table, for when no process is running.
			switchkvm();

			// Process is done running for now.
			// It should have changed its p->state before coming back.
			proc = 0;
		}
		release(&ptable.lock);
	}
}


// Enter scheduler. Must hold only ptable.lock and have
// changed proc->state. Saves and restores intena because
// intena is a property of this kernel thread, not this CPU.
// It should be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but there
// is no process.
void
sched(void)
{
	int intena;

	if (!holding(&ptable.lock))
		panic("sched ptable.lock");
	if (cpu->ncli != 1)
		panic("sched locks");
	if (proc->state == RUNNING)
		panic("sched running");
	if (readeflags()&FL_IF)
		panic("sched interruptible");
	intena = cpu->intena;
	swtch(&proc->context, cpu->scheduler);
	cpu->intena = intena;
}


// Give up the CPU for one scheduling round
void
yield(void)
{
	acquire(&ptable.lock);
	proc->state = RUNNABLE;
	sched();
	release(&ptable.lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch() here. "Return" to user space.
void
forkret(void)
{
	static int first = 1;
	// Still holding ptable.lock from scheduler
	release(&ptable.lock);

	if (first)
	{
		// Some initialization functions must be run in the
		// context of a regular process (e.g. they call sleep),
		// with it's own kernel stack, and thus cannot be run
		// from main().
		first = 0;
		iinit(ROOTDEV);
		initlog(ROOTDEV);
	}
	// Return to "caller", actually trapret()
	// This is because allocproc() arranged that the
	// top word on the stack after p->context is popped
	// off would be 'trapret', so now trapret() begins
	// executing with %esp set to p->tf
	//	(see allocproc)
}


// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
	if (proc == 0)
		panic("sleep");

	if (lk == 0)
		panic("sleep without lk");

	// Must acquire ptable.lock in order to
	// change p->state and then call sched().
	// Once we hold ptable.lock, we can be
	// guaranteed that we won't miss any wakeup
	// (wakeup runs with ptable.lock locked),
	// so it is ok to release lk.
	if (lk != &ptable.lock)
	{
		acquire(&ptable.lock);
		release(lk);
	}

	// Go to sleep
	proc->chan = chan;
	proc->state = SLEEPING;
	sched();

	// Tidy up
	proc->chan = 0;

	// Reacquire original lock
	if (lk != &ptable.lock)
	{
		release(&ptable.lock);
		acquire(lk);
	}
}


// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
	struct proc *p;

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if (p->state == SLEEPING && p->chan == chan)
			p->state = RUNNABLE;
	}
}


// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}


// Kill the process with the given pid.
// Process won't exit until it returns
// to user space.
//	(see trap in trap.c)
int
kill(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if (p->pid == pid)
		{
			p->killed = 1;
			// Wake process from sleep, if necessary
			if (p->state == SLEEPING)
				p->state = RUNNABLE;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}


// Print a process listing to console. FOR DEBUGGING.
// Runs when a user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
	static char *states[] = {
	[UNUSED]	"unused",
	[EMBRYO]	"embryo",
	[SLEEPING]	"sleep ",
	[RUNNABLE]	"runble",
	[RUNNING]	"run   ",
	[ZOMBIE]	"zombie"
	};

	int i;
	struct proc *p;
	char *state;
	uint pc[10];

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
	{
		if (p->state == UNUSED)
			continue;
		if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
			state = states[p->state];
		else
			state = "???";
		cprintf("%d %s %s", p->pid, state, p->name);
		if (p->state == SLEEPING)
		{
			getcallerpcs((uint*)p->context->ebp+2, pc);
			for (i = 0; i < 10 && pc[i] != 0; i++)
				cprintf(" %p", pc[i]);
		}
		cprintf("\n");
	}
}
