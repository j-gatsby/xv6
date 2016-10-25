#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"



// Interrupt descriptor table (shared by all CPUs)
struct gatedesc idt[256];
extern uint vectors[];		// in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;


// Called from main(). Sets up the 256 entries in the table idt.
void
tvinit(void)
{
	int i;

	// Interrupt i is handled by the code address in vectors[i].
	// Each entry point is different because the x86 does not
	// provide the trap number to the interrupt handler.
	// Using 256 different handlers is the only way to distinguish
	// the 256 different cases.
	for (i = 0; i < 256; i++)
		SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);

	// T_SYSCALL, the user system call trap is handled specially:
	// if specifies that the gate is of type 'trap' by passing a
	// value of 1 as the second argument. Trap gates do not clear
	// the IF flag, allowing other interrupts during the system
	// call handler.
	// The kernel also set the system call gate privilege to DPL_USER,
	// which allows a user program to generate the trap with an explicit
	// int instruction. xv6 doesn't allow processes to raise other
	// interrupts, such as device interrupts, with int; if they try,
	// they will encounter a general protection exception, which goes
	// to vector 13.
	SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

	initlock(&tickslock, "time");
}


void
idtinit(void)
{
	lidt(idt, sizeof(idt));
}

// Each handler sets up a trap frame and then calls trap(),
// which looks at the hardware trap number tf->trapno to
// decide why it has been called and what needs to be done.
void
trap(struct trapframe *tf)
{
	// If the trap is T_SYSCALL, call the system call
	// handler syscall().
	if (tf->trapno == T_SYSCALL)
	{
		if (proc->killed)
			exit();
		proc->tf = tf;
		syscall();
		if (proc->killed)
			exit();
		return;
	}

	// After checking for a system call, look for harware
	// interrupts
	switch(tf->trapno)
	{
		case T_IRQ0 + IRQ_TIMER:
				if (cpunum() == 0)
				{
					acquire(&tickslock);
					ticks++;
					wakeup(&ticks);
					release(&tickslock);
				}
				lapiceoi();
				break;

		case T_IRQ0 + IRQ_IDE:
				ideintr();
				lapiceoi();
				break;

		case T_IRQ0 + IRQ_IDE+1:
				// Bochs generates spurious IDE1 interrupts
				break;

		case T_IRQ0 + IRQ_KBD:
				kbdintr();
				lapiceoi();
				break;

		case T_IRQ0 + IRQ_COM1:
				uartintr();
				lapiceoi();
				break;

		// In addition to the expected hardware devices, a trap
		// can be caused by a spurious interrupt, an unwanted
		// hardware interrupt.
		case T_IRQ0 + 7:
		case T_IRQ0 + IRQ_SPURIOUS:
				cprintf("cpu%d: spurious interrupt at %x:%x\n",
						cpunum(), tf->cs, tf->eip);
				lapiceoi();
				break;

		// If the trap is not a system call, and not a hardware
		// device looking for attention, we assume it was caused
		// by incorrect behavior (e.g. divide by zero) as part of
		// the code that was executing before the trap.
		default:
				// If the kernel was running...
				if (proc == 0 || (tf->cs&3) == 0)
				{
					// In kernel, it must be our mistake.
					// There must be a kernel bug. Print the details
					// and then call panic().
					cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
							tf->trapno, cpunum(), tf->eip, rcr2());
					panic("trap");
				}

				// In user space, assume process misbehaved.
				// Print the details.
				cprintf("pid %d %s: trap %d err %d on cpu %d "
						"eip 0x%x addr 0x%x--kill proc\n",
						proc->pid, proc->name, tf->trapno, tf->err, cpunum(), tf->eip,
						rcr2());
				// Set to remember to clean up the user process.
				proc->killed = 1;
	}

	// Force process to exit if it has been killed and is in user space.
	// (If it is still executing in the kernel, let it keep running
	// until it gets to the regular system call return.)
	if (proc && proc->killed && (tf->cs&3) == DPL_USER)
		exit();

	// Force process to give up CPU on clock tick.
	// If interrupts were on while locks held, would need to check nlock.
	if (proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
		yield();

	// Check if the process has been killed since we yielded
	if (proc && proc->killed && (tf->cs&3) == DPL_USER)
		exit();
}
