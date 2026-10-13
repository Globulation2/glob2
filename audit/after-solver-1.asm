void gradient_kernel::propagateFieldCPU<gradient_kernel::propagateTerrainField<gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}, bool, CellRuleTable const&, unsigned int, gradient_kernel::CostIdentity)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_kernel::propagateTerrainField<gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}, bool, CellRuleTable const&, unsigned int, gradient_kernel::CostIdentity)::{lambda(unsigned long)#1}) [clone .cold]

artifacts/gradient-cpu-profile/after-symbols:     file format elf64-x86-64


Disassembly of section .text:

0000000000248451 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_.cold>:
  248451:	mov    %rbx,%rsi
  248454:	mov    0xb0(%rsp),%rbx
  24845c:	mov    %r8d,0x78(%rsp)
  248461:	mov    %r11,0x68(%rsp)
  248466:	mov    %rbx,%rdi
  248469:	mov    %rdx,0x50(%rsp)
  24846e:	movaps %xmm5,0x40(%rsp)
  248473:	movaps %xmm6,0x30(%rsp)
  248478:	call   11c09e <_ZN14GradientBucket4growEm>
  24847d:	mov    (%rbx),%rcx
  248480:	mov    0x18(%rbx),%r9
  248484:	pcmpeqd %xmm3,%xmm3
  248488:	movdqa 0x30(%rsp),%xmm6
  24848e:	movdqa 0x40(%rsp),%xmm5
  248494:	movdqa 0x24fcd64(%rip),%xmm2        # 2745200 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x338>
  24849c:	mov    0x50(%rsp),%rdx
  2484a1:	mov    0x68(%rsp),%r11
  2484a6:	mov    0x78(%rsp),%r8d
  2484ab:	jmp    13b930a <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x178a>
  2484b0:	mov    %r13,%rdi
  2484b3:	mov    %r8d,0x78(%rsp)
  2484b8:	mov    %r11,0x68(%rsp)
  2484bd:	mov    %rdx,0x50(%rsp)
  2484c2:	movaps %xmm5,0x40(%rsp)
  2484c7:	movaps %xmm6,0x30(%rsp)
  2484cc:	call   11c09e <_ZN14GradientBucket4growEm>
  2484d1:	movdqa 0x30(%rsp),%xmm6
  2484d7:	mov    0xb0(%rsp),%rax
  2484df:	pcmpeqd %xmm3,%xmm3
  2484e3:	movdqa 0x40(%rsp),%xmm5
  2484e9:	movdqa 0x24fcd0f(%rip),%xmm2        # 2745200 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x338>
  2484f1:	mov    0x18(%rax),%r9
  2484f5:	mov    0x50(%rsp),%rdx
  2484fa:	mov    0x68(%rsp),%r11
  2484ff:	mov    0x78(%rsp),%r8d
  248504:	jmp    13b92e3 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x1763>
  248509:	mov    %r15,%rdi
  24850c:	mov    %r8,0x70(%rsp)
  248511:	mov    %r10,0x20(%rsp)
  248516:	mov    %rdx,0x18(%rsp)
  24851b:	call   11c09e <_ZN14GradientBucket4growEm>
  248520:	mov    0x18(%r14),%r11
  248524:	mov    0x18(%rsp),%rdx
  248529:	mov    0x20(%rsp),%r10
  24852e:	mov    0x70(%rsp),%r8
  248533:	jmp    13b88a9 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xd29>
  248538:	mov    %rbx,%rdi
  24853b:	mov    %r11,0x18(%rsp)
  248540:	mov    %ecx,(%rsp)
  248543:	call   11c09e <_ZN14GradientBucket4growEm>
  248548:	mov    0xf8(%rsp),%rax
  248550:	mov    (%rbx),%rdx
  248553:	mov    (%rsp),%ecx
  248556:	mov    0x18(%rsp),%r11
  24855b:	mov    0x828(%rax),%r8
  248562:	mov    0x820(%rax),%rdi
  248569:	mov    0x18(%rbx),%rax
  24856d:	lea    0x1(%rax),%rsi
  248571:	mov    %rsi,0x18(%rbx)
  248575:	mov    %ecx,(%rdx,%rax,4)
  248578:	mov    %rbp,%rax
  24857b:	add    $0x1,%rbp
  24857f:	sub    %r14,%rax
  248582:	lea    0x1(%rax,%r15,1),%rax
  248587:	mov    %rax,0x1e0(%rsp)
  24858f:	mov    %r8,%rax
  248592:	sub    %rdi,%rax
  248595:	sar    $0x3,%rax
  248599:	cmp    %rax,%rbp
  24859c:	jb     13b86de <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xb5e>
  2485a2:	mov    %rbp,0xe8(%rsp)
  2485aa:	mov    %rsi,%rdx
  2485ad:	jmp    13b86f2 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xb72>
  2485b2:	mov    %rcx,%rdi
  2485b5:	mov    %rbx,%rsi
  2485b8:	mov    %r11,0x70(%rsp)
  2485bd:	mov    %r8,0x68(%rsp)
  2485c2:	mov    %rcx,(%rsp)
  2485c6:	movaps %xmm5,0x50(%rsp)
  2485cb:	movaps %xmm6,0x30(%rsp)
  2485d0:	call   11c09e <_ZN14GradientBucket4growEm>
  2485d5:	mov    (%rsp),%rcx
  2485d9:	movdqa 0x30(%rsp),%xmm6
  2485df:	pcmpeqd %xmm3,%xmm3
  2485e3:	movdqa 0x50(%rsp),%xmm5
  2485e9:	movdqa 0x24fcc0f(%rip),%xmm2        # 2745200 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x338>
  2485f1:	mov    (%rcx),%rdx
  2485f4:	mov    0x18(%rcx),%r10
  2485f8:	mov    0x68(%rsp),%r8
  2485fd:	mov    0x70(%rsp),%r11
  248602:	jmp    13b8d00 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x1180>
  248607:	mov    %r13,%rdi
  24860a:	mov    %r11,0x70(%rsp)
  24860f:	mov    %r8,0x68(%rsp)
  248614:	mov    %rcx,0x50(%rsp)
  248619:	movaps %xmm5,0x30(%rsp)
  24861e:	movaps %xmm6,(%rsp)
  248622:	call   11c09e <_ZN14GradientBucket4growEm>
  248627:	mov    0x50(%rsp),%rcx
  24862c:	movdqa (%rsp),%xmm6
  248631:	pcmpeqd %xmm3,%xmm3
  248635:	movdqa 0x30(%rsp),%xmm5
  24863b:	movdqa 0x24fcbbd(%rip),%xmm2        # 2745200 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x338>
  248643:	mov    0x18(%rcx),%r10
  248647:	mov    0x68(%rsp),%r8
  24864c:	mov    0x70(%rsp),%r11
  248651:	jmp    13b8ce6 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x1166>
  248656:	mov    %rbp,%rsi
  248659:	mov    %r14,%rdi
  24865c:	mov    %r8,0x70(%rsp)
  248661:	mov    %r10,0x20(%rsp)
  248666:	mov    %rdx,0x18(%rsp)
  24866b:	call   11c09e <_ZN14GradientBucket4growEm>
  248670:	mov    (%r14),%rsi
  248673:	mov    0x18(%r14),%r11
  248677:	mov    0x18(%rsp),%rdx
  24867c:	mov    0x20(%rsp),%r10
  248681:	mov    0x70(%rsp),%r8
  248686:	jmp    13b88c3 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xd43>
  24868b:	mov    %r14,%rsi
  24868e:	mov    %rdx,%rdi
  248691:	mov    %r11,0x60(%rsp)
  248696:	mov    %r9,0x30(%rsp)
  24869b:	mov    %rdx,0x28(%rsp)
  2486a0:	call   11c09e <_ZN14GradientBucket4growEm>
  2486a5:	mov    0x28(%rsp),%rdx
  2486aa:	mov    0x30(%rsp),%r9
  2486af:	mov    0x60(%rsp),%r11
  2486b4:	mov    (%rdx),%rsi
  2486b7:	mov    0x18(%rdx),%rcx
  2486bb:	jmp    13b9d7a <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x21fa>
  2486c0:	mov    %r14,%rdi
  2486c3:	mov    %r11,0x20(%rsp)
  2486c8:	mov    %rcx,0x18(%rsp)
  2486cd:	mov    %r9,(%rsp)
  2486d1:	call   11c09e <_ZN14GradientBucket4growEm>
  2486d6:	mov    (%rsp),%r9
  2486da:	mov    0x18(%rsp),%rcx
  2486df:	mov    0x20(%rsp),%r11
  2486e4:	jmp    13b8277 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6f7>
  2486e9:	mov    %r9,%rdi
  2486ec:	mov    %r11,0x20(%rsp)
  2486f1:	mov    %rcx,0x18(%rsp)
  2486f6:	mov    %r9,(%rsp)
  2486fa:	call   11c09e <_ZN14GradientBucket4growEm>
  2486ff:	mov    (%rsp),%r9
  248703:	mov    0x18(%rsp),%rcx
  248708:	mov    0x20(%rsp),%r11
  24870d:	mov    (%r9),%rdi
  248710:	mov    0x18(%r9),%rax
  248714:	jmp    13b8296 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x716>
  248719:	mov    %r12,%rdi
  24871c:	mov    %r11,0x60(%rsp)
  248721:	mov    %rdx,0x30(%rsp)
  248726:	mov    %r9,0x28(%rsp)
  24872b:	call   11c09e <_ZN14GradientBucket4growEm>
  248730:	mov    0x30(%rsp),%rdx
  248735:	mov    0x28(%rsp),%r9
  24873a:	mov    0x60(%rsp),%r11
  24873f:	mov    0x18(%rdx),%rcx
  248743:	jmp    13b9d60 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x21e0>
  248748:	mov    %rbp,%rdi
  24874b:	mov    %ebx,(%rsp)
  24874e:	call   11c09e <_ZN14GradientBucket4growEm>
  248753:	mov    0x18(%rbp),%rcx
  248757:	mov    0x0(%rbp),%rdi
  24875b:	mov    (%rsp),%r8d
  24875f:	lea    0x1(%rcx),%rsi
  248763:	jmp    13b7ca5 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x125>
  248768:	mov    %r14,%rdi
  24876b:	mov    %r8,0x20(%rsp)
  248770:	mov    %rdx,0x18(%rsp)
  248775:	mov    %r9,(%rsp)
  248779:	call   11c09e <_ZN14GradientBucket4growEm>
  24877e:	mov    (%rsp),%r9
  248782:	mov    0x18(%rsp),%rdx
  248787:	mov    0x20(%rsp),%r8
  24878c:	jmp    13bae07 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x3287>
  248791:	mov    0x98(%rsp),%rdi
  248799:	mov    %r8,0x20(%rsp)
  24879e:	mov    %rdx,0x18(%rsp)
  2487a3:	mov    %r9,(%rsp)
  2487a7:	call   11c09e <_ZN14GradientBucket4growEm>
  2487ac:	mov    (%rsp),%r9
  2487b0:	mov    0x18(%rsp),%rdx
  2487b5:	mov    0x20(%rsp),%r8
  2487ba:	jmp    13bae2a <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x32aa>
  2487bf:	mov    %rbp,%rdi
  2487c2:	mov    %r11,0x20(%rsp)
  2487c7:	mov    %rcx,0x18(%rsp)
  2487cc:	mov    %r9,(%rsp)
  2487d0:	call   11c09e <_ZN14GradientBucket4growEm>
  2487d5:	mov    (%rsp),%r9
  2487d9:	mov    0x18(%rsp),%rcx
  2487de:	mov    0x20(%rsp),%r11
  2487e3:	jmp    13b8240 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6c0>
  2487e8:	mov    %r13,%rdi
  2487eb:	mov    %r11,0x20(%rsp)
  2487f0:	mov    %rcx,0x18(%rsp)
  2487f5:	mov    %r9,(%rsp)
  2487f9:	call   11c09e <_ZN14GradientBucket4growEm>
  2487fe:	mov    (%rsp),%r9
  248802:	mov    0x18(%rsp),%rcx
  248807:	mov    0x20(%rsp),%r11
  24880c:	jmp    13b825c <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6dc>
  248811:	mov    %rbx,%rdi
  248814:	mov    %r8,0x20(%rsp)
  248819:	mov    %rdx,0x18(%rsp)
  24881e:	mov    %r9,(%rsp)
  248822:	call   11c09e <_ZN14GradientBucket4growEm>
  248827:	mov    (%rsp),%r9
  24882b:	mov    0x18(%rsp),%rdx
  248830:	mov    0x20(%rsp),%r8
  248835:	jmp    13bae45 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x32c5>
  24883a:	mov    %rdx,%rsi
  24883d:	mov    %r15,%rdi
  248840:	mov    %r8,0x18(%rsp)
  248845:	mov    %r9,(%rsp)
  248849:	call   11c09e <_ZN14GradientBucket4growEm>
  24884e:	mov    (%r15),%rsi
  248851:	mov    0x18(%r15),%rax
  248855:	mov    (%rsp),%r9
  248859:	mov    0x18(%rsp),%r8
  24885e:	jmp    13bae63 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x32e3>
  248863:	mov    %rbp,%rdi
  248866:	mov    %r8,(%rsp)
  24886a:	call   11c09e <_ZN14GradientBucket4growEm>
  24886f:	mov    0x0(%rbp),%rdx
  248873:	mov    (%rsp),%r8
  248877:	mov    0xf8(%rsp),%rax
  24887f:	mov    0x828(%rax),%rdi
  248886:	mov    0x820(%rax),%rcx
  24888d:	mov    0x18(%rbp),%rax
  248891:	lea    0x1(%rax),%rsi
  248895:	mov    %rsi,0x18(%rbp)
  248899:	mov    %r13d,(%rdx,%rax,4)
  24889d:	mov    %r12,%rax
  2488a0:	add    $0x1,%r12
  2488a4:	sub    %r15,%rax
  2488a7:	lea    0x1(%rax,%r8,1),%rax
  2488ac:	mov    %rax,0x1e0(%rsp)
  2488b4:	mov    %rdi,%rax
  2488b7:	sub    %rcx,%rax
  2488ba:	sar    $0x3,%rax
  2488be:	cmp    %rax,%r12
  2488c1:	jb     13b7ff8 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x478>
  2488c7:	mov    %rsi,0xb0(%rsp)
  2488cf:	mov    %r12,%r11
  2488d2:	jmp    13b800e <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x48e>
