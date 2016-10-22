
# When an x86 PC boots, it starts executing a program called the BIOS,
# (Basic Input/Output System), which is stored in the non-volatile
# memory on the motherboard.
#
# The BIOS's job is to prepare the hardware and then transfer control
# to the operating system. Specifically, it transfers control to code
# loaded from the boot sector (the first 512-byte sector of the boot
# disk), which contains the boot loader (instructions that load the
# kernel into memory.
#
# The BIOS loads the boot sector memory at address 0x7c00, then jumps
# (sets the processor's %ip) to that address.
#
# When the boot loader begins executing, the processor is simulating
# an Intel 8088. The loader's job is to put the processor into a
# more modern operating mode, to load the xv6 kernel from disk into
# memory, and then to transfer control to the kernel.
#
# The xv6 boot loader comprises two source files: this one (bootasm.S),
# which is written in a combination of 16-bit and 32-bit x86 assembly,
# and one written in C (bootmain.c).


#include "asm.h"
#include "memlayout.h"
#include "mmu.h"


#### bootasm.S ####
# Start the first CPU: switch to 32-bit protected mode, jump into C.
# The BIOS loads this code, from the first sector of the hard disk,
# into memory at physical address 0x7c00 and starts executing in
# real mode with %cs=0 %ip=7c00.

.code16					# Assemble for 16-bit mode
.globl start
start:
	cli					# Disables any interrupts handlers that the BIOS
						# may have set up as part of initializing the
						# hardware, since the BIOS is no longer running.

	# Since we have not idea what these registers might contain,
	# we set %ax to zero, and then copy that zero into %ds, %es,
	# and %ss.
	xorw	%ax,%ax		# Set %ax to zero
	movw	%ax,%ds		# -> Data Segment
	movw	%ax,%es		# -> Extra Segment
	movw	%ax,%ss		# -> Stack Segment

	# Physical address line A20 is tied to zero so that the first
	# PCs with 2 MB would run software that assumed 1 MB. Undo that.
	# The boot loader must enable the 21st address bit using I/O to
	# the keyboard controller on ports 0x64 and 0x60.
seta20.1:
	inb		$0x64,%al		# Wait for not busy
	testb	$0x2,%al
	jnz		seta20.1

	movb	$0xd1,%al		# 0xd1 -> port 0x64
	outb	%al,$0x64

seta20.2:
	inb		$0x64,%al		# Wait for not busy
	testb	$0x2,%al
	jnz		seta20.2

	movb	$0xdf,%al		# 0xdf -> port 0x60
	outb	%al,$0x60

	# Switch from real to protected mode. Use a bootstrap GDT that
	# makes virtual addresses map directly to physical addresses,
	# so that the effective memory map doesn't change during the
	# transition.
	lgdt	gdtdesc
	movl	%cr0,%eax
	orl		$CR0_PE, %eax
	movl	%eax, %cr0

	# Complete the transition to 32-bit protected mode by using
	# a long jmp to reload %cs and %eip. The segment descriptors are
	# set up with no translation, so that the mapping is still the
	# identity mapping.
	ljmp	$(SEG_KCODE<<3), $start32

.code32		# Tell assembler to generate 32-bit code now.
start32:

	# Set up the protected-mode data segment registers
	movw	$(SEG_KDATA<<3), %ax	# Our data segment selector
	movw	%ax, %ds				# -> DS: Data Segment
	movw	%ax, %es				# -> ES: Extra Segment
	movw	%ax, %ss				# -> SS: Stack Segment
	movw	$0, %ax					# Zero segments not ready for use
	movw	%ax, %fs				# -> FS
	movw	%ax, %gs				# -> GS

	# Set up the stack pointer and call into C
	movl	$start, %esp
	call	bootmain

	# If bootmain returns (it shouldn't), trigger a Bochs
	# breakpoint, if running under Bochs, then loop.
	movw	$0x8a00, %ax			# 0x8a00 -> port 0x8a00
	movw	%ax, %dx
	outw	%ax, %dx
	movw	$0x8ae0, %ax			# 0x8ae0 -> port 0x8a00
	outw	%ax, %dx
spin:
	jmp		spin

# Bootstrap GDT
.p2align 2							# force 4 byte alignment
gdt:
	SEG_NULLASM								# null seg
	SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)	# code seg
	SEG_ASM(STA_W, 0x0, 0xffffffff)			# data seg

gdtdesc:
	.word	(gdtdesc - gdt - 1)				# sizeof(gdt) - 1
	.long	gdt								# address gdt
