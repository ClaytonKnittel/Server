
.obj/http.o:	file format Mach-O 64-bit x86-64

Disassembly of section __TEXT,__text:
_http_init:
       0:	55 	pushq	%rbp
       1:	48 89 e5 	movq	%rsp, %rbp
       4:	41 56 	pushq	%r14
       6:	53 	pushq	%rbx
       7:	48 8d 3d 00 00 00 00 	leaq	(%rip), %rdi
       e:	48 8d 35 6b 02 00 00 	leaq	619(%rip), %rsi
      15:	31 db 	xorl	%ebx, %ebx
      17:	31 d2 	xorl	%edx, %edx
      19:	e8 00 00 00 00 	callq	0 <_http_init+0x1e>
      1e:	85 c0 	testl	%eax, %eax
      20:	0f 84 dc 00 00 00 	je	220 <_http_init+0x102>
      26:	41 89 c6 	movl	%eax, %r14d
      29:	48 8b 05 00 00 00 00 	movq	(%rip), %rax
      30:	48 8b 38 	movq	(%rax), %rdi
      33:	48 8d 35 c0 0c 00 00 	leaq	3264(%rip), %rsi
      3a:	31 c0 	xorl	%eax, %eax
      3c:	44 89 f2 	movl	%r14d, %edx
      3f:	e8 00 00 00 00 	callq	0 <_http_init+0x44>
      44:	48 8d 3d 49 17 00 00 	leaq	5961(%rip), %rdi
      4b:	31 c0 	xorl	%eax, %eax
      4d:	e8 00 00 00 00 	callq	0 <_http_init+0x52>
      52:	44 89 f0 	movl	%r14d, %eax
      55:	83 c0 fe 	addl	$-2, %eax
      58:	83 f8 0b 	cmpl	$11, %eax
      5b:	77 19 	ja	25 <_http_init+0x76>
      5d:	48 8d 0d a8 00 00 00 	leaq	168(%rip), %rcx
      64:	48 63 04 81 	movslq	(%rcx,%rax,4), %rax
      68:	48 01 c8 	addq	%rcx, %rax
      6b:	ff e0 	jmpq	*%rax
      6d:	48 8d 3d 1c 19 00 00 	leaq	6428(%rip), %rdi
      74:	eb 74 	jmp	116 <_http_init+0xea>
      76:	48 8d 3d 1f 17 00 00 	leaq	5919(%rip), %rdi
      7d:	31 c0 	xorl	%eax, %eax
      7f:	44 89 f6 	movl	%r14d, %esi
      82:	e8 00 00 00 00 	callq	0 <_http_init+0x87>
      87:	eb 66 	jmp	102 <_http_init+0xef>
      89:	48 8d 3d 30 18 00 00 	leaq	6192(%rip), %rdi
      90:	eb 58 	jmp	88 <_http_init+0xea>
      92:	48 8d 3d 07 18 00 00 	leaq	6151(%rip), %rdi
      99:	eb 4f 	jmp	79 <_http_init+0xea>
      9b:	48 8d 3d de 17 00 00 	leaq	6110(%rip), %rdi
      a2:	eb 46 	jmp	70 <_http_init+0xea>
      a4:	48 8d 3d 15 17 00 00 	leaq	5909(%rip), %rdi
      ab:	eb 3d 	jmp	61 <_http_init+0xea>
      ad:	48 8d 3d 2c 18 00 00 	leaq	6188(%rip), %rdi
      b4:	eb 34 	jmp	52 <_http_init+0xea>
      b6:	48 8d 3d 93 17 00 00 	leaq	6035(%rip), %rdi
      bd:	eb 2b 	jmp	43 <_http_init+0xea>
      bf:	48 8d 3d 4a 18 00 00 	leaq	6218(%rip), %rdi
      c6:	eb 22 	jmp	34 <_http_init+0xea>
      c8:	48 8d 3d 01 19 00 00 	leaq	6401(%rip), %rdi
      cf:	eb 19 	jmp	25 <_http_init+0xea>
      d1:	48 8d 3d 48 17 00 00 	leaq	5960(%rip), %rdi
      d8:	eb 10 	jmp	16 <_http_init+0xea>
      da:	48 8d 3d 0f 17 00 00 	leaq	5903(%rip), %rdi
      e1:	eb 07 	jmp	7 <_http_init+0xea>
      e3:	48 8d 3d 56 18 00 00 	leaq	6230(%rip), %rdi
      ea:	e8 00 00 00 00 	callq	0 <_http_init+0xef>
      ef:	48 8d 3d b9 16 00 00 	leaq	5817(%rip), %rdi
      f6:	31 c0 	xorl	%eax, %eax
      f8:	e8 00 00 00 00 	callq	0 <_http_init+0xfd>
      fd:	bb 01 00 00 00 	movl	$1, %ebx
     102:	89 d8 	movl	%ebx, %eax
     104:	5b 	popq	%rbx
     105:	41 5e 	popq	%r14
     107:	5d 	popq	%rbp
     108:	c3 	retq
     109:	0f 1f 00 	nopl	(%rax)
     10c:	61  <unknown>
     10d:	ff ff  <unknown>
     10f:	ff 7d ff  <unknown>
     112:	ff ff  <unknown>
     114:	86 ff 	xchgb	%bh, %bh
     116:	ff ff  <unknown>
     118:	8f ff ff  <unknown>
     11b:	ff 98 ff ff ff a1 	lcalll	*-1577058305(%rax)
     121:	ff ff  <unknown>
     123:	ff aa ff ff ff b3 	ljmpl	*-1275068417(%rdx)
     129:	ff ff  <unknown>
     12b:	ff bc ff ff ff c5 ff  <unknown>
     132:	ff ff  <unknown>
     134:	ce  <unknown>
     135:	ff ff  <unknown>
     137:	ff d7 	callq	*%rdi
     139:	ff ff  <unknown>
     13b:	ff 0f 	decl	(%rdi)
     13d:	1f  <unknown>
     13e:	40 00 55 48 	addb	%dl, 72(%rbp)

_http_exit:
     140:	55 	pushq	%rbp
     141:	48 89 e5 	movq	%rsp, %rbp
     144:	48 8d 3d 00 00 00 00 	leaq	(%rip), %rdi
     14b:	5d 	popq	%rbp
     14c:	e9 00 00 00 00 	jmp	0 <_http_exit+0x11>
     151:	66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     15b:	0f 1f 44 00 00 	nopl	(%rax,%rax)

_http_parse:
     160:	55 	pushq	%rbp
     161:	48 89 e5 	movq	%rsp, %rbp
     164:	41 57 	pushq	%r15
     166:	41 56 	pushq	%r14
     168:	41 55 	pushq	%r13
     16a:	41 54 	pushq	%r12
     16c:	53 	pushq	%rbx
     16d:	48 81 ec 98 01 00 00 	subq	$408, %rsp
     174:	49 89 f4 	movq	%rsi, %r12
     177:	49 89 fe 	movq	%rdi, %r14
     17a:	48 8b 05 00 00 00 00 	movq	(%rip), %rax
     181:	48 8b 00 	movq	(%rax), %rax
     184:	48 89 45 d0 	movq	%rax, -48(%rbp)
     188:	48 8d b5 d0 fe ff ff 	leaq	-304(%rbp), %rsi
     18f:	ba 00 01 00 00 	movl	$256, %edx
     194:	4c 89 e7 	movq	%r12, %rdi
     197:	e8 00 00 00 00 	callq	0 <_http_parse+0x3c>
     19c:	48 85 c0 	testq	%rax, %rax
     19f:	74 63 	je	99 <_http_parse+0xa4>
     1a1:	4c 8d ad d0 fe ff ff 	leaq	-304(%rbp), %r13
     1a8:	4c 8d bd 40 fe ff ff 	leaq	-448(%rbp), %r15
     1af:	90 	nop
     1b0:	41 0f b6 06 	movzbl	(%r14), %eax
     1b4:	c0 e8 02 	shrb	$2, %al
     1b7:	bb ff ff ff ff 	movl	$4294967295, %ebx
     1bc:	24 03 	andb	$3, %al
     1be:	74 10 	je	16 <_http_parse+0x70>
     1c0:	3c 03 	cmpb	$3, %al
     1c2:	75 2b 	jne	43 <_http_parse+0x8f>
     1c4:	eb 40 	jmp	64 <_http_parse+0xa6>
     1c6:	66 2e 0f 1f 84 00 00 00 00 00 	nopw	%cs:(%rax,%rax)
     1d0:	ba 08 00 00 00 	movl	$8, %edx
     1d5:	45 31 c0 	xorl	%r8d, %r8d
     1d8:	48 8d 3d 00 00 00 00 	leaq	(%rip), %rdi
     1df:	4c 89 ee 	movq	%r13, %rsi
     1e2:	4c 89 f9 	movq	%r15, %rcx
     1e5:	e8 00 00 00 00 	callq	0 <_http_parse+0x8a>
     1ea:	83 f8 01 	cmpl	$1, %eax
     1ed:	74 3b 	je	59 <_http_parse+0xca>
     1ef:	ba 00 01 00 00 	movl	$256, %edx
     1f4:	4c 89 e7 	movq	%r12, %rdi
     1f7:	4c 89 ee 	movq	%r13, %rsi
     1fa:	e8 00 00 00 00 	callq	0 <_http_parse+0x9f>
     1ff:	48 85 c0 	testq	%rax, %rax
     202:	75 ac 	jne	-84 <_http_parse+0x50>
     204:	31 db 	xorl	%ebx, %ebx
     206:	48 8b 05 00 00 00 00 	movq	(%rip), %rax
     20d:	48 8b 00 	movq	(%rax), %rax
     210:	48 3b 45 d0 	cmpq	-48(%rbp), %rax
     214:	75 34 	jne	52 <_http_parse+0xea>
     216:	89 d8 	movl	%ebx, %eax
     218:	48 81 c4 98 01 00 00 	addq	$408, %rsp
     21f:	5b 	popq	%rbx
     220:	41 5c 	popq	%r12
     222:	41 5d 	popq	%r13
     224:	41 5e 	popq	%r14
     226:	41 5f 	popq	%r15
     228:	5d 	popq	%rbp
     229:	c3 	retq
     22a:	41 80 0e 0c 	orb	$12, (%r14)
     22e:	41 8a 46 01 	movb	1(%r14), %al
     232:	24 c0 	andb	$-64, %al
     234:	0c 11 	orb	$17, %al
     236:	41 88 46 01 	movb	%al, 1(%r14)
     23a:	48 8b 05 00 00 00 00 	movq	(%rip), %rax
     241:	48 8b 00 	movq	(%rax), %rax
     244:	48 3b 45 d0 	cmpq	-48(%rbp), %rax
     248:	74 cc 	je	-52 <_http_parse+0xb6>
     24a:	e8 00 00 00 00 	callq	0 <_http_parse+0xef>
     24f:	90 	nop

_http_respond:
     250:	55 	pushq	%rbp
     251:	48 89 e5 	movq	%rsp, %rbp
     254:	8a 0f 	movb	(%rdi), %cl
     256:	89 ca 	movl	%ecx, %edx
     258:	80 e2 0c 	andb	$12, %dl
     25b:	b8 ff ff ff ff 	movl	$4294967295, %eax
     260:	80 fa 0c 	cmpb	$12, %dl
     263:	75 11 	jne	17 <_http_respond+0x26>
     265:	80 c9 0c 	orb	$12, %cl
     268:	88 0f 	movb	%cl, (%rdi)
     26a:	8a 47 01 	movb	1(%rdi), %al
     26d:	24 c0 	andb	$-64, %al
     26f:	0c 11 	orb	$17, %al
     271:	88 47 01 	movb	%al, 1(%rdi)
     274:	31 c0 	xorl	%eax, %eax
     276:	5d 	popq	%rbp
     277:	c3 	retq
