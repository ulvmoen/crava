/*
 * Copyright (c) 1997-1999 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* This file was automatically generated --- DO NOT EDIT */
/* Generated on Tue May 18 13:55:35 EDT 1999 */

#include <fftw-int.h>
#include <fftw.h>

/* Generated by: ./genfft -magic-alignment-check -magic-twiddle-load-all -magic-variables 4 -magic-loopi -hc2hc-forward 9 */

/*
 * This function contains 180 FP additions, 120 FP multiplications,
 * (or, 133 additions, 73 multiplications, 47 fused multiply/add),
 * 35 stack variables, and 72 memory accesses
 */
static const fftw_real K342020143 = FFTW_KONST(+0.342020143325668733044099614682259580763083368);
static const fftw_real K813797681 = FFTW_KONST(+0.813797681349373692844693217248393223289101568);
static const fftw_real K939692620 = FFTW_KONST(+0.939692620785908384054109277324731469936208134);
static const fftw_real K296198132 = FFTW_KONST(+0.296198132726023843175338011893050938967728390);
static const fftw_real K852868531 = FFTW_KONST(+0.852868531952443209628250963940074071936020296);
static const fftw_real K173648177 = FFTW_KONST(+0.173648177666930348851716626769314796000375677);
static const fftw_real K556670399 = FFTW_KONST(+0.556670399226419366452912952047023132968291906);
static const fftw_real K766044443 = FFTW_KONST(+0.766044443118978035202392650555416673935832457);
static const fftw_real K984807753 = FFTW_KONST(+0.984807753012208059366743024589523013670643252);
static const fftw_real K150383733 = FFTW_KONST(+0.150383733180435296639271897612501926072238258);
static const fftw_real K642787609 = FFTW_KONST(+0.642787609686539326322643409907263432907559884);
static const fftw_real K663413948 = FFTW_KONST(+0.663413948168938396205421319635891297216863310);
static const fftw_real K866025403 = FFTW_KONST(+0.866025403784438646763723170752936183471402627);
static const fftw_real K500000000 = FFTW_KONST(+0.500000000000000000000000000000000000000000000);

void fftw_hc2hc_forward_9(fftw_real *A, const fftw_complex *W, int iostride, int m, int dist)
{
     int i;
     fftw_real *X;
     fftw_real *Y;
     X = A;
     Y = A + (9 * iostride);
     {
	  fftw_real tmp131;
	  fftw_real tmp145;
	  fftw_real tmp150;
	  fftw_real tmp149;
	  fftw_real tmp134;
	  fftw_real tmp157;
	  fftw_real tmp140;
	  fftw_real tmp148;
	  fftw_real tmp151;
	  fftw_real tmp132;
	  fftw_real tmp133;
	  fftw_real tmp135;
	  fftw_real tmp146;
	  ASSERT_ALIGNED_DOUBLE();
	  tmp131 = X[0];
	  {
	       fftw_real tmp141;
	       fftw_real tmp142;
	       fftw_real tmp143;
	       fftw_real tmp144;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp141 = X[2 * iostride];
	       tmp142 = X[5 * iostride];
	       tmp143 = X[8 * iostride];
	       tmp144 = tmp142 + tmp143;
	       tmp145 = tmp141 + tmp144;
	       tmp150 = tmp141 - (K500000000 * tmp144);
	       tmp149 = tmp143 - tmp142;
	  }
	  tmp132 = X[3 * iostride];
	  tmp133 = X[6 * iostride];
	  tmp134 = tmp132 + tmp133;
	  tmp157 = tmp133 - tmp132;
	  {
	       fftw_real tmp136;
	       fftw_real tmp137;
	       fftw_real tmp138;
	       fftw_real tmp139;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp136 = X[iostride];
	       tmp137 = X[4 * iostride];
	       tmp138 = X[7 * iostride];
	       tmp139 = tmp137 + tmp138;
	       tmp140 = tmp136 + tmp139;
	       tmp148 = tmp136 - (K500000000 * tmp139);
	       tmp151 = tmp138 - tmp137;
	  }
	  Y[-3 * iostride] = K866025403 * (tmp145 - tmp140);
	  tmp135 = tmp131 + tmp134;
	  tmp146 = tmp140 + tmp145;
	  X[3 * iostride] = tmp135 - (K500000000 * tmp146);
	  X[0] = tmp135 + tmp146;
	  {
	       fftw_real tmp159;
	       fftw_real tmp155;
	       fftw_real tmp156;
	       fftw_real tmp158;
	       fftw_real tmp147;
	       fftw_real tmp152;
	       fftw_real tmp153;
	       fftw_real tmp154;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp159 = K866025403 * tmp157;
	       tmp155 = (K663413948 * tmp151) - (K642787609 * tmp148);
	       tmp156 = (K150383733 * tmp149) - (K984807753 * tmp150);
	       tmp158 = tmp155 + tmp156;
	       tmp147 = tmp131 - (K500000000 * tmp134);
	       tmp152 = (K766044443 * tmp148) + (K556670399 * tmp151);
	       tmp153 = (K173648177 * tmp150) + (K852868531 * tmp149);
	       tmp154 = tmp152 + tmp153;
	       X[iostride] = tmp147 + tmp154;
	       X[4 * iostride] = tmp147 + (K866025403 * (tmp155 - tmp156)) - (K500000000 * tmp154);
	       X[2 * iostride] = tmp147 + (K173648177 * tmp148) - (K296198132 * tmp149) - (K939692620 * tmp150) - (K852868531 * tmp151);
	       Y[-iostride] = tmp159 + tmp158;
	       Y[-4 * iostride] = (K866025403 * (tmp157 + (tmp153 - tmp152))) - (K500000000 * tmp158);
	       Y[-2 * iostride] = (K813797681 * tmp149) - (K342020143 * tmp150) - (K150383733 * tmp151) - (K984807753 * tmp148) - tmp159;
	  }
     }
     X = X + dist;
     Y = Y - dist;
     for (i = 2; i < m; i = i + 2, X = X + dist, Y = Y - dist, W = W + 8) {
	  fftw_real tmp19;
	  fftw_real tmp117;
	  fftw_real tmp70;
	  fftw_real tmp116;
	  fftw_real tmp123;
	  fftw_real tmp122;
	  fftw_real tmp30;
	  fftw_real tmp67;
	  fftw_real tmp65;
	  fftw_real tmp87;
	  fftw_real tmp104;
	  fftw_real tmp113;
	  fftw_real tmp92;
	  fftw_real tmp103;
	  fftw_real tmp48;
	  fftw_real tmp76;
	  fftw_real tmp100;
	  fftw_real tmp112;
	  fftw_real tmp81;
	  fftw_real tmp101;
	  ASSERT_ALIGNED_DOUBLE();
	  {
	       fftw_real tmp24;
	       fftw_real tmp68;
	       fftw_real tmp29;
	       fftw_real tmp69;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp19 = X[0];
	       tmp117 = Y[-8 * iostride];
	       {
		    fftw_real tmp21;
		    fftw_real tmp23;
		    fftw_real tmp20;
		    fftw_real tmp22;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp21 = X[3 * iostride];
		    tmp23 = Y[-5 * iostride];
		    tmp20 = c_re(W[2]);
		    tmp22 = c_im(W[2]);
		    tmp24 = (tmp20 * tmp21) - (tmp22 * tmp23);
		    tmp68 = (tmp22 * tmp21) + (tmp20 * tmp23);
	       }
	       {
		    fftw_real tmp26;
		    fftw_real tmp28;
		    fftw_real tmp25;
		    fftw_real tmp27;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp26 = X[6 * iostride];
		    tmp28 = Y[-2 * iostride];
		    tmp25 = c_re(W[5]);
		    tmp27 = c_im(W[5]);
		    tmp29 = (tmp25 * tmp26) - (tmp27 * tmp28);
		    tmp69 = (tmp27 * tmp26) + (tmp25 * tmp28);
	       }
	       tmp70 = K866025403 * (tmp68 - tmp69);
	       tmp116 = tmp68 + tmp69;
	       tmp123 = tmp117 - (K500000000 * tmp116);
	       tmp122 = K866025403 * (tmp29 - tmp24);
	       tmp30 = tmp24 + tmp29;
	       tmp67 = tmp19 - (K500000000 * tmp30);
	  }
	  {
	       fftw_real tmp53;
	       fftw_real tmp89;
	       fftw_real tmp58;
	       fftw_real tmp84;
	       fftw_real tmp63;
	       fftw_real tmp85;
	       fftw_real tmp64;
	       fftw_real tmp90;
	       ASSERT_ALIGNED_DOUBLE();
	       {
		    fftw_real tmp50;
		    fftw_real tmp52;
		    fftw_real tmp49;
		    fftw_real tmp51;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp50 = X[2 * iostride];
		    tmp52 = Y[-6 * iostride];
		    tmp49 = c_re(W[1]);
		    tmp51 = c_im(W[1]);
		    tmp53 = (tmp49 * tmp50) - (tmp51 * tmp52);
		    tmp89 = (tmp51 * tmp50) + (tmp49 * tmp52);
	       }
	       {
		    fftw_real tmp55;
		    fftw_real tmp57;
		    fftw_real tmp54;
		    fftw_real tmp56;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp55 = X[5 * iostride];
		    tmp57 = Y[-3 * iostride];
		    tmp54 = c_re(W[4]);
		    tmp56 = c_im(W[4]);
		    tmp58 = (tmp54 * tmp55) - (tmp56 * tmp57);
		    tmp84 = (tmp56 * tmp55) + (tmp54 * tmp57);
	       }
	       {
		    fftw_real tmp60;
		    fftw_real tmp62;
		    fftw_real tmp59;
		    fftw_real tmp61;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp60 = X[8 * iostride];
		    tmp62 = Y[0];
		    tmp59 = c_re(W[7]);
		    tmp61 = c_im(W[7]);
		    tmp63 = (tmp59 * tmp60) - (tmp61 * tmp62);
		    tmp85 = (tmp61 * tmp60) + (tmp59 * tmp62);
	       }
	       tmp64 = tmp58 + tmp63;
	       tmp90 = tmp84 + tmp85;
	       {
		    fftw_real tmp83;
		    fftw_real tmp86;
		    fftw_real tmp88;
		    fftw_real tmp91;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp65 = tmp53 + tmp64;
		    tmp83 = tmp53 - (K500000000 * tmp64);
		    tmp86 = K866025403 * (tmp84 - tmp85);
		    tmp87 = tmp83 + tmp86;
		    tmp104 = tmp83 - tmp86;
		    tmp113 = tmp89 + tmp90;
		    tmp88 = K866025403 * (tmp63 - tmp58);
		    tmp91 = tmp89 - (K500000000 * tmp90);
		    tmp92 = tmp88 + tmp91;
		    tmp103 = tmp91 - tmp88;
	       }
	  }
	  {
	       fftw_real tmp36;
	       fftw_real tmp78;
	       fftw_real tmp41;
	       fftw_real tmp73;
	       fftw_real tmp46;
	       fftw_real tmp74;
	       fftw_real tmp47;
	       fftw_real tmp79;
	       ASSERT_ALIGNED_DOUBLE();
	       {
		    fftw_real tmp33;
		    fftw_real tmp35;
		    fftw_real tmp32;
		    fftw_real tmp34;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp33 = X[iostride];
		    tmp35 = Y[-7 * iostride];
		    tmp32 = c_re(W[0]);
		    tmp34 = c_im(W[0]);
		    tmp36 = (tmp32 * tmp33) - (tmp34 * tmp35);
		    tmp78 = (tmp34 * tmp33) + (tmp32 * tmp35);
	       }
	       {
		    fftw_real tmp38;
		    fftw_real tmp40;
		    fftw_real tmp37;
		    fftw_real tmp39;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp38 = X[4 * iostride];
		    tmp40 = Y[-4 * iostride];
		    tmp37 = c_re(W[3]);
		    tmp39 = c_im(W[3]);
		    tmp41 = (tmp37 * tmp38) - (tmp39 * tmp40);
		    tmp73 = (tmp39 * tmp38) + (tmp37 * tmp40);
	       }
	       {
		    fftw_real tmp43;
		    fftw_real tmp45;
		    fftw_real tmp42;
		    fftw_real tmp44;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp43 = X[7 * iostride];
		    tmp45 = Y[-iostride];
		    tmp42 = c_re(W[6]);
		    tmp44 = c_im(W[6]);
		    tmp46 = (tmp42 * tmp43) - (tmp44 * tmp45);
		    tmp74 = (tmp44 * tmp43) + (tmp42 * tmp45);
	       }
	       tmp47 = tmp41 + tmp46;
	       tmp79 = tmp73 + tmp74;
	       {
		    fftw_real tmp72;
		    fftw_real tmp75;
		    fftw_real tmp77;
		    fftw_real tmp80;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp48 = tmp36 + tmp47;
		    tmp72 = tmp36 - (K500000000 * tmp47);
		    tmp75 = K866025403 * (tmp73 - tmp74);
		    tmp76 = tmp72 + tmp75;
		    tmp100 = tmp72 - tmp75;
		    tmp112 = tmp78 + tmp79;
		    tmp77 = K866025403 * (tmp46 - tmp41);
		    tmp80 = tmp78 - (K500000000 * tmp79);
		    tmp81 = tmp77 + tmp80;
		    tmp101 = tmp80 - tmp77;
	       }
	  }
	  {
	       fftw_real tmp114;
	       fftw_real tmp31;
	       fftw_real tmp66;
	       fftw_real tmp111;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp114 = K866025403 * (tmp112 - tmp113);
	       tmp31 = tmp19 + tmp30;
	       tmp66 = tmp48 + tmp65;
	       tmp111 = tmp31 - (K500000000 * tmp66);
	       X[0] = tmp31 + tmp66;
	       X[3 * iostride] = tmp111 + tmp114;
	       Y[-6 * iostride] = tmp111 - tmp114;
	  }
	  {
	       fftw_real tmp120;
	       fftw_real tmp115;
	       fftw_real tmp118;
	       fftw_real tmp119;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp120 = K866025403 * (tmp65 - tmp48);
	       tmp115 = tmp112 + tmp113;
	       tmp118 = tmp116 + tmp117;
	       tmp119 = tmp118 - (K500000000 * tmp115);
	       Y[0] = tmp115 + tmp118;
	       Y[-3 * iostride] = tmp120 + tmp119;
	       X[6 * iostride] = -(tmp119 - tmp120);
	  }
	  {
	       fftw_real tmp71;
	       fftw_real tmp124;
	       fftw_real tmp94;
	       fftw_real tmp126;
	       fftw_real tmp98;
	       fftw_real tmp121;
	       fftw_real tmp95;
	       fftw_real tmp125;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp71 = tmp67 + tmp70;
	       tmp124 = tmp122 + tmp123;
	       {
		    fftw_real tmp82;
		    fftw_real tmp93;
		    fftw_real tmp96;
		    fftw_real tmp97;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp82 = (K766044443 * tmp76) + (K642787609 * tmp81);
		    tmp93 = (K173648177 * tmp87) + (K984807753 * tmp92);
		    tmp94 = tmp82 + tmp93;
		    tmp126 = K866025403 * (tmp93 - tmp82);
		    tmp96 = (K766044443 * tmp81) - (K642787609 * tmp76);
		    tmp97 = (K173648177 * tmp92) - (K984807753 * tmp87);
		    tmp98 = K866025403 * (tmp96 - tmp97);
		    tmp121 = tmp96 + tmp97;
	       }
	       X[iostride] = tmp71 + tmp94;
	       tmp95 = tmp71 - (K500000000 * tmp94);
	       Y[-7 * iostride] = tmp95 - tmp98;
	       X[4 * iostride] = tmp95 + tmp98;
	       Y[-iostride] = tmp121 + tmp124;
	       tmp125 = tmp124 - (K500000000 * tmp121);
	       X[7 * iostride] = -(tmp125 - tmp126);
	       Y[-4 * iostride] = tmp126 + tmp125;
	  }
	  {
	       fftw_real tmp99;
	       fftw_real tmp128;
	       fftw_real tmp106;
	       fftw_real tmp127;
	       fftw_real tmp110;
	       fftw_real tmp129;
	       fftw_real tmp107;
	       fftw_real tmp130;
	       ASSERT_ALIGNED_DOUBLE();
	       tmp99 = tmp67 - tmp70;
	       tmp128 = tmp123 - tmp122;
	       {
		    fftw_real tmp102;
		    fftw_real tmp105;
		    fftw_real tmp108;
		    fftw_real tmp109;
		    ASSERT_ALIGNED_DOUBLE();
		    tmp102 = (K173648177 * tmp100) + (K984807753 * tmp101);
		    tmp105 = (K342020143 * tmp103) - (K939692620 * tmp104);
		    tmp106 = tmp102 + tmp105;
		    tmp127 = K866025403 * (tmp105 - tmp102);
		    tmp108 = (K173648177 * tmp101) - (K984807753 * tmp100);
		    tmp109 = (K342020143 * tmp104) + (K939692620 * tmp103);
		    tmp110 = K866025403 * (tmp108 + tmp109);
		    tmp129 = tmp108 - tmp109;
	       }
	       X[2 * iostride] = tmp99 + tmp106;
	       tmp107 = tmp99 - (K500000000 * tmp106);
	       Y[-8 * iostride] = tmp107 - tmp110;
	       Y[-5 * iostride] = tmp107 + tmp110;
	       Y[-2 * iostride] = tmp129 + tmp128;
	       tmp130 = tmp128 - (K500000000 * tmp129);
	       X[5 * iostride] = -(tmp127 + tmp130);
	       X[8 * iostride] = -(tmp130 - tmp127);
	  }
     }
     if (i == m) {
	  fftw_real tmp5;
	  fftw_real tmp10;
	  fftw_real tmp14;
	  fftw_real tmp18;
	  fftw_real tmp7;
	  fftw_real tmp8;
	  fftw_real tmp12;
	  fftw_real tmp16;
	  fftw_real tmp6;
	  fftw_real tmp9;
	  fftw_real tmp13;
	  fftw_real tmp17;
	  fftw_real tmp11;
	  fftw_real tmp15;
	  fftw_real tmp4;
	  fftw_real tmp1;
	  fftw_real tmp3;
	  fftw_real tmp2;
	  ASSERT_ALIGNED_DOUBLE();
	  tmp5 = X[2 * iostride];
	  tmp10 = X[7 * iostride];
	  tmp14 = tmp5 + tmp10;
	  tmp18 = tmp5 - tmp10;
	  tmp7 = X[4 * iostride];
	  tmp8 = X[5 * iostride];
	  tmp12 = tmp8 + tmp7;
	  tmp16 = tmp8 - tmp7;
	  tmp6 = X[8 * iostride];
	  tmp9 = X[iostride];
	  tmp13 = tmp6 + tmp9;
	  tmp17 = tmp6 - tmp9;
	  tmp1 = X[0];
	  tmp3 = X[3 * iostride];
	  tmp2 = X[6 * iostride];
	  tmp11 = K866025403 * (tmp3 + tmp2);
	  tmp15 = tmp1 - (K500000000 * (tmp2 - tmp3));
	  tmp4 = tmp1 + tmp2 - tmp3;
	  Y[0] = -(tmp11 + (K984807753 * tmp12) + (K342020143 * tmp13) + (K642787609 * tmp14));
	  Y[-3 * iostride] = (K342020143 * tmp12) + (K984807753 * tmp14) - (K642787609 * tmp13) - tmp11;
	  Y[-2 * iostride] = tmp11 + (K342020143 * tmp14) - (K984807753 * tmp13) - (K642787609 * tmp12);
	  X[2 * iostride] = tmp15 + (K173648177 * tmp17) - (K939692620 * tmp18) - (K766044443 * tmp16);
	  X[3 * iostride] = tmp15 + (K939692620 * tmp16) + (K766044443 * tmp17) + (K173648177 * tmp18);
	  X[0] = tmp15 + (K766044443 * tmp18) - (K939692620 * tmp17) - (K173648177 * tmp16);
	  Y[-iostride] = K866025403 * (tmp8 + tmp7 - (tmp5 + tmp6 + tmp9 + tmp10));
	  X[iostride] = tmp4 + (K500000000 * (tmp8 + tmp9 + tmp10 - (tmp5 + tmp6 + tmp7)));
	  X[4 * iostride] = tmp4 + tmp5 + tmp6 + tmp7 - (tmp8 + tmp9 + tmp10);
     }
}

static const int twiddle_order[] =
{1, 2, 3, 4, 5, 6, 7, 8};
fftw_codelet_desc fftw_hc2hc_forward_9_desc =
{
     "fftw_hc2hc_forward_9",
     (void (*)()) fftw_hc2hc_forward_9,
     9,
     FFTW_FORWARD,
     FFTW_HC2HC,
     201,
     8,
     twiddle_order,
};
