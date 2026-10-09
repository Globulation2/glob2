
artifacts/food-ledger-wrap/glob2:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .text:

000000000049d720 <AIMaximaFoodLedger::Input::normalizeX(int) const>:
  49d720:	endbr64
  49d724:	mov    (%rdi),%ecx
  49d726:	mov    %esi,%eax
  49d728:	xor    %edx,%edx
  49d72a:	test   %ecx,%ecx
  49d72c:	jle    49d742 <AIMaximaFoodLedger::Input::normalizeX(int) const+0x22>
  49d72e:	mov    0x60(%rdi),%edx
  49d731:	lea    -0x1(%rcx),%esi
  49d734:	cmp    %esi,%edx
  49d736:	je     49d748 <AIMaximaFoodLedger::Input::normalizeX(int) const+0x28>
  49d738:	cltd
  49d739:	idiv   %ecx
  49d73b:	add    %edx,%ecx
  49d73d:	test   %edx,%edx
  49d73f:	cmovs  %ecx,%edx
  49d742:	mov    %edx,%eax
  49d744:	ret
  49d745:	nopl   (%rax)
  49d748:	and    %eax,%edx
  49d74a:	mov    %edx,%eax
  49d74c:	ret

Disassembly of section .fini:
