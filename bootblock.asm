
bootblock.o:     file format elf32-i386


Disassembly of section .text:

00007c00 <start>:
# real mode with %cs=0 %ip=7c00.

.code16					# Assemble for 16-bit mode
.globl start
start:
	cli					# BIOS enabled interrupts; disable
    7c00:	fa                   	cli    

	# Zero data segment registers DS, ES, and SS
	xorw	%ax,%ax		# Set %ax to zero
    7c01:	31 c0                	xor    %eax,%eax
	movw	%ax,%ds		# -> Data Segment
    7c03:	8e d8                	mov    %eax,%ds
	movw	%ax,%es		# -> Extra Segment
    7c05:	8e c0                	mov    %eax,%es
	movw	%ax,%ss		# -> Stack Segment
    7c07:	8e d0                	mov    %eax,%ss

00007c09 <seta20.1>:

	# Physical address line A20 is tied to zero so that the first
	# PCs with 2 MB would run software that assumed 1 MB. Undo that.
seta20.1:
	inb		$0x64,%al		# Wait for not busy
    7c09:	e4 64                	in     $0x64,%al
	testb	$0x2,%al
    7c0b:	a8 02                	test   $0x2,%al
	jnz		seta20.1
    7c0d:	75 fa                	jne    7c09 <seta20.1>

	movb	$0xd1,%al		# 0xd1 -> port 0x64
    7c0f:	b0 d1                	mov    $0xd1,%al
	outb	%al,$0x64
    7c11:	e6 64                	out    %al,$0x64

00007c13 <seta20.2>:

seta20.2:
	inb		$0x64,%al		# Wait for not busy
    7c13:	e4 64                	in     $0x64,%al
	testb	$0x2,%al
    7c15:	a8 02                	test   $0x2,%al
	jnz		seta20.2
    7c17:	75 fa                	jne    7c13 <seta20.2>

	movb	$0xdf,%al		# 0xdf -> port 0x60
    7c19:	b0 df                	mov    $0xdf,%al
	outb	%al,$0x60
    7c1b:	e6 60                	out    %al,$0x60

	# Switch from real to protected mode. Use a bootstrap GDT that
	# makes virtual addresses map directly to physical addresses,
	# so that the effective memory map doesn't change during the
	# transition.
	lgdt	gdtdesc
    7c1d:	0f 01 16             	lgdtl  (%esi)
    7c20:	78 7c                	js     7c9e <readsect+0xc>
	movl	%cr0,%eax
    7c22:	0f 20 c0             	mov    %cr0,%eax
	orl		$CRO_PE, %eax
    7c25:	66 83 c8 01          	or     $0x1,%ax
	movl	%eax, %cr0
    7c29:	0f 22 c0             	mov    %eax,%cr0

	# Complete the transition to 32-bit protected mode by using
	# a long jmp to reload %cs and %eip. The segment descriptors are
	# set up with no translation, so that the mapping is still the
	# identity mapping.
	ljmp	$(SEG_KCODE<<3), $start32
    7c2c:	ea 31 7c 08 00 66 b8 	ljmp   $0xb866,$0x87c31

00007c31 <start32>:

.code32		# Tell assembler to generate 32-bit code now.
start32:
	# Set up the protected-mode data segment registers
	movw	$(SEG_KDATA<<3), %ax	# Our data segment selector
    7c31:	66 b8 10 00          	mov    $0x10,%ax
	movw	%ax, %ds				# -> DS: Data Segment
    7c35:	8e d8                	mov    %eax,%ds
	movw	%ax, %es				# -> ES: Extra Segment
    7c37:	8e c0                	mov    %eax,%es
	movw	%ax, %ss				# -> SS: Stack Segment
    7c39:	8e d0                	mov    %eax,%ss
	movw	$0, %ax					# Zero segments not ready for use
    7c3b:	66 b8 00 00          	mov    $0x0,%ax
	movw	%ax, %fs				# -> FS
    7c3f:	8e e0                	mov    %eax,%fs
	movw	%ax, %gs				# -> GS
    7c41:	8e e8                	mov    %eax,%gs

	# Set up the stack pointer and call into C
	movl	$start, %esp
    7c43:	bc 00 7c 00 00       	mov    $0x7c00,%esp
	call	bootmain
    7c48:	e8 d6 00 00 00       	call   7d23 <bootmain>

	# If bootmain returns (it shouldn't), trigger a Bochs
	# breakpoint, if running under Bochs, then loop.
	movw	$0x8a00, %ax			# 0x8a00 -> port 0x8a00
    7c4d:	66 b8 00 8a          	mov    $0x8a00,%ax
	movw	%ax, %dx
    7c51:	66 89 c2             	mov    %ax,%dx
	outw	%ax, %dx
    7c54:	66 ef                	out    %ax,(%dx)
	movw	$0x8ae0, %ax			# 0x8ae0 -> port 0x8a00
    7c56:	66 b8 e0 8a          	mov    $0x8ae0,%ax
	outw	%ax, %dx
    7c5a:	66 ef                	out    %ax,(%dx)

00007c5c <spin>:
spin:
	jmp		spin
    7c5c:	eb fe                	jmp    7c5c <spin>
    7c5e:	66 90                	xchg   %ax,%ax

00007c60 <gdt>:
	...
    7c68:	ff 0f                	decl   (%edi)
    7c6a:	00 00                	add    %al,(%eax)
    7c6c:	00 9a cf 00 ff 0f    	add    %bl,0xfff00cf(%edx)
    7c72:	00 00                	add    %al,(%eax)
    7c74:	00 92 cf 00 17 00    	add    %dl,0x1700cf(%edx)

00007c78 <gdtdesc>:
    7c78:	17                   	pop    %ss
    7c79:	00 60 7c             	add    %ah,0x7c(%eax)
    7c7c:	00 00                	add    %al,(%eax)
    7c7e:	90                   	nop
    7c7f:	90                   	nop

00007c80 <waitdisk>:
    7c80:	55                   	push   %ebp
    7c81:	89 e5                	mov    %esp,%ebp
    7c83:	ba f7 01 00 00       	mov    $0x1f7,%edx
    7c88:	ec                   	in     (%dx),%al
    7c89:	83 e0 c0             	and    $0xffffffc0,%eax
    7c8c:	3c 40                	cmp    $0x40,%al
    7c8e:	75 f8                	jne    7c88 <waitdisk+0x8>
    7c90:	5d                   	pop    %ebp
    7c91:	c3                   	ret    

00007c92 <readsect>:
    7c92:	55                   	push   %ebp
    7c93:	89 e5                	mov    %esp,%ebp
    7c95:	57                   	push   %edi
    7c96:	53                   	push   %ebx
    7c97:	8b 5d 0c             	mov    0xc(%ebp),%ebx
    7c9a:	e8 e1 ff ff ff       	call   7c80 <waitdisk>
    7c9f:	ba f2 01 00 00       	mov    $0x1f2,%edx
    7ca4:	b0 01                	mov    $0x1,%al
    7ca6:	ee                   	out    %al,(%dx)
    7ca7:	b2 f3                	mov    $0xf3,%dl
    7ca9:	88 d8                	mov    %bl,%al
    7cab:	ee                   	out    %al,(%dx)
    7cac:	89 d8                	mov    %ebx,%eax
    7cae:	c1 e8 08             	shr    $0x8,%eax
    7cb1:	b2 f4                	mov    $0xf4,%dl
    7cb3:	ee                   	out    %al,(%dx)
    7cb4:	89 d8                	mov    %ebx,%eax
    7cb6:	c1 e8 10             	shr    $0x10,%eax
    7cb9:	b2 f5                	mov    $0xf5,%dl
    7cbb:	ee                   	out    %al,(%dx)
    7cbc:	89 d8                	mov    %ebx,%eax
    7cbe:	c1 e8 18             	shr    $0x18,%eax
    7cc1:	83 c8 e0             	or     $0xffffffe0,%eax
    7cc4:	b2 f6                	mov    $0xf6,%dl
    7cc6:	ee                   	out    %al,(%dx)
    7cc7:	b2 f7                	mov    $0xf7,%dl
    7cc9:	b0 20                	mov    $0x20,%al
    7ccb:	ee                   	out    %al,(%dx)
    7ccc:	e8 af ff ff ff       	call   7c80 <waitdisk>
    7cd1:	8b 7d 08             	mov    0x8(%ebp),%edi
    7cd4:	b9 80 00 00 00       	mov    $0x80,%ecx
    7cd9:	ba f0 01 00 00       	mov    $0x1f0,%edx

00007cde <cld>:
    7cde:	f3 6d                	rep insl (%dx),%es:(%edi)
    7ce0:	5b                   	pop    %ebx
    7ce1:	5f                   	pop    %edi
    7ce2:	5d                   	pop    %ebp
    7ce3:	c3                   	ret    

00007ce4 <readseg>:
    7ce4:	55                   	push   %ebp
    7ce5:	89 e5                	mov    %esp,%ebp
    7ce7:	57                   	push   %edi
    7ce8:	56                   	push   %esi
    7ce9:	53                   	push   %ebx
    7cea:	8b 5d 08             	mov    0x8(%ebp),%ebx
    7ced:	8b 75 10             	mov    0x10(%ebp),%esi
    7cf0:	89 df                	mov    %ebx,%edi
    7cf2:	03 7d 0c             	add    0xc(%ebp),%edi
    7cf5:	89 f0                	mov    %esi,%eax
    7cf7:	25 ff 01 00 00       	and    $0x1ff,%eax
    7cfc:	29 c3                	sub    %eax,%ebx
    7cfe:	c1 ee 09             	shr    $0x9,%esi
    7d01:	46                   	inc    %esi
    7d02:	39 df                	cmp    %ebx,%edi
    7d04:	76 15                	jbe    7d1b <readseg+0x37>
    7d06:	56                   	push   %esi
    7d07:	53                   	push   %ebx
    7d08:	e8 85 ff ff ff       	call   7c92 <readsect>
    7d0d:	81 c3 00 02 00 00    	add    $0x200,%ebx
    7d13:	46                   	inc    %esi
    7d14:	83 c4 08             	add    $0x8,%esp
    7d17:	39 df                	cmp    %ebx,%edi
    7d19:	77 eb                	ja     7d06 <readseg+0x22>
    7d1b:	8d 65 f4             	lea    -0xc(%ebp),%esp
    7d1e:	5b                   	pop    %ebx
    7d1f:	5e                   	pop    %esi
    7d20:	5f                   	pop    %edi
    7d21:	5d                   	pop    %ebp
    7d22:	c3                   	ret    

00007d23 <bootmain>:
    7d23:	55                   	push   %ebp
    7d24:	89 e5                	mov    %esp,%ebp
    7d26:	57                   	push   %edi
    7d27:	56                   	push   %esi
    7d28:	53                   	push   %ebx
    7d29:	83 ec 0c             	sub    $0xc,%esp
    7d2c:	6a 00                	push   $0x0
    7d2e:	68 00 10 00 00       	push   $0x1000
    7d33:	68 00 00 01 00       	push   $0x10000
    7d38:	e8 a7 ff ff ff       	call   7ce4 <readseg>
    7d3d:	83 c4 0c             	add    $0xc,%esp
    7d40:	81 3d 00 00 01 00 7f 	cmpl   $0x464c457f,0x10000
    7d47:	45 4c 46 
    7d4a:	75 50                	jne    7d9c <bootmain+0x79>
    7d4c:	a1 1c 00 01 00       	mov    0x1001c,%eax
    7d51:	8d 98 00 00 01 00    	lea    0x10000(%eax),%ebx
    7d57:	0f b7 35 2c 00 01 00 	movzwl 0x1002c,%esi
    7d5e:	c1 e6 05             	shl    $0x5,%esi
    7d61:	01 de                	add    %ebx,%esi
    7d63:	39 f3                	cmp    %esi,%ebx
    7d65:	73 2f                	jae    7d96 <bootmain+0x73>
    7d67:	8b 7b 0c             	mov    0xc(%ebx),%edi
    7d6a:	ff 73 04             	pushl  0x4(%ebx)
    7d6d:	ff 73 10             	pushl  0x10(%ebx)
    7d70:	57                   	push   %edi
    7d71:	e8 6e ff ff ff       	call   7ce4 <readseg>
    7d76:	8b 4b 14             	mov    0x14(%ebx),%ecx
    7d79:	8b 43 10             	mov    0x10(%ebx),%eax
    7d7c:	83 c4 0c             	add    $0xc,%esp
    7d7f:	39 c1                	cmp    %eax,%ecx
    7d81:	76 0c                	jbe    7d8f <bootmain+0x6c>
    7d83:	01 c7                	add    %eax,%edi
    7d85:	29 c1                	sub    %eax,%ecx
    7d87:	b8 00 00 00 00       	mov    $0x0,%eax
    7d8c:	fc                   	cld    
    7d8d:	f3 aa                	rep stos %al,%es:(%edi)
    7d8f:	83 c3 20             	add    $0x20,%ebx
    7d92:	39 de                	cmp    %ebx,%esi
    7d94:	77 d1                	ja     7d67 <bootmain+0x44>
    7d96:	ff 15 18 00 01 00    	call   *0x10018
    7d9c:	8d 65 f4             	lea    -0xc(%ebp),%esp
    7d9f:	5b                   	pop    %ebx
    7da0:	5e                   	pop    %esi
    7da1:	5f                   	pop    %edi
    7da2:	5d                   	pop    %ebp
    7da3:	c3                   	ret    
