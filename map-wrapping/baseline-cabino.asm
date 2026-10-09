
/home/bradley/.codex/worktrees/b625/glob2/artifacts/map-wrapping/baseline-glob2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

0000000000663450 <_ZNK6Cabino8Gradient9getHeightEii>:
  663450:	f3 0f 1e fa          	endbr64
  663454:	49 89 f8             	mov    %rdi,%r8
  663457:	89 f0                	mov    %esi,%eax
  663459:	8b 3f                	mov    (%rdi),%edi
  66345b:	41 89 d1             	mov    %edx,%r9d
  66345e:	99                   	cltd
  66345f:	41 8b 48 04          	mov    0x4(%r8),%ecx
  663463:	f7 ff                	idiv   %edi
  663465:	44 89 c8             	mov    %r9d,%eax
  663468:	85 d2                	test   %edx,%edx
  66346a:	8d 34 3a             	lea    (%rdx,%rdi,1),%esi
  66346d:	0f 49 f2             	cmovns %edx,%esi
  663470:	99                   	cltd
  663471:	f7 f9                	idiv   %ecx
  663473:	49 8b 40 18          	mov    0x18(%r8),%rax
  663477:	01 d1                	add    %edx,%ecx
  663479:	85 d2                	test   %edx,%edx
  66347b:	0f 49 ca             	cmovns %edx,%ecx
  66347e:	0f af f9             	imul   %ecx,%edi
  663481:	8d 14 37             	lea    (%rdi,%rsi,1),%edx
  663484:	0f bf 04 50          	movswl (%rax,%rdx,2),%eax
  663488:	83 e8 01             	sub    $0x1,%eax
  66348b:	c3                   	ret

Disassembly of section .fini:
