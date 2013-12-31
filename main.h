/* 
 * vim: ts=4
 *
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
 *
 *
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include "libdreamdvd_config.h"

// set to 1 if a start screen should be displayed
#define SHOW_START_SCREEN 1

#define CONVERT_TO_DVB_COMPLIANT_AC3
#define CONVERT_TO_DVB_COMPLIANT_DTS

#define NUM_SPU_BACKBUFFER 8

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

#if defined(HAVE_LINUX_DVB_VERSION_H)
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#define CONFIG_API_VERSION 3
#elif defined(HAVE_OST_DMX_H)
#include <ost/video.h>
#include <ost/audio.h>
#define CONFIG_API_VERSION 1
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

#define MAX_AUDIO       8
#define MAX_SPU         32

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

enum {SPU_NOP, SPU_SHOW, SPU_HIDE, SPU_FORCE};
struct ddvd_spu_return {
	int display_time;
	int x_start;
	int x_end;
	int y_start;
	int y_end;
	int force_hide;
	unsigned long long pts;
};

struct ddvd_resize_return {
	int x_start;
	int x_end;
	int y_start;
	int y_end;	
	
	int x_offset, y_offset, width, height;
};

// some global stuff 

dvdnav_t *dvdnav;
dvdnav_cell_change_event_t ddvd_lastCellEventInfo;

int ddvd_wait_for_user;
int ddvd_lpcm_count;
int ddvd_iframerun;
int ddvd_still_frame;
int ddvd_iframesend;
int ddvd_last_iframe_len;
int ddvd_spu_ptr;
int ddvd_lbb_changed;
int ddvd_clear_screen;

enum {
	TOFF    = 0x00,
	FASTFW  = 0x01,
	FASTBW  = 0x02,
	TRICKFW = 0x04,
	TRICKBW = 0x08,
	SLOWFW  = 0x10,
	SLOWBW  = 0x20
};

int ddvd_trickmode, ddvd_trickspeed;

enum {
	STOP  = 0x00,
	PLAY  = 0x01,
	PAUSE = 0x02,
	STEP  = 0x04
};

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

struct ddvd_size_evt {
	int width;
	int height;
	int aspect;
};

struct ddvd_framerate_evt {
	int framerate;
};

struct ddvd_progressive_evt {
	int progressive;
};

/* struct to maintain subtitle stream info */
struct spu_map_t {
	int8_t logical_id	:  8;
	int8_t stream_id	:  8;
	int16_t lang		: 16;
};

/* struct for ddvd nav handle*/
struct ddvd {
	/* config options */
	char language[2]; 				// iso code (de, en, ...)
	int aspect;						// 0-> 4:3 1-> 16:9 2-> 16:10
	int tv_mode;					// 0-> letterbox 1-> pan_scan 2-> justscale
	int tv_mode2;					// 0-> letterbox 1-> pan_scan 2-> justscale
	int tv_system;					// 0-> PAL 1-> NTSC
	int ac3thru;					// 0-> internal soft decoding 1-> ac3 pass thru to optical out
	unsigned char *lfb;				// framebuffer to render subtitles and menus
	int xres;						// x resolution of the framebuffer (normally 720, we dont scale inside libdreamdvd)
	int yres;						// y resolution of the framebuffer (normally 576, we dont scale inside libdreamdvd)
	int stride;						// line_length of the framebuffer (normally 720*bypp, but not always like on DM7025)
	int bypp;						// the bytes per pixel only 1 (8bit framebuffer) or 4 (32bit) are supported
	int canscale;
	int key_pipe[2];				// pipe for sending a command/remote control key (sizeof(int)) to the player
	int message_pipe[2];			// pipe for getting player status, osd time and text as well as 8bit color tables
	char *dvd_path;					// the path of a dvd block device ("/dev/dvd"), an iso-file ("/hdd/dvd.iso")
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
	struct ddvd_size_evt last_size;
	struct ddvd_framerate_evt last_framerate;
	struct ddvd_progressive_evt last_progressive;
	uint64_t next_time_update;
	
	int in_menu;
	int resume_title;				// title, chapter, block for resuming dvd or
	int resume_chapter;				// getting actual resume position
	unsigned long int resume_block;
	int resume_audio_id;
	int resume_audio_lock;
	int resume_spu_id;
	int resume_spu_lock;
	int should_resume;
	struct ddvd_resize_return blit_area;
	int angle_current;
	int angle_num;

	int audio_format[MAX_AUDIO];
	struct spu_map_t spu_map[MAX_SPU];
};

/* internal functions */
static struct 	ddvd_time ddvd_get_osd_time(struct ddvd *playerconfig);
static int 		ddvd_readpipe(int pipefd, void *dest, size_t bytes, int blocked_read);
static int 		ddvd_check_aspect(int dvd_aspect, int dvd_scale_perm, int tv_aspect, int tv_mode);
static uint64_t	ddvd_get_time(void);
static void 	ddvd_play_empty(int device_clear);
static void 	ddvd_device_clear(void);
static struct 	ddvd_spu_return	ddvd_spu_decode_data(char *spu_buf, const uint8_t * buffer, unsigned long long pts);
static void 	ddvd_blit_to_argb(void *_dst, const void *_src, int pix);
#if CONFIG_API_VERSION == 3
static void 	ddvd_set_pcr_offset(void);
static void 	ddvd_unset_pcr_offset(void);
#endif
struct 	ddvd_resize_return	ddvd_resize_pixmap_xbpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
struct 	ddvd_resize_return	ddvd_resize_pixmap_xbpp_smooth(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
struct	ddvd_resize_return	ddvd_resize_pixmap_1bpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
struct	ddvd_resize_return	(*ddvd_resize_pixmap)(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors);
#endif
