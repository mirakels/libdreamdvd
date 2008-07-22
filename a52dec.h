/* 
 * DreamDVD V0.9 - DVD-Player for Dreambox
 * Copyright (C) 2007 by Seddi
 * 
 * This DVD Player is based upon the great work from the libdvdnav project,
 * a52dec library, ffmpeg and the knowledge from all the people who made 
 * watching DVD within linux possible.
 * 
 * DreamDVD is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * DreamDVD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * part of libdreamdvd
 */

#ifndef __A52DEC_H__

#define __A52DEC_H__


typedef int32_t sample_t;
typedef int32_t level_t;

typedef struct a52_state_s a52_state_t;

#define A52_CHANNEL 0
#define A52_MONO 1
#define A52_STEREO 2
#define A52_3F 3
#define A52_2F1R 4
#define A52_3F1R 5
#define A52_2F2R 6
#define A52_3F2R 7
#define A52_CHANNEL1 8
#define A52_CHANNEL2 9
#define A52_DOLBY 10
#define A52_CHANNEL_MASK 15

#define A52_LFE 16
#define A52_ADJUST_LEVEL 32

// liba52 function defs for using dlsym
a52_state_t * (*a52_init) (uint32_t);
sample_t * (*a52_samples) (a52_state_t *);
int (*a52_syncinfo) (uint8_t * , int * , int * , int * );
int (*a52_frame) (a52_state_t * , uint8_t * , int * , level_t * , sample_t );
int (*a52_block) (a52_state_t * );
void (*a52_free) (a52_state_t * );

void *a52_handle;

a52_state_t * state;

int ddvd_ac3_decode(const uint8_t *input, unsigned int len, int16_t *output);
int ddvd_load_liba52();
void ddvd_close_liba52();

#endif
