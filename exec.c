// exec() is the system call that creates the user part of the address space.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
	char *s, *last;
	int i, off;
	uint argc, sz,sp, ustack[3+MAXARG+1];
	struct elfhdr elf;
	struct inode *ip;
	struct proghdr ph;
	pde_t *pgdir, *oldpgdir;

	begin_op();
	// Initalize the user part of the address space
	// from a file stored in the file system.
	// Open the named binary (path).
	if ((ip = namei(path)) == 0)
	{
		end_op();
		return -1;
	}
	ilock(ip);
	pgdir = 0;

	// Check ELF header
	if (readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
		goto bad;
	if (elf.magic != ELF_MAGIC)
		goto bad;
	// Allocate a new page table with no user mappings
	if ((pgdir = setupkvm()) == 0)
		goto bad;

	// Load program into memory
	sz = 0;
	for (i = 0, off = elf.phoff; i < elf.phnum; i++, off+=sizeof(ph))
	{
		if (readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
			goto bad;
		if (ph.type != ELF_PROG_LOAD)
			continue;
		if (ph.memsz < ph.filesz)
			goto bad;
		if (ph.vaddr + ph.memsz < ph.vaddr)
			goto bad;
		// Allocate memory for each ELF segment
		if ((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
			goto bad;
		if (ph.vaddr % PGSIZE != 0)
			goto bad;
		// Load each segment into memory
		if (loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
			goto bad;
	}
	iunlockput(ip);
	end_op();
	ip = 0;

	// Allocate two pages at the next page boundary.
	// Use the second as the user stack.
	sz = PGROUNDUP(sz);
	if ((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
		goto bad;
	// Make the first inaccessible.
	// This will be the guard page, right below the stack.
	// This is used to guard a stack growing off of the stack page.
	// The guard page is not mapped, and so if the stack does run
	// off of the stack page, the hardware will generate an
	// exception because it cannot translate the faulting address.
	clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
	sp = sz;

	// Push argument strings, prepare rest of stack in ustack.
	for (argc = 0; argv[argc]; argc++)
	{
		if (argc >= MAXARG)
			goto bad;
		// Copy the argument strings to the top of the stack, one
		// at a time,
		sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
		if (copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
			goto bad;
		// Record the pointers to the arg strings in ustack[],
		// after skipping over the first three slots, which
		// will be filled in below.
		ustack[3+argc] = sp;
	}
	// Place a null pointer at the end of what will be
	// the argv list passed to main().
	ustack[3+argc] = 0;

	// Record the first three entries in ustack[]
	ustack[0] = 0xffffffff;			// fake return PC
	ustack[1] = argc;				// arg count
	ustack[2] = sp - (argc+1)*4;	// argv pointer

	sp -= (3+argc+1) * 4;
	if (copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
		goto bad;

	// Save program name. FOR DEBUGGING.
	for (last = s = path; *s; s++)
		if(*s == '/')
			last = s+1;

	safestrcpy(proc->name, last, sizeof(proc->name));

	// Commit to the user image
	oldpgdir = proc->pgdir;
	proc->pgdir = pgdir;
	proc->sz = sz;
	proc->tf->eip = elf.entry;		// main
	proc->tf->esp = sp;
	switchuvm(proc);
	// exec() must wait until it is sure that the system
	// call will succeed before it can free the old image.
	freevm(oldpgdir);

	return 0;

// If exec() detects an error, like an invalid program segment,
// we jump here, free the new image, and return -1.
// The only error cases happen during the creation of the image.
bad:
	if (pgdir)
		freevm(pgdir);
	if (ip)
	{
		iunlockput(ip);
		end_op();
	}
	cprintf("exec() failed");
	return -1;
}
