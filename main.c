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

#include "main.h"
#include "mpegaudioenc.h"
#include "a52dec.h"

/*
 * local helper functions
 */
static ssize_t safe_write(int fd, const void *buf, size_t count)
{
	const uint8_t *ptr = buf;
	size_t written = 0;
	ssize_t n;

	while (written < count) {
		n = write(fd, &ptr[written], count - written);
		if (n < 0) {
			if (errno != EINTR) {
				perror("write");
				return written ? written : -1;
			}
		} else {
			written += n;
		}
	}

	return written;
}

static void write_string(const char *filename, const char *string)
{
	FILE *f;

	f = fopen(filename, "w");
	if (f == NULL) {
		perror(filename);
		return;
	}

	fputs(string, f);
	fclose(f);
}

static int open_pipe(int fd[2])
{
	int flags;

	fd[0] = fd[1] = -1;

	if (pipe(fd) < 0) {
		perror("pipe");
		goto err;
	}

	flags = fcntl(fd[0], F_GETFL);
	if (flags < 0) {
		perror("F_GETFL");
		goto err;
	}
	if (fcntl(fd[0], F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("F_SETFL");
		goto err;
	}

	return 0;

err:
	if (fd[0] != -1) {
		close(fd[0]);
		fd[0] = -1;
	}
	if (fd[1] != -1) {
		close(fd[1]);
		fd[1] = -1;
	}

	return -1;
}

/*
 * external functions
 */

// create ddvd handle and set defaults
struct ddvd *ddvd_create(void)
{
	struct ddvd *pconfig;

	pconfig = malloc(sizeof(struct ddvd));
	if (pconfig == NULL) {
		perror("malloc");
		return NULL;
	}

	memset(pconfig, 0, sizeof(struct ddvd));

	// defaults
	ddvd_set_ac3thru(pconfig, 0);
	ddvd_set_language(pconfig, "en");
	ddvd_set_dvd_path(pconfig, "/dev/cdroms/cdrom0");
	ddvd_set_video(pconfig, DDVD_4_3, DDVD_LETTERBOX, DDVD_PAL);
	ddvd_set_lfb(pconfig, NULL, 720, 576, 1, 720);
	struct ddvd_resume resume_info;
	resume_info.title=resume_info.chapter=resume_info.block=resume_info.audio_id=resume_info.audio_lock=resume_info.spu_id=resume_info.spu_lock=0;
	ddvd_set_resume_pos(pconfig, resume_info);
	pconfig->should_resume = 0;
	pconfig->next_time_update = 0;
	strcpy(pconfig->title_string, "");

	// open pipes
	if (open_pipe(pconfig->key_pipe) < 0) {
		ddvd_close(pconfig);
		return NULL;
	}
	if (open_pipe(pconfig->message_pipe) < 0) {
		ddvd_close(pconfig);
		return NULL;
	}

	return pconfig;
}

// destroy ddvd handle
void ddvd_close(struct ddvd *pconfig)
{
	if (pconfig->message_pipe[0] != -1)
		close(pconfig->message_pipe[0]);
	if (pconfig->message_pipe[1] != -1)
		close(pconfig->message_pipe[1]);
	if (pconfig->key_pipe[0] != -1)
		close(pconfig->key_pipe[0]);
	if (pconfig->key_pipe[1] != -1)
		close(pconfig->key_pipe[1]);
	if (pconfig->dvd_path != NULL)
		free(pconfig->dvd_path);

	free(pconfig);
}

// get message_pipe fd for polling functions in the host app
int ddvd_get_messagepipe_fd(struct ddvd *pconfig)
{
	return pconfig->message_pipe[0];
}

// set resume postion
void ddvd_set_resume_pos(struct ddvd *pconfig, struct ddvd_resume resume_info)
{
	pconfig->resume_title = resume_info.title;
	pconfig->resume_chapter = resume_info.chapter;
	pconfig->resume_block = resume_info.block;
	pconfig->should_resume = 1;
	pconfig->resume_audio_id = resume_info.audio_id;
	pconfig->resume_audio_lock = resume_info.audio_lock;
	pconfig->resume_spu_id = resume_info.spu_id;
	pconfig->resume_spu_lock = resume_info.spu_lock;
}

// set framebuffer options
void ddvd_set_lfb(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride)
{
	pconfig->lfb = lfb;
	pconfig->xres = xres;
	pconfig->yres = yres;
	pconfig->stride = stride;
	pconfig->bypp = bypp;
}

// set path to dvd block device or file structure/iso
void ddvd_set_dvd_path(struct ddvd *pconfig, const char *path)
{
	if (pconfig->dvd_path != NULL)
		free(pconfig->dvd_path);

	pconfig->dvd_path = strdup(path);
}

// set language
void ddvd_set_language(struct ddvd *pconfig, const char lang[2])
{
	memcpy(pconfig->language, lang, 2);
}

// set internal ac3 decoding (needs liba52 which will be dynamically loaded)
void ddvd_set_ac3thru(struct ddvd *pconfig, int ac3thru)
{
	pconfig->ac3thru = ac3thru;
}

// set video options
void ddvd_set_video(struct ddvd *pconfig, int aspect, int tv_mode, int tv_system)
{
	pconfig->aspect = aspect;
	pconfig->tv_mode = tv_mode;
	pconfig->tv_system = tv_system;
}

// send commands/keys to the main player
void ddvd_send_key(struct ddvd *pconfig, int key)
{
	safe_write(pconfig->key_pipe[1], &key, sizeof(int));
}

// skip n seconds in playing n>0 forward - n<0 backward
void ddvd_skip_seconds(struct ddvd *pconfig, int seconds)
{
	if (seconds < 0)
		ddvd_send_key(pconfig, DDVD_SKIP_BWD);
	else
		ddvd_send_key(pconfig, DDVD_SKIP_FWD);

	ddvd_send_key(pconfig, seconds);
}

// jump to beginning of given title
void ddvd_set_title(struct ddvd *pconfig, int title)
{
	ddvd_send_key(pconfig, DDVD_SET_TITLE);
	ddvd_send_key(pconfig, title);
}

// jump to beginning of given chapter
void ddvd_set_chapter(struct ddvd *pconfig, int chapter)
{
	ddvd_send_key(pconfig, DDVD_SET_CHAPTER);
	ddvd_send_key(pconfig, chapter);
}

// get and process the next message from the main player
int ddvd_get_next_message(struct ddvd *pconfig, int blocked)
{
	int res;
	int i;

	if (ddvd_readpipe(pconfig->message_pipe[0], &res, sizeof(int), blocked) != sizeof(int))
		res = DDVD_NULL;

	switch (res)		// more data to process ?
	{
	case DDVD_COLORTABLE_UPDATE:
		for (i = 0; i < 4; i++)
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_col[i], sizeof(struct ddvd_color), 1);
		break;
	case DDVD_SHOWOSD_TIME:
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time), 1);
		break;
	case DDVD_SHOWOSD_STATE_FFWD:
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_trickspeed, sizeof(pconfig->last_trickspeed), 1);
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time), 1);
		break;
	case DDVD_SHOWOSD_STATE_FBWD:
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_trickspeed, sizeof(pconfig->last_trickspeed), 1);
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time), 1);
		break;
	case DDVD_SHOWOSD_STRING:
		ddvd_readpipe(pconfig->message_pipe[0], pconfig->last_string, sizeof(pconfig->last_string), 1);
		break;
	case DDVD_SHOWOSD_AUDIO:
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_id, sizeof(int), 1);
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_lang, sizeof(uint16_t), 1);
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_type, sizeof(int), 1);
		break;
	case DDVD_SHOWOSD_SUBTITLE:
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_spu_id, sizeof(int), 1);
		ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_spu_lang, sizeof(uint16_t), 1);
		break;
	default:
		break;
	}

	return res;
}

// get last colortable for 8bit mode (4 colors)
void ddvd_get_last_colortable(struct ddvd *pconfig, void *colortable)
{
	memcpy(colortable, pconfig->last_col, sizeof(pconfig->last_col));
}

// get last received playing time as struct ddvd_time
void ddvd_get_last_time(struct ddvd *pconfig, void *timestamp)
{
	memcpy(timestamp, &pconfig->last_time, sizeof(pconfig->last_time));
}

// get the actual trickspeed (2-64x) when in trickmode
void ddvd_get_last_trickspeed(struct ddvd *pconfig, void *trickspeed)
{
	memcpy(trickspeed, &pconfig->last_trickspeed, sizeof(pconfig->last_trickspeed));
}

// get last text message from player
void ddvd_get_last_string(struct ddvd *pconfig, void *text)
{
	memcpy(text, pconfig->last_string, sizeof(pconfig->last_string));
}

// get the active audio track
void ddvd_get_last_audio(struct ddvd *pconfig, void *id, void *lang, void *type)
{
	memcpy(id, &pconfig->last_audio_id, sizeof(pconfig->last_audio_id));
	memcpy(lang, &pconfig->last_audio_lang, sizeof(pconfig->last_audio_lang));
	memcpy(type, &pconfig->last_audio_type, sizeof(pconfig->last_audio_type));
}

// get the active SPU track
void ddvd_get_last_spu(struct ddvd *pconfig, void *id, void *lang)
{
	memcpy(id, &pconfig->last_spu_id, sizeof(pconfig->last_spu_id));
	memcpy(lang, &pconfig->last_spu_lang, sizeof(pconfig->last_spu_lang));
}

// get dvd title string
void ddvd_get_title_string(struct ddvd *pconfig, char *title_string)
{
	memcpy(title_string, pconfig->title_string, sizeof(pconfig->title_string));
}

// get actual position for resuming
void ddvd_get_resume_pos(struct ddvd *pconfig, struct ddvd_resume *resume_info)
{
	memcpy(&resume_info->title, &pconfig->resume_title, sizeof(pconfig->resume_title));
	memcpy(&resume_info->chapter, &pconfig->resume_chapter, sizeof(pconfig->resume_chapter));
	memcpy(&resume_info->block, &pconfig->resume_block, sizeof(pconfig->resume_block));
	memcpy(&resume_info->audio_id, &pconfig->resume_audio_id, sizeof(pconfig->resume_audio_id));
	memcpy(&resume_info->audio_lock, &pconfig->resume_audio_lock, sizeof(pconfig->resume_audio_lock));
	memcpy(&resume_info->spu_id, &pconfig->resume_spu_id, sizeof(pconfig->resume_spu_id));
	memcpy(&resume_info->spu_lock, &pconfig->resume_spu_lock, sizeof(pconfig->resume_spu_lock));
}

// the main player loop
enum ddvd_result ddvd_run(struct ddvd *playerconfig)
{
	if (playerconfig->lfb == NULL) {
		printf("Frame/backbuffer not given to libdreamdvd. Will not start the player !\n");
		return DDVD_INVAL;
	}

	// we need to know the first vts_change and the next cell change for resuming a dvd
	int first_vts_change = 1;
	int next_cell_change = 0;
	int ddvd_have_ntsc = -1;
	
	ddvd_screeninfo_xres = playerconfig->xres;
	ddvd_screeninfo_yres = playerconfig->yres;
	ddvd_screeninfo_stride = playerconfig->stride;
	int ddvd_screeninfo_bypp = playerconfig->bypp;
	int message_pipe = playerconfig->message_pipe[1];
	int key_pipe = playerconfig->key_pipe[0];
	unsigned char *p_lfb = playerconfig->lfb;
	enum ddvd_result res = DDVD_OK;
	int msg;
	// try to load liba52.so.0 for softdecoding
	int have_liba52 = ddvd_load_liba52();
	
	// decide which resize routine we should use
	// on 4bpp mode we use bicubic resize for sd skins because we get much better results with subtitles and the speed is ok
	// for hd skins we use nearest neighbor resize because upscaling to hd is too slow with bicubic resize
	if (ddvd_screeninfo_bypp == 1)
		ddvd_resize_pixmap = ddvd_resize_pixmap_spu = &ddvd_resize_pixmap_1bpp;
	else if (ddvd_screeninfo_xres > 720)
		ddvd_resize_pixmap = ddvd_resize_pixmap_spu = &ddvd_resize_pixmap_xbpp;
	else
	{
		ddvd_resize_pixmap = &ddvd_resize_pixmap_xbpp;
		ddvd_resize_pixmap_spu = &ddvd_resize_pixmap_xbpp_smooth;
	}

	uint8_t *last_iframe = NULL;
	uint8_t *spu_buffer = NULL;
	uint8_t *spu_backbuffer = NULL;

	// init backbuffer (SPU)
	ddvd_lbb = malloc(720 * 576);	// the spu backbuffer is always max DVD PAL 720x576 pixel (NTSC 720x480)
	if (ddvd_lbb == NULL) {
		perror("SPU-Backbuffer <mem allocation failed>");
		res = DDVD_NOMEM;
		goto err_malloc;
	}
	ddvd_lbb2 = malloc(ddvd_screeninfo_xres * ddvd_screeninfo_yres * ddvd_screeninfo_bypp);
	if (ddvd_lbb2 == NULL) {
		perror("SPU-Backbuffer <mem allocation failed>");
		res = DDVD_NOMEM;
		goto err_malloc;
	}
	
	last_iframe = malloc(320 * 1024);
	if (last_iframe == NULL) {
		perror("malloc");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

	spu_buffer = malloc(2 * (128 * 1024));
	if (spu_buffer == NULL) {
		perror("malloc");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

	spu_backbuffer = malloc(3 * 2 * (128 * 1024));
	if (spu_backbuffer == NULL) {
		perror("malloc");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

	memset(ddvd_lbb, 0, 720 * 576);
	memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen

	msg = DDVD_SCREEN_UPDATE;
	safe_write(message_pipe, &msg, sizeof(int));

	printf("Opening output...\n");

#if CONFIG_API_VERSION == 1
	ddvd_output_fd = open("/dev/video", O_WRONLY);
	if (ddvd_output_fd == -1) {
		perror("/dev/video");
		res = DDVD_BUSY;
		goto err_open_output_fd;
	}

	ddvd_fdvideo = open("/dev/dvb/card0/video0", O_RDWR);
	if (ddvd_fdvideo == -1) {
		perror("/dev/dvb/card0/video0");
		res = DDVD_BUSY;
		goto err_open_fdvideo;
	}

	ddvd_fdaudio = open("/dev/dvb/card0/audio0", O_RDWR);
	if (ddvd_fdaudio == -1) {
		perror("/dev/dvb/card0/audio0");
		res = DDVD_BUSY;
		goto err_open_fdaudio;
	}

	ddvd_ac3_fd = open("/dev/sound/dsp1", O_RDWR);
	if (ddvd_ac3_fd == -1) {
		perror("/dev/sound/dsp1");
		res = DDVD_BUSY;
		goto err_open_ac3_fd;
	}

	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY) < 0)
		perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		perror("VIDEO_PLAY");

	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY) < 0)
		perror("AUDIO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		perror("AUDIO_PLAY");

#elif CONFIG_API_VERSION == 3
	ddvd_output_fd = ddvd_fdvideo = open("/dev/dvb/adapter0/video0", O_RDWR);
	if (ddvd_fdvideo == -1) {
		perror("/dev/dvb/adapter0/video0");
		res = DDVD_BUSY;
		goto err_open_fdvideo;
	}

	ddvd_ac3_fd = ddvd_fdaudio = open("/dev/dvb/adapter0/audio0", O_RDWR);
	if (ddvd_fdaudio == -1) {
		perror("/dev/dvb/adapter0/audio0");
		res = DDVD_BUSY;
		goto err_open_ac3_fd;
	}

	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY) < 0)
		perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_SET_STREAMTYPE, 0) < 0)	// set mpeg2
		perror("VIDEO_SET_STREAMTYPE");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		perror("VIDEO_PLAY");

	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY) < 0)
		perror("AUDIO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		perror("AUDIO_PLAY");
#else
#error please define CONFIG_API_VERSION to be 1 or 3
#endif

// show startup screen
#if SHOW_START_SCREEN == 1
#if CONFIG_API_VERSION == 1
	int i;
	//that really sucks but there is no other way
	for (i = 0; i < 10; i++)
		safe_write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
#else
	unsigned char pes_header[] = { 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x00, 0x00 };
	safe_write(ddvd_output_fd, pes_header, sizeof(pes_header));
	safe_write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
	safe_write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
#endif
#endif

	int audio_type = DDVD_AC3;

#if CONFIG_API_VERSION == 3
	ddvd_set_pcr_offset();
#endif

	uint8_t mem[DVD_VIDEO_LB_LEN];
	uint8_t *buf = mem;
	int result, event, len;

	unsigned char lpcm_data[2048 * 6 * 6 /*4608 */ ];
	int mpa_header_length;
	int mpa_count, mpa_count2;
	uint8_t mpa_data[2048 * 4];
	int ac3_len;
	int16_t ac3_tmp[2048 * 6 * 6];

	ddvd_mpa_init(48000, 192000);	//init MPA Encoder with 48kHz and 192k Bitrate

	int ac3thru = 1;
	if (have_liba52) {
		state = a52_init(0);	//init AC3 Decoder 
		ac3thru = playerconfig->ac3thru;
	}

	char osdtext[512];
	strcpy(osdtext, "");

	int tv_aspect = playerconfig->aspect;	//0-> 4:3 lb 1-> 4:3 ps 2-> 16:9 3-> always 16:9
	int tv_mode = playerconfig->tv_mode;
	int dvd_aspect = 0;	//0-> 4:3 2-> 16:9
	int dvd_scale_perm = 0;
	int tv_scale = 0;	//0-> off 1-> letterbox 2-> panscan
	int spu_active_id = -1;
	int finished = 0;
	int audio_id;
	int report_audio_info = 0;

	ddvd_trickmode = TOFF;
	ddvd_trickspeed = 0;

	int rccode;
	int ismute = 0;

	if (ac3thru) {
		if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
			perror("AUDIO_SET_AV_SYNC");
#ifdef CONVERT_TO_DVB_COMPLIANT_AC3
		if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 0) < 0)	// AC3 (dvb compliant)
#else
		if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 3) < 0)	// AC3 VOB
#endif
			perror("AUDIO_SET_BYPASS_MODE");
	} else {
		if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
			perror("AUDIO_SET_AV_SYNC");
		if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
			perror("AUDIO_SET_BYPASS_MODE");
	}

#if CONFIG_API_VERSION == 1	
	// set video system
	int pal_ntsc = playerconfig->tv_system;
	int saa;
	if (pal_ntsc == 1)
		saa = SAA_NTSC;
	else
		saa = SAA_PAL;
	int saafd = open("/dev/dbox/saa0", O_RDWR);
	if (saafd >= 0) {
		if (ioctl(saafd, SAAIOSENC, &saa) < 0)
			perror("SAAIOSENC");
		close(saafd);
	}
#endif
	
	/* open dvdnav handle */
	printf("Opening DVD...%s\n", playerconfig->dvd_path);
	if (dvdnav_open(&dvdnav, playerconfig->dvd_path) != DVDNAV_STATUS_OK) {
		printf("Error on dvdnav_open\n");
		sprintf(osdtext, "Error: Cant open DVD Source: %s", playerconfig->dvd_path);
		msg = DDVD_SHOWOSD_STRING;
		safe_write(message_pipe, &msg, sizeof(int));
		safe_write(message_pipe, &osdtext, sizeof(osdtext));
		res = DDVD_FAIL_OPEN;
		goto err_dvdnav_open;
	}

	/* set read ahead cache usage to no */
	if (dvdnav_set_readahead_flag(dvdnav, 0) != DVDNAV_STATUS_OK) {
		printf("Error on dvdnav_set_readahead_flag: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	/* set the language */
	if (dvdnav_menu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
	    dvdnav_audio_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
	    dvdnav_spu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK) {
		printf("Error on setting languages: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	/* set the PGC positioning flag to have position information relatively to the
	 * whole feature instead of just relatively to the current chapter */
	if (dvdnav_set_PGC_positioning_flag(dvdnav, 1) != DVDNAV_STATUS_OK) {
		printf("Error on dvdnav_set_PGC_positioning_flag: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	int audio_lock = 0;
	int spu_lock = 0;
	int audio_format[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	unsigned long long vpts, apts, spts, pts;

	audio_id = dvdnav_get_active_audio_stream(dvdnav);
	ddvd_playmode = PLAY;

	ddvd_lbb_changed = 0;

	unsigned long long spu_backpts[3];

	ddvd_play_empty(FALSE);
	ddvd_get_time();	//set timestamp

	playerconfig->in_menu = 0;

	const char *dvd_titlestring;
	if (dvdnav_get_title_string(dvdnav, &dvd_titlestring) == DVDNAV_STATUS_OK)
		strcpy(playerconfig->title_string, dvd_titlestring);

	msg = DDVD_SHOWOSD_TITLESTRING;
	safe_write(message_pipe, &msg, sizeof(int));

	/* the read loop which regularly calls dvdnav_get_next_block
	 * and handles the returned events */
	int reached_eof = 0;
	int reached_sof = 0;
	while (!finished) {
		pci_t *pci = 0;
		dsi_t *dsi = 0;
		int buttonN = -1;
		int in_menu = 0;

		/* the main reading function */
		if (ddvd_playmode == PLAY) {	//skip when not in play mode
			// trickmode
			if (ddvd_trickmode) {
				if (ddvd_trick_timer_end <= ddvd_get_time()) {
					if (ddvd_trickmode == FASTBW) {	//jump back ?
						uint32_t pos, len;
						dvdnav_get_position(dvdnav, &pos, &len);
						//90000 = 1 Sek. -> 45000 = 0.5 Sek. -> Speed Faktor=2
						int64_t posneu = ((pos * ddvd_lastCellEventInfo.pgc_length) / len) - (45000 * 2 * ddvd_trickspeed);
						int64_t posneu2 = posneu <= 0 ? 0 : (posneu * len) / ddvd_lastCellEventInfo.pgc_length;
						dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
						if (posneu < 0) {	// reached begin of movie
							reached_sof = 1;
							msg = DDVD_SHOWOSD_TIME;
						} else {
							msg = DDVD_SHOWOSD_STATE_FBWD;
						}
					} else if (ddvd_trickmode == FASTFW) {	//jump forward ?
						uint32_t pos, len;
						dvdnav_get_position(dvdnav, &pos, &len);
						//90000 = 1 Sek. -> 22500 = 0.25 Sek. -> Speed Faktor=2
						int64_t posneu = ((pos * ddvd_lastCellEventInfo.pgc_length) / len) + (22500 * 2 * ddvd_trickspeed);
						int64_t posneu2 = (posneu * len) / ddvd_lastCellEventInfo.pgc_length;
						if (posneu2 && len && posneu2 >= len) {	// reached end of movie
							posneu2 = len - 250;
							reached_eof = 1;
							msg = DDVD_SHOWOSD_TIME;
						} else {
							msg = DDVD_SHOWOSD_STATE_FFWD;
						}
						dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
					}
					ddvd_trick_timer_end = ddvd_get_time() + 300;
					ddvd_lpcm_count = 0;
				}
			}

			result = dvdnav_get_next_block(dvdnav, buf, &event, &len);
			if (result == DVDNAV_STATUS_ERR) {
				printf("Error getting next block: %s\n", dvdnav_err_to_string(dvdnav));
				sprintf(osdtext, "Error: Getting next block: %s", dvdnav_err_to_string(dvdnav));
				msg = DDVD_SHOWOSD_STRING;
				safe_write(message_pipe, &msg, sizeof(int));
				safe_write(message_pipe, &osdtext, sizeof(osdtext));
				res = DDVD_FAIL_READ;
				goto err_dvdnav;
			}

send_message:
			// send OSD Data
			if (msg > 0) {
				struct ddvd_time info;
				switch (msg) {
				case DDVD_SHOWOSD_TIME:
					info = ddvd_get_osd_time(playerconfig);
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &info, sizeof(struct ddvd_time));
					break;
				case DDVD_SHOWOSD_STATE_FFWD:
					info = ddvd_get_osd_time(playerconfig);
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &ddvd_trickspeed, sizeof(int));
					safe_write(message_pipe, &info, sizeof(struct ddvd_time));
					break;
				case DDVD_SHOWOSD_STATE_FBWD:
					info = ddvd_get_osd_time(playerconfig);
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &ddvd_trickspeed, sizeof(int));
					safe_write(message_pipe, &info, sizeof(struct ddvd_time));
					break;
				default:
					break;
				}
				msg = 0;
			}

			if (reached_eof) {
				msg = DDVD_EOF_REACHED;
				safe_write(message_pipe, &msg, sizeof(int));
				reached_eof = 0;
			}

			if (reached_sof) {
				msg = DDVD_SOF_REACHED;
				safe_write(message_pipe, &msg, sizeof(int));
				reached_sof = 0;
			}

			if (ddvd_get_time() > playerconfig->next_time_update) {
				msg = DDVD_SHOWOSD_TIME;
				goto send_message;
			}
			// send iFrame
			if (ddvd_iframesend < 0) {
#if CONFIG_API_VERSION == 1
				ddvd_device_clear();
#endif
				ddvd_iframesend = 0;
			}

			if (ddvd_iframesend > 0) {
#if CONFIG_API_VERSION == 1
				ddvd_device_clear();
#endif
				if (ddvd_still_frame && ddvd_last_iframe_len) {
#if 0
					static int ifnum = 0;
					static char ifname[255];
					snprintf(ifname, 255, "/tmp/dvd.iframe.%3.3d.asm.pes", ifnum++);
					FILE *f = fopen(ifname, "wb");
					fwrite(last_iframe, 1, ddvd_last_iframe_len, f);
					fclose(f);
#endif

#if CONFIG_API_VERSION == 1
					//that really sucks but there is no other way
					int i;
					for (i = 0; i < 10; i++)
						safe_write(ddvd_output_fd, last_iframe, ddvd_last_iframe_len);
#else
					safe_write(ddvd_output_fd, last_iframe, ddvd_last_iframe_len);
#endif
					//printf("Show iframe with size: %d\n",ddvd_last_iframe_len);
					ddvd_last_iframe_len = 0;
				}

				ddvd_iframesend = -1;
			}
			// wait timer
			if (ddvd_wait_timer_active) {
				if (ddvd_wait_timer_end <= ddvd_get_time()) {
					ddvd_wait_timer_active = 0;
					dvdnav_still_skip(dvdnav);
					//printf("wait timer done\n");
				}
			}
			// SPU timer
			if (ddvd_spu_timer_active) {
				if (ddvd_spu_timer_end <= ddvd_get_time()) {
					ddvd_spu_timer_active = 0;
					memset(ddvd_lbb, 0, 720 * 576);	//clear SPU backbuffer
					ddvd_lbb_changed = 1;
					//printf("spu timer done\n");
				}
			}

			switch (event) {
			case DVDNAV_BLOCK_OK:
				/* We have received a regular block of the currently playing MPEG stream.
				 * So we do some demuxing and decoding. */

				// collect audio data
				if (((buf[14 + 3]) & 0xF0) == 0xC0)
					audio_format[(buf[14 + 3]) - 0xC0] = DDVD_MPEG;
				if ((buf[14 + 3]) == 0xBD && ((buf[14 + buf[14 + 8] + 9]) & 0xF8) == 0x80)
					audio_format[(buf[14 + buf[14 + 8] + 9]) - 0x80] = DDVD_AC3;
				if ((buf[14 + 3]) == 0xBD && ((buf[14 + buf[14 + 8] + 9]) & 0xF8) == 0x88)
					audio_format[(buf[14 + buf[14 + 8] + 9]) - 0x88] = DDVD_DTS;
				if ((buf[14 + 3]) == 0xBD && ((buf[14 + buf[14 + 8] + 9]) & 0xF8) == 0xA0)
					audio_format[(buf[14 + buf[14 + 8] + 9]) - 0xA0] = DDVD_LPCM;

				if ((buf[14 + 3] & 0xF0) == 0xE0) {	// video
					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
						vpts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						vpts |= buf[14 + 10] << 22;
						vpts |= (buf[14 + 11] >> 1) << 15;
						vpts |= buf[14 + 12] << 7;
						vpts |= (buf[14 + 14] >> 1);
						//printf("VPTS? %X\n",(int)vpts);
					}
#if CONFIG_API_VERSION == 1
					// Eliminate 00 00 01 B4 sequence error packet because it breaks the pallas mpeg decoder
					// This is very strange because the 00 00 01 B4 is partly inside the header extension ...
					if (buf[21] == 0x00 && buf[22] == 0x00 && buf[23] == 0x01 && buf[24] == 0xB4) {
						buf[21] = 0x01;
					}
					if (buf[22] == 0x00 && buf[23] == 0x00 && buf[24] == 0x01 && buf[25] == 0xB4) {
						buf[22] = 0x01;
					}
#endif
					// if we have 16:9 Zoom Mode on the DVD and we use a "always 16:9" mode on tv we have
					// to patch the mpeg header and the Sequence Display Extension inside the Stream in some cases
					if (dvd_aspect == 3 && (tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_9) && (tv_mode == DDVD_PAN_SCAN || tv_mode == DDVD_LETTERBOX)) {
						int z=0;
						for (z=0; z<2040; z++)
						{
							if (buf[z] == 0x0 && buf[z+1] == 0x0 && buf[z+2] ==0x01 && buf[z+3] == 0xB5 && (buf[z+4] == 0x22 || buf[z+4] == 0x23))
							{
								buf[z+5]=0x22;
								buf[z+5]=0x0B;
								buf[z+6]=0x42;
								buf[z+7]=0x12;
								buf[z+8]=0x00;
							}
						}
						if (buf[33] == 0 && buf[33 + 1] == 0 && buf[33 + 2] == 1 && buf[33 + 3] == 0xB3) {
							buf[33 + 7] = (buf[33 + 7] & 0xF) + 0x30;
						}
						if (buf[36] == 0 && buf[36 + 1] == 0 && buf[36 + 2] == 1 && buf[36 + 3] == 0xB3) {
							buf[36 + 7] = (buf[36 + 7] & 0xF) + 0x30;
						}
					}
			
					// check yres for detecting ntsc/pal
					if (ddvd_have_ntsc == -1) {
						if ((buf[33] == 0 && buf[33 + 1] == 0 && buf[33 + 2] == 1 && buf[33 + 3] == 0xB3 && ((buf[33+5] & 0xF) << 8) + buf[33+6] == 0x1E0) 
							|| (buf[36] == 0 && buf[36 + 1] == 0 && buf[36 + 2] == 1 && buf[36 + 3] == 0xB3 && ((buf[36+5] & 0xF) << 8) + buf[36+6] == 0x1E0))
							ddvd_have_ntsc = 1;
						else
							ddvd_have_ntsc = 0;
					}

					safe_write(ddvd_output_fd, buf + 14, 2048 - 14);

					// 14+8 header_length
					// 14+(header_length)+3  -> start mpeg header
					// buf[14+buf[14+8]+3] start mpeg header

					int datalen = (buf[19] + (buf[18] << 8) + 6) - buf[14 + 8];	// length mpeg packet
					int data = buf[14 + buf[14 + 8] + 3];	// start mpeg packet(header)

					int do_copy = (ddvd_iframerun == 0x01) && !(buf[data] == 0 && buf[data + 1] == 0 && buf[data + 2] == 1) ? 1 : 0;
					int have_pictureheader = 0;
					int haveslice = 0;
					int setrun = 0;

					while (datalen > 6) {
						if (buf[data] == 0 && buf[data + 1] == 0 && buf[data + 2] == 1) {
							if (buf[data + 3] == 0x00)	//picture
							{
								if (!setrun) {
									ddvd_iframerun = ((buf[data + 5] >> 3) & 0x07);
									setrun = 1;
								}

								if (ddvd_iframerun < 0x01 || 0x03 < ddvd_iframerun) {
									data++;
									datalen--;
									continue;
								}
								have_pictureheader = 1;
								data += 5;
								datalen -= 5;
								datalen = 6;
							} else if (buf[data + 3] == 0xB3 && datalen >= 8)	//sequence header
							{
								ddvd_last_iframe_len = 0;	// clear iframe buffer
								data += 7;
								datalen -= 7;
							} else if (buf[data + 3] == 0xBE)	//padding stream
							{
								break;
							} else if (0x01 <= buf[data + 3] && buf[data + 3] <= 0xaf)	//slice ?
							{
								if (!have_pictureheader && ddvd_last_iframe_len == 0)
									haveslice = 1;
							}
						}
						data++;
						datalen--;
					}
					if ((ddvd_iframerun <= 0x01 || do_copy) && ddvd_still_frame) {
						if (haveslice)
							ddvd_iframerun = 0xFF;
						else if (ddvd_last_iframe_len < (320 * 1024) - (buf[19] + (buf[18] << 8) + 6)) {
							memcpy(last_iframe + ddvd_last_iframe_len, buf + 14, buf[19] + (buf[18] << 8) + 6);
							ddvd_last_iframe_len += buf[19] + (buf[18] << 8) + 6;
						}
					}
				} else if ((buf[14 + 3]) == 0xC0 + audio_id)	// mpeg audio
				{
					if (audio_type != DDVD_MPEG) {
						//printf("Switch to MPEG Audio\n");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
							perror("AUDIO_SET_AV_SYNC");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
							perror("AUDIO_SET_BYPASS_MODE");
						audio_type = DDVD_MPEG;
					}

					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
						apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						apts |= buf[14 + 10] << 22;
						apts |= (buf[14 + 11] >> 1) << 15;
						apts |= buf[14 + 12] << 7;
						apts |= (buf[14 + 14] >> 1);
						//printf("APTS? %X\n",(int)apts);
					}

					safe_write(ddvd_ac3_fd, buf + 14, buf[19] + (buf[18] << 8) + 6);
				} else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0xA0 + audio_id)	// lpcm audio
				{
					if (audio_type != DDVD_LPCM) {
						//printf("Switch to LPCM Audio\n");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
							perror("AUDIO_SET_AV_SYNC");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
							perror("AUDIO_SET_BYPASS_MODE");
						audio_type = DDVD_LPCM;
						ddvd_lpcm_count = 0;
					}
					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
						apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						apts |= buf[14 + 10] << 22;
						apts |= (buf[14 + 11] >> 1) << 15;
						apts |= buf[14 + 12] << 7;
						apts |= (buf[14 + 14] >> 1);
						//printf("APTS? %X\n",(int)apts);
					}
					int i = 0;
					char abuf[(((buf[18] << 8) | buf[19]) - buf[22] - 14)];
#if BYTE_ORDER == BIG_ENDIAN
					// just copy, byte order is correct on ppc machines
					memcpy(abuf, buf + 14 + buf[14 + 8] + 9 + 7, (((buf[18] << 8) | buf[19]) - buf[22] - 14));
					i = (((buf[18] << 8) | buf[19]) - buf[22] - 14);
#else
					// byte swapping .. we become the wrong byteorder on lpcm on the 7025
					while (i < (((buf[18] << 8) | buf[19]) - buf[22] - 14)) {
						abuf[i + 0] = (buf[14 + buf[14 + 8] + 9 + 7 + i + 1]);
						abuf[i + 1] = (buf[14 + buf[14 + 8] + 9 + 7 + i + 0]);
						i += 2;
					}
#endif
					// we will encode the raw lpcm data to mpeg audio and send them with pts
					// information to the decoder to get a sync. playing the pcm data via
					// oss will break the pic/sound sync. So believe it or not, this is the 
					// smartest way to get a synced lpcm track ;-)
					if (ddvd_lpcm_count == 0) {	// save mpeg header with pts
						memcpy(mpa_data, buf + 14, buf[14 + 8] + 9);
						mpa_header_length = buf[14 + 8] + 9;
					}
					if (ddvd_lpcm_count + i >= 4608) {	//we have to send 4608 bytes to the encoder
						memcpy(lpcm_data + ddvd_lpcm_count, abuf, 4608 - ddvd_lpcm_count);
						//encode
						mpa_count = ddvd_mpa_encode_frame(mpa_data + mpa_header_length, 4608, lpcm_data);
						//patch pes__packet_length
						mpa_count = mpa_count + mpa_header_length - 6;
						mpa_data[4] = mpa_count >> 8;
						mpa_data[5] = mpa_count & 0xFF;
						//patch header type to mpeg
						mpa_data[3] = 0xC0;
						//write
						safe_write(ddvd_ac3_fd, mpa_data, mpa_count + mpa_header_length);
						memcpy(lpcm_data, abuf + (4608 - ddvd_lpcm_count), i - (4608 - ddvd_lpcm_count));
						ddvd_lpcm_count = i - (4608 - ddvd_lpcm_count);
						memcpy(mpa_data, buf + 14, buf[14 + 8] + 9);
						mpa_header_length = buf[14 + 8] + 9;
					} else {
						memcpy(lpcm_data + ddvd_lpcm_count, abuf, i);
						ddvd_lpcm_count += i;
					}

					//safe_write(ddvd_ac3_fd, buf+14 , buf[19]+(buf[18]<<8)+6);
				} else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0x88 + audio_id) {	// dts audio
					if (audio_type != DDVD_DTS) {
						//printf("Switch to DTS Audio (thru)\n");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
							perror("AUDIO_SET_AV_SYNC");
						if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 5) < 0)
							perror("AUDIO_SET_BYPASS_MODE");
						audio_type = DDVD_DTS;
					}

					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
						apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						apts |= buf[14 + 10] << 22;
						apts |= (buf[14 + 11] >> 1) << 15;
						apts |= buf[14 + 12] << 7;
						apts |= (buf[14 + 14] >> 1);
						//printf("APTS? %X\n",(int)apts);
					}

					safe_write(ddvd_ac3_fd, buf + 14, buf[19] + (buf[18] << 8) + 6);	// not working yet ....
				} else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0x80 + audio_id) {	// ac3 audio
					if (audio_type != DDVD_AC3) {
						//printf("Switch to AC3 Audio\n");
						if (ac3thru || !have_liba52) {	// !have_liba52 and !ac3thru should never happen, but who knows ;)
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								perror("AUDIO_SET_AV_SYNC");
#ifdef CONVERT_TO_DVB_COMPLIANT_AC3
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 0) < 0)	// AC3 (dvb compliant)
#else
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 3) < 0)	// AC3 VOB
#endif
								perror("AUDIO_SET_BYPASS_MODE");
						} else {
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								perror("AUDIO_SET_AV_SYNC");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
								perror("AUDIO_SET_BYPASS_MODE");
						}
						audio_type = DDVD_AC3;
					}

					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
						apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						apts |= buf[14 + 10] << 22;
						apts |= (buf[14 + 11] >> 1) << 15;
						apts |= buf[14 + 12] << 7;
						apts |= (buf[14 + 14] >> 1);
						//printf("APTS? %X\n",(int)apts);
					}

					if (ac3thru || !have_liba52) {	// !have_liba52 and !ac3thru should never happen, but who knows ;)
#ifdef CONVERT_TO_DVB_COMPLIANT_AC3
						unsigned short pes_len = (buf[14 + 4] << 8 | buf[14 + 5]);
						pes_len -= 4;	// strip first 4 bytes of pes payload
						buf[14 + 4] = pes_len >> 8;	// patch pes len
						buf[15 + 4] = pes_len & 0xFF;

						safe_write(ddvd_ac3_fd, buf + 14, 9 + buf[14 + 8]);	// write pes_header
						safe_write(ddvd_ac3_fd, buf + 14 + 9 + buf[14 + 8] + 4, pes_len - (3 + buf[14 + 8]));	// write pes_payload
#else
						safe_write(ddvd_ac3_fd, buf + 14, buf[19] + (buf[18] << 8) + 6);
#endif
						//fwrite(buf+buf[22]+27, 1, ((buf[18]<<8)|buf[19])-buf[22]-7, fac3); //debugwrite
					} else {
						// a bit more funny than lpcm sound, because we do a complete recoding here
						// we will decode the ac3 data to plain lpcm and will then encode to mpeg
						// audio and send them with pts information to the decoder to get a sync.

						// decode and convert ac3 to raw lpcm
						ac3_len = ddvd_ac3_decode(buf + buf[22] + 27, ((buf[18] << 8) | buf[19]) - buf[22] - 7, ac3_tmp);

						// save the pes header incl. PTS
						memcpy(mpa_data, buf + 14, buf[14 + 8] + 9);
						mpa_header_length = buf[14 + 8] + 9;

						//apts-=(((unsigned long long)(ddvd_lpcm_count)*90)/192);

						//mpa_data[14]=(int)((apts<<1)&0xFF);
						//mpa_data[12]=(int)((apts>>7)&0xFF);
						//mpa_data[11]=(int)(((apts<<1)>>15)&0xFF);
						//mpa_data[10]=(int)((apts>>22)&0xFF);

						// copy lpcm data into buffer for encoding
						memcpy(lpcm_data + ddvd_lpcm_count, ac3_tmp, ac3_len);
						ddvd_lpcm_count += ac3_len;

						// encode the whole packet to mpa
						mpa_count2 = mpa_count = 0;
						while (ddvd_lpcm_count >= 4608) {
							mpa_count = ddvd_mpa_encode_frame(mpa_data + mpa_header_length + mpa_count2, 4608, lpcm_data);
							mpa_count2 += mpa_count;
							ddvd_lpcm_count -= 4608;
							memcpy(lpcm_data, lpcm_data + 4608, ddvd_lpcm_count);
						}

						// patch pes__packet_length
						mpa_count = mpa_count2 + mpa_header_length - 6;
						mpa_data[4] = mpa_count >> 8;
						mpa_data[5] = mpa_count & 0xFF;

						// patch header type to mpeg
						mpa_data[3] = 0xC0;

						// write to decoder
						safe_write(ddvd_ac3_fd, mpa_data, mpa_count2 + mpa_header_length);

					}
				} else if ((buf[14 + 3]) == 0xBD && ((buf[14 + buf[14 + 8] + 9]) & 0xE0) == 0x20 && ((buf[14 + buf[14 + 8] + 9]) & 0x1F) == spu_active_id) {	// SPU packet
					memcpy(spu_buffer + ddvd_spu_ptr, buf + buf[22] + 14 + 10, 2048 - (buf[22] + 14 + 10));
					ddvd_spu_ptr += 2048 - (buf[22] + 14 + 10);

					if (buf[14 + 7] & 128) {
						/* damn gcc bug */
#if CONFIG_API_VERSION == 3
						spts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
						spts |= buf[14 + 10] << 22;
						spts |= (buf[14 + 11] >> 1) << 15;
						spts |= buf[14 + 12] << 7;
						spts |= (buf[14 + 14] >> 1);
#else
						spts = (buf[14 + 9] >> 1) << 29;	// need a corrected "spts" because vulcan/pallas will give us a 32bit pts instead of 33bit
						spts |= buf[14 + 10] << 21;
						spts |= (buf[14 + 11] >> 1) << 14;
						spts |= buf[14 + 12] << 6;
						spts |= buf[14 + 12] >> 2;
#endif
						//printf("SPTS? %X\n",(int)spts);
					}

					if (ddvd_spu_ptr >= (spu_buffer[0] << 8 | spu_buffer[1]))	// SPU packet complete ?
					{
						if (ddvd_spu_backnr == 3)	// backbuffer already full ?
						{
							int tmplen = (spu_backbuffer[0] << 8 | spu_backbuffer[1]);
							memcpy(spu_backbuffer, spu_backbuffer + tmplen, ddvd_spu_backptr - tmplen);	// delete oldest SPU packet
							spu_backpts[0] = spu_backpts[1];
							spu_backpts[1] = spu_backpts[2];
							ddvd_spu_backnr = 2;
							ddvd_spu_backptr -= tmplen;
						}

						memcpy(spu_backbuffer + ddvd_spu_backptr, spu_buffer, (spu_buffer[0] << 8 | spu_buffer[1]));	// copy into backbuffer
						spu_backpts[ddvd_spu_backnr++] = spts;	// store pts
						ddvd_spu_backptr += (spu_buffer[0] << 8 | spu_buffer[1]);	// increase ptr

						ddvd_spu_ptr = 0;
					}
				}
				break;

			case DVDNAV_NOP:
				/* Nothing to do here. */
				break;

			case DVDNAV_STILL_FRAME:
				/* We have reached a still frame. So we start a timer to wait
				 * the amount of time specified by the still's length while still handling
				 * user input to make menus and other interactive stills work.
				 * A length of 0xff means an indefinite still which has to be skipped
				 * indirectly by some user interaction. */
				{
					if (ddvd_iframesend == 0 && ddvd_last_iframe_len)
						ddvd_iframesend = 1;

					dvdnav_still_event_t *still_event = (dvdnav_still_event_t *) buf;
					if (still_event->length < 0xff) {
						if (!ddvd_wait_timer_active) {
							ddvd_wait_timer_active = 1;
							ddvd_wait_timer_end = ddvd_get_time() + (still_event->length * 1000);	//ms
						}
					} else
						ddvd_wait_for_user = 1;
				}
				break;

			case DVDNAV_WAIT:
				/* We have reached a point in DVD playback, where timing is critical.
				 * We dont use readahead, so we can simply skip the wait state. */
				dvdnav_wait_skip(dvdnav);
				break;

			case DVDNAV_SPU_CLUT_CHANGE:
				/* We received a new color lookup table so we read and store
				 * it */
				{
					int i = 0, i2 = 0;
					uint8_t pal[16 * 4];
#if BYTE_ORDER == BIG_ENDIAN
					memcpy(pal, buf, 16 * sizeof(uint32_t));
#else
					for (; i < 16; ++i)
						*(int *)(pal + i * 4) = htonl(*(int *)(buf + i * 4));
					i = 0;
#endif
					while (i < 16 * 4) {
						int y = buf[i + 1];
						signed char cr = buf[i + 2];	//v
						signed char cb = buf[i + 3];	//u
						//printf("%d %d %d ->", y, cr, cb);
						y = pal[i + 1];
						cr = pal[i + 2];	//v
						cb = pal[i + 3];	//u
						//printf(" %d %d %d\n", y, cr, cb);
						i += 4;
					}
					i = 0;

					while (i < 16 * 4) {
						int y = pal[i + 1];
						signed char cr = pal[i + 2];	//v
						signed char cb = pal[i + 3];	//u

						y = 76310 * (y - 16);	//yuv2rgb
						cr -= 128;
						cb -= 128;
						int r = CLAMP((y + 104635 * cr) >> 16);
						int g = CLAMP((y - 53294 * cr - 25690 * cb) >> 16);
						int b = CLAMP((y + 132278 * cb) >> 16);

						ddvd_bl[i2] = b << 8;
						ddvd_gn[i2] = g << 8;
						ddvd_rd[i2] = r << 8;
						i += 4;
						i2++;
					}
					//CHANGE COLORMAP
				}
				break;

			case DVDNAV_SPU_STREAM_CHANGE:
				/* We received a new SPU stream ID */
				if (spu_lock)
					break;

				dvdnav_spu_stream_change_event_t *ev = (dvdnav_spu_stream_change_event_t *) buf;
				switch (tv_scale) {
				case 0:	//off
					spu_active_id = ev->physical_wide;
					break;
				case 1:	//letterbox
					spu_active_id = ev->physical_letterbox;
					break;
				case 2:	//panscan
					spu_active_id = ev->physical_pan_scan;
					break;
				default:	// should not happen
					spu_active_id = ev->physical_wide;
					break;
				}	
				uint16_t spu_lang = 0xFFFF;
				int spu_id_logical;
				spu_id_logical = dvdnav_get_spu_logical_stream(dvdnav, spu_active_id);
				spu_lang = dvdnav_spu_stream_to_lang(dvdnav, (spu_id_logical >= 0 ? spu_id_logical : spu_active_id) & 0x1F);
				if (spu_lang == 0xFFFF) {
					spu_lang = 0x2D2D;	// SPU "off, unknown or maybe menuoverlays" 
				}							
				msg = DDVD_SHOWOSD_SUBTITLE;
				safe_write(message_pipe, &msg, sizeof(int));
				safe_write(message_pipe, &spu_active_id, sizeof(int));
				safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
				//printf("SPU Stream change: w %X l: %X p: %X active: %X\n",ev->physical_wide,ev->physical_letterbox,ev->physical_pan_scan,spu_active_id);
				break;

			case DVDNAV_AUDIO_STREAM_CHANGE:
				/* We received a new Audio stream ID  */
				if (!audio_lock)
				{
					audio_id = dvdnav_get_active_audio_stream(dvdnav);
					report_audio_info = 1;
				}
				break;

			case DVDNAV_HIGHLIGHT:
				/* Lets display some Buttons */
				if (ddvd_clear_buttons == 0) {
					dvdnav_highlight_event_t *highlight_event = (dvdnav_highlight_event_t *) buf;

					pci = dvdnav_get_current_nav_pci(dvdnav);
					dsi = dvdnav_get_current_nav_dsi(dvdnav);
					dvdnav_highlight_area_t hl;

					int libdvdnav_workaround = 0;

					if (pci->hli.hl_gi.btngr_ns) {
						int btns_per_group = 36 / pci->hli.hl_gi.btngr_ns;
						btni_t *btni = NULL;
						int modeMask = 1 << tv_scale;

						if (!btni && pci->hli.hl_gi.btngr_ns >= 1 && (pci->hli.hl_gi.btngr1_dsp_ty & modeMask)) {
							btni = &pci->hli.btnit[0 * btns_per_group + highlight_event->buttonN - 1];
						}
						if (!btni && pci->hli.hl_gi.btngr_ns >= 2 && (pci->hli.hl_gi.btngr2_dsp_ty & modeMask)) {
							btni = &pci->hli.btnit[1 * btns_per_group + highlight_event->buttonN - 1];
						}
						if (!btni && pci->hli.hl_gi.btngr_ns >= 3 && (pci->hli.hl_gi.btngr3_dsp_ty & modeMask)) {
							btni = &pci->hli.btnit[2 * btns_per_group + highlight_event->buttonN - 1];
						}

						if (btni && btni->btn_coln != 0) {
							// get and set clut for actual button
							unsigned char tmp, tmp2;
							struct ddvd_color colneu;
							int i;
							msg = DDVD_COLORTABLE_UPDATE;
							if (ddvd_screeninfo_bypp == 1)
								safe_write(message_pipe, &msg, sizeof(int));
							for (i = 0; i < 4; i++) {
								tmp = ((pci->hli.btn_colit.btn_coli[btni->btn_coln - 1][0]) >> (16 + 4 * i)) & 0xf;
								tmp2 = ((pci->hli.btn_colit.btn_coli[btni->btn_coln - 1][0]) >> (4 * i)) & 0xf;
								colneu.blue = ddvd_bl[i + 252] = ddvd_bl[tmp];
								colneu.green = ddvd_gn[i + 252] = ddvd_gn[tmp];
								colneu.red = ddvd_rd[i + 252] = ddvd_rd[tmp];
								colneu.trans = ddvd_tr[i + 252] = (0xF - tmp2) * 0x1111;
								if (ddvd_screeninfo_bypp == 1)
									safe_write(message_pipe, &colneu, sizeof(struct ddvd_color));
							}
							msg = DDVD_NULL;
							//CHANGE COLORMAP

							memset(ddvd_lbb2, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen ..
							//copy button into screen
							for (i = btni->y_start; i < btni->y_end; i++) {
								if (ddvd_screeninfo_bypp == 1)
									memcpy(ddvd_lbb2 + btni->x_start + 720 * (i),
									       ddvd_lbb + btni->x_start + 720 * (i),
									       btni->x_end - btni->x_start);
								else
									ddvd_blit_to_argb(ddvd_lbb2 + btni->x_start * ddvd_screeninfo_bypp +
											  720 * ddvd_screeninfo_bypp * i,
											  ddvd_lbb + btni->x_start + 720 * (i),
											  btni->x_end - btni->x_start);
							}

							libdvdnav_workaround = 1;
						}
					}
					if (!libdvdnav_workaround && dvdnav_get_highlight_area(pci, highlight_event->buttonN, 0, &hl) == DVDNAV_STATUS_OK) {
						// get and set clut for actual button
						unsigned char tmp, tmp2;
						struct ddvd_color colneu;
						int i;
						msg = DDVD_COLORTABLE_UPDATE;
						if (ddvd_screeninfo_bypp == 1)
							safe_write(message_pipe, &msg, sizeof(int));
						for (i = 0; i < 4; i++) {
							tmp = ((hl.palette) >> (16 + 4 * i)) & 0xf;
							tmp2 = ((hl.palette) >> (4 * i)) & 0xf;
							colneu.blue = ddvd_bl[i + 252] = ddvd_bl[tmp];
							colneu.green = ddvd_gn[i + 252] = ddvd_gn[tmp];
							colneu.red = ddvd_rd[i + 252] = ddvd_rd[tmp];
							colneu.trans = ddvd_tr[i + 252] = (0xF - tmp2) * 0x1111;
							if (ddvd_screeninfo_bypp == 1)
								safe_write(message_pipe, &colneu, sizeof(struct ddvd_color));
						}
						msg = DDVD_NULL;
						//CHANGE COLORMAP

						memset(ddvd_lbb2, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen ..
						//copy button into screen
						for (i = hl.sy; i < hl.ey; i++) {
							if (ddvd_screeninfo_bypp == 1)
								memcpy(ddvd_lbb2 + hl.sx + 720 * (i),
								       ddvd_lbb + hl.sx + 720 * (i), hl.ex - hl.sx);
							else
								ddvd_blit_to_argb(ddvd_lbb2 + hl.sx * ddvd_screeninfo_bypp + 720 * ddvd_screeninfo_bypp * i,
										  ddvd_lbb + hl.sx + 720 * (i), hl.ex - hl.sx);
						}
						libdvdnav_workaround = 1;
					}
					if (!libdvdnav_workaround)
						memset(ddvd_lbb2, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen .. 

					int y_source = ddvd_have_ntsc ? 480 : 576; // correct ntsc overlay
					int x_offset = (dvd_aspect == 0 && (tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_10) && tv_mode == DDVD_PAN_SCAN) ? (int)(ddvd_screeninfo_xres - ddvd_screeninfo_xres/1.33)>>1 : 0; // correct 16:9 panscan (pillarbox) overlay
					int y_offset = (dvd_aspect == 0 && (tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_10) && tv_mode == DDVD_LETTERBOX) ? (int)(ddvd_screeninfo_yres*1.16 - ddvd_screeninfo_yres)>>1 : 0; // correct 16:9 letterbox overlay
					uint64_t start=ddvd_get_time();
					if (x_offset != 0 || y_offset != 0 || y_source != ddvd_screeninfo_yres || ddvd_screeninfo_xres != 720)
						ddvd_resize_pixmap(ddvd_lbb2, 720, y_source, ddvd_screeninfo_xres, ddvd_screeninfo_yres, x_offset, y_offset, ddvd_screeninfo_bypp); // resize
					memcpy(p_lfb, ddvd_lbb2, ddvd_screeninfo_xres * ddvd_screeninfo_yres * ddvd_screeninfo_bypp); //copy backbuffer into screen
					printf("needed time for resizing: %d ms\n",(int)(ddvd_get_time()-start));
					msg = DDVD_SCREEN_UPDATE;
					safe_write(message_pipe, &msg, sizeof(int));
				} else {
					ddvd_clear_buttons = 0;
					//printf("clear buttons\n");
				}
				break;

			case DVDNAV_VTS_CHANGE:
				{
					/* Some status information like video aspect and video scale permissions do
					 * not change inside a VTS. Therefore we will set it new at this place */
					ddvd_play_empty(FALSE);
					audio_lock = 0;	// reset audio & spu lock
					spu_lock = 0;
					audio_format[0] = audio_format[1] = audio_format[2] = audio_format[4] = audio_format[4] = audio_format[5] = audio_format[6] = audio_format[7] = -1;

					dvd_aspect = dvdnav_get_video_aspect(dvdnav);
					dvd_scale_perm = dvdnav_get_video_scale_permission(dvdnav);
					tv_scale = ddvd_check_aspect(dvd_aspect, dvd_scale_perm, tv_aspect, tv_mode);
					//printf("DVD Aspect: %d TV Aspect: %d Scale: %d Allowed: %d\n",dvd_aspect,tv_aspect,tv_scale,dvd_scale_perm);
					
					// resuming a dvd ?
					if (playerconfig->should_resume && first_vts_change) {
						first_vts_change = 0;
						int title_numbers,part_numbers;
						dvdnav_get_number_of_titles(dvdnav, &title_numbers);
						dvdnav_get_number_of_parts(dvdnav, playerconfig->resume_title, &part_numbers);
						if (playerconfig->resume_title <= title_numbers && playerconfig->resume_title > 0 && playerconfig->resume_chapter <= part_numbers && playerconfig->resume_chapter > 0) {
							dvdnav_part_play(dvdnav, playerconfig->resume_title, playerconfig->resume_chapter);
							next_cell_change = 1;
						} else {
							playerconfig->should_resume = 0;
							playerconfig->resume_title = 0;
							playerconfig->resume_chapter = 0;
							playerconfig->resume_block = 0;
							playerconfig->resume_audio_id = 0;
							playerconfig->resume_audio_lock = 0;
							playerconfig->resume_spu_id = 0;
							playerconfig->resume_spu_lock = 0;							
							perror("DVD resuming failed");
						}
						
						
					}
				}
				break;

			case DVDNAV_CELL_CHANGE:
				{
					/* Store new cell information */
					memcpy(&ddvd_lastCellEventInfo, buf, sizeof(dvdnav_cell_change_event_t));

					if ((ddvd_still_frame & CELL_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
						ddvd_iframesend = 1;

					ddvd_still_frame = (dvdnav_get_next_still_flag(dvdnav) != 0) ? CELL_STILL : 0;
					
					// resuming a dvd ?
					if (playerconfig->should_resume && next_cell_change) {
						next_cell_change = 0;
						playerconfig->should_resume = 0;
						if (dvdnav_sector_search(dvdnav, playerconfig->resume_block, SEEK_SET) != DVDNAV_STATUS_OK)
						{
							perror("DVD resuming failed");
						} else {
							audio_id = playerconfig->resume_audio_id;
							audio_lock = 1;//playerconfig->resume_audio_lock;
							spu_active_id = playerconfig->resume_spu_id & 0x1F;
							spu_lock = 1;//playerconfig->resume_spu_lock;
							report_audio_info = 1;
							uint16_t spu_lang = 0xFFFF;
							int spu_id_logical;
							spu_id_logical = dvdnav_get_spu_logical_stream(dvdnav, spu_active_id);
							spu_lang = dvdnav_spu_stream_to_lang(dvdnav, (spu_id_logical >= 0 ? spu_id_logical : spu_active_id) & 0x1F);
							if (spu_lang == 0xFFFF) {
								spu_lang = 0x2D2D;	// SPU "off" 
								spu_active_id = -1;
							}
							msg = DDVD_SHOWOSD_SUBTITLE;
							safe_write(message_pipe, &msg, sizeof(int));
							safe_write(message_pipe, &spu_active_id, sizeof(int));
							safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
							msg = DDVD_SHOWOSD_TIME; // send new position to the frontend
						}
						playerconfig->resume_title = 0;
						playerconfig->resume_chapter = 0;
						playerconfig->resume_block = 0;
						playerconfig->resume_audio_id = 0;
						playerconfig->resume_audio_lock = 0;
						playerconfig->resume_spu_id = 0;
						playerconfig->resume_spu_lock = 0;
					}
				}
				break;

			case DVDNAV_NAV_PACKET:
				/* A NAV packet provides PTS discontinuity information, angle linking information and
				 * button definitions for DVD menus. We have to handle some stilframes here */
				pci = dvdnav_get_current_nav_pci(dvdnav);
				dsi = dvdnav_get_current_nav_dsi(dvdnav);

				if ((ddvd_still_frame & NAV_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
					ddvd_iframesend = 1;

				if (dsi->vobu_sri.next_video == 0xbfffffff)
					ddvd_still_frame |= NAV_STILL;	//|= 1;
				else
					ddvd_still_frame &= ~NAV_STILL;	//&= 1;
				break;

			case DVDNAV_HOP_CHANNEL:
				/* This event is issued whenever a non-seamless operation has been executed.
				 * So we drop our buffers */
				ddvd_play_empty(TRUE);
				break;

			case DVDNAV_STOP:
				/* Playback should end here. */
				printf("DVDNAV_STOP\n");
				playerconfig->resume_title = 0;
				playerconfig->resume_chapter = 0;
				playerconfig->resume_block = 0;
				playerconfig->resume_audio_id = 0;
				playerconfig->resume_audio_lock = 0;
				playerconfig->resume_spu_id = 0;
				playerconfig->resume_spu_lock = 0;
				finished = 1;
				break;

			default:
				printf("Unknown event (%i)\n", event);
				finished = 1;
				break;
			}
		}
		// spu handling
#if CONFIG_API_VERSION == 1
		unsigned int tpts;
		if (ioctl(ddvd_output_fd, VIDEO_GET_PTS, &tpts) < 0)
			perror("VIDEO_GET_PTS");
		pts = (unsigned long long)tpts;
		signed long long diff = spts - pts;
		if (ddvd_spu_backnr > 0 && diff <= 0xFF)	// we only have a 32bit pts on vulcan/pallas (instead of 33bit) so we need some tolerance on syncing SPU for menus
													// so on non animated menus the buttons will be displayed to soon, but we we have to accept it
#else
		if (ioctl(ddvd_fdvideo, VIDEO_GET_PTS, &pts) < 0)
			perror("VIDEO_GET_PTS");
		if (ddvd_spu_backnr > 0 && pts >= spu_backpts[0])
#endif
		{
			int tmplen = (spu_backbuffer[0] << 8 | spu_backbuffer[1]);

			memset(ddvd_lbb, 0, 720 * 576);	//clear backbuffer .. 
			ddvd_display_time = ddvd_spu_decode_data(spu_backbuffer, tmplen);	// decode
			ddvd_lbb_changed = 1;

			struct ddvd_color colneu;
			int ctmp;
			msg = DDVD_COLORTABLE_UPDATE;
			if (ddvd_screeninfo_bypp == 1)
				safe_write(message_pipe, &msg, sizeof(int));
			for (ctmp = 0; ctmp < 4; ctmp++) {
				colneu.blue = ddvd_bl[ctmp + 252];
				colneu.green = ddvd_gn[ctmp + 252];
				colneu.red = ddvd_rd[ctmp + 252];
				colneu.trans = ddvd_tr[ctmp + 252];
				if (ddvd_screeninfo_bypp == 1)
					safe_write(message_pipe, &colneu, sizeof(struct ddvd_color));
			}
			msg = DDVD_NULL;

			memcpy(spu_backbuffer, spu_backbuffer + tmplen, ddvd_spu_backptr - tmplen);	// delete SPU packet
			spu_backpts[0] = spu_backpts[1];
			spu_backpts[1] = spu_backpts[2];
			ddvd_spu_backnr--;
			ddvd_spu_backptr -= tmplen;

			// set timer
			if (ddvd_display_time > 0) {
				ddvd_spu_timer_active = 1;
				ddvd_spu_timer_end = ddvd_get_time() + (ddvd_display_time * 10);	//ms
			} else
				ddvd_spu_timer_active = 0;

			pci = dvdnav_get_current_nav_pci(dvdnav);	//update highlight buttons
			dsi = dvdnav_get_current_nav_dsi(dvdnav);
			if (pci->hli.hl_gi.btn_ns > 0) {
				dvdnav_get_current_highlight(dvdnav, &buttonN);
				dvdnav_button_select(dvdnav, pci, buttonN);
				ddvd_lbb_changed = 0;
				in_menu = 1;
			}
		}

		if (!in_menu) {
			if (!pci)
				pci = dvdnav_get_current_nav_pci(dvdnav);

			in_menu = pci && pci->hli.hl_gi.btn_ns > 0;
		}

		if (in_menu && !playerconfig->in_menu) {
			int bla = DDVD_MENU_OPENED;
			safe_write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu = 1;
		} else if (!in_menu && playerconfig->in_menu) {
			int bla = DDVD_MENU_CLOSED;
			safe_write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu = 0;
		}

		if (ddvd_wait_for_user) {
			struct pollfd pfd[1];	// make new pollfd array
			pfd[0].fd = key_pipe;
			pfd[0].events = POLLIN | POLLPRI | POLLERR;
			poll(pfd, 1, -1);
		}
		//Userinput
		if (ddvd_readpipe(key_pipe, &rccode, sizeof(int), 0) == sizeof(int)) {
			int keydone = 1;

			if (!dsi)
				dsi = dvdnav_get_current_nav_dsi(dvdnav);

			if (buttonN == -1)
				dvdnav_get_current_highlight(dvdnav, &buttonN);

			switch (rccode)	// actions inside and outside of menu
			{
				case DDVD_SET_MUTE:
					ismute = 1;
					break;
				case DDVD_UNSET_MUTE:
					ismute = 0;
					break;
				default:
					keydone = 0;
					break;
			}

			if (!keydone && in_menu) {
				switch (rccode)	//Actions inside a Menu
				{
				case DDVD_KEY_UP:	//Up
					dvdnav_upper_button_select(dvdnav, pci);
					break;
				case DDVD_KEY_DOWN:	//Down
					dvdnav_lower_button_select(dvdnav, pci);
					break;
				case DDVD_KEY_LEFT:	//left
					dvdnav_left_button_select(dvdnav, pci);
					break;
				case DDVD_KEY_RIGHT:	//right
					dvdnav_right_button_select(dvdnav, pci);
					break;
				case DDVD_KEY_OK:	//OK
					ddvd_wait_for_user = 0;
					memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen ..
					memset(ddvd_lbb, 0, 720 * 576);	//clear backbuffer
					msg = DDVD_SCREEN_UPDATE;
					safe_write(message_pipe, &msg, sizeof(int));
					ddvd_clear_buttons = 1;
					dvdnav_button_activate(dvdnav, pci);
					ddvd_play_empty(TRUE);
					if (ddvd_wait_timer_active)
						ddvd_wait_timer_active = 0;
					break;
				case DDVD_KEY_MENU:	//Dream
					if (dvdnav_menu_call(dvdnav, DVD_MENU_Root) == DVDNAV_STATUS_OK)
						ddvd_play_empty(TRUE);
					break;
				case DDVD_KEY_AUDIOMENU:	//Audio
					if (dvdnav_menu_call(dvdnav, DVD_MENU_Audio) == DVDNAV_STATUS_OK)
						ddvd_play_empty(TRUE);
					break;
				case DDVD_KEY_EXIT:	//Exit
					{
						printf("DDVD_KEY_EXIT (menu)\n");
						playerconfig->resume_title = 0;
						playerconfig->resume_chapter = 0;
						playerconfig->resume_block = 0;
						playerconfig->resume_audio_id = 0;
						playerconfig->resume_audio_lock = 0;
						playerconfig->resume_spu_id = 0;
						playerconfig->resume_spu_lock = 0;
						finished = 1;
					}
					break;						
				case DDVD_SKIP_FWD:
				case DDVD_SKIP_BWD:
				case DDVD_SET_TITLE:
				case DDVD_SET_CHAPTER:
					// we must empty the pipe here...
					ddvd_readpipe(key_pipe, &keydone, sizeof(int), 1);
				default:
					break;
				}
			} else if (!keydone)	//Actions inside a Movie
			{
				switch (rccode)	//Main Actions
				{
				case DDVD_KEY_PREV_CHAPTER:	//left
					{
						int titleNo, chapterNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &chapterNumber);
						chapterNo--;
						if (chapterNo > chapterNumber)
							chapterNo = 1;
						if (chapterNo <= 0)
							chapterNo = chapterNumber;
						dvdnav_part_play(dvdnav, titleNo, chapterNo);
						ddvd_play_empty(TRUE);
						msg = DDVD_SHOWOSD_TIME;
						break;
					}
				case DDVD_KEY_NEXT_CHAPTER:	//right
					{
						int titleNo, chapterNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &chapterNumber);
						chapterNo++;
						if (chapterNo > chapterNumber)
							chapterNo = 1;
						if (chapterNo <= 0)
							chapterNo = chapterNumber;
						dvdnav_part_play(dvdnav, titleNo, chapterNo);
						ddvd_play_empty(TRUE);
						msg = DDVD_SHOWOSD_TIME;
						break;
					}
				case DDVD_KEY_PREV_TITLE:
					{
						int titleNo, titleNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_titles(dvdnav, &titleNumber);
						titleNo--;
						if (titleNo > titleNumber)
							titleNo = 1;
						if (titleNo <= 0)
							titleNo = titleNumber;
						dvdnav_part_play(dvdnav, titleNo, 1);
						ddvd_play_empty(TRUE);
						msg = DDVD_SHOWOSD_TIME;
						break;
					}
				case DDVD_KEY_NEXT_TITLE:
					{
						int titleNo, titleNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_titles(dvdnav, &titleNumber);
						titleNo++;
						if (titleNo > titleNumber)
							titleNo = 1;
						if (titleNo <= 0)
							titleNo = titleNumber;
						dvdnav_part_play(dvdnav, titleNo, 1);
						ddvd_play_empty(TRUE);
						msg = DDVD_SHOWOSD_TIME;
						break;
					}
				case DDVD_KEY_PAUSE:	// Pause
					{
						if (ddvd_playmode == PLAY) {
							ddvd_playmode = PAUSE;
							if (ioctl(ddvd_fdaudio, AUDIO_PAUSE) < 0)
								perror("AUDIO_PAUSE");
							if (ioctl(ddvd_fdvideo, VIDEO_FREEZE) < 0)
								perror("VIDEO_FREEZE");
							msg = DDVD_SHOWOSD_STATE_PAUSE;
							safe_write(message_pipe, &msg, sizeof(int));
							break;
						} else if (ddvd_playmode != PAUSE)
							break;
						// fall through to PLAY
					}
				case DDVD_KEY_PLAY:	// Play
					{
						if (ddvd_playmode == PAUSE || ddvd_trickmode) {
							ddvd_playmode = PLAY;
key_play:
#if CONFIG_API_VERSION == 1
							ddvd_device_clear();
#endif
							if (ddvd_trickmode && !ismute)
								if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 0) < 0)
									perror("AUDIO_SET_MUTE");
							ddvd_trickmode = TOFF;
							if (ddvd_playmode == PLAY) {
								if (ioctl(ddvd_fdaudio, AUDIO_CONTINUE) < 0)
									perror("AUDIO_CONTINUE");
								if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
									perror("VIDEO_CONTINUE");
								msg = DDVD_SHOWOSD_STATE_PLAY;
								safe_write(message_pipe, &msg, sizeof(int));
							}
							msg = DDVD_SHOWOSD_TIME;
						}
						break;
					}
				case DDVD_KEY_MENU:	//Dream
					if (dvdnav_menu_call(dvdnav, DVD_MENU_Root) == DVDNAV_STATUS_OK)
						ddvd_play_empty(TRUE);
					break;
				case DDVD_KEY_AUDIOMENU:	//Audio
					if (dvdnav_menu_call(dvdnav, DVD_MENU_Audio) == DVDNAV_STATUS_OK)
						ddvd_play_empty(TRUE);
					break;
				case DDVD_KEY_EXIT:	//Exit
					{
						printf("DDVD_KEY_EXIT (menu)\n");
						int resume_title, resume_chapter; //safe resume info
						uint32_t resume_block, total_block;
						if (dvdnav_current_title_info(dvdnav, &resume_title, &resume_chapter) && (0 != resume_title)) {
							if(dvdnav_get_position (dvdnav, &resume_block, &total_block) == DVDNAV_STATUS_OK) {
								playerconfig->resume_title = resume_title;
								playerconfig->resume_chapter = resume_chapter;
								playerconfig->resume_block = resume_block;
								playerconfig->resume_audio_id = audio_id;
								playerconfig->resume_audio_lock = audio_lock;
								playerconfig->resume_spu_id = spu_active_id;
								playerconfig->resume_spu_lock = spu_lock;
							} else perror("error getting resume position");
						} perror("error getting resume position");					
						finished = 1;
					}
					break;						
				case DDVD_KEY_FFWD:	//FastForward
				case DDVD_KEY_FBWD:	//FastBackward
					{
						if (ddvd_trickmode == TOFF) {
							if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 1) < 0)
								perror("AUDIO_SET_MUTE");
							ddvd_trickspeed = 2;
							ddvd_trickmode = (rccode == DDVD_KEY_FBWD ? FASTBW : FASTFW);
						} else if (ddvd_trickmode == (rccode == DDVD_KEY_FBWD ? FASTFW : FASTBW)) {
							ddvd_trickspeed /= 2;
							if (ddvd_trickspeed == 1) {
								ddvd_trickspeed = 0;
								goto key_play;
							}
						} else if (ddvd_trickspeed < 64)
							ddvd_trickspeed *= 2;
						break;
					}
				case DDVD_KEY_AUDIO:	//change audio track 
					{
						int count = 0;
						audio_id = (audio_id == 7 ? 0 : audio_id+1);
						while (audio_format[audio_id] == -1 && count++ < 7)
						{
							audio_id = (audio_id == 7 ? 0 : audio_id+1);
						}
						report_audio_info = 1;
						ddvd_play_empty(TRUE);
						audio_lock = 1;
						ddvd_lpcm_count = 0;
						break;
					}
				case DDVD_KEY_SUBTITLE:	//change spu track 
					{
						uint16_t spu_lang = 0xFFFF;
						int spu_id_logical;
						spu_active_id++;
						spu_id_logical = dvdnav_get_spu_logical_stream(dvdnav, spu_active_id);
						spu_lang = dvdnav_spu_stream_to_lang(dvdnav, (spu_id_logical >= 0 ? spu_id_logical : spu_active_id) & 0x1F);
						if (spu_lang == 0xFFFF) {
							spu_lang = 0x2D2D;	// SPU "off" 
							spu_active_id = -1;
						}
						spu_lock = 1;
						msg = DDVD_SHOWOSD_SUBTITLE;
						safe_write(message_pipe, &msg, sizeof(int));
						safe_write(message_pipe, &spu_active_id, sizeof(int));
						safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
						break;
					}
				case DDVD_GET_TIME:	// frontend wants actual time
					msg = DDVD_SHOWOSD_TIME;
					break;
				case DDVD_SKIP_FWD:
				case DDVD_SKIP_BWD:
					{
						int skip;
						ddvd_readpipe(key_pipe, &skip, sizeof(int), 1);
						if (ddvd_trickmode == TOFF) {
							uint32_t pos, len;
							dvdnav_get_position(dvdnav, &pos, &len);
							printf("DDVD_SKIP pos=%u len=%u \n", pos, len);
							//90000 = 1 Sek.
							if (!len)
								len = 1;
							long long int posneu = ((pos * ddvd_lastCellEventInfo.pgc_length) / len) + (90000 * skip);
							printf("DDVD_SKIP posneu1=%lld\n", posneu);
							long long int posneu2 = posneu <= 0 ? 0 : (posneu * len) / ddvd_lastCellEventInfo.pgc_length;
							printf("DDVD_SKIP posneu2=%lld\n", posneu2);
							if (len && posneu2 && posneu2 >= len)	// reached end of movie
							{
								posneu2 = len - 250;
								reached_eof = 1;
							}
							dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
							ddvd_lpcm_count = 0;
							msg = DDVD_SHOWOSD_TIME;
						}
						break;
					}
				case DDVD_SET_TITLE:
					{
						int title, totalTitles;
						ddvd_readpipe(key_pipe, &title, sizeof(int), 1);
						dvdnav_get_number_of_titles(dvdnav, &totalTitles);
						printf("DDVD_SET_TITLE %d/%d\n", title, totalTitles);
						if (title <= totalTitles) {
							dvdnav_part_play(dvdnav, title, 0);
							ddvd_play_empty(TRUE);
							msg = DDVD_SHOWOSD_TIME;
						}
						break;
					}
				case DDVD_SET_CHAPTER:
					{
						int chapter, totalChapters, chapterNo, titleNo;
						ddvd_readpipe(key_pipe, &chapter, sizeof(int), 1);
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &totalChapters);
						printf("DDVD_SET_CHAPTER %d/%d in title %d\n", chapter, totalChapters, titleNo);
						if (chapter <= totalChapters) {
							dvdnav_part_play(dvdnav, titleNo, chapter);
							ddvd_play_empty(TRUE);
							msg = DDVD_SHOWOSD_TIME;
						}
						break;
					}
				default:
					break;
				}
			}
		}
		// spu handling
		if (ddvd_lbb_changed == 1) {
			
			if (ddvd_screeninfo_bypp == 1)
				memcpy(ddvd_lbb2, ddvd_lbb, 720 * 576);
			else {
				int i = 0;
				for (; i < 576; ++i)
					ddvd_blit_to_argb(ddvd_lbb2 + i * 720 * ddvd_screeninfo_bypp, ddvd_lbb + i * 720, 720);
			}
			int y_source = ddvd_have_ntsc ? 480 : 576; // correct ntsc overlay
			int x_offset = (dvd_aspect == 0 && (tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_10) && tv_mode == DDVD_PAN_SCAN) ? (int)(ddvd_screeninfo_xres - ddvd_screeninfo_xres/1.33)>>1 : 0; // correct 16:9 panscan (pillarbox) overlay
			int y_offset = (dvd_aspect == 0 && (tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_10) && tv_mode == DDVD_LETTERBOX) ? (int)(ddvd_screeninfo_yres*1.16 - ddvd_screeninfo_yres)>>1 : 0; // correct 16:9 letterbox overlay
			uint64_t start=ddvd_get_time();
			if (x_offset != 0 || y_offset != 0 || y_source != ddvd_screeninfo_yres || ddvd_screeninfo_xres != 720)
				ddvd_resize_pixmap_spu(ddvd_lbb2, 720, y_source, ddvd_screeninfo_xres, ddvd_screeninfo_yres, x_offset, y_offset, ddvd_screeninfo_bypp); // resize
			memcpy(p_lfb, ddvd_lbb2, ddvd_screeninfo_xres * ddvd_screeninfo_yres * ddvd_screeninfo_bypp); //copy backbuffer into screen
			printf("needed time for resizing: %d ms\n",(int)(ddvd_get_time()-start));
			int msg_old = msg;	// save and restore msg it may not bee empty
			msg = DDVD_SCREEN_UPDATE;
			safe_write(message_pipe, &msg, sizeof(int));
			msg = msg_old;
			ddvd_lbb_changed = 0;		
		}
		// report audio info
		if (report_audio_info) { 
			if (audio_format[audio_id] > -1) {
				uint16_t audio_lang = 0xFFFF;
				int audio_id_logical;
				audio_id_logical = dvdnav_get_audio_logical_stream(dvdnav, audio_id);
				audio_lang = dvdnav_audio_stream_to_lang(dvdnav, audio_id_logical);
				if (audio_lang == 0xFFFF)
					audio_lang = 0x2D2D;	
				int msg_old = msg;	// save and restore msg it may not bee empty
				msg = DDVD_SHOWOSD_AUDIO;
				safe_write(message_pipe, &msg, sizeof(int));
				safe_write(message_pipe, &audio_id, sizeof(int));
				safe_write(message_pipe, &audio_lang, sizeof(uint16_t));
				safe_write(message_pipe, &audio_format[audio_id], sizeof(int));
				msg = msg_old;
				report_audio_info = 0;
			}
		}

	}

err_dvdnav:
	/* destroy dvdnav handle */
	if (dvdnav_close(dvdnav) != DVDNAV_STATUS_OK)
		printf("Error on dvdnav_close: %s\n", dvdnav_err_to_string(dvdnav));

err_dvdnav_open:
	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_DEMUX) < 0)
		perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_DEMUX) < 0)
		perror("AUDIO_SELECT_SOURCE");

	if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)	// restore AudioDecoder State
		perror("AUDIO_SET_AV_SYNC");
	if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
		perror("AUDIO_SET_BYPASS_MODE");

	close(ddvd_ac3_fd);
err_open_ac3_fd:
	close(ddvd_fdaudio);
err_open_fdaudio:
	close(ddvd_fdvideo);
err_open_fdvideo:
	close(ddvd_output_fd);
err_open_output_fd:

	if (have_liba52) {
		a52_free(state);
		ddvd_close_liba52();
	}

	//Clear Screen
	memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);
	msg = DDVD_SCREEN_UPDATE;
	safe_write(message_pipe, &msg, sizeof(int));

err_malloc:
	// clean up
	if (ddvd_lbb != NULL)
		free(ddvd_lbb);
	if (last_iframe != NULL)
		free(last_iframe);
	if (spu_buffer != NULL)
		free(spu_buffer);
	if (spu_backbuffer != NULL)
		free(spu_backbuffer);

#if CONFIG_API_VERSION == 3
	ddvd_unset_pcr_offset();
#endif
	return res;
}

/*
 * internal functions
 */

// reading from pipe
static int ddvd_readpipe(int pipefd, void *dest, size_t bytes, int blocked_read)
{
	size_t bytes_completed = 0;

	while (bytes_completed < bytes) {
		ssize_t rd = read(pipefd, dest + bytes_completed, bytes - bytes_completed);
		if (rd < 0) {
			if (errno == EAGAIN) {
				if (blocked_read || bytes_completed) {
					usleep(1);
					continue;
				}
				break;	// leave while loop
			}
			/* else if (errno == ????) // hier sollte evtl noch geschaut werden welcher error code kommt wenn die pipe geschlossen wurde... 
			   break; */
			printf("unhandled read error %d(%m)\n", errno);
		}

		bytes_completed += rd;
		if (!blocked_read && !bytes_completed)
			break;
	}

	return bytes_completed;
}

// get actual playing time
static struct ddvd_time ddvd_get_osd_time(struct ddvd *playerconfig)
{
	int titleNo;
	struct ddvd_time info;
	uint32_t pos, len;

	info.pos_minutes = info.pos_hours = info.pos_seconds = info.pos_chapter = info.pos_title = 0;
	info.end_minutes = info.end_hours = info.end_seconds = info.end_chapter = 0;

	dvdnav_get_number_of_titles(dvdnav, &info.end_title);
	dvdnav_current_title_info(dvdnav, &titleNo, &info.pos_chapter);

	if (titleNo) {
		dvdnav_get_number_of_parts(dvdnav, titleNo, &info.end_chapter);
		dvdnav_get_position_in_title(dvdnav, &pos, &len);

		uint64_t len_s = ddvd_lastCellEventInfo.pgc_length / 90000;
		uint64_t pos_s = ((ddvd_lastCellEventInfo.pgc_length / len) * pos) / 90000;

		info.pos_minutes = pos_s / 60;
		info.pos_hours = info.pos_minutes / 60;
		info.pos_minutes = info.pos_minutes - (info.pos_hours * 60);
		info.pos_seconds = pos_s - ((info.pos_hours * 60) + info.pos_minutes) * 60;
		info.end_minutes = len_s / 60;
		info.end_hours = info.end_minutes / 60;
		info.end_minutes = info.end_minutes - (info.end_hours * 60);
		info.end_seconds = len_s - ((info.end_hours * 60) + info.end_minutes) * 60;

		info.pos_title = titleNo;
	}

	playerconfig->next_time_update = ddvd_get_time() + 1000;

	return info;
}

// video out aspect/scale
static int ddvd_check_aspect(int dvd_aspect, int dvd_scale_perm, int tv_aspect, int tv_mode)
{
	int tv_scale = 0; // widescreen spu

	if (dvd_aspect == 0) // dvd 4:3
	{
		if((tv_aspect == DDVD_16_9 || tv_aspect == DDVD_16_10) && (tv_mode == DDVD_PAN_SCAN || tv_mode == DDVD_LETTERBOX))
			tv_scale = 2; // pan_scan spu
		if (tv_aspect == DDVD_4_3 && tv_mode == DDVD_PAN_SCAN)
			tv_scale = 2; // pan_scan spu
		if (tv_aspect == DDVD_4_3 && tv_mode == DDVD_LETTERBOX)
			tv_scale = 1; // letterbox spu
	} 
	
	return tv_scale;
}

// get timestamp
static uint64_t ddvd_get_time(void)
{
	static time_t t0 = 0;
	struct timeval t;

	if (gettimeofday(&t, NULL) == 0) {
		if (t0 == 0)
			t0 = t.tv_sec;	// this avoids an overflow (we only work with deltas)
		return (uint64_t) (t.tv_sec - t0) * 1000 + (uint64_t) (t.tv_usec / 1000);
	}

	return 0;
}

// Empty all Buffers
static void ddvd_play_empty(int device_clear)
{
	ddvd_wait_for_user = 0;
	ddvd_clear_buttons = 0;
	ddvd_lpcm_count = 0;
	ddvd_iframerun = 0;
	ddvd_still_frame = 0;
	ddvd_iframesend = 0;
	ddvd_last_iframe_len = 0;
	ddvd_spu_ptr = 0;
	ddvd_spu_backnr = 0;
	ddvd_spu_backptr = 0;

	ddvd_wait_timer_active = 0;
	ddvd_wait_timer_end = 0;

	ddvd_spu_timer_active = 0;
	ddvd_spu_timer_end = 0;

	if (device_clear)
		ddvd_device_clear();
}

// Empty Device Buffers
static void ddvd_device_clear(void)
{
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		perror("AUDIO_PLAY");

	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		perror("VIDEO_PLAY");

	if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
		perror("AUDIO_SET_AV_SYNC");
}

// SPU Decoder
static int ddvd_spu_decode_data(const uint8_t * buffer, int len)
{
	int x1spu, x2spu, y1spu, y2spu, xspu, yspu;
	int offset[2];
	int size, datasize, controlsize, aligned, id;
	int menubutton = 0;
	int display_time = -1;

	size = (buffer[0] << 8 | buffer[1]);
	datasize = (buffer[2] << 8 | buffer[3]);
	controlsize = (buffer[datasize + 2] << 8 | buffer[datasize + 3]);

	//printf("SPU_dec: Size: %X Datasize: %X Controlsize: %X\n",size,datasize,controlsize);
	// parse header
	int i = datasize + 4;

	while (i < size && buffer[i] != 0xFF) {
		switch (buffer[i]) {
		case 0x00:	/* menu button special color handling */
			menubutton = 1;
			memset(ddvd_lbb, 0, 720 * 576);	//clear backbuffer
			i++;
			break;
		case 0x01:	/* show */
			i++;
			break;
		case 0x02:	/* hide */
			i++;
			break;
		case 0x03:	/* palette */
			{
				ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer + i + 1);
				//printf("update palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

				ddvd_bl[3 + 252] = ddvd_bl[clut->entry0];
				ddvd_gn[3 + 252] = ddvd_gn[clut->entry0];
				ddvd_rd[3 + 252] = ddvd_rd[clut->entry0];

				ddvd_bl[2 + 252] = ddvd_bl[clut->entry1];
				ddvd_gn[2 + 252] = ddvd_gn[clut->entry1];
				ddvd_rd[2 + 252] = ddvd_rd[clut->entry1];

				ddvd_bl[1 + 252] = ddvd_bl[clut->entry2];
				ddvd_gn[1 + 252] = ddvd_gn[clut->entry2];
				ddvd_rd[1 + 252] = ddvd_rd[clut->entry2];

				ddvd_bl[0 + 252] = ddvd_bl[clut->entry3];
				ddvd_gn[0 + 252] = ddvd_gn[clut->entry3];
				ddvd_rd[0 + 252] = ddvd_rd[clut->entry3];

				//CHANGE COLORMAP
				i += 3;
			}
			break;
		case 0x04:	/* transparency palette */
			{
				ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer + i + 1);
				//printf("update transp palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

				ddvd_tr[0 + 252] = (0xF - clut->entry3) * 0x1111;
				ddvd_tr[1 + 252] = (0xF - clut->entry2) * 0x1111;
				ddvd_tr[2 + 252] = (0xF - clut->entry1) * 0x1111;
				ddvd_tr[3 + 252] = (0xF - clut->entry0) * 0x1111;

				//CHANGE COLORMAP
				i += 3;
			}
			break;
		case 0x05:	/* image coordinates */
			//printf("image coords\n");
			xspu = x1spu = (((unsigned int)buffer[i + 1]) << 4) + (buffer[i + 2] >> 4);
			yspu = y1spu = (((unsigned int)buffer[i + 4]) << 4) + (buffer[i + 5] >> 4);
			x2spu = (((buffer[i + 2] & 0x0f) << 8) + buffer[i + 3]);
			y2spu = (((buffer[i + 5] & 0x0f) << 8) + buffer[i + 6]);
			//printf("%d %d %d %d\n", xspu, yspu, x2spu, y2spu);
			i += 7;
			break;
		case 0x06:	/* image 1 / image 2 offsets */
			//printf("image offsets\n");
			offset[0] = (((unsigned int)buffer[i + 1]) << 8) + buffer[i + 2];
			offset[1] = (((unsigned int)buffer[i + 3]) << 8) + buffer[i + 4];
			//printf("%d %d\n", offset[0], offset[1]);
			i += 5;
			break;
		default:
			i++;
			break;
		}
	}

	//get display time
	if (i + 6 <= size) {
		if (buffer[i + 5] == 0x02 && buffer[i + 6] == 0xFF) {
			display_time = ((buffer[i + 1] << 8) + buffer[i + 2]);
			//printf("Display Time: %d\n",ddvd_display_time);
		}
	}
	//printf("SPU_dec: Image coords x1: %d y1: %d x2: %d y2: %d\n",x1spu,y1spu,x2spu,y2spu);
	//printf("Offset[0]: %X Offset[1]: %X\n",offset[0],offset[1]);

	// parse picture

	aligned = 1;
	id = 0;

	while (offset[1] < datasize + 2 && yspu <= 575)	// there are some special cases the decoder tries to write more than 576 lines in our buffer and we dont want this ;)
	{
		u_int len;
		u_int code;

		code = (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
		aligned = aligned ? 0 : 1;

		if (code < 0x0004) {
			code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
			aligned = aligned ? 0 : 1;
			if (code < 0x0010) {
				code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
				aligned = aligned ? 0 : 1;
				if (code < 0x0040) {
					code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
					aligned = aligned ? 0 : 1;
				}
			}
		}

		len = code >> 2;

		if (len == 0)
			len = (x2spu - xspu) + 1;

		memset(ddvd_lbb + xspu + 720 * (yspu), (code & 3) + 252, len);	//drawpixel into backbuffer
		xspu += len;
		if (xspu > x2spu) {
			if (!aligned) {
				code = (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
				aligned = aligned ? 0 : 1;
			}
			xspu = x1spu;	//next line
			yspu++;
			id = id ? 0 : 1;
		}
	}

	return display_time;
}

// blit to argb in 32bit mode
static void ddvd_blit_to_argb(void *_dst, const void *_src, int pix)
{
	unsigned long *dst = _dst;
	const unsigned char *src = _src;
	while (pix--) {
		int p = (*src++);
		int a, r, g, b;
		if (p == 0) {
			r = g = b = a = 0;	//clear screen (transparency)
		} else {
			a = 0xFF - (ddvd_tr[p] >> 8);
			r = ddvd_rd[p] >> 8;
			g = ddvd_gn[p] >> 8;
			b = ddvd_bl[p] >> 8;
		}
		*dst++ = (a << 24) | (r << 16) | (g << 8) | (b << 0);
	}
}

#if CONFIG_API_VERSION == 3

// set decoder buffer offsets to a minimum
static void ddvd_set_pcr_offset(void)
{
	write_string("/proc/stb/pcr/pcr_stc_offset", "200");
	write_string("/proc/stb/vmpeg/0/sync_offset", "200");
}

// reset decoder buffer offsets
static void ddvd_unset_pcr_offset(void)
{
	write_string("/proc/stb/pcr/pcr_stc_offset", "2710");
	write_string("/proc/stb/vmpeg/0/sync_offset", "2710");
}

#endif


// "nearest neighbor" pixmap resizing
void ddvd_resize_pixmap_xbpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int colors)
{
    int x_ratio = (int)((xsource<<16)/(xdest-2*xoffset)) ;
    int y_ratio = (int)(((ysource-2*yoffset)<<16)/ydest) ;
	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource*ysource*colors);
	memcpy(pixmap_tmp, pixmap, xsource*ysource*colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	
	int x2, y2, c, i ,j;
    for (i=0;i<ydest;i++) {
        for (j=0;j<(xdest-2*xoffset);j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16)+yoffset ;
            for (c=0; c<colors; c++)
				pixmap[((i*xdest)+j)*colors + c + xoffset*colors] = pixmap_tmp[((y2*xsource)+x2)*colors + c] ;
        }                
    }   
	free(pixmap_tmp);
}

// bicubic picture resize
void ddvd_resize_pixmap_xbpp_smooth(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int colors)
{
	unsigned int xs,ys,xd,yd,dpixel,fx,fy;
	unsigned int c,tmp_i;
	int x,y,t,t1;
	xs=xsource; // x-resolution source
	ys=ysource-2*yoffset; // y-resolution source
	xd=xdest-2*xoffset; // x-resolution destination
	yd=ydest; // y-resolution destination
	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource*ysource*colors);
	memcpy(pixmap_tmp, pixmap, xsource*ysource*colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	
	// get x scale factor, use bitshifting to get rid of floats
	fx=((xs-1)<<16)/xd;

	// get y scale factor, use bitshifting to get rid of floats
	fy=((ys-1)<<16)/yd;

	unsigned int sx1[xd],sx2[xd],sy1,sy2;
	
	// pre calculating sx1/sx2 for faster resizing
	for (x=0; x<xd; x++) 
	{
		// first x source pixel for calculating destination pixel
		sx1[x]=(fx*x)>>16; //floor()

		// last x source pixel for calculating destination pixel
		sx2[x]=sx1[x]+(fx>>16);
		if (fx & 0x7FFF) //ceil()
			sx2[x]++;
	}
	
	// Scale
	for (y=0; y<yd; y++) 
	{

		// first y source pixel for calculating destination pixel
		sy1=(fy*y)>>16; //floor()

		// last y source pixel for calculating destination pixel
		sy2=sy1+(fy>>16);
		if (fy & 0x7FFF) //ceil()
			sy2++;

		for (x=0; x<xd; x++) 
		{
			// we do this for every color
			for (c=0; c<colors; c++) 
			{
				// calculating destination pixel
				tmp_i=0;
				dpixel=0;
				for (t1=sy1; t1<sy2; t1++) 
				{
					for (t=sx1[x]; t<=sx2[x]; t++) 
					{
						tmp_i+=(int)pixmap_tmp[(t*colors)+c+((t1+yoffset)*xs*colors)];
						dpixel++;		
					}
				}
				// writing calculated pixel into destination pixmap
				pixmap[((x+xoffset)*colors)+c+(y*(xd+2*xoffset)*colors)]=tmp_i/dpixel;
			}
		}
	}
	free(pixmap_tmp);
}

// very simple linear resize used for 1bypp mode
void ddvd_resize_pixmap_1bpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int colors) 
{
	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource * ysource * colors);
	memcpy(pixmap_tmp, pixmap, xsource * ysource * colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	int i, fx, fy, tmp;
	
	// precalculate scale factor, use factor 10 to get rid of floats
	fx=xsource*10/(xdest-2*xoffset);
	fy=(ysource-2*yoffset)*10/ydest;
	
	// scale x
	for (i = 0; i < (xdest-2*xoffset); i++)
		pixmap[i]=pixmap_tmp[((fx*i)/10)+xoffset];

	// scale y
	for (i = 0; i < ydest; i++)
	{
		tmp=(fy*i)/10;
		if (tmp != i)
			memcpy(pixmap + (i*xdest), pixmap + (tmp + yoffset) * xdest, xdest);
	}	
	free(pixmap_tmp);
}

