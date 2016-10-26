// xv6 represents file system blocks using struct buf

struct buf {
	// flags track the relationship between memory and disk
	int flags;
	// device number
	uint dev;
	// sector (block) number
	uint blockno;
	// The sleeplock struct replaces the use of the B_BUSY flag
	struct sleeplock lock;
	uint refcnt;
	struct buf *prev;	// LRU cache list
	struct buf *next;
	struct buf *qnext;	// disk queue
	// BSIZE is identical to the IDE's SECTOR_SIZE (512 bytes),
	// and thus, each buffer represents the contents of one
	// sector on a particular disk drive.
	uchar data[BSIZE];
};

// FLAGS

#define B_VALID		0x2		// data in the buffer has been read in from disk
#define B_DIRTY		0x4		// data in the buffer needs to be written to disk

// The B_BUSY flag has been removed in favor of sleeplocks.
// When the B_BUSY flag was set, the buffer was said to be 'locked',
// indicating that some process was using the buffer and other processes
// must not use the buffer at that time.
