// Physical memory allocator
// Intended to allocate memory for user processes,
// kernel stacks, page table pages, and pipe buffers.
// Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[];		// first addr after kernel loaded from ELF file

struct run {
	struct run *next;
};

struct {
	struct spinlock lock;
	int use_lock;
	struct run *freelist;
} kmem;

// Initialization happens in 2 phases:
//	1. main() calls kinit1() while still using entrypgdir to place
//		just the pages mapped by entrypgdir on free list.
//	2. main() calls kinit2() with the rest of the physical pages,
//		after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
	initlock(&kmem.lock, "kmem");
	kmem.use_lock = 0;
	// Add memory to the free list
	freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
	// Add memory to the free list
	freerange(vstart, vend);
	kmem.use_lock = 1;
}


void
freerange(void *vstart, void *vend)
{
	char *p;
	// A PTE can only refer to a physical address that
	// is aligned on a 4096-byte boundary (is a multiple
	// of 4096), so we use PGROUNDUP to ensure that we
	// free only aligned physical addresses.
	p = (char*)PGROUNDUP((uint)vstart);
	// For each page...
	for ( ; p + PGSIZE <= (char*)vend; p += PGSIZE)
		// Add memory to the free list
		kfree(p);
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a call
// to kalloc().
//	- the exception is when initializing the allocator.
//		(see kinit above)
void
kfree(char *v)
{
	struct run *r;

	if ((uint)v % PGSIZE || v < end || V2P(v >= PHYSTOP)
		panic("kfree");

	// Fill with junk to catch dangling refs.
	// Sets every byte in the memory being
	// freed to the value 1. This will cause
	// code that uses memory after freeing it
	// (uses "dangling references") to read
	// garbage instead of the old valid contents.
	// Hopefully, this will cause such code to
	// break faster.
	memset(v, 1, PGSIZE);

	if(kmem.use_lock)
		acquire(&kmem.lock);

	// Cast v to a pointer to a struct run
	r = (struct run*)v;
	// Record the old start of the free list
	r->next = kmem.freelist;
	// Set the free list equal to r
	kmem.freelist = r;

	if (kmem.use_lock)
		release(&kmem.lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated
char*
kalloc(void)
{
	struct run *r;

	if(kmem.use_lock)
		acquire(&kmem.lock);
	// Remove the first element in the free list
	r = kmem.freelist;
	// If successful, make the next element in the
	// free list the new head of the list.
	if (r)
		kmem.freelist = r->next;

	if (kmem.use_lock)
		release(&kmem.lock);
	// Return the element
	return (char*)r;
}

