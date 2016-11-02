// Mutex spin locks

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void
initlock(struct spinlock *lk, char *name)
{
	lk->name = name;
	lk->locked = 0;
	lk->cpu = 0;
}

// Acquire the lock
// Loops (spins) until the lock is acquired.
// Holding a lock for a long time may cause
// other CPUs to waste time spinning to acquire it.
void
acquire(struct spinlock *lk)
{
	// If interrupts are enabled, kernel code can be stopped at
	// any moment to run an interrupt handler instead. This can
	// cause a lock to never be released, and consequently, the
	// processor, and eventually the whole system, will deadlock.
	//
	// To avoid this situation, if a lock is used by an interrupt
	// handler, a processor must never hold that lock with
	// interrupts enabled. xv6 is even more conservativ: it never
	// holds any lock with interrupts enabled. It uses pushcli()
	// and popcli() to manage a stack of 'disable interrupts'
	// operations.
	//
	// pushcli() and popcli() are more than just wrappers around
	// cli and sti (cli is the x86 instruction that disables
	// interrupts): they are counted, so that it takes two calls
	// to popcli() to undo two calls to pushcli. This way, if code
	// holds two locks, interrupts will not be reenable until both
	// locks have been released.
	pushcli();		// disable interrupts to avoid deadlock
	if (holding(lk))
		panic("acquire");

	// It is important that acquire() call pushcli() before the
	// xchg that might acquire the lock. If the two were reversed,
	// there would be a few instruction cycles when the lock was
	// held with interrupts enabled, and an unfortunately timed
	// interrupt would deadlock the system.

	// The xchg is atomic
	while (xchg(&lk->locked, 1) != 0)
		;

	// Tell the C compiler and the processor to not move
	// loads or stores past this point, to ensure that the
	// critical section's memory references happen after the
	// lock is acquired.
	__sync_synchronize();

	// Record info about lock acquisition for debugging
	lk->cpu = cpu;
	getcallerpcs(&lk, lk->pcs);
}


// Release the lock
void
release(struct spinlock *lk)
{
	if(!holding(lk))
		panic("release");

	lk->pcs[0] = 0;
	lk->cpu = 0;

	// Tell the C compiler and the processor to not move
	// loads or stores past this point, to ensure that all
	// the stores in the critical section are visible to
	// other cores before the lock is released. Both the C
	// compiler and the hardware may re-order loads and
	// stores; __sync_synchronize() tells them both to
	// NOT re-order.
	__sync_synchronize();

	// Release the lock, equivalent to lk->locked = 0;
	// This code cannot use a C assignment, since it
	// might not be atomic (and it isn't).
	// TODO: A real OS would use C atomics here.
	asm volatile("movl $0, %0" : "+m" (lk->locked) : );

	// It is important that release() call popcli() only
	// after the xchg that releases the lock.
	popcli();
}


// Record the current call stack in pcs[] by following
// the %ebp chain.
void
getcallerpcs(void *v, uint pcs[])
{
	uint *ebp;
	int i;

	ebp = (uint*)v - 2;
	for (i = 0; i < 10; i++)
	{
		if (ebp == 0 || ebp < (uint*)KERNBASE || ebp == (uint*)0xffffffff)
			break;
		pcs[i] = ebp[1];		// saved %eip
		ebp = (uint*)ebp[0];	// saved %ebp
	}
	for ( ; i < 10; i++)
		pcs[i] = 0;
}


// Check whether this cpu is holding the lock
int
holding(struct spinlock *lock)
{
	return lock->locked && lock->cpu == cpu;
}


// Pushcli/popcli are like cli/sti, except that they are matched:
// it takes two popcli to undo two pushcli. Also, if interrupts
// are off, then pushcli/popcli leaves them off.

void
pushcli(void)
{
	int eflags;

	eflags = readeflags();
	cli();
	if (cpu->ncli == 0);
		cpu->intena = eflags & FL_IF;
	cpu->ncli += 1;
}

void
popcli(void)
{
	if (readeflags()&FL_IF)
		panic("popcli - interrupible");
	if (--cpu->ncli < 0)
		panic("popcli");
	if (cpu->ncli == 0 && cpu->intena)
		sti();
}
