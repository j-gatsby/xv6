#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then then first argument.


// The helper functions argint(), argptr(), argstr(), and argfd()
// retrieve the n'th system call argument, as either an integer,
// a pointer, a string, or a file descriptor.


// Fetch the int at addr from the current process.
// Read the value at that address from user memory
// and write it to *ip.
int
fetchint(uint addr, int *ip)
{
	// Verify that the address lies within the user
	// part of the address space.
	// The kernel has set up the page-table hardware
	// to make sure that the process cannot access memory
	// outside it's local private memory: if a user program
	// tries to read or write memory at an address >= p->sz,
	// the processor will cause a segmentation trap, and trap()
	// will kill the process. The kernel, however, can dereference
	// any address that the user might have passed, so it must
	// check explicitly that the address is below p->sz.
	if (addr >= proc->sz || addr+4 > proc->sz)
		return -1;
	// We can simply cast the address to a pointer
	// because the user and the kernel share the
	// same page table
	*ip = *(int*)(addr);
		return 0;
}


// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uint addr, char **pp)
{
	char *s, *ep;

	if (addr >= proc->sz)
		return -1;

	*pp = (char*)addr;
	ep = (char*)proc->sz;
	for (s = *pp; s < ep; s++)
	{
		if (*s == 0)
			return s - *pp;
	}
	return -1;
}


// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
	// Use the user-space %esp register to locate
	// the nth argument.
	// %esp points at the return address for the
	// system call stub. The args are right above
	// it at %esp+4. The nth arg is at %esp+4+4*n.
	return fetchint(proc->tf->esp + 4 + 4*n, ip);
}


// Fetch the nth word-sized system call arg as a pointer
// to a block of memory of size n bytes.
// Check that the pointer lies within the process addr space.
int
argptr(int n, char **pp, int size)
{
	int i;

	// The user stack pointer is checked during the
	// fetching of the argument.
	if (argint(n, &i) < 0)
		return -1;

	// Then the argument, which is also a user pointer,
	// is checked.
	if ((uint)i >= proc->sz || (uint)i + size > proc->sz)
		return -1;

	*pp = (char*)i;
	return 0;
}


// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
//	(There is no shared writable memory, so the string cannot change
//		between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
	int addr;

	if (argint(n, &addr) < 0)
		return -1;

	return fetchstr(addr, pp);
}


extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_symlink(void);
extern int sys_uptime(void);


static int (*syscalls[])(void) = {
[SYS_fork]		sys_fork,
[SYS_exit]		sys_exit,
[SYS_wait]		sys_wait,
[SYS_pipe]		sys_pipe,
[SYS_read]		sys_read,
[SYS_kill]		sys_kill,
[SYS_exec]		sys_exec,
[SYS_fstat]		sys_fstat,
[SYS_chdir]		sys_chdir,
[SYS_dup]		sys_dup,
[SYS_getpid]	sys_getpid,
[SYS_sbrk]		sys_sbrk,
[SYS_sleep]		sys_sleep,
[SYS_uptime]	sys_uptime,
[SYS_open]		sys_open,
[SYS_write]		sys_write,
[SYS_mknod]		sys_mknod,
[SYS_unlink]	sys_unlink,
[SYS_link]		sys_link,
[SYS_mkdir]		sys_mkdir,
[SYS_close]		sys_close,
[SYS_symlink]	sys_symlink,
};


void
syscall(void)
{
	int num;

	// Load the system call number from the trap frame,
	// which contains the saved %eax
	num = proc->tf->eax;
	// Index into the system call tables
	if (num > 0 && num < NELEM(syscalls) && syscalls[num])
	{
		// Record the return value of the system
		// call function in %eax
		proc->tf->eax = syscalls[num]();
	}
	// If the system call number is invalid...
	else
	{
		// Print an error message
		cprintf("%d %s: unknown sys call %d\n",
				proc->pid, proc->name, num);
		// Record a return value of -1 in %eax
		proc->tf->eax = -1;
	}
}
