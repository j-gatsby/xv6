// Simple PIO-based (non-DMA) IDE driver code
// The IDE device provides access to disks connected to the PC
// standard IDE controller. IDE is now falling out of fashion
// in favor of SCSI and SATA, but the interface is simple and
// lets us concentrate on the overall structure of a driver,
// instead of the details of a particular piece of hardware.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE		512
#define IDE_BSY			0x80
#define IDE_DRDY		0x40
#define IDE_DF			0x20
#define IDE_ERR			0x01

#define IDE_CMD_READ	0x20
#define IDE_CMD_WRITE	0x30
#define IDE_CMD_RDMUL	0xc4
#define IDE_CMD_WRMUL	0xc5

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating the queue.

static struct spinlock idelock;

// xv6 represents file system blocks using struct buf.
// BSIZE is identical to SECTOR_SIZE, and thus, each
// buffer represents the contents of one sector on a
// particular disk device.
static struct buf *idequeue;
// NOTE: although the xv6 file system chooses BSIZE to be identical
//			to the IDE's SECTOR_SIZE, the driver can handle a BSIZE
//			that is a multiple of SECTOR_SIZE. Real world operating
//			systems often use bigger blocks than 512 bytes to obtain
//			higher disk throughput.


static int havedisk1;
static void idestart(struct buf*);

// Wait for IDE disk to become ready
static int
idewait(int checkerr)
{
	int r;
	// A PC motherboard presents the status bits of the disk
	// hardware on I/O port 0x1f7.
	// Polls the status bits until the busy bit is clear and the
	// ready bit is set.
	while (((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
		;

	if (checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
		return -1;

	return 0;
}


void
ideinit(void)
{
	int i;

	initlock(&idelock, "ide");
	// Enable interrupts on a uniprocessor
	picenable(IRQ_IDE);
	// Enable interrupts on a multiprocessor, but only on the
	// last CPU (ncpu-1). On a two processor system, CPU 1
	// handles disk interrupts.
	ioapicenable(IRQ_IDE, ncpu - 1);

	// Probe the disk hardware.
	// Wait for the disk to be able to accept commands
	idewait(0);
	// Check if disk 1 is present.
	// We can assume that disk 0 is present because the boot
	// loader and the kernel were both loaded from disk 0.
	// Write to I/O port 0x1f6 to select disk 1.
	outb(0x1f6, 0xe0 | (1<<4));
	// Wait a while for the status bit to show
	// that the disk is ready.
	for (i = 0; i < 1000; i++)
	{
		// PC motherboard presents the status bits of the
		// disk hardware on I/O port 0x1f7
		if (inb(0x1f7) != 0)
		{
			havedisk1 = 1;
			break;
		}
	}
	// If havedisk1 != 1 at this point, we assume the disk is absent.

	// Switch back to disk 0
	outb(0x1f6, 0xe0 | (0<<4));
}


// Start the request for b.
// Caller must hold idelock.
static void
idestart(struct buf *b)
{
	if (b == 0)
		panic("idestart");
	if (b->blockno >= FSSIZE)
		panic("incorrect blockno");

	int sector_per_block = BSIZE/SECTOR_SIZE;
	int sector = b->blockno * sector_per_block;
	int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ : IDE_CMD_RDMUL;
	int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

	if (sector_per_block > 7) panic("idestart");

	idewait(0);
	outb(0x3f6, 0);					// generate interrupt
	outb(0x1f2, sector_per_block);	// number of sectors
	outb(0x1f3, sector & 0xff);
	outb(0x1f4, (sector >> 8) & 0xff);
	outb(0x1f5, (sector >> 16) & 0xff);
	outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | (sector>>24&0x0f));

	// Issue either a read or a write for the buffer's device
	// and sector according to the flags.

	// If the operation is a write...
	if (b->flags & B_DIRTY)
	{
		// supply the data now, and the interrupt will
		// signal that the data has been written to disk
		outb(0x1f7, write_cmd);
		outsl(0x1f0, b->data, BSIZE/4);
	}
	// If the operation is a read...
	else
	{
		// the interrupt will signal that the data is
		// ready, and the handler will read it.
		outb(0x1f7, read_cmd);
	}
}


// Interrupt handler
void
ideintr(void)
{
	struct buf *b;

	// First queued buffer is the active request.
	// Consult the first buffer in the queue to find
	// out which operation was happening.
	acquire(&idelock);
	if ((b = idequeue) == 0)
	{
		release(&idelock);
		// cprintf("spurious IDE interrupt\n");
		return;
	}
	idequeue = b->qnext;

	// Read data, if needed.
	// If the buffer was being read and the disk
	// controller has data waiting, read the data
	// into the buffer with insl.
	if (!(b->flags & B_DIRTY) && idewait(1) >= 0)
		insl(0x1f0, b->data, BSIZE/4);

	// Now the buffer is ready.
	// Wake process waiting for this buf.
	b->flags |= B_VALID;
	b->flags &= ~B_DIRTY;
	wakeup(b);

	// Pass the next waiting buffer to the disk.
	if (idequeue != 0)
		idestart(idequeue);

	release(&idelock);
}


// Sync buf with disk.
// Maintains a queue of pending disk requests and uses interrupts to
// find out when each request has finished. Can only handle one operation
// at a time. Sends the buffer at the front of the queue to the disk
// hardware. The other buffers in the queue simply wait their turn.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
	struct buf **pp;

	if (!holdingsleep(&b->lock))
		panic("iderw: buf not busy");
	if ((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
		panic("iderw: nothing to do");
	if (b->dev != 0 && !havedisk1)
		panic("iderw: ide disk 1 not present");

	acquire(&idelock);

	// Add buffer b to the end of idequeue.
	b->qnext = 0;
	for (pp=&idequeue; *pp; pp=&(*pp)->qnext)
		;
	*pp = b;

	// Start disk, if necessary.
	// If the buffer is at the front of the queue...
	if (idequeue == b)
		// send it to the disk hardware
		idestart(b);

	// Wait for request to finish
	while ((b->flags & (B_VALID|B_DIRTY)) != B_VALID)
	{
		// Polling does not make efficient use of the CPU.
		// Instead, we sleep, waiting for the interrupt handler
		// to record in the buffer's flags that the operation
		// is done. While this process is sleeping, xv6 will
		// schedule other processes to keep the CPU busy.
		sleep(b, &idelock);
	}

	// Eventually, the disk will finish its operation and trigger
	// an interrupt. trap() will call ideintr() to handle it.

	release(&idelock);
}
