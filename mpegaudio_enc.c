/*
 * The simplest mpeg audio layer 2 encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This routines are normaly part of FFmpeg and had been isolated
 * for use in DreamDVD by Seddi.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * part of libdreamdvd
 */


#include "mpegaudio_enc.h"


void ddvd_mpa_init(int init_freq, int init_bitrate)
{
	int i, v, table;
    float a;
	
	ddvd_mpa_freq=init_freq;
	ddvd_mpa_bit_rate=init_bitrate;
	
	ddvd_mpa_lsf = 0;
    for(i=0;i<3;i++) {
        if (ddvd_mpa_ff_mpa_freq_tab[i] == ddvd_mpa_freq)
            break;
        if ((ddvd_mpa_ff_mpa_freq_tab[i] / 2) == ddvd_mpa_freq) {
            ddvd_mpa_lsf = 1;
            break;
        }
    }
    if (i == 3){
        return;
    }
    ddvd_mpa_freq_index = i;
		
	/* encoding bitrate & frequency */
    for(i=0;i<15;i++) {
        if (ddvd_mpa_ff_mpa_bitrate_tab[ddvd_mpa_lsf][1][i] == ddvd_mpa_bit_rate/1000)
            break;
    }
    if (i == 15){
        return;
    }
    ddvd_mpa_bitrate_index = i;

	/* compute total header size & pad bit */

    a = (float)(ddvd_mpa_bit_rate * MPA_FRAME_SIZE) / (ddvd_mpa_freq * 8.0);
    ddvd_mpa_frame_size = ((int)a) * 8;

	/* frame fractional size to compute padding */
    ddvd_mpa_frame_frac = 0;
    ddvd_mpa_frame_frac_incr = (int)((a - FLOOR(a)) * 65536.0);

    /* select the right allocation table */
    table = ddvd_mpa_ff_mpa_l2_select_table(ddvd_mpa_bit_rate/1000, NB_CHANNELS, ddvd_mpa_freq, ddvd_mpa_lsf);

    /* number of used subbands */
    ddvd_mpa_sblimit = ddvd_mpa_ff_mpa_sblimit_table[table];
    ddvd_mpa_alloc_table = ddvd_mpa_ff_mpa_alloc_tables[table];


    for(i=0;i<NB_CHANNELS;i++)
        ddvd_mpa_samples_offset[i] = 0;
	
    for(i=0;i<257;i++) {
        int v;
        v = ddvd_mpa_ff_mpa_enwindow[i];
#if WFRAC_BITS != 16
        v = (v + (1 << (16 - WFRAC_BITS - 1))) >> (16 - WFRAC_BITS);
#endif
        ddvd_mpa_filter_bank[i] = v;
        if ((i & 63) != 0)
            v = -v;
        if (i != 0)
            ddvd_mpa_filter_bank[512 - i] = v;
    }
	for(i=0;i<64;i++) {
        v = (int)(pow(2.0, (3 - i) / 3.0) * (1 << 20));
        if (v <= 0)
            v = 1;
        ddvd_mpa_scale_factor_table[i] = v;
#define P 15
        ddvd_mpa_scale_factor_shift[i] = 21 - P - (i / 3);
        ddvd_mpa_scale_factor_mult[i] = (1 << P) * pow(2.0, (i % 3) / 3.0);
    }
    for(i=0;i<128;i++) {
        v = i - 64;
        if (v <= -3)
            v = 0;
        else if (v < 0)
            v = 1;
        else if (v == 0)
            v = 2;
        else if (v < 3)
            v = 3;
        else
            v = 4;
        ddvd_mpa_scale_diff_table[i] = v;
    }
    for(i=0;i<17;i++) {
        v = ddvd_mpa_ff_mpa_quant_bits[i];
        if (v < 0)
            v = -v;
        else
            v = v * 3;
        ddvd_mpa_total_quant_bits[i] = 12 * v;
    }
}


/* 32 point floating point IDCT without 1/sqrt(2) coef zero scaling */
static void ddvd_mpa_idct32(int *out, int *tab)
{
    int i, j;
    int *t, *t1, xr;
    const int *xp = ddvd_mpa_costab32;

    for(j=31;j>=3;j-=2) tab[j] += tab[j - 2];

    t = tab + 30;
    t1 = tab + 2;
    do {
        t[0] += t[-4];
        t[1] += t[1 - 4];
        t -= 4;
    } while (t != t1);

    t = tab + 28;
    t1 = tab + 4;
    do {
        t[0] += t[-8];
        t[1] += t[1-8];
        t[2] += t[2-8];
        t[3] += t[3-8];
        t -= 8;
    } while (t != t1);

    t = tab;
    t1 = tab + 32;
    do {
        t[ 3] = -t[ 3];
        t[ 6] = -t[ 6];

        t[11] = -t[11];
        t[12] = -t[12];
        t[13] = -t[13];
        t[15] = -t[15];
        t += 16;
    } while (t != t1);


    t = tab;
    t1 = tab + 8;
    do {
        int x1, x2, x3, x4;

        x3 = MUL(t[16], FIX(SQRT2*0.5));
        x4 = t[0] - x3;
        x3 = t[0] + x3;

        x2 = MUL(-(t[24] + t[8]), FIX(SQRT2*0.5));
        x1 = MUL((t[8] - x2), xp[0]);
        x2 = MUL((t[8] + x2), xp[1]);

        t[ 0] = x3 + x1;
        t[ 8] = x4 - x2;
        t[16] = x4 + x2;
        t[24] = x3 - x1;
        t++;
    } while (t != t1);

    xp += 2;
    t = tab;
    t1 = tab + 4;
    do {
        xr = MUL(t[28],xp[0]);
        t[28] = (t[0] - xr);
        t[0] = (t[0] + xr);

        xr = MUL(t[4],xp[1]);
        t[ 4] = (t[24] - xr);
        t[24] = (t[24] + xr);

        xr = MUL(t[20],xp[2]);
        t[20] = (t[8] - xr);
        t[ 8] = (t[8] + xr);

        xr = MUL(t[12],xp[3]);
        t[12] = (t[16] - xr);
        t[16] = (t[16] + xr);
        t++;
    } while (t != t1);
    xp += 4;

    for (i = 0; i < 4; i++) {
        xr = MUL(tab[30-i*4],xp[0]);
        tab[30-i*4] = (tab[i*4] - xr);
        tab[   i*4] = (tab[i*4] + xr);

        xr = MUL(tab[ 2+i*4],xp[1]);
        tab[ 2+i*4] = (tab[28-i*4] - xr);
        tab[28-i*4] = (tab[28-i*4] + xr);

        xr = MUL(tab[31-i*4],xp[0]);
        tab[31-i*4] = (tab[1+i*4] - xr);
        tab[ 1+i*4] = (tab[1+i*4] + xr);

        xr = MUL(tab[ 3+i*4],xp[1]);
        tab[ 3+i*4] = (tab[29-i*4] - xr);
        tab[29-i*4] = (tab[29-i*4] + xr);

        xp += 2;
    }

    t = tab + 30;
    t1 = tab + 1;
    do {
        xr = MUL(t1[0], *xp);
        t1[0] = (t[0] - xr);
        t[0] = (t[0] + xr);
        t -= 2;
        t1 += 2;
        xp++;
    } while (t >= tab);

    for(i=0;i<32;i++) {
        out[i] = tab[ddvd_mpa_bitinv32[i]];
    }
}



static void ddvd_mpa_filter(int ch, short *samples, int incr)
{
    short *p, *q;
    int sum, offset, i, j;
    int tmp[64];
    int tmp1[32];
    int *out;

    //    print_pow1(samples, 1152);

    offset = ddvd_mpa_samples_offset[ch];
    out = &ddvd_mpa_sb_samples[ch][0][0][0];
    for(j=0;j<36;j++) {
        /* 32 samples at once */
        for(i=0;i<32;i++) {
            ddvd_mpa_samples_buf[ch][offset + (31 - i)] = samples[0];
            samples += incr;
        }

        /* filter */
        p = ddvd_mpa_samples_buf[ch] + offset;
        q = ddvd_mpa_filter_bank;
        /* maxsum = 23169 */
        for(i=0;i<64;i++) {
            sum = p[0*64] * q[0*64];
            sum += p[1*64] * q[1*64];
            sum += p[2*64] * q[2*64];
            sum += p[3*64] * q[3*64];
            sum += p[4*64] * q[4*64];
            sum += p[5*64] * q[5*64];
            sum += p[6*64] * q[6*64];
            sum += p[7*64] * q[7*64];
            tmp[i] = sum;
            p++;
            q++;
        }
        tmp1[0] = tmp[16] >> WSHIFT;
        for( i=1; i<=16; i++ ) tmp1[i] = (tmp[i+16]+tmp[16-i]) >> WSHIFT;
        for( i=17; i<=31; i++ ) tmp1[i] = (tmp[i+16]-tmp[80-i]) >> WSHIFT;

        ddvd_mpa_idct32(out, tmp1);

        /* advance of 32 samples */
        offset -= 32;
        out += 32;
        /* handle the wrap around */
        if (offset < 0) {
            memmove(ddvd_mpa_samples_buf[ch] + SAMPLES_BUF_SIZE - (512 - 32),
                    ddvd_mpa_samples_buf[ch], (512 - 32) * 2);
            offset = SAMPLES_BUF_SIZE - 512;
        }
    }
    ddvd_mpa_samples_offset[ch] = offset;

    //    print_pow(s->sb_samples, 1152);
}

static void ddvd_mpa_compute_scale_factors(unsigned char ddvd_mpa_scale_code[SBLIMIT],
                                  unsigned char ddvd_mpa_scale_factors[SBLIMIT][3],
                                  int ddvd_mpa_sb_samples[3][12][SBLIMIT],
                                  int ddvd_mpa_sblimit)
{
    int *p, vmax, v, n, i, j, k, code;
    int index, d1, d2;
    unsigned char *sf = &ddvd_mpa_scale_factors[0][0];

    for(j=0;j<ddvd_mpa_sblimit;j++) {
        for(i=0;i<3;i++) {
            /* find the max absolute value */
            p = &ddvd_mpa_sb_samples[i][0][j];
            vmax = abs(*p);
            for(k=1;k<12;k++) {
                p += SBLIMIT;
                v = abs(*p);
                if (v > vmax)
                    vmax = v;
            }
            /* compute the scale factor index using log 2 computations */
            if (vmax > 0) {
                n = ddvd_mpa_av_log2(vmax);
                /* n is the position of the MSB of vmax. now
                   use at most 2 compares to find the index */
                index = (21 - n) * 3 - 3;
                if (index >= 0) {
                    while (vmax <= ddvd_mpa_scale_factor_table[index+1])
                        index++;
                } else {
                    index = 0; /* very unlikely case of overflow */
                }
            } else {
                index = 62; /* value 63 is not allowed */
            }

#if 0
            printf("%2d:%d in=%x %x %d\n",
                   j, i, vmax, ddvd_mpa_scale_factor_table[index], index);
#endif
            /* store the scale factor */
            assert(index >=0 && index <= 63);
            sf[i] = index;
        }

        /* compute the transmission factor : look if the scale factors
           are close enough to each other */
        d1 = ddvd_mpa_scale_diff_table[sf[0] - sf[1] + 64];
        d2 = ddvd_mpa_scale_diff_table[sf[1] - sf[2] + 64];

        /* handle the 25 cases */
        switch(d1 * 5 + d2) {
        case 0*5+0:
        case 0*5+4:
        case 3*5+4:
        case 4*5+0:
        case 4*5+4:
            code = 0;
            break;
        case 0*5+1:
        case 0*5+2:
        case 4*5+1:
        case 4*5+2:
            code = 3;
            sf[2] = sf[1];
            break;
        case 0*5+3:
        case 4*5+3:
            code = 3;
            sf[1] = sf[2];
            break;
        case 1*5+0:
        case 1*5+4:
        case 2*5+4:
            code = 1;
            sf[1] = sf[0];
            break;
        case 1*5+1:
        case 1*5+2:
        case 2*5+0:
        case 2*5+1:
        case 2*5+2:
            code = 2;
            sf[1] = sf[2] = sf[0];
            break;
        case 2*5+3:
        case 3*5+3:
            code = 2;
            sf[0] = sf[1] = sf[2];
            break;
        case 3*5+0:
        case 3*5+1:
        case 3*5+2:
            code = 2;
            sf[0] = sf[2] = sf[1];
            break;
        case 1*5+3:
            code = 2;
            if (sf[0] > sf[2])
              sf[0] = sf[2];
            sf[1] = sf[2] = sf[0];
            break;
        default:
            assert(0); //cannot happen
            code = 0;           /* kill warning */
        }

#if 0
        printf("%d: %2d %2d %2d %d %d -> %d\n", j,
               sf[0], sf[1], sf[2], d1, d2, code);
#endif
        ddvd_mpa_scale_code[j] = code;
        sf += 3;
    }
}

/* The most important function : psycho acoustic module. In this
   encoder there is basically none, so this is the worst you can do,
   but also this is the simpler. */
static void ddvd_mpa_psycho_acoustic_model(short smr[SBLIMIT])
{
    int i;

    for(i=0;i<ddvd_mpa_sblimit;i++) {
        smr[i] = (int)(ddvd_mpa_fixed_smr[i] * 10);
    }
}

/* Try to maximize the smr while using a number of bits inferior to
   the frame size. I tried to make the code simpler, faster and
   smaller than other encoders :-) */
static void ddvd_mpa_compute_bit_allocation(short smr1[MPA_MAX_CHANNELS][SBLIMIT],
                                   unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                                   int *padding)
{
    int i, ch, b, max_smr, max_ch, max_sb, current_frame_size, max_frame_size;
    int incr;
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char subband_status[MPA_MAX_CHANNELS][SBLIMIT];
    const unsigned char *alloc;

    memcpy(smr, smr1, NB_CHANNELS * sizeof(short) * SBLIMIT);
    memset(subband_status, SB_NOTALLOCATED, NB_CHANNELS * SBLIMIT);
    memset(bit_alloc, 0, NB_CHANNELS * SBLIMIT);

    /* compute frame size and padding */
    max_frame_size = ddvd_mpa_frame_size;
    ddvd_mpa_frame_frac += ddvd_mpa_frame_frac_incr;
    if (ddvd_mpa_frame_frac >= 65536) {
        ddvd_mpa_frame_frac -= 65536;
        ddvd_mpa_do_padding = 1;
        max_frame_size += 8;
    } else {
        ddvd_mpa_do_padding = 0;
    }

    /* compute the header + bit alloc size */
    current_frame_size = 32;
    alloc = ddvd_mpa_alloc_table;
    for(i=0;i<ddvd_mpa_sblimit;i++) {
        incr = alloc[0];
        current_frame_size += incr * NB_CHANNELS;
        alloc += 1 << incr;
    }
    for(;;) {
        /* look for the subband with the largest signal to mask ratio */
        max_sb = -1;
        max_ch = -1;
        max_smr = 0x80000000;
        for(ch=0;ch<NB_CHANNELS;ch++) {
            for(i=0;i<ddvd_mpa_sblimit;i++) {
                if (smr[ch][i] > max_smr && subband_status[ch][i] != SB_NOMORE) {
                    max_smr = smr[ch][i];
                    max_sb = i;
                    max_ch = ch;
                }
            }
        }

        if (max_sb < 0)
            break;

        /* find alloc table entry (XXX: not optimal, should use
           pointer table) */
        alloc = ddvd_mpa_alloc_table;
        for(i=0;i<max_sb;i++) {
            alloc += 1 << alloc[0];
        }

        if (subband_status[max_ch][max_sb] == SB_NOTALLOCATED) {
            /* nothing was coded for this band: add the necessary bits */
            incr = 2 + ddvd_mpa_nb_scale_factors[ddvd_mpa_scale_code[max_ch][max_sb]] * 6;
            incr += ddvd_mpa_total_quant_bits[alloc[1]];
        } else {
            /* increments bit allocation */
            b = bit_alloc[max_ch][max_sb];
            incr = ddvd_mpa_total_quant_bits[alloc[b + 1]] -
                ddvd_mpa_total_quant_bits[alloc[b]];
        }

        if (current_frame_size + incr <= max_frame_size) {
            /* can increase size */
            b = ++bit_alloc[max_ch][max_sb];
            current_frame_size += incr;
            /* decrease smr by the resolution we added */
            smr[max_ch][max_sb] = smr1[max_ch][max_sb] - ddvd_mpa_quant_snr[alloc[b]];
            /* max allocation size reached ? */
            if (b == ((1 << alloc[0]) - 1))
                subband_status[max_ch][max_sb] = SB_NOMORE;
            else
                subband_status[max_ch][max_sb] = SB_ALLOCATED;
        } else {
            /* cannot increase the size of this subband */
            subband_status[max_ch][max_sb] = SB_NOMORE;
        }
    }
    *padding = max_frame_size - current_frame_size;
	
	assert(*padding >= 0);

}

static void ddvd_mpa_encode_frame_internal(unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT],
                         int padding)
{
    int i, j, k, l, bit_alloc_bits, b, ch;
    unsigned char *sf;
    int q[3];
    ddvd_mpa_PutBitContext *p = &pb;

    /* header */

    ddvd_mpa_put_bits(p, 12, 0xfff);
    ddvd_mpa_put_bits(p, 1, 1 - ddvd_mpa_lsf); /* 1 = mpeg1 ID, 0 = mpeg2 lsf ID */
    ddvd_mpa_put_bits(p, 2, 4-2);  /* layer 2 */
    ddvd_mpa_put_bits(p, 1, 1); /* no error protection */
    ddvd_mpa_put_bits(p, 4, ddvd_mpa_bitrate_index);
    ddvd_mpa_put_bits(p, 2, ddvd_mpa_freq_index);
    ddvd_mpa_put_bits(p, 1, ddvd_mpa_do_padding); /* use padding */
    ddvd_mpa_put_bits(p, 1, 0);             /* private_bit */
    ddvd_mpa_put_bits(p, 2, NB_CHANNELS == 2 ? MPA_STEREO : MPA_MONO);
    ddvd_mpa_put_bits(p, 2, 0); /* mode_ext */
    ddvd_mpa_put_bits(p, 1, 0); /* no copyright */
    ddvd_mpa_put_bits(p, 1, 1); /* original */
    ddvd_mpa_put_bits(p, 2, 0); /* no emphasis */

    /* bit allocation */
    j = 0;
    for(i=0;i<ddvd_mpa_sblimit;i++) {
        bit_alloc_bits = ddvd_mpa_alloc_table[j];
        for(ch=0;ch<NB_CHANNELS;ch++) {
            ddvd_mpa_put_bits(p, bit_alloc_bits, bit_alloc[ch][i]);
        }
        j += 1 << bit_alloc_bits;
    }

    /* scale codes */
    for(i=0;i<ddvd_mpa_sblimit;i++) {
        for(ch=0;ch<NB_CHANNELS;ch++) {
            if (bit_alloc[ch][i])
                ddvd_mpa_put_bits(p, 2, ddvd_mpa_scale_code[ch][i]);
        }
    }

    /* scale factors */
    for(i=0;i<ddvd_mpa_sblimit;i++) {
        for(ch=0;ch<NB_CHANNELS;ch++) {
            if (bit_alloc[ch][i]) {
                sf = &ddvd_mpa_scale_factors[ch][i][0];
                switch(ddvd_mpa_scale_code[ch][i]) {
                case 0:
                    ddvd_mpa_put_bits(p, 6, sf[0]);
                    ddvd_mpa_put_bits(p, 6, sf[1]);
                    ddvd_mpa_put_bits(p, 6, sf[2]);
                    break;
                case 3:
                case 1:
                    ddvd_mpa_put_bits(p, 6, sf[0]);
                    ddvd_mpa_put_bits(p, 6, sf[2]);
                    break;
                case 2:
                    ddvd_mpa_put_bits(p, 6, sf[0]);
                    break;
                }
            }
        }
    }

    /* quantization & write sub band samples */

    for(k=0;k<3;k++) {
        for(l=0;l<12;l+=3) {
            j = 0;
            for(i=0;i<ddvd_mpa_sblimit;i++) {
                bit_alloc_bits = ddvd_mpa_alloc_table[j];
                for(ch=0;ch<NB_CHANNELS;ch++) {
                    b = bit_alloc[ch][i];
                    if (b) {
                        int qindex, steps, m, sample, bits;
                        /* we encode 3 sub band samples of the same sub band at a time */
                        qindex = ddvd_mpa_alloc_table[j+b];
                        steps = ddvd_mpa_ff_mpa_quant_steps[qindex];
                        for(m=0;m<3;m++) {
                            sample = ddvd_mpa_sb_samples[ch][k][l + m][i];
                            /* divide by scale factor */

                            {
                                int q1, e, shift, mult;
                                e = ddvd_mpa_scale_factors[ch][i][k];
                                shift = ddvd_mpa_scale_factor_shift[e];
                                mult = ddvd_mpa_scale_factor_mult[e];

                                /* normalize to P bits */
                                if (shift < 0)
                                    q1 = sample << (-shift);
                                else
                                    q1 = sample >> shift;
                                q1 = (q1 * mult) >> P;
                                q[m] = ((q1 + (1 << P)) * steps) >> (P + 1);
                            }

                            if (q[m] >= steps)
                                q[m] = steps - 1;
							if (q[m] <= 0) //FIXME
                                q[m] = 0;
                            assert(q[m] >= 0 && q[m] < steps);
                        }
                        bits = ddvd_mpa_ff_mpa_quant_bits[qindex];
                        if (bits < 0) {
                            /* group the 3 values to save bits */
                            ddvd_mpa_put_bits(p, -bits,
                                     q[0] + steps * (q[1] + steps * q[2]));

                        } else {

                            ddvd_mpa_put_bits(p, bits, q[0]);
                            ddvd_mpa_put_bits(p, bits, q[1]);
                            ddvd_mpa_put_bits(p, bits, q[2]);
                        }
                    }
                }
                /* next subband in alloc table */
                j += 1 << bit_alloc_bits;
            }
        }
    }

    /* padding */
    for(i=0;i<padding;i++)
        ddvd_mpa_put_bits(p, 1, 0);

    /* flush */
    ddvd_mpa_flush_put_bits(p);
}

int ddvd_mpa_encode_frame(unsigned char *frame, int buf_size, void *data)
{
    short *samples = data;
    short smr[MPA_MAX_CHANNELS][SBLIMIT];
    unsigned char bit_alloc[MPA_MAX_CHANNELS][SBLIMIT];
    int padding, i;

    for(i=0;i<NB_CHANNELS;i++) {
        ddvd_mpa_filter(i, samples + i, NB_CHANNELS);
    }

    for(i=0;i<NB_CHANNELS;i++) {
        ddvd_mpa_compute_scale_factors(ddvd_mpa_scale_code[i], ddvd_mpa_scale_factors[i],
                              ddvd_mpa_sb_samples[i], ddvd_mpa_sblimit);
    }
    for(i=0;i<NB_CHANNELS;i++) {
        ddvd_mpa_psycho_acoustic_model(smr[i]);
    }
    ddvd_mpa_compute_bit_allocation(smr, bit_alloc, &padding);

    ddvd_mpa_init_put_bits(&pb, frame, MPA_MAX_CODED_FRAME_SIZE);

    ddvd_mpa_encode_frame_internal(bit_alloc, padding);

    ddvd_mpa_nb_samples += MPA_FRAME_SIZE;
    return ddvd_mpa_pbBufPtr(&pb) - pb.buf;
}

