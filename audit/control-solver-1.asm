void gradient_kernel::propagateFieldCPU<gradient_kernel::propagateTerrainField<gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}, bool, CellRuleTable const&, unsigned int, gradient_kernel::CostIdentity)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_kernel::propagateTerrainField<gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}>(unsigned short*, int, int, field::Grid, GradientWorkspace&, gradient_preparation::propagate(gradient_preparation::Request const&, SimulationSnapshot::Handle const&, unsigned short*, GradientWorkspace&)::{lambda(unsigned long)#1}, bool, CellRuleTable const&, unsigned int, gradient_kernel::CostIdentity)::{lambda(unsigned long)#1}) [clone .cold]

artifacts/gradient-cpu-profile/control-symbols:     file format elf64-x86-64


Disassembly of section .text:

000000000023ebfe <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_.cold>:
  23ebfe:	mov    %rbp,%rsi
  23ec01:	mov    %r12,%rdi
  23ec04:	mov    %r11,0x70(%rsp)
  23ec09:	mov    %r10,0x88(%rsp)
  23ec11:	movaps %xmm7,0x60(%rsp)
  23ec16:	movaps %xmm6,0x50(%rsp)
  23ec1b:	movaps %xmm9,0x40(%rsp)
  23ec21:	movaps %xmm8,0x30(%rsp)
  23ec27:	call   11757c <_ZN14GradientBucket4growEm>
  23ec2c:	mov    (%r12),%rcx
  23ec30:	pcmpeqd %xmm4,%xmm4
  23ec34:	mov    0x18(%r12),%rax
  23ec39:	movdqa 0x30(%rsp),%xmm8
  23ec40:	movdqa 0x40(%rsp),%xmm9
  23ec47:	movdqa 0x50(%rsp),%xmm6
  23ec4d:	movdqa 0x60(%rsp),%xmm7
  23ec53:	movdqa 0x2412f55(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ec5b:	mov    0x70(%rsp),%r11
  23ec60:	mov    0x88(%rsp),%r10
  23ec68:	jmp    12f8166 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x2ce6>
  23ec6d:	mov    0xb0(%rsp),%rdi
  23ec75:	mov    %r10d,0x90(%rsp)
  23ec7d:	mov    %rdx,0x88(%rsp)
  23ec85:	movaps %xmm5,0x50(%rsp)
  23ec8a:	movaps %xmm6,0x30(%rsp)
  23ec8f:	call   11757c <_ZN14GradientBucket4growEm>
  23ec94:	movdqa 0x30(%rsp),%xmm6
  23ec9a:	mov    0xb8(%rsp),%rax
  23eca2:	pcmpeqd %xmm3,%xmm3
  23eca6:	movdqa 0x50(%rsp),%xmm5
  23ecac:	movdqa 0x2412efc(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ecb4:	mov    0x18(%rax),%r8
  23ecb8:	mov    0x88(%rsp),%rdx
  23ecc0:	mov    0x90(%rsp),%r10d
  23ecc8:	jmp    12f6722 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x12a2>
  23eccd:	mov    %rcx,%rdi
  23ecd0:	mov    %rbp,%rsi
  23ecd3:	mov    %rcx,(%rsp)
  23ecd7:	movaps %xmm5,0x70(%rsp)
  23ecdc:	movaps %xmm6,0x10(%rsp)
  23ece1:	call   11757c <_ZN14GradientBucket4growEm>
  23ece6:	mov    (%rsp),%rcx
  23ecea:	movdqa 0x10(%rsp),%xmm6
  23ecf0:	pcmpeqd %xmm3,%xmm3
  23ecf4:	movdqa 0x70(%rsp),%xmm5
  23ecfa:	movdqa 0x2412eae(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ed02:	mov    (%rcx),%rdx
  23ed05:	mov    0x18(%rcx),%rdi
  23ed09:	jmp    12f61a9 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xd29>
  23ed0e:	mov    %r15,%rdi
  23ed11:	mov    %r11,0xb0(%rsp)
  23ed19:	mov    %r9,0x70(%rsp)
  23ed1e:	mov    %rdx,0x40(%rsp)
  23ed23:	movaps %xmm7,0x60(%rsp)
  23ed28:	movaps %xmm6,0x50(%rsp)
  23ed2d:	movaps %xmm9,0x30(%rsp)
  23ed33:	movaps %xmm8,0x10(%rsp)
  23ed39:	call   11757c <_ZN14GradientBucket4growEm>
  23ed3e:	movdqa 0x10(%rsp),%xmm8
  23ed45:	mov    0x40(%rsp),%rdx
  23ed4a:	pcmpeqd %xmm4,%xmm4
  23ed4e:	movdqa 0x30(%rsp),%xmm9
  23ed55:	movdqa 0x50(%rsp),%xmm6
  23ed5b:	movdqa 0x60(%rsp),%xmm7
  23ed61:	movdqa 0x2412e47(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ed69:	mov    0x70(%rsp),%r9
  23ed6e:	mov    0xb0(%rsp),%r11
  23ed76:	jmp    12f5b55 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6d5>
  23ed7b:	mov    %rbx,%rdi
  23ed7e:	mov    %r11,0xb0(%rsp)
  23ed86:	mov    %r9,0x70(%rsp)
  23ed8b:	mov    %rdx,0x40(%rsp)
  23ed90:	movaps %xmm7,0x60(%rsp)
  23ed95:	movaps %xmm6,0x50(%rsp)
  23ed9a:	movaps %xmm9,0x30(%rsp)
  23eda0:	movaps %xmm8,0x10(%rsp)
  23eda6:	call   11757c <_ZN14GradientBucket4growEm>
  23edab:	movdqa 0x10(%rsp),%xmm8
  23edb2:	mov    0x40(%rsp),%rdx
  23edb7:	pcmpeqd %xmm4,%xmm4
  23edbb:	movdqa 0x30(%rsp),%xmm9
  23edc2:	movdqa 0x50(%rsp),%xmm6
  23edc8:	movdqa 0x60(%rsp),%xmm7
  23edce:	movdqa 0x2412dda(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23edd6:	mov    0x70(%rsp),%r9
  23eddb:	mov    0xb0(%rsp),%r11
  23ede3:	jmp    12f5b70 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6f0>
  23ede8:	mov    %r13,%rsi
  23edeb:	mov    %r15,%rdi
  23edee:	mov    %r9,0x60(%rsp)
  23edf3:	movaps %xmm5,0x40(%rsp)
  23edf8:	movaps %xmm6,0x30(%rsp)
  23edfd:	call   11757c <_ZN14GradientBucket4growEm>
  23ee02:	mov    (%r15),%rcx
  23ee05:	mov    0x18(%r15),%r8
  23ee09:	pcmpeqd %xmm3,%xmm3
  23ee0d:	movdqa 0x30(%rsp),%xmm6
  23ee13:	movdqa 0x40(%rsp),%xmm5
  23ee19:	movdqa 0x2412d8f(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ee21:	mov    0x60(%rsp),%r9
  23ee26:	jmp    12f6e12 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x1992>
  23ee2b:	mov    %rdi,%rsi
  23ee2e:	mov    %rbp,%rdi
  23ee31:	mov    %rcx,0x30(%rsp)
  23ee36:	call   11757c <_ZN14GradientBucket4growEm>
  23ee3b:	mov    0x18(%rbp),%rsi
  23ee3f:	mov    0x0(%rbp),%r10
  23ee43:	mov    $0xffff,%r9d
  23ee49:	mov    0x30(%rsp),%rcx
  23ee4e:	lea    0x1(%rsi),%rdi
  23ee52:	jmp    12f55c2 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x142>
  23ee57:	mov    %r12,%rdi
  23ee5a:	mov    %r9,0x60(%rsp)
  23ee5f:	movaps %xmm5,0x40(%rsp)
  23ee64:	movaps %xmm6,0x30(%rsp)
  23ee69:	call   11757c <_ZN14GradientBucket4growEm>
  23ee6e:	mov    0x18(%r15),%r8
  23ee72:	movdqa 0x30(%rsp),%xmm6
  23ee78:	pcmpeqd %xmm3,%xmm3
  23ee7c:	movdqa 0x40(%rsp),%xmm5
  23ee82:	movdqa 0x2412d26(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ee8a:	mov    0x60(%rsp),%r9
  23ee8f:	jmp    12f6df8 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x1978>
  23ee94:	mov    0xb8(%rsp),%r14
  23ee9c:	mov    %r12,%rsi
  23ee9f:	mov    %r10d,0x90(%rsp)
  23eea7:	mov    %rdx,0x88(%rsp)
  23eeaf:	mov    %r14,%rdi
  23eeb2:	movaps %xmm5,0x50(%rsp)
  23eeb7:	movaps %xmm6,0x30(%rsp)
  23eebc:	call   11757c <_ZN14GradientBucket4growEm>
  23eec1:	mov    (%r14),%rcx
  23eec4:	mov    0x18(%r14),%r8
  23eec8:	pcmpeqd %xmm3,%xmm3
  23eecc:	movdqa 0x30(%rsp),%xmm6
  23eed2:	movdqa 0x50(%rsp),%xmm5
  23eed8:	movdqa 0x2412cd0(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23eee0:	mov    0x88(%rsp),%rdx
  23eee8:	mov    0x90(%rsp),%r10d
  23eef0:	jmp    12f6749 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x12c9>
  23eef5:	mov    %rbx,%rdi
  23eef8:	mov    %r10,0xb0(%rsp)
  23ef00:	mov    %r11,0xa8(%rsp)
  23ef08:	mov    %r9,0x70(%rsp)
  23ef0d:	movaps %xmm7,0x60(%rsp)
  23ef12:	movaps %xmm6,0x50(%rsp)
  23ef17:	movaps %xmm9,0x40(%rsp)
  23ef1d:	movaps %xmm8,0x30(%rsp)
  23ef23:	call   11757c <_ZN14GradientBucket4growEm>
  23ef28:	movdqa 0x30(%rsp),%xmm8
  23ef2f:	mov    0x70(%rsp),%r9
  23ef34:	pcmpeqd %xmm4,%xmm4
  23ef38:	movdqa 0x40(%rsp),%xmm9
  23ef3f:	movdqa 0x50(%rsp),%xmm6
  23ef45:	movdqa 0x60(%rsp),%xmm7
  23ef4b:	movdqa 0x2412c5d(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23ef53:	mov    0xa8(%rsp),%r11
  23ef5b:	mov    0xb0(%rsp),%r10
  23ef63:	jmp    12f7848 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x23c8>
  23ef68:	mov    %rcx,%rsi
  23ef6b:	mov    %r9,%rdi
  23ef6e:	mov    %r11,0x70(%rsp)
  23ef73:	mov    %r9,0x10(%rsp)
  23ef78:	movaps %xmm7,0x60(%rsp)
  23ef7d:	movaps %xmm6,0x50(%rsp)
  23ef82:	movaps %xmm9,0x40(%rsp)
  23ef88:	movaps %xmm8,0x30(%rsp)
  23ef8e:	call   11757c <_ZN14GradientBucket4growEm>
  23ef93:	mov    0x10(%rsp),%r9
  23ef98:	mov    0x70(%rsp),%r11
  23ef9d:	pcmpeqd %xmm4,%xmm4
  23efa1:	movdqa 0x30(%rsp),%xmm8
  23efa8:	movdqa 0x40(%rsp),%xmm9
  23efaf:	mov    (%r9),%rsi
  23efb2:	mov    0x18(%r9),%rax
  23efb6:	movdqa 0x50(%rsp),%xmm6
  23efbc:	movdqa 0x60(%rsp),%xmm7
  23efc2:	movdqa 0x2412be6(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23efca:	jmp    12f5b8f <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x70f>
  23efcf:	mov    0xc0(%rsp),%rdi
  23efd7:	mov    %rcx,0x70(%rsp)
  23efdc:	movaps %xmm5,0x10(%rsp)
  23efe1:	movaps %xmm6,(%rsp)
  23efe5:	call   11757c <_ZN14GradientBucket4growEm>
  23efea:	mov    0x70(%rsp),%rcx
  23efef:	movdqa (%rsp),%xmm6
  23eff4:	pcmpeqd %xmm3,%xmm3
  23eff8:	movdqa 0x10(%rsp),%xmm5
  23effe:	movdqa 0x2412baa(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f006:	mov    0x18(%rcx),%rdi
  23f00a:	jmp    12f618f <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xd0f>
  23f00f:	mov    %r12,%rdi
  23f012:	mov    %r9,(%rsp)
  23f016:	add    $0x1,%rbp
  23f01a:	add    $0x1,%r13
  23f01e:	call   11757c <_ZN14GradientBucket4growEm>
  23f023:	mov    (%r12),%r10
  23f027:	mov    (%rsp),%r9
  23f02b:	pcmpeqd %xmm3,%xmm3
  23f02f:	mov    0x168(%rsp),%rax
  23f037:	movdqa 0x2412b71(%rip),%xmm2        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f03f:	mov    0x828(%rax),%rcx
  23f046:	mov    0x820(%rax),%rdx
  23f04d:	mov    0x18(%r12),%rax
  23f052:	lea    0x1(%rax),%rsi
  23f056:	mov    %rsi,0x18(%r12)
  23f05b:	mov    %r15d,(%r10,%rax,4)
  23f05f:	mov    %rcx,%rax
  23f062:	sub    %rdx,%rax
  23f065:	sar    $0x3,%rax
  23f069:	cmp    %rax,%rbp
  23f06c:	jb     12f5fe1 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xb61>
  23f072:	mov    %rbp,0x108(%rsp)
  23f07a:	mov    %r13,0xd8(%rsp)
  23f082:	mov    %rsi,0x100(%rsp)
  23f08a:	jmp    12f6005 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0xb85>
  23f08f:	mov    %rbp,%rdi
  23f092:	mov    %r11,0xb0(%rsp)
  23f09a:	mov    %r9,0x70(%rsp)
  23f09f:	mov    %rdx,0x40(%rsp)
  23f0a4:	movaps %xmm7,0x60(%rsp)
  23f0a9:	movaps %xmm6,0x50(%rsp)
  23f0ae:	movaps %xmm9,0x30(%rsp)
  23f0b4:	movaps %xmm8,0x10(%rsp)
  23f0ba:	call   11757c <_ZN14GradientBucket4growEm>
  23f0bf:	movdqa 0x10(%rsp),%xmm8
  23f0c6:	mov    0x40(%rsp),%rdx
  23f0cb:	pcmpeqd %xmm4,%xmm4
  23f0cf:	movdqa 0x30(%rsp),%xmm9
  23f0d6:	movdqa 0x50(%rsp),%xmm6
  23f0dc:	movdqa 0x60(%rsp),%xmm7
  23f0e2:	movdqa 0x2412ac6(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f0ea:	mov    0x70(%rsp),%r9
  23f0ef:	mov    0xb0(%rsp),%r11
  23f0f7:	jmp    12f5b3a <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x6ba>
  23f0fc:	mov    %r11,%rdi
  23f0ff:	mov    %r10,0x88(%rsp)
  23f107:	mov    %r11,0x30(%rsp)
  23f10c:	movaps %xmm7,0x70(%rsp)
  23f111:	movaps %xmm6,0x60(%rsp)
  23f116:	movaps %xmm9,0x50(%rsp)
  23f11c:	movaps %xmm8,0x40(%rsp)
  23f122:	call   11757c <_ZN14GradientBucket4growEm>
  23f127:	mov    0x30(%rsp),%r11
  23f12c:	pcmpeqd %xmm4,%xmm4
  23f130:	movdqa 0x40(%rsp),%xmm8
  23f137:	movdqa 0x50(%rsp),%xmm9
  23f13e:	movdqa 0x60(%rsp),%xmm6
  23f144:	movdqa 0x70(%rsp),%xmm7
  23f14a:	movdqa 0x2412a5e(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f152:	mov    0x88(%rsp),%r10
  23f15a:	jmp    12f8122 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x2ca2>
  23f15f:	mov    0x100(%rsp),%rdi
  23f167:	mov    %r10,0x88(%rsp)
  23f16f:	mov    %r11,0x70(%rsp)
  23f174:	movaps %xmm7,0x60(%rsp)
  23f179:	movaps %xmm6,0x50(%rsp)
  23f17e:	movaps %xmm9,0x40(%rsp)
  23f184:	movaps %xmm8,0x30(%rsp)
  23f18a:	call   11757c <_ZN14GradientBucket4growEm>
  23f18f:	movdqa 0x30(%rsp),%xmm8
  23f196:	mov    0x70(%rsp),%r11
  23f19b:	pcmpeqd %xmm4,%xmm4
  23f19f:	movdqa 0x40(%rsp),%xmm9
  23f1a6:	movdqa 0x50(%rsp),%xmm6
  23f1ac:	movdqa 0x60(%rsp),%xmm7
  23f1b2:	movdqa 0x24129f6(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f1ba:	mov    0x88(%rsp),%r10
  23f1c2:	jmp    12f8145 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x2cc5>
  23f1c7:	mov    %r15,%rdi
  23f1ca:	mov    %r10,0xb0(%rsp)
  23f1d2:	mov    %r11,0xa8(%rsp)
  23f1da:	mov    %r9,0x70(%rsp)
  23f1df:	movaps %xmm7,0x60(%rsp)
  23f1e4:	movaps %xmm6,0x50(%rsp)
  23f1e9:	movaps %xmm9,0x40(%rsp)
  23f1ef:	movaps %xmm8,0x30(%rsp)
  23f1f5:	call   11757c <_ZN14GradientBucket4growEm>
  23f1fa:	movdqa 0x30(%rsp),%xmm8
  23f201:	mov    0x70(%rsp),%r9
  23f206:	pcmpeqd %xmm4,%xmm4
  23f20a:	movdqa 0x40(%rsp),%xmm9
  23f211:	movdqa 0x50(%rsp),%xmm6
  23f217:	movdqa 0x60(%rsp),%xmm7
  23f21d:	movdqa 0x241298b(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f225:	mov    0xa8(%rsp),%r11
  23f22d:	mov    0xb0(%rsp),%r10
  23f235:	jmp    12f7863 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x23e3>
  23f23a:	mov    %r9,%rdi
  23f23d:	mov    %r10,0xb0(%rsp)
  23f245:	mov    %r11,0xa8(%rsp)
  23f24d:	mov    %r9,0x30(%rsp)
  23f252:	movaps %xmm7,0x70(%rsp)
  23f257:	movaps %xmm6,0x60(%rsp)
  23f25c:	movaps %xmm9,0x50(%rsp)
  23f262:	movaps %xmm8,0x40(%rsp)
  23f268:	call   11757c <_ZN14GradientBucket4growEm>
  23f26d:	mov    0x30(%rsp),%r9
  23f272:	pcmpeqd %xmm4,%xmm4
  23f276:	movdqa 0x40(%rsp),%xmm8
  23f27d:	movdqa 0x50(%rsp),%xmm9
  23f284:	movdqa 0x60(%rsp),%xmm6
  23f28a:	movdqa 0x70(%rsp),%xmm7
  23f290:	movdqa 0x2412918(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f298:	mov    0xa8(%rsp),%r11
  23f2a0:	mov    0xb0(%rsp),%r10
  23f2a8:	jmp    12f787e <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x23fe>
  23f2ad:	mov    %r11,%rdi
  23f2b0:	mov    %rbp,%rsi
  23f2b3:	mov    %r10,0xb0(%rsp)
  23f2bb:	mov    %r9,0xa8(%rsp)
  23f2c3:	mov    %r11,0x30(%rsp)
  23f2c8:	movaps %xmm7,0x70(%rsp)
  23f2cd:	movaps %xmm6,0x60(%rsp)
  23f2d2:	movaps %xmm9,0x50(%rsp)
  23f2d8:	movaps %xmm8,0x40(%rsp)
  23f2de:	call   11757c <_ZN14GradientBucket4growEm>
  23f2e3:	mov    0x30(%rsp),%r11
  23f2e8:	pcmpeqd %xmm4,%xmm4
  23f2ec:	movdqa 0x40(%rsp),%xmm8
  23f2f3:	movdqa 0x50(%rsp),%xmm9
  23f2fa:	movdqa 0x60(%rsp),%xmm6
  23f300:	mov    (%r11),%rcx
  23f303:	mov    0x18(%r11),%rax
  23f307:	movdqa 0x70(%rsp),%xmm7
  23f30d:	movdqa 0x241289b(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f315:	mov    0xa8(%rsp),%r9
  23f31d:	mov    0xb0(%rsp),%r10
  23f325:	jmp    12f789c <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x241c>
  23f32a:	mov    %rbx,%rdi
  23f32d:	mov    %r10,0x88(%rsp)
  23f335:	mov    %r11,0x70(%rsp)
  23f33a:	movaps %xmm7,0x60(%rsp)
  23f33f:	movaps %xmm6,0x50(%rsp)
  23f344:	movaps %xmm9,0x40(%rsp)
  23f34a:	movaps %xmm8,0x30(%rsp)
  23f350:	call   11757c <_ZN14GradientBucket4growEm>
  23f355:	movdqa 0x30(%rsp),%xmm8
  23f35c:	mov    0x70(%rsp),%r11
  23f361:	pcmpeqd %xmm4,%xmm4
  23f365:	movdqa 0x40(%rsp),%xmm9
  23f36c:	movdqa 0x50(%rsp),%xmm6
  23f372:	movdqa 0x60(%rsp),%xmm7
  23f378:	movdqa 0x2412830(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f380:	mov    0x88(%rsp),%r10
  23f388:	jmp    12f8107 <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x2c87>
  23f38d:	mov    %rbp,%rdi
  23f390:	add    $0x1,%rbx
  23f394:	add    $0x1,%r12
  23f398:	call   11757c <_ZN14GradientBucket4growEm>
  23f39d:	mov    0x0(%rbp),%r9
  23f3a1:	pcmpeqd %xmm4,%xmm4
  23f3a5:	mov    0x168(%rsp),%rax
  23f3ad:	movdqa 0x24127fb(%rip),%xmm3        # 2651bb0 <_ZZNSt19_Sp_make_shared_tag5_S_tiEvE5__tag+0x228>
  23f3b5:	mov    0x828(%rax),%rcx
  23f3bc:	mov    0x820(%rax),%rdx
  23f3c3:	mov    0x18(%rbp),%rax
  23f3c7:	lea    0x1(%rax),%rsi
  23f3cb:	mov    %rsi,0x18(%rbp)
  23f3cf:	mov    %r14d,(%r9,%rax,4)
  23f3d3:	mov    %rcx,%rax
  23f3d6:	sub    %rdx,%rax
  23f3d9:	sar    $0x3,%rax
  23f3dd:	cmp    %rax,%rbx
  23f3e0:	jb     12f58ef <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x46f>
  23f3e6:	mov    %rbx,0x178(%rsp)
  23f3ee:	mov    %r15,%r8
  23f3f1:	mov    %r12,0xd8(%rsp)
  23f3f9:	mov    %r9,(%rsp)
  23f3fd:	mov    %rsi,0x170(%rsp)
  23f405:	jmp    12f591a <_ZN15gradient_kernel17propagateFieldCPUIZNS_21propagateTerrainFieldIZN20gradient_preparation9propagateERKNS2_7RequestERKN18SimulationSnapshot6HandleEPtR17GradientWorkspaceEUlmE_EEvSA_iiN5field4GridESC_T_bRK13CellRuleTablejNS_12CostIdentityEEUlmE_EEvSA_iiSF_SC_SG_+0x49a>
