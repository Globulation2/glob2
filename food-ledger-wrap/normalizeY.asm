
artifacts/food-ledger-wrap/glob2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000049d750 <AIMaximaFoodLedger::Input::normalizeY(int) const>:
  49d750:	endbr64
  49d754:	mov    0x4(%rdi),%ecx
  49d757:	mov    %esi,%eax
  49d759:	xor    %edx,%edx
  49d75b:	test   %ecx,%ecx
  49d75d:	jle    49d773 <AIMaximaFoodLedger::Input::normalizeY(int) const+0x23>
  49d75f:	mov    0x64(%rdi),%edx
  49d762:	lea    -0x1(%rcx),%esi
  49d765:	cmp    %esi,%edx
  49d767:	je     49d780 <AIMaximaFoodLedger::Input::normalizeY(int) const+0x30>
  49d769:	cltd
  49d76a:	idiv   %ecx
  49d76c:	add    %edx,%ecx
  49d76e:	test   %edx,%edx
  49d770:	cmovs  %ecx,%edx
  49d773:	mov    %edx,%eax
  49d775:	ret
  49d776:	cs nopw 0x0(%rax,%rax,1)
  49d780:	and    %eax,%edx
  49d782:	mov    %edx,%eax
  49d784:	ret

Disassembly of section .fini:
