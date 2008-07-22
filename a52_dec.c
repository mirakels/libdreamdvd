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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <inttypes.h>
#include <dlfcn.h>


#include "a52dec.h"


// try to dynamically load and wrap liba52.so.0

int ddvd_load_liba52()
{
	a52_handle = dlopen("liba52.so.0",RTLD_LAZY);
	
	if (a52_handle)
	{
		a52_init = (a52_state_t* (*)(uint32_t)) dlsym(a52_handle, "a52_init");
		a52_samples = (sample_t* (*)(a52_state_t*)) dlsym(a52_handle, "a52_samples");
		a52_syncinfo = (int (*)(uint8_t*, int*, int*, int*)) dlsym(a52_handle, "a52_syncinfo");
		a52_frame = (int (*)(a52_state_t* ,uint8_t* ,int* ,level_t* ,sample_t)) dlsym(a52_handle, "a52_frame");
		a52_block = (int (*)(a52_state_t*)) dlsym(a52_handle, "a52_block");
		a52_free = (void (*)(a52_state_t*)) dlsym(a52_handle, "a52_free");

		printf("libdreamdvd: soft ac3 decoding is available, liba52.so.0 loaded !\n");
		return 1;
	}
	else
	{
		printf("libdreamdvd: soft ac3 decoding is not available, liba52.so.0 not found !\n");
		return 0;
	}
}

// close dynamically loaded liba52.so.0

void ddvd_close_liba52()
{
	dlclose(a52_handle);
}

// convert 32bit samples to 16bit

static inline int16_t a52_convert (int32_t i)
{
    i >>= 15;
    return (i > 32767) ? 32767 : ((i < -32768) ? -32768 : i);
}

// liba52 gives us 256 samples left - 256 samples right
// we need 1 left - 1 right so lets sort them

static void a52_convert2s16_2 (sample_t * _f, int16_t * s16)
{
    int i;
    int32_t * f = (int32_t *) _f;

    for (i = 0; i < 256; i++) {
	s16[2*i] = a52_convert (f[i]);
	s16[2*i+1] = a52_convert (f[i+256]);
    }
}

// a52 decode function (needs liba52)

int ddvd_ac3_decode(const uint8_t *input, unsigned int len, int16_t *output)
{
	static int sample_rate;
    static int flags;
    int bit_rate;
	int out_len=0;
	const uint8_t *end; 
	end=input+len;
	
    static uint8_t buf[3840];
    static uint8_t * bufptr = buf;
    static uint8_t * bufpos = buf + 7;

    while (1) {
	len = end - input;
	if (!len)
	    break;
	if (len > bufpos - bufptr)
	    len = bufpos - bufptr;
	
	memcpy (bufptr, input, len);
	bufptr += len;
	input += len;

	if (bufptr == bufpos) {
	    if (bufpos == buf + 7) {
		int length;

		length = a52_syncinfo (buf, &flags, &sample_rate, &bit_rate);
		if (!length) {
		    for (bufptr = buf; bufptr < buf + 6; bufptr++)
			bufptr[0] = bufptr[1];
		    continue;
		}
		bufpos = buf + length;
	    } else {
		level_t level;
		sample_t bias;
		int i;

		flags=A52_DOLBY|A52_ADJUST_LEVEL;
			
		bias=0;
		level=(1 << 26);

		if (a52_frame (state, buf, &flags, &level, bias))
		    goto error;

		for (i = 0; i < 6; i++) {
		    if (a52_block (state))
			goto error;
			a52_convert2s16_2(a52_samples(state),output);
			output+=512;
			out_len+=1024;
		}
		bufptr = buf;
		bufpos = buf + 7;
		continue;
	    error:
		bufptr = buf;
		bufpos = buf + 7;
	    }
	}
    }
	return out_len;
}
