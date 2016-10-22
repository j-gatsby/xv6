#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void) __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[];	// first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it after
// doing some setup required for memory allocators to work.
int
main(void)
{
	// Initialize the physical page allocator, at least partially.
	// Right now main() cannot use locks or memory above 4Mb.
	// Set up lock-less allocation in the first 4Mb.
	kinit1(end, P2V(4*1024*1024));

	// The page table created by 'entry' has enough mappings to
	// allow the kernel's C code to start running. However,
	// we now immediately change to a new kernel page table,
	// in order to carry out a more elaborate plan for
	// describing process address space.
	kvmalloc();

	mpinit();			// detect other processors
	lapicinit();		// interrupt controller
	seginit();			// segment descriptors
	cprintf("\ncpu%d: starting xv6\n\n", cpunum());
	picinit();			// another interrupt controller
	ioapicinit();		// another interrupt controller
	consoleinit();		// console hardware
	uartinit();			// serial port
	pinit();			// process table
	tvinit();			// trap vectors
	binit();			// buffer cache
	fileinit();			// file table
	ideinit();			// disk
	if(!ismp)
		timerinit();	// uniprocessor timer
	startothers();		// start other processors

	// main() can now use locks and memory above 4Mb.

	// Enable locking and arrange for more memory to be allocatable.
	// The physical allocator refers to physical pages by their
	// virtual addresses, as mapped in high memory, not by their
	// physical addresses, so P2V is used to translate PHYSTOP
	// (a physical address) to a virtual address.
	kinit2(P2V(4*1024*1024), P2V(PHYSTOP));		// must come after startothers()
	userinit();			// first user process
	mpmain();			// finish this processor's setup
}


// Other CPUs jump here from entryother.S
static void
mpenter(void)
{
	switchkvm();
	seginit();
	lapicinit();
	mpmain();
}


// Common CPU setup code
static void
mpmain(void)
{
	cprintf("cpu%d: starting\n", cpunum());
	idtinit();				// load idt register
	xchg(&cpu->started, 1);	// tell startothers() we are up
	scheduler();			// start running processes
}


pde_t entrypgdir[];			// for entry.S

// Start the non-boot (AP) processors
static void
startothers(void)
{
	extern uchar _binary_entryother_start[], _binary_entryother_size[];
	uchar *code;
	struct cpu *c;
	char *stack;

	// Write entry code to unused memory at 0x7000
	// The linker has placed the image of entryother.S
	// in _binary_entryother_start.
	code = P2V(0x7000);
	memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

	for (c = cpus; c < cpus+ncpu; c++)
	{
		if (c == cpus+cpunum())		// we have already started
			continue;

		// Tell entryother.S what stack to use, where to enter,
		// and what pgdir to use. We cannot use kpgdir yet, since
		// the AP processor is running in low memory, so we use
		// entrypgdir for the APs, too.
		stack = kalloc();
		*(void**)(code-4) = stack + KSTACKSIZE;
		*(void**)(code-8) = mpenter;
		*(int**)(code-12) = (void *) V2P(entrypgdir);

		lapicstartap(c->apicid, V2P(code));

		// Wait for cpu to finish mpmain()
		while(c->started == 0)
			;
	}
}


// The boot table used in entry.S and entryother.S
//	- page directories (and page tables) must start on
//		page boundaries, hence the __aligned__ attribute
//	- PTE_PS in a page directory entry enables 4Mbyte pages

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
	// Map VA's [0, 4MB) to PAs [0, 4MB)
	[0] = (0) | PTE_P | PTE_W | PTE_PS,
	// Map VA's [KERNBASE, KERNBASE+4MB) to PAs [0, 4MB)
	[KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};
