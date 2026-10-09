
/home/bradley/.codex/worktrees/b625/glob2/artifacts/map-wrapping/candidate-glob2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000066a010 <_ZNK6Cabino8Gradient9getHeightEii>:
  66a010:	f3 0f 1e fa          	endbr64
  66a014:	8b 47 04             	mov    0x4(%rdi),%eax
  66a017:	89 d1                	mov    %edx,%ecx
  66a019:	8b 17                	mov    (%rdi),%edx
  66a01b:	83 e8 01             	sub    $0x1,%eax
  66a01e:	21 c8                	and    %ecx,%eax
  66a020:	0f af c2             	imul   %edx,%eax
  66a023:	83 ea 01             	sub    $0x1,%edx
  66a026:	21 f2                	and    %esi,%edx
  66a028:	01 c2                	add    %eax,%edx
  66a02a:	48 8b 47 18          	mov    0x18(%rdi),%rax
  66a02e:	89 d2                	mov    %edx,%edx
  66a030:	0f bf 04 50          	movswl (%rax,%rdx,2),%eax
  66a034:	83 e8 01             	sub    $0x1,%eax
  66a037:	c3                   	ret

Disassembly of section .fini:
