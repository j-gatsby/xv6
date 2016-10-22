# Initial process execs /init.
# This code runs in user space.
#
# The first action is to invoke the exec() system call.
# We begin by pushing three values onto
# the stack - $argv, $init, and $0.
# Then we set %eax to SYS_exec and execute int T_SYSCALL,
# which asks the kernel to run exec(). If successful,
# it will never return, and will start running the
# program named by $init, which is a pointer to the
# nul-terminated string "/init". The other arg is
# the argv array of command-line arguments. The 0
# marks the end of this array.
# If exec() fails and does return, we loop, calling
# the exit() system call, which definitely should
# not return.
#
# This code manually crafts the first system call to
# look like a ordinary system call. This setup avoids
# special-casing the first process (in this case, it's
# first system call), and instead reuses the code that
# xv6 must provide for standard operation.
#
# So, exec() is replacing initcode.S with the /init binary,
# loaded out of the file system. Now initcode.S is done,
# and the process will run /init instead.


#include "syscall.h"
#include "traps.h"


# exec(init, argv)
.globl start
start:
	pushl $argv
	pushl $init
	pushl $0		// where caller pc would be
	movl $SYS_exec, %eax
	int $T_SYSCALL

# for(;;) exit();
exit:
	movl $SYS_exit, %eax
	int $T_SYSCALL
	jmp exit

# char init[] = "/init\0";
init:
	.string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
	.long init
	.long 0
