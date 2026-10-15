_ZN4Team9integrityEv:
.LVL197:
.LFB14464:
	.loc 16 91 1 is_stmt 1 view -0
	.cfi_startproc
	.loc 16 92 2 view .LVU1002
	.loc 16 91 1 is_stmt 0 view .LVU1003
	stp	x29, x30, [sp, -64]!
	.cfi_def_cfa_offset 64
	.cfi_offset 29, -64
	.cfi_offset 30, -56
	mov	x29, sp
	str	x23, [sp, 48]
	.cfi_offset 23, -16
	mov	x23, x0
	.loc 16 92 2 view .LVU1004
	ldr	w0, [x0, 1240]
.LVL198:
	.loc 16 91 1 view .LVU1005
	stp	x19, x20, [sp, 16]
	stp	x21, x22, [sp, 32]
	.cfi_offset 19, -48
	.cfi_offset 20, -40
	.cfi_offset 21, -32
	.cfi_offset 22, -24
	.loc 16 92 2 view .LVU1006
	cmp	w0, 200
	bgt	.L308
	add	x21, x23, 112
	mov	x19, 0
	.loc 16 95 14 view .LVU1007
	mov	x20, 0
	b	.L206
.LVL199:
	.p2align 2,,3
.L311:
.LBB7759:
.LBB7760:
.LBB7761:
.LBI7761:
	.loc 18 20 33 is_stmt 1 discriminator 2 view .LVU1008
.LBB7762:
.LBI7762:
	.file 19 "/usr/include/c++/11/span"
	.loc 19 211 2 discriminator 2 view .LVU1009
.LBB7763:
.LBI7763:
	.file 20 "/usr/include/c++/11/bits/ranges_base.h"
	.loc 20 536 2 discriminator 2 view .LVU1010
	.loc 20 536 2 is_stmt 0 discriminator 2 view .LVU1011
.LBE7763:
.LBB7764:
.LBI7764:
	.loc 20 414 2 is_stmt 1 discriminator 2 view .LVU1012
	.loc 20 414 2 is_stmt 0 discriminator 2 view .LVU1013
.LBE7764:
.LBB7765:
.LBI7765:
	.loc 19 155 2 is_stmt 1 discriminator 2 view .LVU1014
.LBB7766:
.LBI7766:
	.file 21 "/usr/include/c++/11/bits/ptr_traits.h"
	.loc 21 198 5 discriminator 2 view .LVU1015
	.loc 21 198 5 is_stmt 0 discriminator 2 view .LVU1016
.LBE7766:
.LBB7767:
.LBI7767:
	.loc 19 90 2 is_stmt 1 discriminator 2 view .LVU1017
	.loc 19 90 2 is_stmt 0 discriminator 2 view .LVU1018
.LBE7767:
.LBE7765:
.LBE7762:
.LBE7761:
.LBB7768:
.LBI7768:
	.loc 19 276 7 is_stmt 1 discriminator 2 view .LVU1019
.LBB7769:
	.loc 19 278 2 discriminator 2 view .LVU1020
	.loc 19 278 2 is_stmt 0 discriminator 2 view .LVU1021
.LBE7769:
.LBE7768:
	.loc 16 101 4 discriminator 2 view .LVU1022
	ldrh	w0, [x3, x20, lsl 1]
	cmp	w0, w19
	bne	.L212
.LVL200:
.LBB7770:
.LBI7770:
	.loc 18 21 22 is_stmt 1 discriminator 4 view .LVU1023
.LBB7771:
.LBI7771:
	.loc 19 211 2 discriminator 4 view .LVU1024
.LBB7772:
.LBI7772:
	.loc 20 536 2 discriminator 4 view .LVU1025
.LBB7773:
.LBI7773:
	.loc 5 1172 7 discriminator 4 view .LVU1026
	.loc 5 1172 7 is_stmt 0 discriminator 4 view .LVU1027
.LBE7773:
.LBE7772:
.LBB7774:
.LBI7774:
	.loc 20 414 2 is_stmt 1 discriminator 4 view .LVU1028
	.loc 20 414 2 is_stmt 0 discriminator 4 view .LVU1029
.LBE7774:
.LBB7775:
.LBI7775:
	.loc 19 155 2 is_stmt 1 discriminator 4 view .LVU1030
.LBB7776:
.LBI7776:
	.loc 21 198 5 discriminator 4 view .LVU1031
	.loc 21 198 5 is_stmt 0 discriminator 4 view .LVU1032
.LBE7776:
.LBB7777:
.LBI7777:
	.loc 19 90 2 is_stmt 1 discriminator 4 view .LVU1033
	.loc 19 90 2 is_stmt 0 discriminator 4 view .LVU1034
.LBE7777:
.LBE7775:
.LBE7771:
.LBE7770:
.LBB7778:
.LBI7778:
	.loc 19 276 7 is_stmt 1 discriminator 4 view .LVU1035
.LBB7779:
	.loc 19 278 2 discriminator 4 view .LVU1036
	.loc 19 278 2 is_stmt 0 discriminator 4 view .LVU1037
.LBE7779:
.LBE7778:
	.loc 16 101 4 discriminator 4 view .LVU1038
	ldr	x0, [x23, 136]
	ldr	x0, [x0, x20, lsl 3]
	cmp	x0, x2
	bne	.L212
	.loc 16 101 146 is_stmt 1 view .LVU1039
	.loc 16 102 4 view .LVU1040
	.loc 16 103 4 is_stmt 0 view .LVU1041
	bl	_ZN8Building9integrityEv
.LVL201:
	.loc 16 102 4 view .LVU1042
	add	x20, x20, 1
.LVL202:
	.loc 16 103 4 is_stmt 1 view .LVU1043
	ands	w22, w0, 255
	beq	.L309
.LVL203:
.L211:
	.loc 16 103 4 is_stmt 0 view .LVU1044
.LBE7760:
	.loc 16 96 2 is_stmt 1 discriminator 2 view .LVU1045
	.loc 16 96 19 discriminator 2 view .LVU1046
	add	x19, x19, 1
.LVL204:
	.loc 16 96 19 is_stmt 0 discriminator 2 view .LVU1047
	cmp	x19, 1024
	beq	.L310
.LVL205:
.L206:
.LBB7815:
	.loc 16 98 3 is_stmt 1 view .LVU1048
	.loc 16 98 13 is_stmt 0 view .LVU1049
	ldr	x1, [x23, 56]
.LBB7780:
.LBB7781:
.LBB7782:
	.loc 5 919 66 view .LVU1050
	ldr	x3, [x21]
.LBE7782:
.LBE7781:
.LBE7780:
	.loc 16 98 13 view .LVU1051
	ldr	x2, [x1, x19, lsl 3]
.LVL206:
	.loc 16 99 3 is_stmt 1 view .LVU1052
.LBB7787:
.LBB7785:
.LBB7783:
	.loc 5 919 40 is_stmt 0 view .LVU1053
	ldr	x1, [x21, 8]
.LBE7783:
.LBE7785:
.LBE7787:
	.loc 16 99 3 view .LVU1054
	cbz	x2, .L211
	.loc 16 101 4 is_stmt 1 view .LVU1055
.LVL207:
.LBB7788:
.LBI7780:
	.loc 18 18 14 view .LVU1056
.LBB7786:
.LBI7781:
	.loc 5 918 7 view .LVU1057
.LBB7784:
	.loc 5 919 50 is_stmt 0 view .LVU1058
	sub	x1, x1, x3
.LBE7784:
.LBE7786:
.LBE7788:
	.loc 16 101 4 view .LVU1059
	cmp	x20, x1, asr 1
	bcc	.L311
.L212:
	.loc 16 101 4 is_stmt 1 discriminator 5 view .LVU1060
.LVL208:
.LBB7789:
.LBI7789:
	.loc 2 611 5 discriminator 5 view .LVU1061
.LBB7790:
	.loc 2 616 18 is_stmt 0 discriminator 5 view .LVU1062
	adrp	x20, :got:_ZSt4cerr
.LVL209:
	.loc 2 616 18 discriminator 5 view .LVU1063
	mov	x2, 18
.LVL210:
	.loc 2 616 18 discriminator 5 view .LVU1064
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL211:
	.loc 2 616 18 discriminator 5 view .LVU1065
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL212:
	.loc 2 616 18 discriminator 5 view .LVU1066
.LBE7790:
.LBE7789:
.LBB7791:
.LBI7791:
	.loc 2 611 5 is_stmt 1 discriminator 5 view .LVU1067
.LBB7792:
	.loc 2 616 18 is_stmt 0 discriminator 5 view .LVU1068
	adrp	x1, .LC12
	mov	x0, x19
	add	x1, x1, :lo12:.LC12
	mov	x2, 126
.LVL213:
.L307:
	.loc 2 616 18 discriminator 5 view .LVU1069
.LBE7792:
.LBE7791:
.LBE7815:
.LBE7759:
.LBB7818:
.LBB7819:
	.loc 2 616 18 view .LVU1070
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL214:
	.loc 2 616 18 view .LVU1071
.LBE7819:
.LBE7818:
.LBB7821:
.LBI7821:
	.loc 2 108 7 is_stmt 1 view .LVU1072
.LBB7822:
.LBI7822:
	.loc 2 684 5 view .LVU1073
.LBB7823:
	.loc 2 685 19 is_stmt 0 view .LVU1074
	ldr	x0, [x19]
	ldr	x0, [x0, -24]
	add	x0, x0, x19
	ldr	x19, [x0, 240]
.LVL215:
.LBB7824:
.LBI7824:
	.loc 3 449 7 is_stmt 1 view .LVU1075
.LBB7825:
.LBI7825:
	.loc 3 47 5 view .LVU1076
.LBB7826:
	.loc 3 49 7 is_stmt 0 view .LVU1077
	cbz	x19, .L214
.LVL216:
	.loc 3 49 7 view .LVU1078
.LBE7826:
.LBE7825:
.LBE7824:
.LBE7823:
.LBE7822:
.LBE7821:
.LBB7832:
.LBB7833:
.LBB7834:
.LBB7835:
.LBB7836:
.LBB7837:
.LBI7837:
	.loc 1 875 7 is_stmt 1 view .LVU1079
.LBB7838:
	.loc 1 877 2 view .LVU1080
	ldrb	w0, [x19, 56]
	cbz	w0, .L242
	.loc 1 878 4 view .LVU1081
	.loc 1 878 51 is_stmt 0 view .LVU1082
	ldrb	w1, [x19, 67]
.LVL217:
.L243:
	.loc 1 878 51 view .LVU1083
.LBE7838:
.LBE7837:
.LBE7836:
	.loc 2 685 19 view .LVU1084
	ldr	x0, [x20, #:got_lo12:_ZSt4cerr]
	.loc 2 685 46 view .LVU1085
	mov	w22, 0
	.loc 2 685 19 view .LVU1086
	bl	_ZNSo3putEc
.LVL218:
.LBB7847:
.LBI7847:
	.loc 2 706 5 is_stmt 1 view .LVU1087
.LBB7848:
	.loc 2 707 24 is_stmt 0 view .LVU1088
	bl	_ZNSo5flushEv
.LVL219:
.L210:
	.loc 2 707 24 view .LVU1089
.LBE7848:
.LBE7847:
.LBE7835:
.LBE7834:
.LBE7833:
.LBE7832:
	.loc 16 135 1 view .LVU1090
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
.LVL220:
	.loc 16 135 1 view .LVU1091
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
.LVL221:
	.p2align 2,,3
.L242:
	.cfi_restore_state
.LBB7901:
.LBB7855:
.LBB7853:
.LBB7851:
.LBB7849:
.LBB7845:
.LBB7843:
.LBB7839:
.LBI7839:
	.loc 1 875 7 is_stmt 1 view .LVU1092
.LBB7840:
	.loc 1 879 2 view .LVU1093
	.loc 1 879 21 is_stmt 0 view .LVU1094
	mov	x0, x19
	bl	_ZNKSt5ctypeIcE13_M_widen_initEv
.LVL222:
	.loc 1 880 2 is_stmt 1 view .LVU1095
	.loc 1 880 23 is_stmt 0 view .LVU1096
	ldr	x2, [x19]
	adrp	x0, :got:_ZNKSt5ctypeIcE8do_widenEc
	mov	w1, 10
	ldr	x0, [x0, #:got_lo12:_ZNKSt5ctypeIcE8do_widenEc]
	ldr	x2, [x2, 48]
	cmp	x2, x0
	beq	.L243
	mov	x0, x19
	blr	x2
.LVL223:
	and	w1, w0, 255
	b	.L243
.LVL224:
	.p2align 2,,3
.L308:
	.loc 1 880 23 view .LVU1097
.LBE7840:
.LBE7839:
.LBE7843:
.LBE7845:
.LBE7849:
.LBE7851:
.LBE7853:
.LBE7855:
.LBE7901:
	.loc 16 92 2 is_stmt 1 view .LVU1098
.LBB7902:
.LBI7902:
	.loc 2 611 5 view .LVU1099
.LBB7903:
	.loc 2 616 18 is_stmt 0 view .LVU1100
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL225:
	.loc 2 616 18 view .LVU1101
.LBE7903:
.LBE7902:
.LBB7904:
.LBI7818:
	.loc 2 611 5 is_stmt 1 view .LVU1102
.LBB7820:
	.loc 2 616 18 is_stmt 0 view .LVU1103
	adrp	x1, .LC11
	mov	x0, x19
	add	x1, x1, :lo12:.LC11
	mov	x2, 61
	b	.L307
.LVL226:
	.p2align 2,,3
.L309:
	.loc 2 616 18 view .LVU1104
.LBE7820:
.LBE7904:
.LBB7905:
.LBB7816:
	.loc 16 103 4 is_stmt 1 view .LVU1105
.LBB7793:
.LBI7793:
	.loc 2 611 5 view .LVU1106
.LBB7794:
	.loc 2 616 18 is_stmt 0 view .LVU1107
	adrp	x20, :got:_ZSt4cerr
.LVL227:
	.loc 2 616 18 view .LVU1108
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL228:
	.loc 2 616 18 view .LVU1109
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL229:
	.loc 2 616 18 view .LVU1110
.LBE7794:
.LBE7793:
.LBB7795:
.LBI7795:
	.loc 2 611 5 is_stmt 1 view .LVU1111
.LBB7796:
	.loc 2 616 18 is_stmt 0 view .LVU1112
	mov	x0, x19
	adrp	x1, .LC13
	mov	x2, 14
	add	x1, x1, :lo12:.LC13
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL230:
	.loc 2 616 18 view .LVU1113
.LBE7796:
.LBE7795:
.LBB7797:
.LBI7797:
	.loc 2 108 7 is_stmt 1 view .LVU1114
.LBB7798:
.LBI7798:
	.loc 2 684 5 view .LVU1115
.LBB7799:
	.loc 2 685 19 is_stmt 0 view .LVU1116
	ldr	x0, [x19]
	ldr	x0, [x0, -24]
	add	x0, x0, x19
	ldr	x19, [x0, 240]
.LVL231:
.LBB7800:
.LBI7800:
	.loc 3 449 7 is_stmt 1 view .LVU1117
.LBB7801:
.LBI7801:
	.loc 3 47 5 view .LVU1118
.LBB7802:
	.loc 3 49 7 is_stmt 0 view .LVU1119
	cbz	x19, .L214
.LVL232:
	.loc 3 49 7 view .LVU1120
.LBE7802:
.LBE7801:
.LBB7803:
.LBI7803:
	.loc 1 875 7 is_stmt 1 view .LVU1121
.LBB7804:
	.loc 1 877 2 view .LVU1122
	ldrb	w0, [x19, 56]
	cbz	w0, .L217
	.loc 1 878 4 view .LVU1123
	.loc 1 878 51 is_stmt 0 view .LVU1124
	ldrb	w1, [x19, 67]
.LVL233:
.L218:
	.loc 1 878 51 view .LVU1125
.LBE7804:
.LBE7803:
.LBE7800:
	.loc 2 685 19 view .LVU1126
	ldr	x0, [x20, #:got_lo12:_ZSt4cerr]
	bl	_ZNSo3putEc
.LVL234:
.LBB7809:
.LBI7809:
	.loc 2 706 5 is_stmt 1 view .LVU1127
.LBB7810:
	.loc 2 707 24 is_stmt 0 view .LVU1128
	bl	_ZNSo5flushEv
.LVL235:
	.loc 2 707 24 view .LVU1129
.LBE7810:
.LBE7809:
.LBE7799:
.LBE7798:
.LBE7797:
.LBE7816:
.LBE7905:
	.loc 16 135 1 view .LVU1130
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldr	x23, [sp, 48]
.LVL236:
	.loc 16 135 1 view .LVU1131
	ldp	x29, x30, [sp], 64
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
.LVL237:
.L310:
	.cfi_restore_state
	.loc 16 106 2 is_stmt 1 view .LVU1132
.LBB7906:
.LBI7906:
	.loc 18 18 14 view .LVU1133
.LBB7907:
.LBI7907:
	.loc 5 918 7 view .LVU1134
.LBB7908:
	.loc 5 919 50 is_stmt 0 view .LVU1135
	ldr	x0, [x21, 8]
	ldr	x1, [x23, 112]
	sub	x0, x0, x1
.LBE7908:
.LBE7907:
.LBE7906:
	.loc 16 106 2 view .LVU1136
	cmp	x20, x0, asr 1
	bne	.L312
	.loc 16 106 54 is_stmt 1 discriminator 2 view .LVU1137
	.loc 16 107 2 discriminator 2 view .LVU1138
.LVL238:
.LBB7909:
.LBB7910:
.LBI7910:
	.file 22 "/usr/include/c++/11/bits/stl_list.h"
	.loc 22 946 7 discriminator 2 view .LVU1139
.LBB7911:
	.loc 22 947 16 is_stmt 0 discriminator 2 view .LVU1140
	ldr	x19, [x23, 904]
.LVL239:
.LBB7912:
.LBI7912:
	.loc 22 201 7 is_stmt 1 discriminator 2 view .LVU1141
	.loc 22 201 7 is_stmt 0 discriminator 2 view .LVU1142
.LBE7912:
.LBE7911:
.LBE7910:
	.loc 16 107 70 is_stmt 1 discriminator 2 view .LVU1143
.LBB7914:
.LBI7914:
	.loc 22 964 7 discriminator 2 view .LVU1144
.LBE7914:
.LBB7919:
.LBB7913:
	.loc 22 947 16 is_stmt 0 discriminator 2 view .LVU1145
	add	x21, x23, 904
.LVL240:
	.loc 22 947 16 discriminator 2 view .LVU1146
.LBE7913:
.LBE7919:
.LBB7920:
.LBB7915:
.LBB7916:
.LBI7916:
	.loc 22 201 7 is_stmt 1 discriminator 2 view .LVU1147
	.loc 22 201 7 is_stmt 0 discriminator 2 view .LVU1148
.LBE7916:
.LBE7915:
.LBE7920:
	.loc 16 107 70 discriminator 2 view .LVU1149
	cmp	x19, x21
	bne	.L222
	b	.L237
.LVL241:
	.p2align 2,,3
.L225:
	.loc 16 109 22 is_stmt 1 view .LVU1150
	.loc 16 110 3 view .LVU1151
	ldr	x1, [x0, 560]
	cbz	x1, .L313
	.loc 16 110 30 view .LVU1152
	.loc 16 111 3 view .LVU1153
	ldr	w1, [x1, 328]
	cbz	w1, .L314
	.loc 16 111 41 discriminator 2 view .LVU1154
	.loc 16 112 3 discriminator 2 view .LVU1155
	ldrh	w0, [x0, 72]
	ldr	x20, [x23, 56]
	bl	_ZN13BuildingUtils7GIDtoIDEt
.LVL242:
	ldr	x0, [x20, w0, sxtw 3]
	cbz	x0, .L315
	.loc 16 112 61 view .LVU1156
	.loc 16 107 2 view .LVU1157
.LVL243:
.LBB7921:
.LBI7921:
	.loc 22 218 7 view .LVU1158
.LBB7922:
	.loc 22 220 21 is_stmt 0 view .LVU1159
	ldr	x19, [x19]
.LVL244:
	.loc 22 220 21 view .LVU1160
.LBE7922:
.LBE7921:
	.loc 16 107 70 is_stmt 1 view .LVU1161
.LBB7923:
	.loc 22 964 7 view .LVU1162
.LBB7918:
.LBB7917:
	.loc 22 201 7 view .LVU1163
	.loc 22 201 7 is_stmt 0 view .LVU1164
.LBE7917:
.LBE7918:
.LBE7923:
	.loc 16 107 70 view .LVU1165
	cmp	x19, x21
	beq	.L237
.L222:
	.loc 16 109 3 is_stmt 1 view .LVU1166
	ldr	x0, [x19, 16]
	cbnz	x0, .L225
	.loc 16 109 3 view .LVU1167
.LVL245:
.LBB7924:
.LBI7924:
	.loc 2 611 5 view .LVU1168
.LBB7925:
	.loc 2 616 18 is_stmt 0 view .LVU1169
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL246:
	.loc 2 616 18 view .LVU1170
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL247:
	.loc 2 616 18 view .LVU1171
.LBE7925:
.LBE7924:
.LBB7926:
.LBI7926:
	.loc 2 611 5 is_stmt 1 view .LVU1172
.LBB7927:
	.loc 2 616 18 is_stmt 0 view .LVU1173
	adrp	x1, .LC15
	mov	x0, x19
	add	x1, x1, :lo12:.LC15
	mov	x2, 3
	b	.L307
.LVL248:
.L217:
	.loc 2 616 18 view .LVU1174
.LBE7927:
.LBE7926:
.LBE7909:
.LBB7940:
.LBB7817:
.LBB7814:
.LBB7813:
.LBB7812:
.LBB7811:
.LBB7808:
.LBB7807:
.LBB7805:
.LBI7805:
	.loc 1 875 7 is_stmt 1 view .LVU1175
.LBB7806:
	.loc 1 879 2 view .LVU1176
	.loc 1 879 21 is_stmt 0 view .LVU1177
	mov	x0, x19
	bl	_ZNKSt5ctypeIcE13_M_widen_initEv
.LVL249:
	.loc 1 880 2 is_stmt 1 view .LVU1178
	.loc 1 880 23 is_stmt 0 view .LVU1179
	ldr	x2, [x19]
	adrp	x0, :got:_ZNKSt5ctypeIcE8do_widenEc
	mov	w1, 10
	ldr	x0, [x0, #:got_lo12:_ZNKSt5ctypeIcE8do_widenEc]
	ldr	x2, [x2, 48]
	cmp	x2, x0
	beq	.L218
	mov	x0, x19
	blr	x2
.LVL250:
	and	w1, w0, 255
	b	.L218
.LVL251:
.L312:
	.loc 1 880 23 view .LVU1180
.LBE7806:
.LBE7805:
.LBE7807:
.LBE7808:
.LBE7811:
.LBE7812:
.LBE7813:
.LBE7814:
.LBE7817:
.LBE7940:
	.loc 16 106 2 is_stmt 1 view .LVU1181
.LBB7941:
.LBI7941:
	.loc 2 611 5 view .LVU1182
.LBB7942:
	.loc 2 616 18 is_stmt 0 view .LVU1183
	adrp	x20, :got:_ZSt4cerr
.LVL252:
	.loc 2 616 18 view .LVU1184
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL253:
	.loc 2 616 18 view .LVU1185
.LBE7942:
.LBE7941:
.LBB7943:
.LBI7943:
	.loc 2 611 5 is_stmt 1 view .LVU1186
.LBB7944:
	.loc 2 616 18 is_stmt 0 view .LVU1187
	adrp	x1, .LC14
	mov	x0, x19
	add	x1, x1, :lo12:.LC14
	mov	x2, 36
	b	.L307
.LVL254:
.L237:
	.loc 2 616 18 view .LVU1188
.LBE7944:
.LBE7943:
	.loc 16 114 2 is_stmt 1 view .LVU1189
.LBB7945:
.LBB7856:
.LBI7856:
	.loc 22 946 7 view .LVU1190
.LBB7857:
	.loc 22 947 16 is_stmt 0 view .LVU1191
	ldr	x19, [x23, 880]
.LVL255:
.LBB7858:
.LBI7858:
	.loc 22 201 7 is_stmt 1 view .LVU1192
	.loc 22 201 7 is_stmt 0 view .LVU1193
.LBE7858:
.LBE7857:
.LBE7856:
	.loc 16 114 67 is_stmt 1 view .LVU1194
.LBB7860:
.LBI7860:
	.loc 22 964 7 view .LVU1195
.LBE7860:
.LBB7865:
.LBB7859:
	.loc 22 947 16 is_stmt 0 view .LVU1196
	add	x20, x23, 880
.LVL256:
	.loc 22 947 16 view .LVU1197
.LBE7859:
.LBE7865:
.LBB7866:
.LBB7861:
.LBB7862:
.LBI7862:
	.loc 22 201 7 is_stmt 1 view .LVU1198
	.loc 22 201 7 is_stmt 0 view .LVU1199
.LBE7862:
.LBE7861:
.LBE7866:
	.loc 16 114 67 view .LVU1200
	cmp	x19, x20
	beq	.L224
.L223:
	.loc 16 116 3 is_stmt 1 view .LVU1201
	ldr	x0, [x19, 16]
	cbz	x0, .L316
	.loc 16 116 22 view .LVU1202
	.loc 16 117 3 view .LVU1203
	ldr	x1, [x0, 560]
	cbz	x1, .L317
	.loc 16 117 30 view .LVU1204
	.loc 16 118 3 view .LVU1205
.LVL257:
.LBB7867:
.LBI7867:
	.file 23 "src/building/types/BuildingCatalog.h"
	.loc 23 198 10 view .LVU1206
.LBB7868:
	.loc 23 198 46 view .LVU1207
	.loc 23 198 64 is_stmt 0 view .LVU1208
	ldr	x1, [x0, 272]
	.loc 23 198 60 view .LVU1209
	ldrb	w1, [x1, 63]
	tbnz	x1, 0, .L241
.LVL258:
	.loc 23 198 60 view .LVU1210
.LBE7868:
.LBE7867:
	.loc 16 118 3 is_stmt 1 view .LVU1211
.LBB7869:
.LBI7869:
	.loc 2 611 5 view .LVU1212
.LBB7870:
	.loc 2 616 18 is_stmt 0 view .LVU1213
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL259:
	.loc 2 616 18 view .LVU1214
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL260:
	.loc 2 616 18 view .LVU1215
.LBE7870:
.LBE7869:
.LBB7871:
.LBI7871:
	.loc 2 611 5 is_stmt 1 view .LVU1216
.LBB7872:
	.loc 2 616 18 is_stmt 0 view .LVU1217
	adrp	x1, .LC19
	mov	x0, x19
	add	x1, x1, :lo12:.LC19
	mov	x2, 31
	b	.L307
.LVL261:
.L315:
	.loc 2 616 18 view .LVU1218
.LBE7872:
.LBE7871:
.LBE7945:
.LBB7946:
	.loc 16 112 3 is_stmt 1 view .LVU1219
.LBB7928:
.LBI7928:
	.loc 2 611 5 view .LVU1220
.LBB7929:
	.loc 2 616 18 is_stmt 0 view .LVU1221
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL262:
	.loc 2 616 18 view .LVU1222
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL263:
	.loc 2 616 18 view .LVU1223
.LBE7929:
.LBE7928:
.LBB7930:
.LBI7930:
	.loc 2 611 5 is_stmt 1 view .LVU1224
.LBB7931:
	.loc 2 616 18 is_stmt 0 view .LVU1225
	adrp	x1, .LC18
	mov	x0, x19
	add	x1, x1, :lo12:.LC18
	mov	x2, 42
	b	.L307
.LVL264:
.L314:
	.loc 2 616 18 view .LVU1226
.LBE7931:
.LBE7930:
	.loc 16 111 3 is_stmt 1 view .LVU1227
.LBB7932:
.LBI7932:
	.loc 2 611 5 view .LVU1228
.LBB7933:
	.loc 2 616 18 is_stmt 0 view .LVU1229
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL265:
	.loc 2 616 18 view .LVU1230
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL266:
	.loc 2 616 18 view .LVU1231
.LBE7933:
.LBE7932:
.LBB7934:
.LBI7934:
	.loc 2 611 5 is_stmt 1 view .LVU1232
.LBB7935:
	.loc 2 616 18 is_stmt 0 view .LVU1233
	adrp	x1, .LC17
	mov	x0, x19
	add	x1, x1, :lo12:.LC17
	mov	x2, 22
	b	.L307
.LVL267:
.L313:
	.loc 2 616 18 view .LVU1234
.LBE7935:
.LBE7934:
	.loc 16 110 3 is_stmt 1 view .LVU1235
.LBB7936:
.LBI7936:
	.loc 2 611 5 view .LVU1236
.LBB7937:
	.loc 2 616 18 is_stmt 0 view .LVU1237
	adrp	x20, :got:_ZSt4cerr
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x20, #:got_lo12:_ZSt4cerr]
.LVL268:
	.loc 2 616 18 view .LVU1238
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL269:
	.loc 2 616 18 view .LVU1239
.LBE7937:
.LBE7936:
.LBB7938:
.LBI7938:
	.loc 2 611 5 is_stmt 1 view .LVU1240
.LBB7939:
	.loc 2 616 18 is_stmt 0 view .LVU1241
	adrp	x1, .LC16
	mov	x0, x19
	add	x1, x1, :lo12:.LC16
	mov	x2, 11
	b	.L307
.LVL270:
.L241:
	.loc 2 616 18 view .LVU1242
.LBE7939:
.LBE7938:
.LBE7946:
.LBB7947:
	.loc 16 118 50 is_stmt 1 view .LVU1243
	.loc 16 119 3 view .LVU1244
	ldrh	w0, [x0, 72]
	ldr	x21, [x23, 56]
	bl	_ZN13BuildingUtils7GIDtoIDEt
.LVL271:
	ldr	x0, [x21, w0, sxtw 3]
	cbz	x0, .L318
	.loc 16 119 61 view .LVU1245
	.loc 16 114 2 view .LVU1246
.LVL272:
.LBB7873:
.LBI7873:
	.loc 22 218 7 view .LVU1247
.LBB7874:
	.loc 22 220 21 is_stmt 0 view .LVU1248
	ldr	x19, [x19]
.LVL273:
	.loc 22 220 21 view .LVU1249
.LBE7874:
.LBE7873:
	.loc 16 114 67 is_stmt 1 view .LVU1250
.LBB7875:
	.loc 22 964 7 view .LVU1251
.LBB7864:
.LBB7863:
	.loc 22 201 7 view .LVU1252
	.loc 22 201 7 is_stmt 0 view .LVU1253
.LBE7863:
.LBE7864:
.LBE7875:
	.loc 16 114 67 view .LVU1254
	cmp	x19, x20
	bne	.L223
.L224:
	.loc 16 114 67 view .LVU1255
	add	x21, x23, 64
.LBB7876:
.LBB7854:
.LBB7852:
.LBB7850:
.LBB7846:
.LBB7844:
.LBB7842:
.LBB7841:
	.loc 1 880 23 view .LVU1256
	mov	x20, 0
.LBE7841:
.LBE7842:
.LBE7844:
.LBE7846:
.LBE7850:
.LBE7852:
.LBE7854:
.LBE7876:
.LBE7947:
	.loc 16 122 14 view .LVU1257
	mov	x19, 0
.LVL274:
	.loc 16 122 14 view .LVU1258
	b	.L238
.LVL275:
.L321:
.LBB7948:
.LBB7949:
.LBB7950:
.LBI7950:
	.loc 18 20 33 is_stmt 1 discriminator 2 view .LVU1259
.LBB7951:
.LBI7951:
	.loc 19 211 2 discriminator 2 view .LVU1260
.LBB7952:
.LBI7952:
	.loc 20 536 2 discriminator 2 view .LVU1261
	.loc 20 536 2 is_stmt 0 discriminator 2 view .LVU1262
.LBE7952:
.LBB7953:
.LBI7953:
	.loc 20 414 2 is_stmt 1 discriminator 2 view .LVU1263
	.loc 20 414 2 is_stmt 0 discriminator 2 view .LVU1264
.LBE7953:
.LBB7954:
.LBI7954:
	.loc 19 155 2 is_stmt 1 discriminator 2 view .LVU1265
.LBB7955:
.LBI7955:
	.loc 21 198 5 discriminator 2 view .LVU1266
	.loc 21 198 5 is_stmt 0 discriminator 2 view .LVU1267
.LBE7955:
.LBB7956:
.LBI7956:
	.loc 19 90 2 is_stmt 1 discriminator 2 view .LVU1268
	.loc 19 90 2 is_stmt 0 discriminator 2 view .LVU1269
.LBE7956:
.LBE7954:
.LBE7951:
.LBE7950:
.LBB7957:
.LBI7957:
	.loc 19 276 7 is_stmt 1 discriminator 2 view .LVU1270
.LBB7958:
	.loc 19 278 2 discriminator 2 view .LVU1271
	.loc 19 278 2 is_stmt 0 discriminator 2 view .LVU1272
.LBE7958:
.LBE7957:
	.loc 16 128 4 discriminator 2 view .LVU1273
	ldrh	w0, [x2, x19, lsl 1]
	cmp	w0, w20
	bne	.L246
.LVL276:
.LBB7959:
.LBI7959:
	.loc 18 21 22 is_stmt 1 discriminator 4 view .LVU1274
.LBB7960:
.LBI7960:
	.loc 19 211 2 discriminator 4 view .LVU1275
.LBB7961:
.LBI7961:
	.loc 20 536 2 discriminator 4 view .LVU1276
.LBB7962:
.LBI7962:
	.loc 5 1172 7 discriminator 4 view .LVU1277
	.loc 5 1172 7 is_stmt 0 discriminator 4 view .LVU1278
.LBE7962:
.LBE7961:
.LBB7963:
.LBI7963:
	.loc 20 414 2 is_stmt 1 discriminator 4 view .LVU1279
	.loc 20 414 2 is_stmt 0 discriminator 4 view .LVU1280
.LBE7963:
.LBB7964:
.LBI7964:
	.loc 19 155 2 is_stmt 1 discriminator 4 view .LVU1281
.LBB7965:
.LBI7965:
	.loc 21 198 5 discriminator 4 view .LVU1282
	.loc 21 198 5 is_stmt 0 discriminator 4 view .LVU1283
.LBE7965:
.LBB7966:
.LBI7966:
	.loc 19 90 2 is_stmt 1 discriminator 4 view .LVU1284
	.loc 19 90 2 is_stmt 0 discriminator 4 view .LVU1285
.LBE7966:
.LBE7964:
.LBE7960:
.LBE7959:
.LBB7967:
.LBI7967:
	.loc 19 276 7 is_stmt 1 discriminator 4 view .LVU1286
.LBB7968:
	.loc 19 278 2 discriminator 4 view .LVU1287
	.loc 19 278 2 is_stmt 0 discriminator 4 view .LVU1288
.LBE7968:
.LBE7967:
	.loc 16 128 4 discriminator 4 view .LVU1289
	ldr	x0, [x23, 88]
	ldr	x0, [x0, x19, lsl 3]
	cmp	x0, x1
	bne	.L246
	.loc 16 128 121 is_stmt 1 view .LVU1290
	.loc 16 129 4 view .LVU1291
	.loc 16 130 4 is_stmt 0 view .LVU1292
	bl	_ZN4Unit9integrityEv
.LVL277:
	.loc 16 129 4 view .LVU1293
	add	x19, x19, 1
.LVL278:
	.loc 16 130 4 is_stmt 1 view .LVU1294
	ands	w22, w0, 255
	beq	.L319
.LVL279:
.L245:
	.loc 16 130 4 is_stmt 0 view .LVU1295
.LBE7949:
	.loc 16 123 2 is_stmt 1 discriminator 2 view .LVU1296
	.loc 16 123 17 discriminator 2 view .LVU1297
	add	x20, x20, 1
.LVL280:
	.loc 16 123 17 is_stmt 0 discriminator 2 view .LVU1298
	cmp	x20, 1024
	beq	.L320
.LVL281:
.L238:
.LBB7992:
	.loc 16 125 3 is_stmt 1 view .LVU1299
	.loc 16 125 9 is_stmt 0 view .LVU1300
	ldr	x0, [x23, 48]
.LBB7969:
.LBB7970:
.LBB7971:
	.loc 5 919 66 view .LVU1301
	ldr	x2, [x21]
.LBE7971:
.LBE7970:
.LBE7969:
	.loc 16 125 9 view .LVU1302
	ldr	x1, [x0, x20, lsl 3]
.LVL282:
	.loc 16 126 3 is_stmt 1 view .LVU1303
.LBB7976:
.LBB7974:
.LBB7972:
	.loc 5 919 40 is_stmt 0 view .LVU1304
	ldr	x0, [x21, 8]
.LBE7972:
.LBE7974:
.LBE7976:
	.loc 16 126 3 view .LVU1305
	cbz	x1, .L245
	.loc 16 128 4 is_stmt 1 view .LVU1306
.LVL283:
.LBB7977:
.LBI7969:
	.loc 18 18 14 view .LVU1307
.LBB7975:
.LBI7970:
	.loc 5 918 7 view .LVU1308
.LBB7973:
	.loc 5 919 50 is_stmt 0 view .LVU1309
	sub	x0, x0, x2
.LBE7973:
.LBE7975:
.LBE7977:
	.loc 16 128 4 view .LVU1310
	cmp	x19, x0, asr 1
	bcc	.L321
.L246:
	.loc 16 128 4 is_stmt 1 discriminator 5 view .LVU1311
.LVL284:
.LBB7978:
.LBI7978:
	.loc 2 611 5 discriminator 5 view .LVU1312
.LBB7979:
	.loc 2 616 18 is_stmt 0 discriminator 5 view .LVU1313
	adrp	x19, :got:_ZSt4cerr
.LVL285:
	.loc 2 616 18 discriminator 5 view .LVU1314
	mov	x2, 18
	adrp	x1, .LC10
.LVL286:
	.loc 2 616 18 discriminator 5 view .LVU1315
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
.LBE7979:
.LBE7978:
	.loc 16 128 4 discriminator 5 view .LVU1316
	mov	w22, 0
.LBB7981:
.LBB7980:
	.loc 2 616 18 discriminator 5 view .LVU1317
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL287:
	.loc 2 616 18 discriminator 5 view .LVU1318
.LBE7980:
.LBE7981:
.LBB7982:
.LBI7982:
	.loc 2 611 5 is_stmt 1 discriminator 5 view .LVU1319
.LBB7983:
	.loc 2 616 18 is_stmt 0 discriminator 5 view .LVU1320
	adrp	x1, .LC20
	add	x1, x1, :lo12:.LC20
	mov	x2, 101
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL288:
	.loc 2 616 18 discriminator 5 view .LVU1321
.LBE7983:
.LBE7982:
.LBB7984:
.LBI7984:
	.loc 2 108 7 is_stmt 1 discriminator 5 view .LVU1322
.LBB7985:
	.loc 2 113 13 is_stmt 0 discriminator 5 view .LVU1323
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL289:
	.loc 2 113 13 discriminator 5 view .LVU1324
.LBE7985:
.LBE7984:
	.loc 16 128 4 is_stmt 1 discriminator 5 view .LVU1325
	b	.L210
.LVL290:
.L317:
	.loc 16 128 4 is_stmt 0 discriminator 5 view .LVU1326
.LBE7992:
.LBE7948:
.LBB7994:
	.loc 16 117 3 is_stmt 1 view .LVU1327
.LBB7877:
.LBI7877:
	.loc 2 611 5 view .LVU1328
.LBB7878:
	.loc 2 616 18 is_stmt 0 view .LVU1329
	adrp	x19, :got:_ZSt4cerr
.LVL291:
	.loc 2 616 18 view .LVU1330
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
.LBE7878:
.LBE7877:
	.loc 16 117 3 view .LVU1331
	mov	w22, 0
.LBB7880:
.LBB7879:
	.loc 2 616 18 view .LVU1332
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL292:
	.loc 2 616 18 view .LVU1333
.LBE7879:
.LBE7880:
.LBB7881:
.LBI7881:
	.loc 2 611 5 is_stmt 1 view .LVU1334
.LBB7882:
	.loc 2 616 18 is_stmt 0 view .LVU1335
	adrp	x1, .LC16
	add	x1, x1, :lo12:.LC16
	mov	x2, 11
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL293:
	.loc 2 616 18 view .LVU1336
.LBE7882:
.LBE7881:
.LBB7883:
.LBI7883:
	.loc 2 108 7 is_stmt 1 view .LVU1337
.LBB7884:
	.loc 2 113 13 is_stmt 0 view .LVU1338
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL294:
	.loc 2 113 13 view .LVU1339
.LBE7884:
.LBE7883:
	.loc 16 117 3 is_stmt 1 view .LVU1340
	b	.L210
.LVL295:
.L316:
	.loc 16 116 3 view .LVU1341
.LBB7885:
.LBI7885:
	.loc 2 611 5 view .LVU1342
.LBB7886:
	.loc 2 616 18 is_stmt 0 view .LVU1343
	adrp	x19, :got:_ZSt4cerr
.LVL296:
	.loc 2 616 18 view .LVU1344
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
.LBE7886:
.LBE7885:
	.loc 16 116 3 view .LVU1345
	mov	w22, 0
.LBB7888:
.LBB7887:
	.loc 2 616 18 view .LVU1346
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL297:
	.loc 2 616 18 view .LVU1347
.LBE7887:
.LBE7888:
.LBB7889:
.LBI7889:
	.loc 2 611 5 is_stmt 1 view .LVU1348
.LBB7890:
	.loc 2 616 18 is_stmt 0 view .LVU1349
	adrp	x1, .LC15
	add	x1, x1, :lo12:.LC15
	mov	x2, 3
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL298:
	.loc 2 616 18 view .LVU1350
.LBE7890:
.LBE7889:
.LBB7891:
.LBI7891:
	.loc 2 108 7 is_stmt 1 view .LVU1351
.LBB7892:
	.loc 2 113 13 is_stmt 0 view .LVU1352
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL299:
	.loc 2 113 13 view .LVU1353
.LBE7892:
.LBE7891:
	.loc 16 116 3 is_stmt 1 view .LVU1354
	b	.L210
.LVL300:
.L318:
	.loc 16 119 3 view .LVU1355
.LBB7893:
.LBI7893:
	.loc 2 611 5 view .LVU1356
.LBB7894:
	.loc 2 616 18 is_stmt 0 view .LVU1357
	adrp	x19, :got:_ZSt4cerr
.LVL301:
	.loc 2 616 18 view .LVU1358
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
.LBE7894:
.LBE7893:
	.loc 16 119 3 view .LVU1359
	mov	w22, 0
.LBB7896:
.LBB7895:
	.loc 2 616 18 view .LVU1360
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL302:
	.loc 2 616 18 view .LVU1361
.LBE7895:
.LBE7896:
.LBB7897:
.LBI7897:
	.loc 2 611 5 is_stmt 1 view .LVU1362
.LBB7898:
	.loc 2 616 18 is_stmt 0 view .LVU1363
	adrp	x1, .LC18
	add	x1, x1, :lo12:.LC18
	mov	x2, 42
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL303:
	.loc 2 616 18 view .LVU1364
.LBE7898:
.LBE7897:
.LBB7899:
.LBI7899:
	.loc 2 108 7 is_stmt 1 view .LVU1365
.LBB7900:
	.loc 2 113 13 is_stmt 0 view .LVU1366
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL304:
	.loc 2 113 13 view .LVU1367
.LBE7900:
.LBE7899:
	.loc 16 119 3 is_stmt 1 view .LVU1368
	b	.L210
.LVL305:
.L319:
	.loc 16 119 3 is_stmt 0 view .LVU1369
.LBE7994:
.LBB7995:
.LBB7993:
	.loc 16 130 4 is_stmt 1 view .LVU1370
.LBB7986:
.LBI7986:
	.loc 2 611 5 view .LVU1371
.LBB7987:
	.loc 2 616 18 is_stmt 0 view .LVU1372
	adrp	x19, :got:_ZSt4cerr
.LVL306:
	.loc 2 616 18 view .LVU1373
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL307:
	.loc 2 616 18 view .LVU1374
.LBE7987:
.LBE7986:
.LBB7988:
.LBI7988:
	.loc 2 611 5 is_stmt 1 view .LVU1375
.LBB7989:
	.loc 2 616 18 is_stmt 0 view .LVU1376
	adrp	x1, .LC21
	add	x1, x1, :lo12:.LC21
	mov	x2, 14
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL308:
	.loc 2 616 18 view .LVU1377
.LBE7989:
.LBE7988:
.LBB7990:
.LBI7990:
	.loc 2 108 7 is_stmt 1 view .LVU1378
.LBB7991:
	.loc 2 113 13 is_stmt 0 view .LVU1379
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL309:
	.loc 2 113 13 view .LVU1380
.LBE7991:
.LBE7990:
	.loc 16 130 4 is_stmt 1 view .LVU1381
	b	.L210
.LVL310:
.L214:
	.loc 16 130 4 is_stmt 0 view .LVU1382
.LBE7993:
.LBE7995:
.LBB7996:
.LBB7831:
.LBB7830:
.LBB7829:
.LBB7828:
.LBB7827:
	.loc 3 50 18 view .LVU1383
	bl	_ZSt16__throw_bad_castv
.LVL311:
.L320:
	.loc 3 50 18 view .LVU1384
.LBE7827:
.LBE7828:
.LBE7829:
.LBE7830:
.LBE7831:
.LBE7996:
	.loc 16 133 2 is_stmt 1 view .LVU1385
.LBB7997:
.LBI7997:
	.loc 18 18 14 view .LVU1386
.LBB7998:
.LBI7998:
	.loc 5 918 7 view .LVU1387
.LBB7999:
	.loc 5 919 50 is_stmt 0 view .LVU1388
	ldr	x0, [x21, 8]
	ldr	x1, [x23, 64]
	sub	x0, x0, x1
.LBE7999:
.LBE7998:
.LBE7997:
	.loc 16 133 2 view .LVU1389
	cmp	x19, x0, asr 1
	bne	.L322
	.loc 16 134 9 view .LVU1390
	mov	w22, 1
	b	.L210
.L322:
	.loc 16 133 2 is_stmt 1 view .LVU1391
.LVL312:
.LBB8000:
.LBI8000:
	.loc 2 611 5 view .LVU1392
.LBB8001:
	.loc 2 616 18 is_stmt 0 view .LVU1393
	adrp	x19, :got:_ZSt4cerr
.LVL313:
	.loc 2 616 18 view .LVU1394
	mov	x2, 18
	adrp	x1, .LC10
	add	x1, x1, :lo12:.LC10
	ldr	x19, [x19, #:got_lo12:_ZSt4cerr]
.LBE8001:
.LBE8000:
	.loc 16 133 2 view .LVU1395
	mov	w22, 0
.LBB8003:
.LBB8002:
	.loc 2 616 18 view .LVU1396
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL314:
	.loc 2 616 18 view .LVU1397
.LBE8002:
.LBE8003:
.LBB8004:
.LBI8004:
	.loc 2 611 5 is_stmt 1 view .LVU1398
.LBB8005:
	.loc 2 616 18 is_stmt 0 view .LVU1399
	adrp	x1, .LC22
	add	x1, x1, :lo12:.LC22
	mov	x2, 28
	mov	x0, x19
	bl	_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l
.LVL315:
	.loc 2 616 18 view .LVU1400
.LBE8005:
.LBE8004:
.LBB8006:
.LBI8006:
	.loc 2 108 7 is_stmt 1 view .LVU1401
.LBB8007:
	.loc 2 113 13 is_stmt 0 view .LVU1402
	mov	x0, x19
	bl	_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0
.LVL316:
	.loc 2 113 13 view .LVU1403
.LBE8007:
.LBE8006:
	.loc 16 133 2 is_stmt 1 view .LVU1404
	b	.L210
	.cfi_endproc
.LFE14464: