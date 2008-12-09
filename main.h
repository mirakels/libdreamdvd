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

#ifndef __MAIN_H__

#define __MAIN_H__

// set to 1 if a start screen should be displayed
#define SHOW_START_SCREEN 1

#define CONVERT_TO_DVB_COMPLIANT_AC3

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <byteswap.h>
#include <errno.h>
#include <poll.h>

#include <dvdnav/dvdnav.h>
#include "ddvdlib.h"

#if SHOW_START_SCREEN == 1
#include "logo.h" // startup screen 
#endif

#ifndef BYTE_ORDER
#error "no BYTE_ORDER defined!!!!"
#endif

#if BYTE_ORDER == BIG_ENDIAN
#warning "assume api v1 when byte order is big endian !!"
#define CONFIG_API_VERSION 1
#else
#warning "assume api v3 when byte order is little endian !!"
#define CONFIG_API_VERSION 3
#endif

#if CONFIG_API_VERSION == 1
#include <ost/video.h>
#include <ost/audio.h>
#elif CONFIG_API_VERSION == 3
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#endif

#define BUFFER_SIZE 4096
#define AC3_BUFFER_SIZE (6*1024*16)

#if CONFIG_API_VERSION == 1
#define VIDEO_GET_PTS           _IOR('o', 1, unsigned int*)
#endif
#if CONFIG_API_VERSION == 3
#ifndef VIDEO_GET_PTS
#define VIDEO_GET_PTS              _IOR('o', 57, unsigned long long)
#endif
#endif

#define CLAMP(x)     ((x < 0) ? 0 : ((x > 255) ? 255 : x))
#define CELL_STILL       0x02
#define NAV_STILL        0x04
#define TRUE 1
#define FALSE 0
#define SAAIOSWSS      10 /* set wide screen signaling data */
#define SAAIOSENC       4 /* set encoder (pal/ntsc) */
#define SAA_WSS_43F     0
#define SAA_WSS_169F    7
#define SAA_WSS_OFF     8
#define SAA_NTSC        0
#define SAA_PAL         1

typedef struct ddvd_spudec_clut_struct {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t	entry0	: 4;
	uint8_t	entry1	: 4;
	uint8_t	entry2	: 4;
	uint8_t	entry3	: 4;
#else
	uint8_t	entry1	: 4;
	uint8_t	entry0	: 4;
	uint8_t	entry3	: 4;
	uint8_t	entry2	: 4;
#endif
} ddvd_spudec_clut_t;

struct ddvd_spu_return {
	int display_time;
	int x_start;
	int x_end;
	int y_start;
	int y_end;
};

// some global stuff 

dvdnav_t *dvdnav;
dvdnav_cell_change_event_t ddvd_lastCellEventInfo;

int ddvd_wait_for_user;
int ddvd_clear_buttons;
int ddvd_lpcm_count;
int ddvd_iframerun;
int ddvd_still_frame;
int ddvd_iframesend;
int ddvd_last_iframe_len;
int ddvd_spu_ptr,ddvd_display_time;
int ddvd_spu_backptr,ddvd_spu_backnr;
int ddvd_lbb_changed;

enum {TOFF, SLOWFW, FASTFW, SLOWBW, FASTBW};
int ddvd_trickmode,ddvd_trickspeed;

enum {STOP, PLAY, PAUSE};
int ddvd_playmode;

int ddvd_wait_timer_active;
uint64_t ddvd_wait_timer_end;

int ddvd_spu_timer_active;
uint64_t ddvd_spu_timer_end;

int ddvd_trick_timer_active;
uint64_t ddvd_trick_timer_end;

unsigned char *ddvd_lbb, *ddvd_lbb2;
int ddvd_output_fd, ddvd_fdvideo, ddvd_fdaudio, ddvd_ac3_fd;

int ddvd_screeninfo_xres, ddvd_screeninfo_yres, ddvd_screeninfo_stride;

unsigned short ddvd_rd[256],ddvd_gn[256],ddvd_bl[256],ddvd_tr[256];

/* struct for ddvd nav handle*/
struct ddvd {
	/* config options */
	char language[2]; 				// iso code (de, en, ...)
	int aspect;						// 0-> 4:3 lb 1-> 4:3 ps 2-> 16:9 3-> always 16:9
	int tv_mode;					//
	int tv_system;					// 0-> PAL 1-> NTSC
	int ac3thru;					// 0-> internal soft decoding 1-> ac3 pass thru to optical out
	unsigned char *lfb;				// framebuffer to render subtitles and menus
	int xres;						// x resolution of the framebuffer (normally 720, we dont scale inside libdreamdvd)
	int yres;						// y resolution of the framebuffer (normally 576, we dont scale inside libdreamdvd)
	int stride;						// line_length of the framebuffer (normally 720*bypp, but not always like on DM7025)
	int bypp;						// the bytes per pixel only 1 (8bit framebuffer) or 4 (32bit) are supported
	int key_pipe[2];				// pipe for sending a command/remote control key (sizeof(int)) to the player
	int message_pipe[2];			// pipe for getting player status, osd time and text as well as 8bit color tables
	char *dvd_path;				// the path of a dvd block device ("/dev/dvd"), an iso-file ("/hdd/dvd.iso")
									// or a dvd file structure ("/hdd/dvd/mymovie") to play 
	/* buffer for actual states */
	char title_string[96];
	struct ddvd_color last_col[4];	// colortable (8Bit mode), 4 colors
	struct ddvd_time last_time;		// last playing time for osd
	int last_trickspeed;			// last trickspeed for osd
	char last_string[512];			// last text message for frontend/osd
	int last_audio_id;				// active audio id
	int last_audio_type;			// active audio type
	uint16_t last_audio_lang;		// active audio language
	int last_spu_id;				// active subtitle id
	uint16_t last_spu_lang;			// active subtitle language
	uint64_t next_time_update;
	int in_menu;
	int resume_title;				// title, chapter, block for resuming dvd or
	int resume_chapter;				// getting actual resume position
	uint32_t resume_block;
	int resume_audio_id;
	int resume_audio_lock;
	int resume_spu_id;
	int resume_spu_lock;
	int should_resume;
};

/* internal functions */
static struct 		ddvd_time ddvd_get_osd_time(struct ddvd *playerconfig);
static int 		ddvd_readpipe(int pipefd, void *dest, size_t bytes, int blocked_read);
static int 		ddvd_check_aspect(int dvd_aspect, int dvd_scale_perm, int tv_aspect, int tv_mode);
static uint64_t 	ddvd_get_time(void);
static void 		ddvd_play_empty(int device_clear);
static void 		ddvd_device_clear(void);
static struct 		ddvd_spu_return	ddvd_spu_decode_data(const uint8_t * buffer, int len);
static void 		ddvd_blit_to_argb(void *_dst, const void *_src, int pix);
#if CONFIG_API_VERSION == 3
static void 		ddvd_set_pcr_offset(void);
static void 		ddvd_unset_pcr_offset(void);
#endif
void 				ddvd_resize_pixmap_xbpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
void 				ddvd_resize_pixmap_xbpp_smooth(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
void				ddvd_resize_pixmap_1bpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
void				(*ddvd_resize_pixmap)(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
void				(*ddvd_resize_pixmap_spu)(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);

#endif
