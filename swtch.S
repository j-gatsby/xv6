# Context switch
#
#	void swtch(struct context **old, struct context *new);
#
# Save current register context in old
# and then load register context from new.
#
# When it is time for a process to give up the CPU, the
# process' kernel thread calls swtch to save its own
# context and return to the scheduler context. Each context
# is represented by a struct context*, a pointer to a structure
# stored on the kernel stack involved.
#
# It pushes the current CPU register onto the stack and saves
# the stack pointer in *old. Then swtch copies new to %esp,
# pops previously saved registers, and returns.
#
# swtch starts by copying its arguments from the stack to the
# caller-saved registers %eax and %edx; swtch must do this before
# it changes the stack pointer and can no longer access the
# arguments via %esp. Then swtch pushes the register state,
# creating a context structure on the current stack. Only the
# callee-saved registers need to be saved; the convention on
# the x86 is that these are %ebp, %ebx, %esi, %edi, and %esp.
#
# swtch pushes the first four explicitly; it saves the last
# implicitly as the struct context* written to *old.
#
# There is one more important register: the program counter %eip.
# It has already been saved on the stack by the 'call' instruction
# that invoked swtch.
#
# Having saved the old context, swtch is ready to restore the
# new one. It moves the pointer to the new context into the
# stack pointer. The new stack has the same form as the old one
# that swtch just left - the new stack was the old one in a
# previous call to swtch - so swtch can invert the sequence to
# restore the new context.
#
# It pops the values for %edi, %esi, %ebx, and %ebp and then returns.
#
# Because swtch has changed the stack pointer, the values restored
# and the instruction address returned to are the ones from the
# new context.

.globl swtch
swtch:
	movl 4(%esp), %eax
	movl 8(%esp), %edx

	# Save old calle-save registers
	pushl %ebp
	pushl %ebx
	pushl %esi
	pushl %edi

	# Switch stacks
	movl %esp, (%eax)
	movl %edx, %esp

	# Load new callee-save registers
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	ret
