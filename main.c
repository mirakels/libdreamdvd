/*
 * vim: ts=4
 *
 * DreamDVD V0.9 - DVD-Player for Dreambox
 * Copyright (C) 2007 by Seddi
 * Updates 2012-2013 Mirakels
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
#include "string.h"
#include "errno.h"

#define Debug(level, str, ...) (DebugLevel > level ? printf("LIBDVD: %07.3f: " str, (float) ddvd_get_time() / 1000.0, ##__VA_ARGS__) : 0)
#define Perror(msg)            Debug(-1, "%s: %s", msg, strerror(errno))

int DebugLevel = 1;

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
				Perror("write");
				return written ? written : -1;
			}
		}
		else
			written += n;
	}

	return written;
}


static void write_string(const char *filename, const char *string)
{
	FILE *f;

	f = fopen(filename, "w");
	if (f == NULL) {
		Perror(filename);
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
		Perror("pipe");
		goto err;
	}

	flags = fcntl(fd[0], F_GETFL);
	if (flags < 0) {
		Perror("F_GETFL");
		goto err;
	}
	if (fcntl(fd[0], F_SETFL, flags | O_NONBLOCK) < 0) {
		Perror("F_SETFL");
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
	int i;

	pconfig = malloc(sizeof(struct ddvd));
	if (pconfig == NULL) {
		Perror("malloc pconfig");
		return NULL;
	}

	memset(pconfig, 0, sizeof(struct ddvd));
	for (i = 0; i < MAX_AUDIO; i++)
		pconfig->audio_format[i] = -1;
    pconfig->last_audio_id = -1;

	for (i = 0; i < MAX_SPU; i++)
		pconfig->spu_map[i].logical_id = pconfig->spu_map[i].stream_id = pconfig->spu_map[i].lang = -1;
    pconfig->last_spu_id = -1;

	// defaults
	ddvd_set_ac3thru(pconfig, 0);
	ddvd_set_language(pconfig, "en");
	ddvd_set_dvd_path(pconfig, "/dev/cdroms/cdrom0");
	ddvd_set_video(pconfig, DDVD_4_3, DDVD_LETTERBOX, DDVD_PAL);
	ddvd_set_lfb(pconfig, NULL, 720, 576, 1, 720);
	struct ddvd_resume resume_info;
	resume_info.title = resume_info.chapter = resume_info.block = resume_info.audio_id =
						resume_info.audio_lock = resume_info.spu_id = resume_info.spu_lock = 0;
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

// set resume position
// At this point the ddvd structure might not yet be completely setup
// Setup 'event' that will be picked up in the main loop.
void ddvd_set_resume_pos(struct ddvd *pconfig, struct ddvd_resume resume_info)
{
    Debug(2, "ddvd_set_resume_pos: title=%d, chapter=%d, block=%lu, audio_id=%d, audio_lock=%d, spu_id=%d, spu_lock=%d\n",
             resume_info.title, resume_info.chapter, resume_info.block, resume_info.audio_id, resume_info.audio_lock,
             resume_info.spu_id, resume_info.spu_lock);

	pconfig->should_resume     = 1;
	pconfig->resume_title      = resume_info.title;
	pconfig->resume_chapter    = resume_info.chapter;
	pconfig->resume_block      = resume_info.block;
	pconfig->resume_audio_id   = resume_info.audio_id;
	pconfig->resume_audio_lock = resume_info.audio_lock;
	pconfig->resume_spu_id     = resume_info.spu_id;
	pconfig->resume_spu_lock   = resume_info.spu_lock;
}

// set framebuffer options
void ddvd_set_lfb(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride)
{
	return ddvd_set_lfb_ex(pconfig, lfb, xres, yres, bypp, stride, 0);
}

void ddvd_set_lfb_ex(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride, int canscale)
{
	pconfig->lfb = lfb;
	pconfig->xres = xres;
	pconfig->yres = yres;
	pconfig->stride = stride;
	pconfig->bypp = bypp;
	pconfig->canscale = canscale;
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
	Debug(2, "ddvd_set_language to %c%c\n", lang[0], lang[1]);
	memcpy(pconfig->language, lang, 2);
}

// set internal ac3 decoding (needs liba52 which will be dynamically loaded)
void ddvd_set_ac3thru(struct ddvd *pconfig, int ac3thru)
{
	pconfig->ac3thru = ac3thru;
}

// set video options
void ddvd_set_video_ex(struct ddvd *pconfig, int aspect, int tv_mode, int tv_mode2, int tv_system)
{
	pconfig->aspect = aspect;
	pconfig->tv_mode = tv_mode;
	pconfig->tv_mode2 = tv_mode2;
	pconfig->tv_system = tv_system;
}

// set subtitle stream id
void ddvd_set_spu(struct ddvd *pconfig, int spu_id)
{
	ddvd_send_key(pconfig, DDVD_SET_SUBTITLE);
	ddvd_send_key(pconfig, spu_id);
}

// set audio stream id
void ddvd_set_audio(struct ddvd *pconfig, int audio_id)
{
	ddvd_send_key(pconfig, DDVD_SET_AUDIO);
	ddvd_send_key(pconfig, audio_id);
}

// set video options
void ddvd_set_video(struct ddvd *pconfig, int aspect, int tv_mode, int tv_system)
{
	ddvd_set_video_ex(pconfig, aspect, tv_mode, tv_mode, tv_system);
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

	switch (res) {		// more data to process ?
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
		case DDVD_SCREEN_UPDATE:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->blit_area, sizeof(pconfig->blit_area), 1);
			break;
		case DDVD_SHOWOSD_ANGLE:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->angle_current, sizeof(int), 1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->angle_num, sizeof(int), 1);
			break;
		case DDVD_SIZE_CHANGED:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_size, sizeof(struct ddvd_size_evt), 1);
			break;
		case DDVD_PROGRESSIVE_CHANGED:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_progressive, sizeof(struct ddvd_progressive_evt), 1);
			break;
		case DDVD_FRAMERATE_CHANGED:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_framerate, sizeof(struct ddvd_framerate_evt), 1);
			break;
		default:
			break;
	}
	return res;
}

// get last blit area
void ddvd_get_last_blit_area(struct ddvd *pconfig, int *x_start, int *x_end, int *y_start, int *y_end)
{
	struct ddvd_resize_return *ptr = &pconfig->blit_area;
	memcpy(x_start, &ptr->x_start, sizeof(int));
	memcpy(x_end, &ptr->x_end, sizeof(int));
	memcpy(y_start, &ptr->y_start, sizeof(int));
	memcpy(y_end, &ptr->y_end, sizeof(int));
}

void ddvd_get_blit_destination(struct ddvd *pconfig, int *x_offset, int *y_offset, int *width, int *height)
{
	struct ddvd_resize_return *ptr = &pconfig->blit_area;
	*x_offset = ptr->x_offset;
	*y_offset = ptr->y_offset;
	*width = ptr->width;
	*height = ptr->height;
}

// get angle info
void ddvd_get_angle_info(struct ddvd*pconfig, int *current, int *num)
{
	memcpy(current, &pconfig->angle_current, sizeof(pconfig->angle_current));
	memcpy(num, &pconfig->angle_num, sizeof(pconfig->angle_num));
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

// get the number of available audio tracks
void ddvd_get_audio_count(struct ddvd *pconfig, void *count)
{
	int c = 0;
	int i;
	for (i = 0; i < MAX_AUDIO; i++) {
		if (pconfig->audio_format[i] != -1)
			c++;
	}
	memcpy(count, &c, sizeof(int));
}

// get the active audio track
void ddvd_get_last_audio(struct ddvd *pconfig, void *id, void *lang, void *type)
{
	memcpy(id, &pconfig->last_audio_id, sizeof(pconfig->last_audio_id));
	memcpy(lang, &pconfig->last_audio_lang, sizeof(pconfig->last_audio_lang));
	memcpy(type, &pconfig->last_audio_type, sizeof(pconfig->last_audio_type));
	Debug(2, "ddvd_get_last_audio id=%d\n", * (int *)id);
}

// get audio track details for given audio track id
void ddvd_get_audio_byid(struct ddvd *pconfig, int audio_id, void *lang, void *type)
{
	int audio_id_logical;
	uint16_t audio_lang = 0xFFFF;
	audio_id_logical = dvdnav_get_audio_logical_stream(dvdnav, audio_id);
	audio_lang = dvdnav_audio_stream_to_lang(dvdnav, audio_id_logical);
	if (audio_lang == 0xFFFF)
		audio_lang = 0x2D2D;
	memcpy(lang, &audio_lang, sizeof(uint16_t));
	memcpy(type, &pconfig->audio_format[audio_id], sizeof(int));
	Debug(2, "ddvd_get_audio_byid %d %c%c\n", audio_id, audio_lang >> 8, audio_lang & 0xff);
}

// get the active SPU track
void ddvd_get_last_spu(struct ddvd *pconfig, void *id, void *lang)
{
	memcpy(id, &pconfig->last_spu_id, sizeof(pconfig->last_spu_id));
	memcpy(lang, &pconfig->last_spu_lang, sizeof(pconfig->last_spu_lang));
	Debug(2, "ddvd_get_last_spu id=%d\n", * (int *)id);
}

// get the number of available subtitle tracks
void ddvd_get_spu_count(struct ddvd *pconfig, void *count)
{
	int c = 0;
	int i;
	for (i = 0; i < MAX_SPU; i++) {
		if (pconfig->spu_map[i].logical_id != -1)
			c++;
	}
	memcpy(count, &c, sizeof(int));
	Debug(2, "ddvd_get_spu_count %d streams\n", c);
}

// get language details for given subtitle track id
void ddvd_get_spu_byid(struct ddvd *pconfig, int spu_id, void *lang)
{
	uint16_t spu_lang = 0xFFFF;
	if (spu_id < MAX_SPU && pconfig->spu_map[spu_id].logical_id > -1)
		spu_lang = pconfig->spu_map[spu_id].lang;
	memcpy(lang, &spu_lang, sizeof(uint16_t));
	Debug(2, "ddvd_get_spu_byid %d: %d %d %c%c\n", spu_id, pconfig->spu_map[spu_id].logical_id,
			pconfig->spu_map[spu_id].stream_id, spu_lang >> 8, spu_lang & 0xff);
}

// get dvd title string
void ddvd_get_title_string(struct ddvd *pconfig, char *title_string)
{
	memcpy(title_string, pconfig->title_string, sizeof(pconfig->title_string));
}

// get actual position for resuming
void ddvd_get_resume_pos(struct ddvd *pconfig, struct ddvd_resume *resume_info)
{
	resume_info->title      = pconfig->resume_title;
	resume_info->chapter    = pconfig->resume_chapter;
	resume_info->block      = pconfig->resume_block;
	resume_info->audio_id   = pconfig->resume_audio_id;
	resume_info->audio_lock = pconfig->resume_audio_lock;
	resume_info->spu_id     = pconfig->resume_spu_id;
	resume_info->spu_lock   = pconfig->resume_spu_lock;
}

void ddvd_get_last_size(struct ddvd *pconfig, int *width, int *height, int *aspect)
{
	*width = pconfig->last_size.width;
	*height = pconfig->last_size.height;
	*aspect = pconfig->last_size.aspect;
}

void ddvd_get_last_progressive(struct ddvd *pconfig, int *progressive)
{
	*progressive = pconfig->last_progressive.progressive;
}

void ddvd_get_last_framerate(struct ddvd *pconfig, int *framerate)
{
	*framerate = pconfig->last_framerate.framerate;
}

static int calc_x_scale_offset(int dvd_aspect, int tv_mode, int tv_mode2, int tv_aspect)
{
	int x_offset=0;

	if (dvd_aspect == 0 && tv_mode == DDVD_PAN_SCAN) {
		switch (tv_aspect) {
			case DDVD_16_10:
				x_offset = (ddvd_screeninfo_xres - ddvd_screeninfo_xres * 12 / 15) / 2;  // correct 16:10 (broadcom 15:9) panscan (pillarbox) overlay
				break;
			case DDVD_16_9:
				x_offset = (ddvd_screeninfo_xres - ddvd_screeninfo_xres * 3 / 4) / 2; // correct 16:9 panscan (pillarbox) overlay
			default:
				break;
		}
	}

	if (dvd_aspect >= 2 && tv_aspect == DDVD_4_3 && tv_mode == DDVD_PAN_SCAN)
		x_offset = -(ddvd_screeninfo_xres * 4 / 3 - ddvd_screeninfo_xres) / 2;

	if (dvd_aspect >= 2 && tv_aspect == DDVD_16_10 && tv_mode2 == DDVD_PAN_SCAN)
		x_offset = -(ddvd_screeninfo_xres * 16 / 15 - ddvd_screeninfo_xres) / 2;

	return x_offset;
}

static int calc_y_scale_offset(int dvd_aspect, int tv_mode, int tv_mode2, int tv_aspect)
{
	int y_offset = 0;

	if (dvd_aspect == 0 && tv_mode == DDVD_LETTERBOX) {
		switch (tv_aspect) {
			case DDVD_16_10:
				y_offset = (ddvd_screeninfo_yres * 15 / 12 - ddvd_screeninfo_yres) / 2; // correct 16:10 (broacom 15:9) letterbox overlay
				break;
			case DDVD_16_9:
				y_offset = (ddvd_screeninfo_yres * 4 / 3 - ddvd_screeninfo_yres) / 2; // correct 16:9 letterbox overlay
			default:
				break;
		}
	}

	if (dvd_aspect >= 2 && tv_aspect == DDVD_4_3 && tv_mode == DDVD_LETTERBOX)
		y_offset = -(ddvd_screeninfo_yres - ddvd_screeninfo_yres * 3 / 4) / 2;

	if (dvd_aspect >= 2 && tv_aspect == DDVD_16_10 && tv_mode2 == DDVD_LETTERBOX)
		y_offset = -(ddvd_screeninfo_yres - ddvd_screeninfo_yres * 15 / 16) / 2;

	return y_offset;
}

#if CONFIG_API_VERSION >= 3
static int readMpegProc(char *str, int decoder)
{
	int val = -1;
	char tmp[64];
	sprintf(tmp, "/proc/stb/vmpeg/%d/%s", decoder, str);
	FILE *f = fopen(tmp, "r");
	if (f) {
		fscanf(f, "%x", &val);
		fclose(f);
	}
	return val;
}

static int readApiSize(int fd, int *xres, int *yres, int *aspect)
{
	video_size_t size;
	if (!ioctl(fd, VIDEO_GET_SIZE, &size)) {
		*xres = size.w;
		*yres = size.h;
		*aspect = size.aspect_ratio == 0 ? 2 : 3;  // convert dvb api to etsi
		return 0;
	}
	return -1;
}

static int readApiFrameRate(int fd, int *framerate)
{
	unsigned int frate;
	if (!ioctl(fd, VIDEO_GET_FRAME_RATE, &frate)) {
		*framerate = frate;
		return 0;
	}
	return -1;
}
#endif

// the main player loop
enum ddvd_result ddvd_run(struct ddvd *playerconfig)
{
	{	char * DL = getenv("LIBDVD_DEBUG");
		if (DL)
			DebugLevel = atoi(DL);
	}

	if (playerconfig->lfb == NULL) {
		Debug(1, "Frame/backbuffer not given to libdreamdvd. Will not start the player !\n");
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
	int audio_lock = 0;
	int spu_lock = 0;

	unsigned long long vpts = 0, apts = 0, spts = 0, pts = 0;
	unsigned long long steppts = 0; // target pts for STEP mode
	ddvd_lbb_changed = 0;
	ddvd_clear_screen = 0;
	int have_highlight = 0;
	int ddvd_wait_highlight = 0;
	const char *dvd_titlestring = NULL;
	playerconfig->in_menu = 0;
	int ddvd_spu_ind = 0;
	int ddvd_spu_play = 0;

	// decide which resize routine we should use
	// on 4bpp mode we use bicubic resize for sd skins because we get much better results with subtitles and the speed is ok
	// for hd skins we use nearest neighbor resize because upscaling to hd is too slow with bicubic resize
	// for bypp != 0 resize function is set in spu/highlight code
	if (ddvd_screeninfo_bypp == 1)
		ddvd_resize_pixmap = &ddvd_resize_pixmap_1bpp;

	uint8_t *last_iframe = NULL;

	// init backbuffer (SPU)
	ddvd_lbb = malloc(720 * 576);	// the spu backbuffer is always max DVD PAL 720x576 pixel (NTSC 720x480)
	if (ddvd_lbb == NULL) {
		Perror("SPU decode buffer <mem allocation failed>");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

	ddvd_lbb2 = malloc(ddvd_screeninfo_xres * ddvd_screeninfo_yres * ddvd_screeninfo_bypp);
	if (ddvd_lbb2 == NULL) {
		Perror("SPU to screen buffer <mem allocation failed>");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

#define SPU_BUFLEN   (2 * (128 * 1024))
	unsigned long long spu_backpts[NUM_SPU_BACKBUFFER];
	unsigned char *ddvd_spu[NUM_SPU_BACKBUFFER];
	pci_t *ddvd_pci[NUM_SPU_BACKBUFFER];
	{   int  i;
		for (i = 0; i < NUM_SPU_BACKBUFFER; i++)  {
			ddvd_spu[i] = malloc(SPU_BUFLEN);    // buffers for decoded SPU packets
			if (ddvd_spu[i] == NULL) {
				Perror("SPU backbuffer <mem allocation failed>");
				res = DDVD_NOMEM;
				goto err_malloc;
			}
			ddvd_pci[i] = malloc(sizeof(pci_t));    // buffers for pci packets
			if (ddvd_pci[i] == NULL) {
				Perror("PCI backbuffer <mem allocation failed>");
				res = DDVD_NOMEM;
				goto err_malloc;
			}
		}
	}

	last_iframe = malloc(320 * 1024);
	if (last_iframe == NULL) {
		Perror("malloc last_iframe");
		res = DDVD_NOMEM;
		goto err_malloc;
	}

	struct ddvd_resize_return blit_area;

	memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen
	blit_area.x_start = blit_area.y_start = 0;
	blit_area.x_end = ddvd_screeninfo_xres - 1;
	blit_area.y_end = ddvd_screeninfo_yres - 1;
	blit_area.x_offset = 0;
	blit_area.y_offset = 0;
	blit_area.width = ddvd_screeninfo_xres;
	blit_area.height = ddvd_screeninfo_yres;

	msg = DDVD_SCREEN_UPDATE;
	safe_write(message_pipe, &msg, sizeof(int));
	safe_write(message_pipe, &blit_area, sizeof(struct ddvd_resize_return));

	Debug(1, "Opening output...\n");

#if CONFIG_API_VERSION == 1
	ddvd_output_fd = open("/dev/video", O_WRONLY);
	if (ddvd_output_fd == -1) {
		Perror("/dev/video");
		res = DDVD_BUSY;
		goto err_open_output_fd;
	}

	ddvd_fdvideo = open("/dev/dvb/card0/video0", O_RDWR);
	if (ddvd_fdvideo == -1) {
		Perror("/dev/dvb/card0/video0");
		res = DDVD_BUSY;
		goto err_open_fdvideo;
	}

	ddvd_fdaudio = open("/dev/dvb/card0/audio0", O_RDWR);
	if (ddvd_fdaudio == -1) {
		Perror("/dev/dvb/card0/audio0");
		res = DDVD_BUSY;
		goto err_open_fdaudio;
	}

	ddvd_ac3_fd = open("/dev/sound/dsp1", O_RDWR);
	if (ddvd_ac3_fd == -1) {
		Perror("/dev/sound/dsp1");
		res = DDVD_BUSY;
		goto err_open_ac3_fd;
	}

	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY) < 0)
		Perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		Perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		Perror("VIDEO_PLAY");

	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY) < 0)
		Perror("AUDIO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		Perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		Perror("AUDIO_PLAY");

#elif CONFIG_API_VERSION == 3
	ddvd_output_fd = ddvd_fdvideo = open("/dev/dvb/adapter0/video0", O_RDWR);
	if (ddvd_fdvideo == -1) {
		Perror("/dev/dvb/adapter0/video0");
		res = DDVD_BUSY;
		goto err_open_fdvideo;
	}

	ddvd_ac3_fd = ddvd_fdaudio = open("/dev/dvb/adapter0/audio0", O_RDWR);
	if (ddvd_fdaudio == -1) {
		Perror("/dev/dvb/adapter0/audio0");
		res = DDVD_BUSY;
		goto err_open_ac3_fd;
	}

	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY) < 0)
		Perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		Perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_SET_STREAMTYPE, 0) < 0)	// set mpeg2
		Perror("VIDEO_SET_STREAMTYPE");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		Perror("VIDEO_PLAY");

	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY) < 0)
		Perror("AUDIO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		Perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		Perror("AUDIO_PLAY");
#else
# error please define CONFIG_API_VERSION to be 1 or 3
#endif

	int i;
// show startup screen
#if SHOW_START_SCREEN == 1
# if CONFIG_API_VERSION == 1
	//that really sucks but there is no other way
	for (i = 0; i < 10; i++)
		safe_write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
# else
	unsigned char pes_header[] = { 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x00, 0x00 };
	safe_write(ddvd_output_fd, pes_header, sizeof(pes_header));
	safe_write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
# endif
#endif

	int audio_type = DDVD_UNKNOWN;

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
	osdtext[0] = 0;

	int tv_aspect = playerconfig->aspect;	// 0-> 4:3 lb 1-> 4:3 ps 2-> 16:9 3-> always 16:9
	int tv_mode = playerconfig->tv_mode;
	int tv_mode2 = playerconfig->tv_mode2; // just used when tv_aspect is 16:10 and dvd_aspect is 16:9
	int dvd_aspect = 0;	// 0-> 4:3 2-> 16:9
	int dvd_scale_perm = 0;
	int tv_scale = 0;	// 0-> off 1-> letterbox 2-> panscan
	int spu_active_id = -1;
	int spu_index = -1;
	int finished = 0;
	int audio_id;
	int report_audio_info = 0;

	struct ddvd_spu_return last_spu_return;
	struct ddvd_spu_return cur_spu_return;
	struct ddvd_resize_return last_blit_area;
	memcpy(&last_blit_area, &blit_area, sizeof(struct ddvd_resize_return));
	last_spu_return.x_start = last_spu_return.y_start = 0;
	last_spu_return.x_end = last_spu_return.y_end = 0;

	ddvd_trickmode = TOFF;
	ddvd_trickspeed = 0;

	int rccode;
	int ismute = 0;

	if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
		Perror("AUDIO_SET_AV_SYNC");
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
			Perror("SAAIOSENC");
		close(saafd);
	}
#else
	{
		struct ddvd_size_evt s_evt;
		struct ddvd_framerate_evt f_evt;
		struct ddvd_progressive_evt p_evt;
		int msg = DDVD_SIZE_CHANGED;
		readApiSize(ddvd_fdvideo, &s_evt.width, &s_evt.height, &s_evt.aspect);
		safe_write(message_pipe, &msg, sizeof(int));
		safe_write(message_pipe, &s_evt, sizeof(s_evt));

		msg = DDVD_FRAMERATE_CHANGED;
		readApiFrameRate(ddvd_fdvideo, &f_evt.framerate);
		safe_write(message_pipe, &msg, sizeof(int));
		safe_write(message_pipe, &f_evt, sizeof(f_evt));

		msg = DDVD_PROGRESSIVE_CHANGED;
		p_evt.progressive = readMpegProc("progressive", 0);
		safe_write(message_pipe, &msg, sizeof(int));
		safe_write(message_pipe, &p_evt, sizeof(p_evt));
	}
#endif

	/* open dvdnav handle */
	Debug(1, "Opening DVD...%s\n", playerconfig->dvd_path);
	if (dvdnav_open(&dvdnav, playerconfig->dvd_path) != DVDNAV_STATUS_OK) {
		Debug(1, "Error on dvdnav_open\n");
		sprintf(osdtext, "Error: Cant open DVD Source: %s", playerconfig->dvd_path);
		msg = DDVD_SHOWOSD_STRING;
		safe_write(message_pipe, &msg, sizeof(int));
		safe_write(message_pipe, &osdtext, sizeof(osdtext));
		res = DDVD_FAIL_OPEN;
		goto err_dvdnav_open;
	}

	/* set read ahead cache usage to no */
	if (dvdnav_set_readahead_flag(dvdnav, 0) != DVDNAV_STATUS_OK) {
		Debug(1, "Error on dvdnav_set_readahead_flag: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	/* set the language */
	if (dvdnav_menu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
		dvdnav_audio_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
		dvdnav_spu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK) {
		Debug(1, "Error on setting languages: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	/* set the PGC positioning flag to have position information relatively to the
	 * whole feature instead of just relatively to the current chapter */
	if (dvdnav_set_PGC_positioning_flag(dvdnav, 1) != DVDNAV_STATUS_OK) {
		Debug(1, "Error on dvdnav_set_PGC_positioning_flag: %s\n", dvdnav_err_to_string(dvdnav));
		res = DDVD_FAIL_PREFS;
		goto err_dvdnav;
	}

	audio_id = dvdnav_get_active_audio_stream(dvdnav);
	ddvd_playmode = PLAY;

	dvdnav_highlight_event_t highlight_event;

	ddvd_play_empty(FALSE);
	ddvd_get_time();	//set timestamp

	if (dvdnav_get_title_string(dvdnav, &dvd_titlestring) == DVDNAV_STATUS_OK)
		strncpy(playerconfig->title_string, dvd_titlestring, 96);
	if (strlen(playerconfig->title_string) == 0) {
		// DVD has no title set,, use dvd_path info
		char *sl = strrchr(playerconfig->dvd_path, '/');
		if (sl == NULL)
			sl = playerconfig->dvd_path;
		else
			sl++;
		strncpy(playerconfig->title_string, sl, 96);
		sl = playerconfig->title_string;
		int len = strlen(sl);
		if (sl[len - 4] == '.' && sl[len - 3] == 'i' && sl[len - 2] == 's' && sl[len - 1] == '0')
			sl[len - 4] = '\0';
	}
	Debug(1, "DVD Title: %s  (DVD says: %s)\n", playerconfig->title_string, dvd_titlestring);

	msg = DDVD_SHOWOSD_TITLESTRING;
	safe_write(message_pipe, &msg, sizeof(int));

	if( dvdnav_title_play(dvdnav, 1 ) != DVDNAV_STATUS_OK)
		Debug(1, "cannot set title (can't decrypt DVD?)\n");

	if( dvdnav_menu_call(dvdnav, DVD_MENU_Title ) != DVDNAV_STATUS_OK) {
		/* Try going to menu root */
		if( dvdnav_menu_call(dvdnav, DVD_MENU_Root) != DVDNAV_STATUS_OK)
			Debug(1, "cannot go to dvd menu\n");
	}

	/* the read loop which regularly calls dvdnav_get_next_block
	 * and handles the returned events */
	int reached_eof = 0;
	int reached_sof = 0;
	uint64_t now;
	int in_menu = 0;
	pci_t *pci = NULL;
	dvdnav_still_event_t still_event;
	int have_still_event = 0;

	while (!finished) {
		dsi_t *dsi = 0;
		int draw_osd = 0;

		/* the main reading function */
		now = ddvd_get_time();
		if (ddvd_playmode & (PLAY|STEP)) {	// Skip when not in play/step mode
			// trickmode
			if (ddvd_trickmode & (TRICKFW | TRICKBW) && now >= ddvd_trick_timer_end) {
				uint32_t pos, len;
				dvdnav_get_position(dvdnav, &pos, &len);
				if (!len)
					len = 1;
				// Backward: 90000 = 1 Sek. -> 45000 = 0.5 Sek.  -> Speed Faktor=2
				// Forward:  90000 = 1 Sek. -> 22500 = 0.25 Sek. -> Speed Faktor=2
				#define FORWARD_WAIT 300
				#define BACKWARD_WAIT 500
				int64_t offset = (ddvd_trickspeed - 1) * 90000L * (ddvd_trickmode & TRICKBW ? BACKWARD_WAIT : FORWARD_WAIT) / 1000;
				int64_t newpos = (int64_t)pos +(offset + (int64_t)(vpts > pts ? pts - vpts : 0)) * (int64_t)len / ddvd_lastCellEventInfo.pgc_length;
				Debug(1, "FAST FW/BW: %d -> %lld - %lld - SPU clr=%d->%d vpts=%llu pts=%llu\n", pos, newpos, offset, ddvd_spu_play, ddvd_spu_ind, vpts, pts);
				if (newpos <= 0) {	// reached begin of movie
					newpos = 0;
					reached_sof = 1;
					// msg = DDVD_SHOWOSD_TIME; // Is osd update needed every jump?
				}
				else if (newpos >= len) {	// reached end of movie
					newpos = len - 250;
					reached_eof = 1;
					// msg = DDVD_SHOWOSD_TIME; // Is osd update needed every jump?
				}
				else
					msg = ddvd_trickmode & TRICKFW ? DDVD_SHOWOSD_STATE_FFWD : DDVD_SHOWOSD_STATE_FBWD;
				dvdnav_sector_search(dvdnav, newpos, SEEK_SET);
				ddvd_trick_timer_end = now + (ddvd_trickmode & TRICKFW ? FORWARD_WAIT : BACKWARD_WAIT);
				ddvd_lpcm_count = 0;
				ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
			}

			result = dvdnav_get_next_block(dvdnav, buf, &event, &len);
			if (result == DVDNAV_STATUS_ERR) {
				Debug(1, "Error getting next block: %s\n", dvdnav_err_to_string(dvdnav));
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
						Debug(4, "OSD_TIME vpts=%llu pts=%llu iframesend=%d\n", vpts, pts, ddvd_iframesend);
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
				Debug(2, "EOF\n");
				msg = DDVD_EOF_REACHED;
				safe_write(message_pipe, &msg, sizeof(int));
				reached_eof = 0;
			}

			if (reached_sof) {
				Debug(2, "SOF\n");
				msg = DDVD_SOF_REACHED;
				safe_write(message_pipe, &msg, sizeof(int));
				reached_sof = 0;
			}

			now = ddvd_get_time();
			if (now >= playerconfig->next_time_update) {
				playerconfig->next_time_update = now + 1000;
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
					safe_write(ddvd_output_fd, last_iframe, ddvd_last_iframe_len); // send twice to avoid no-display...
#endif
					//Debug(1, "Show iframe with size: %d\n",ddvd_last_iframe_len);
					ddvd_last_iframe_len = 0;
				}

				ddvd_iframesend = -1;
			}
			// wait timer
			if (ddvd_wait_timer_active && now >= ddvd_wait_timer_end) {
				ddvd_wait_timer_active = 0;
				dvdnav_still_skip(dvdnav);
				Debug(1, "wait timer done\n");
			}
			// SPU timer
			if (ddvd_spu_timer_active && now >= ddvd_spu_timer_end) {
				ddvd_spu_timer_active = 0;
				Debug(3, "    set clear SPU backbuffer, SPU finished\n");
				ddvd_clear_screen = 1;
			}

			switch (event) {
			case DVDNAV_BLOCK_OK:
				/* We have received a regular block of the currently playing MPEG stream.
				 * So we do some demuxing and decoding. */
				{
					// collect audio data
					int stream_type = buf[14 + buf[14 + 8] + 9];
					if (((buf[14 + 3]) & 0xF0) == 0xC0)
						playerconfig->audio_format[(buf[14 + 3]) - 0xC0] = DDVD_MPEG;
					if ((buf[14 + 3]) == 0xBD && (stream_type & 0xF8) == 0x80)
						playerconfig->audio_format[stream_type - 0x80] = DDVD_AC3;
					if ((buf[14 + 3]) == 0xBD && (stream_type & 0xF8) == 0x88)
						playerconfig->audio_format[stream_type - 0x88] = DDVD_DTS;
					if ((buf[14 + 3]) == 0xBD && (stream_type & 0xF8) == 0xA0)
						playerconfig->audio_format[stream_type - 0xA0] = DDVD_LPCM;

					if ((buf[14 + 3] & 0xF0) == 0xE0) {	// video
						int pes_len = ((buf[14 + 4] << 8) | buf[14 + 5]) + 6;
						int padding = len - (14 + pes_len);
						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							vpts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							vpts |= buf[14 + 10] << 22;
							vpts |= (buf[14 + 11] >> 1) << 15;
							vpts |= buf[14 + 12] << 7;
							vpts |= (buf[14 + 13] >> 1);
							//Debug(1, "VPTS=%llu\n", vpts);
						}
#if CONFIG_API_VERSION == 1
						// Eliminate 00 00 01 B4 sequence error packet because it breaks the pallas mpeg decoder
						// This is very strange because the 00 00 01 B4 is partly inside the header extension ...
						if (buf[21] == 0x00 && buf[22] == 0x00 && buf[23] == 0x01 && buf[24] == 0xB4)
							buf[21] = 0x01;
						if (buf[22] == 0x00 && buf[23] == 0x00 && buf[24] == 0x01 && buf[25] == 0xB4)
							buf[22] = 0x01;
#endif
						// if we have 16:9 Zoom Mode on the DVD and we use a "always 16:9" mode on tv
						// and patch the mpeg header and the Sequence Display Extension inside the Stream in some cases
						if (dvd_aspect == 3 && (
							(tv_aspect == DDVD_16_9 && (tv_mode == DDVD_PAN_SCAN || tv_mode == DDVD_LETTERBOX)) ||
							(tv_aspect == DDVD_16_10 && (tv_mode2 == DDVD_PAN_SCAN || tv_mode2 == DDVD_LETTERBOX)) ) ) {
							int z = 0;
							for (z = 0; z < 2040; z++) {
								if (buf[z] == 0x0 && buf[z + 1] == 0x0 && buf[z + 2] ==0x01 && buf[z + 3] == 0xB5 &&
									(buf[z + 4] == 0x22 || buf[z + 4] == 0x23) ) {
									buf[z + 5] = 0x22;
									buf[z + 5] = 0x0B;
									buf[z + 6] = 0x42;
									buf[z + 7] = 0x12;
									buf[z + 8] = 0x00;
								}
							}
							if (buf[33] == 0 && buf[33 + 1] == 0 && buf[33 + 2] == 1 && buf[33 + 3] == 0xB3)
								buf[33 + 7] = (buf[33 + 7] & 0xF) + 0x30;
							if (buf[36] == 0 && buf[36 + 1] == 0 && buf[36 + 2] == 1 && buf[36 + 3] == 0xB3)
								buf[36 + 7] = (buf[36 + 7] & 0xF) + 0x30;
						}

						// check yres for detecting ntsc/pal
						if (ddvd_have_ntsc == -1) {
							if ( (buf[33] == 0 && buf[33 + 1] == 0 && buf[33 + 2] == 1 && buf[33 + 3] == 0xB3 &&
									( (buf[33 + 5] & 0xF) << 8) + buf[33 + 6] == 0x1E0)
								||
								 (buf[36] == 0 && buf[36 + 1] == 0 && buf[36 + 2] == 1 && buf[36 + 3] == 0xB3 &&
									( (buf[36 + 5] & 0xF) << 8) + buf[36 + 6] == 0x1E0))
								ddvd_have_ntsc = 1;
							else
								ddvd_have_ntsc = 0;
						}

						if (padding > 8) {
							memcpy(buf + 14 + pes_len, "\x00\x00\x01\xE0\x00\x00\x80\x00\x00", 9);
							pes_len += 9;
						}

						safe_write(ddvd_output_fd, buf + 14, pes_len);

						if (padding && padding < 9)
							safe_write(ddvd_output_fd, "\x00\x00\x01\xE0\x00\x00\x80\x00\x00", 9);

						// 14+8 header_length
						// 14+(header_length)+3  -> start mpeg header
						// buf[14+buf[14+8]+3] start mpeg header

						int datalen = (buf[19] + (buf[18] << 8) + 6) - buf[14 + 8];	// length mpeg packet
						int data = buf[14 + buf[14 + 8] + 3];	// start mpeg packet(header)

						int do_copy = (ddvd_iframerun == 0x01) && !(buf[data] == 0 && buf[data + 1] == 0 && buf[data + 2] == 1) ? 1 : 0;
						int have_pictureheader = 0;
						int haveslice = 0;
						int setrun = 0;

						while (datalen > 3) {
							if (buf[data] == 0 && buf[data + 1] == 0 && buf[data + 2] == 1) {
								if (buf[data + 3] == 0x00 && datalen > 6) { //picture
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
								}
								else if (buf[data + 3] == 0xB3 && datalen >= 8) { //sequence header
									ddvd_last_iframe_len = 0;	// clear iframe buffer
									data += 7;
									datalen -= 7;
								}
								else if (buf[data + 3] == 0xBE) { //padding stream
									break;
								}
								else if (0x01 <= buf[data + 3] && buf[data + 3] <= 0xaf) { //slice ?
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
							else if (ddvd_last_iframe_len < (320 * 1024) - ((buf[19] + (buf[18] << 8) + 6) - buf[14 + 8])) {
								int len = buf[19] + (buf[18] << 8) + 6;
								int skip = buf[14 + 8] + 9; // skip complete pes header
								len -= skip;
								if (ddvd_last_iframe_len == 0) { // add simple pes header without pts
									memcpy(last_iframe, "\x00\x00\x01\xE0\x00\x00\x80\x00\x00", 9);
									ddvd_last_iframe_len += 9;
								}
								memcpy(last_iframe + ddvd_last_iframe_len, buf + 14 + skip, len);
								ddvd_last_iframe_len += len;
							}
						}
					}
					else if ((buf[14 + 3]) == 0xC0 + audio_id) {	// mpeg audio
						if (audio_type != DDVD_MPEG) {
							//Debug(1, "Switch to MPEG Audio\n");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								Perror("AUDIO_SET_AV_SYNC");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1) < 0)
								Perror("AUDIO_SET_BYPASS_MODE");
							audio_type = DDVD_MPEG;
						}

						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							apts |= buf[14 + 10] << 22;
							apts |= (buf[14 + 11] >> 1) << 15;
							apts |= buf[14 + 12] << 7;
							apts |= (buf[14 + 13] >> 1);
							//Debug(1, "APTS=%X\n",(int)apts);
						}

						safe_write(ddvd_ac3_fd, buf + 14, buf[19] + (buf[18] << 8) + 6);
					}
					else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0xA0 + audio_id) {	// lpcm audio
						// autodetect bypass mode
						static int lpcm_mode = -1;
						if (lpcm_mode < 0) {
							lpcm_mode = 6;
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, lpcm_mode) < 0)
								lpcm_mode = 0;
						}

						if (audio_type != DDVD_LPCM) {
							//Debug(1, "Switch to LPCM Audio\n");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								Perror("AUDIO_SET_AV_SYNC");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, lpcm_mode) < 0)
								Perror("AUDIO_SET_BYPASS_MODE");
							audio_type = DDVD_LPCM;
							ddvd_lpcm_count = 0;
						}
						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							apts |= buf[14 + 10] << 22;
							apts |= (buf[14 + 11] >> 1) << 15;
							apts |= buf[14 + 12] << 7;
							apts |= (buf[14 + 13] >> 1);
							//Debug(1, "APTS=%X\n",(int)apts);
						}

						if (lpcm_mode == 0) {
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
							}
							else {
								memcpy(lpcm_data + ddvd_lpcm_count, abuf, i);
								ddvd_lpcm_count += i;
							}
						}
						else
							safe_write(ddvd_ac3_fd, buf + 14 , buf[19] + (buf[18] << 8) + 6);
					}
					else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0x88 + audio_id) {	// dts audio
						if (audio_type != DDVD_DTS) {
							//Debug(1, "Switch to DTS Audio (thru)\n");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								Perror("AUDIO_SET_AV_SYNC");
#ifdef CONVERT_TO_DVB_COMPLIANT_DTS
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 2) < 0)	// DTS (dvb compliant)
#else
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 5) < 0)	// DTS VOB
#endif
								Perror("AUDIO_SET_BYPASS_MODE");
							audio_type = DDVD_DTS;
						}

						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							apts |= buf[14 + 10] << 22;
							apts |= (buf[14 + 11] >> 1) << 15;
							apts |= buf[14 + 12] << 7;
							apts |= (buf[14 + 13] >> 1);
							//Debug(1, "APTS=%X\n",(int)apts);
						}

#ifdef CONVERT_TO_DVB_COMPLIANT_DTS
						unsigned short pes_len = (buf[14 + 4] << 8 | buf[14 + 5]);
						pes_len -= 4;	// strip first 4 bytes of pes payload
						buf[14 + 4] = pes_len >> 8;	// patch pes len
						buf[15 + 4] = pes_len & 0xFF;

						safe_write(ddvd_ac3_fd, buf + 14, 9 + buf[14 + 8]);	// write pes_header
						safe_write(ddvd_ac3_fd, buf + 14 + 9 + buf[14 + 8] + 4, pes_len - (3 + buf[14 + 8]));	// write pes_payload
#else
						safe_write(ddvd_ac3_fd, buf + 14, buf[19] + (buf[18] << 8) + 6);
#endif
					}
					else if ((buf[14 + 3]) == 0xBD && (buf[14 + buf[14 + 8] + 9]) == 0x80 + audio_id) {	// ac3 audio
						if (audio_type != DDVD_AC3) {
							//Debug(1, "Switch to AC3 Audio\n");
							int bypassmode;
							if (ac3thru || !have_liba52) // !have_liba52 and !ac3thru should never happen, but who knows ;)
#ifdef CONVERT_TO_DVB_COMPLIANT_AC3
								bypassmode = 0;
#else
								bypassmode = 3;
#endif
							else
								bypassmode = 1;
							if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
								Perror("AUDIO_SET_AV_SYNC");
							if (ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, bypassmode) < 0)
									Perror("AUDIO_SET_BYPASS_MODE");
							audio_type = DDVD_AC3;
						}

						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							apts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							apts |= buf[14 + 10] << 22;
							apts |= (buf[14 + 11] >> 1) << 15;
							apts |= buf[14 + 12] << 7;
							apts |= (buf[14 + 13] >> 1);
							//Debug(1, "APTS=%X\n",(int)apts);
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
							//fwrite(buf + buf[22] + 27, 1, ((buf[18] << 8) | buf[19]) - buf[22] - 7, fac3); //debugwrite
						}
						else {
							// a bit more funny than lpcm sound, because we do a complete recoding here
							// we will decode the ac3 data to plain lpcm and will then encode to mpeg
							// audio and send them with pts information to the decoder to get a sync.

							// decode and convert ac3 to raw lpcm
							ac3_len = ddvd_ac3_decode(buf + buf[22] + 27, ((buf[18] << 8) | buf[19]) - buf[22] - 7, ac3_tmp);

							// save the pes header incl. PTS
							memcpy(mpa_data, buf + 14, buf[14 + 8] + 9);
							mpa_header_length = buf[14 + 8] + 9;

							//apts -= (((unsigned long long)(ddvd_lpcm_count) * 90) / 192);

							//mpa_data[14] = (int)((apts << 1) & 0xFF);
							//mpa_data[12] = (int)((apts >> 7) & 0xFF);
							//mpa_data[11] = (int)(((apts << 1) >> 15) & 0xFF);
							//mpa_data[10] = (int)((apts >> 22) & 0xFF);

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
					}
					else if ((buf[14 + 3]) == 0xBD && ((buf[14 + buf[14 + 8] + 9]) & 0xE0) == 0x20 && ((buf[14 + buf[14 + 8] + 9]) & 0x1F) == spu_active_id) {	// SPU packet
						Debug(2, "DVD SPU BLOCK: spu_nr=%d/%d vpts=%llu pts=%llu highlight=%d\n", ddvd_spu_play, ddvd_spu_ind, vpts, pts, have_highlight);
						if (buf[14 + 7] & 128) {
							/* damn gcc bug */
							spts = ((unsigned long long)(((buf[14 + 9] >> 1) & 7))) << 30;
							spts |= buf[14 + 10] << 22;
							spts |= (buf[14 + 11] >> 1) << 15;
							spts |= buf[14 + 12] << 7;
							spts |= (buf[14 + 13] >> 1);
#if CONFIG_API_VERSION == 1
							spts >>= 1;	// need a corrected "spts" because vulcan/pallas will give us a 32bit pts instead of 33bit
#endif
							Debug(2, "                                                                 SPTS=%llu  %3d:  %d:%02d:%02d.%05d\n", spts, ddvd_spu_ind, (int)(spts/90000/3600), (int)(spts/90000/60)%60, (int)(spts/90000)%60, (int)(spts%90000)*10/9);
						}

						int i = ddvd_spu_ind % NUM_SPU_BACKBUFFER;
						if (ddvd_spu_ind - ddvd_spu_play >= NUM_SPU_BACKBUFFER) {
							Debug(1, "SPU buffers full, skipping SPU for spts=%llu\n", spu_backpts[i]);
							ddvd_spu_play = ddvd_spu_ind - NUM_SPU_BACKBUFFER + 1;
						}

						int pck_len = 2048 - (buf[22] + 14 + 10);
						if (ddvd_spu_ptr + pck_len > SPU_BUFLEN)
							Debug(1, "SPU frame to long (%d > %d)\n", ddvd_spu_ptr + pck_len, SPU_BUFLEN);
						else {
							memcpy(ddvd_spu[i] + ddvd_spu_ptr, buf + buf[22] + 14 + 10, pck_len);
							ddvd_spu_ptr += pck_len;
						}

						int spulen = ddvd_spu[i][0] << 8 | ddvd_spu[i][1];
						if (ddvd_spu_ptr >= spulen) {	// SPU packet complete ?
							int j = (i - 1) % NUM_SPU_BACKBUFFER;
							if (spu_backpts[j] == spts && ddvd_spu_play < ddvd_spu_ind) {  // same spu. Copy data to previous buffer
								memcpy(ddvd_spu[j], ddvd_spu[i], spulen);
								Debug(1, "SPU duplicate %d, %d\n", ddvd_spu_play, ddvd_spu_ind);
								i = j; // to get proper ddvd_pci index
							}
							else {
								spu_backpts[i] = spts;	// store pts
								ddvd_spu_ind++;
							}
							memcpy(ddvd_pci[i], dvdnav_get_current_nav_pci(dvdnav), sizeof(pci_t));
							ddvd_spu_ptr = 0;
						}
					}
				}
				break;

			case DVDNAV_NOP:
				/* Nothing to do here. */
				break;

			case DVDNAV_STILL_FRAME:
				if (ddvd_iframesend == 0 && ddvd_last_iframe_len)
					ddvd_iframesend = 1;

				if (!ddvd_wait_timer_active && !have_still_event) {
					// Save the still event so it can be processed when it is really time to be displayed!
					// Need only to do so when no wait timer is active!
					memcpy(&still_event, buf, sizeof(dvdnav_still_event_t));
					have_still_event = 1;
					Debug(4, "DVDNAV_STILL_FRAME: lenght=%d vpts=%llu pts=%llu\n", still_event.length, vpts, pts);
				}
				break;

			case DVDNAV_WAIT:
				/* We have reached a point in DVD playback, where timing is critical.
				 * We dont use readahead, so we can simply skip the wait state. */
				dvdnav_wait_skip(dvdnav);
				break;

			case DVDNAV_SPU_CLUT_CHANGE:
				/* We received a new color lookup table so we read and store it */
				{
					Debug(2, "DVDNAV_SPU_CLUT_CHANGE vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
					int i = 0, i2 = 0;
					uint8_t pal[16 * 4];
#if BYTE_ORDER == BIG_ENDIAN
					memcpy(pal, buf, 16 * sizeof(uint32_t));
#else
					for (; i < 16; ++i)
						*(int *)(pal + i * 4) = htonl(*(int *)(buf + i * 4));
					i = 0;
#endif
					// dump buf and pal tables for debug reasons
					//while (i < 16 * 4) {
					//	int y = buf[i + 1];
					//	signed char cr = buf[i + 2];	//v
					//	signed char cb = buf[i + 3];	//u
					//	printf("%d %d %d ->", y, cr, cb);
					//	y = pal[i + 1];
					//	cr = pal[i + 2];	//v
					//	cb = pal[i + 3];	//u
					//	printf(" %d %d %d\n", y, cr, cb);
					//	i += 4;
					//}
					//i = 0;

					while (i < 16 * 4) {
						int y = pal[i + 1];
						signed char cr = pal[i + 2] - 128;	//v
						signed char cb = pal[i + 3] - 128;	//u

						y = 76310 * (y - 16);	//yuv2rgb
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
				Debug(2, "DVDNAV_SPU_STREAM_CHANGE vpts=%llu pts=%llu highlight=%d spu_lock=%d\n", vpts, pts, have_highlight, spu_lock);
				/* We received a new SPU stream ID */
				if (spu_lock)
					break;

				int old_active_id = spu_active_id;
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
				for (spu_index = 0; spu_index < MAX_SPU; spu_index++) {
					if (playerconfig->spu_map[spu_index].logical_id == -1) // gone past last valid spu entry
						break;
					if (playerconfig->spu_map[spu_index].stream_id == spu_active_id & 0x1F) {
						spu_lang = playerconfig->spu_map[spu_index].lang;
						break;
					}
				}
				if (spu_lang == 0xFFFF) {
					spu_lang = 0x2D2D;	// SPU "off, unknown or maybe menuoverlays"
					spu_index = -1;
				}
				if (old_active_id != spu_active_id) {
					Debug(3, "   clr spu frame spu_nr=%d->%d\n", ddvd_spu_play, ddvd_spu_ind);
					ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
				}
				msg = DDVD_SHOWOSD_SUBTITLE;
				safe_write(message_pipe, &msg, sizeof(int));
				safe_write(message_pipe, &spu_index, sizeof(int));
				safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
				Debug(3, "SPU Stream change: w %d l: %d p: %d log: %d spu %d: active=%d prevactive=%d spu_lang=%04X %c%c\n", ev->physical_wide, ev->physical_letterbox, ev->physical_pan_scan, ev->logical, spu_index, spu_active_id, old_active_id, spu_lang, spu_lang >> 8, spu_lang & 0xFF);
				spu_active_id &= 0x1F;
				playerconfig->last_spu_id = spu_index;
				break;

			case DVDNAV_AUDIO_STREAM_CHANGE:
				/* We received a new Audio stream ID  */
				if (!audio_lock) {
					audio_id = dvdnav_get_active_audio_stream(dvdnav);
					report_audio_info = 1;
				}
				break;

			case DVDNAV_HIGHLIGHT:
				{
					/* Prepare to display some Buttons */
					dvdnav_highlight_event_t * hl = (dvdnav_highlight_event_t *) buf;
					Debug(2, "DVDNAV_HIGHLIGHT vpts=%llu pts=%llu highlight=%d button=%d mode=%d, bpts=%u%s\n",
								vpts, pts, have_highlight, hl->buttonN, hl->display, hl->pts,
								(highlight_event.buttonN == hl->buttonN && highlight_event.pts == hl->pts) ?
								 " -- probably same as previous" : "");
					memcpy(&highlight_event, buf, sizeof(dvdnav_highlight_event_t));
					have_highlight = 1;
					break;
				}

			case DVDNAV_VTS_CHANGE:
				{
					Debug(2, "DVDNAV_VTS_CHANGE vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
					/* Some status information like video aspect and video scale permissions do
					 * not change inside a VTS. Therefore we will set it new at this place */
					ddvd_play_empty(FALSE);
					audio_lock = 0;	// reset audio & spu lock
					spu_lock = 0;
					for (i = 0; i < MAX_AUDIO; i++)
						playerconfig->audio_format[i] = -1;
					// fill spu_map with data
					int logical_spu, stream_spu;
					for (i = 0; i < MAX_SPU; i++)
						playerconfig->spu_map[i].logical_id = playerconfig->spu_map[i].stream_id = playerconfig->spu_map[i].lang = -1;
					i = 0;
					spu_index = -1;
					for (logical_spu = 0; logical_spu < MAX_SPU; logical_spu++) {
						stream_spu = dvdnav_get_spu_logical_stream(dvdnav, logical_spu);
						if (stream_spu >= 0 && stream_spu < MAX_SPU) {
							playerconfig->spu_map[i].logical_id = logical_spu;
							playerconfig->spu_map[i].stream_id = stream_spu;
							int lang = dvdnav_spu_stream_to_lang(dvdnav, logical_spu);
							playerconfig->spu_map[i].lang = lang;
#if FORCE_DEFAULT_SPULANG
							if (spu_index == -1 && (lang >> 8) == playerconfig->language[0] && (lang & 0xff) == playerconfig->language[1]) {
								spu_index = i;
								msg = DDVD_SHOWOSD_SUBTITLE;
								safe_write(message_pipe, &msg, sizeof(int));
								safe_write(message_pipe, &spu_index, sizeof(int));
								safe_write(message_pipe, &lang, sizeof(uint16_t));
							}
#endif
							Debug(2, "    %d: MPEG spu stream %d -> logical stream %d - %04X %c%c\n", i, stream_spu, logical_spu, lang,
												lang == 0xFFFF ? 'N' : lang >> 8,
												lang == 0xFFFF ? 'A' : lang & 0xFF);
							i++;
						}
					}
					if (spu_index != -1) {
						spu_active_id = playerconfig->spu_map[spu_index].stream_id;
						spu_lock = 1;
						Debug(3, "    Try setting SPU to %s -> spu_active=%d\n", playerconfig->language, spu_active_id);
					}
					playerconfig->last_spu_id = spu_index;

					dvd_aspect = dvdnav_get_video_aspect(dvdnav);
					dvd_scale_perm = dvdnav_get_video_scale_permission(dvdnav);
					tv_scale = ddvd_check_aspect(dvd_aspect, dvd_scale_perm, tv_aspect, tv_mode);
					Debug(3, "    DVD Aspect: %d TV Aspect: %d TV Scale: %d Allowed: %d TV Mode: %d, wanted langage: %c%c\n",
								dvd_aspect, tv_aspect, tv_scale, dvd_scale_perm, tv_mode,
								playerconfig->language[0], playerconfig->language[1]);

					first_vts_change = 0; // After the first VTS resuming can commence
				}
				break;

			case DVDNAV_CELL_CHANGE:
				{
					Debug(2, "DVDNAV_CELL_CHANGE vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
					/* Store new cell information */
					memcpy(&ddvd_lastCellEventInfo, buf, sizeof(dvdnav_cell_change_event_t));

					if ((ddvd_still_frame & CELL_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
						ddvd_iframesend = 1;

					ddvd_still_frame = (dvdnav_get_next_still_flag(dvdnav) != 0) ? CELL_STILL : 0;

					// resuming a dvd ?
					if (playerconfig->should_resume && next_cell_change) {
						if (dvdnav_sector_search(dvdnav, playerconfig->resume_block, SEEK_SET) == DVDNAV_STATUS_OK) {
							Debug(3, "    resuming to block %d\n", playerconfig->resume_block);
							audio_id = playerconfig->resume_audio_id;
							audio_lock = 1;//playerconfig->resume_audio_lock;
							spu_active_id = playerconfig->resume_spu_id;
							spu_lock = 1;//playerconfig->resume_spu_lock;
							report_audio_info = 1;
							uint16_t spu_lang = 0xFFFF;
							int i;
							for (i = 0; i < MAX_SPU; i++) {
								if (playerconfig->spu_map[i].logical_id == -1) // past spu entries
									break;
								if (playerconfig->spu_map[i].stream_id == spu_active_id) {
									spu_lang = playerconfig->spu_map[i].lang;
									spu_index = i;
									break;
								}
							}

							if (spu_lang == 0xFFFF) {
								spu_lang = 0x2D2D;	// SPU "off"
								spu_active_id = -1;
								spu_index = -1;
							}
							playerconfig->last_spu_id = spu_index;
							msg = DDVD_SHOWOSD_SUBTITLE;
							safe_write(message_pipe, &msg, sizeof(int));
							safe_write(message_pipe, &spu_index, sizeof(int));
							safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
							msg = DDVD_SHOWOSD_TIME; // send new position to the frontend
						}
						else
							Debug(2, "    resume failed: cannot find/seek to block %d\n", playerconfig->resume_block);

						// avoid subsequent resumes
						next_cell_change = 0;
						playerconfig->should_resume = 0;
						playerconfig->resume_title = 0;
						playerconfig->resume_chapter = 0;
						playerconfig->resume_block = 0;
						playerconfig->resume_audio_id = 0;
						playerconfig->resume_audio_lock = 0;
						playerconfig->resume_spu_id = 0;
						playerconfig->resume_spu_lock = 0;
					}
					// multiple angles ?
					int num = 0, current = 0;
					dvdnav_get_angle_info(dvdnav, &current, &num);
					msg = DDVD_SHOWOSD_ANGLE;
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &current, sizeof(int));
					safe_write(message_pipe, &num, sizeof(int));
				}
				break;

			case DVDNAV_NAV_PACKET:
				/* A NAV packet provides PTS discontinuity information, angle linking information and
				 * button definitions for DVD menus. We have to handle some stilframes here */
				// Debug(3, "DVDNAV_NAV_PACKJET vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
				if ((ddvd_still_frame & NAV_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
					ddvd_iframesend = 1;

				dsi = dvdnav_get_current_nav_dsi(dvdnav);
				if (dsi->vobu_sri.next_video == 0xbfffffff)
					ddvd_still_frame |= NAV_STILL;	//|= 1;
				else
					ddvd_still_frame &= ~NAV_STILL;	//&= 1;
				break;

			case DVDNAV_HOP_CHANNEL:
				/* This event is issued whenever a non-seamless operation has been executed.
				 * So we drop our buffers */
				Debug(2, "DVDNAV_HOP_CHANNEL vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
				ddvd_play_empty(TRUE);
				break;

			case DVDNAV_STOP:
				/* Playback should end here. */
				Debug(2, "DVDNAV_STOP\n");
				playerconfig->should_resume = 0;
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
				Debug(1, "DVDNAV_Unknown event (%i)\n", event);
				finished = 1;
				break;
			}
		}

		// resuming a dvd ?
		if (playerconfig->should_resume && !first_vts_change && !next_cell_change) {
			int title_numbers, part_numbers;
			dvdnav_get_number_of_titles(dvdnav, &title_numbers);
			dvdnav_get_number_of_parts(dvdnav, playerconfig->resume_title, &part_numbers);
			if (playerconfig->resume_title   > 0 && playerconfig->resume_title   <= title_numbers &&
				playerconfig->resume_chapter > 0 && playerconfig->resume_chapter <= part_numbers) {
				dvdnav_part_play(dvdnav, playerconfig->resume_title, playerconfig->resume_chapter);
				next_cell_change = 1;
				Debug(3, "Resuming after first vts: going to chapter/title (%d/%d)\n",
                               playerconfig->resume_title, playerconfig->resume_chapter);
			}
			else {
				playerconfig->should_resume = 0;
				playerconfig->resume_title = 0;
				playerconfig->resume_chapter = 0;
				playerconfig->resume_block = 0;
				playerconfig->resume_audio_id = 0;
				playerconfig->resume_audio_lock = 0;
				playerconfig->resume_spu_id = 0;
				playerconfig->resume_spu_lock = 0;
				Debug(2, "Resume failed after first vts: chapter/title (%d/%d) out of bounds\n",
                               playerconfig->resume_title, playerconfig->resume_chapter);
			}
		}

		// spu and highlight/button handling
		unsigned long long spupts = spu_backpts[ddvd_spu_play % NUM_SPU_BACKBUFFER];
#if CONFIG_API_VERSION == 1
		unsigned int tpts;
		if (ioctl(ddvd_output_fd, VIDEO_GET_PTS, &tpts) < 0)
			Perror("VIDEO_GET_PTS");
		pts = (unsigned long long)tpts;
		// we only have a 32bit pts on vulcan/pallas (instead of 33bit) so we need some
		// tolerance on syncing SPU for menus so on non animated menus the buttons will
		// be displayed to soon, but we we have to accept it
		signed long long spudiff = pts - spupts + 255;
#else
		struct video_event event;
		if (!ioctl(ddvd_fdvideo, VIDEO_GET_EVENT, &event)) {
			switch(event.type) {
				case VIDEO_EVENT_SIZE_CHANGED:
				{
					struct ddvd_size_evt evt;
					int msg = DDVD_SIZE_CHANGED;
					evt.width = event.u.size.w;
					evt.height = event.u.size.h;
					evt.aspect = event.u.size.aspect_ratio;
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &evt, sizeof(evt));
					Debug(3, "video size: %dx%d@%d\n", evt.width, evt.height, evt.aspect);
					break;
				}
				case VIDEO_EVENT_FRAME_RATE_CHANGED:
				{
					struct ddvd_framerate_evt evt;
					int msg = DDVD_FRAMERATE_CHANGED;
					evt.framerate = event.u.frame_rate;
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &evt, sizeof(evt));
					Debug(3, "framerate: %d\n", evt.framerate);
					break;
				}
				case 16: // VIDEO_EVENT_PROGRESSIVE_CHANGED
				{
					struct ddvd_progressive_evt evt;
					int msg = DDVD_PROGRESSIVE_CHANGED;
					evt.progressive = event.u.frame_rate;
					safe_write(message_pipe, &msg, sizeof(int));
					safe_write(message_pipe, &evt, sizeof(evt));
					Debug(3, "progressive: %d\n", evt.progressive);
					break;
				}
			}
		}
		if (ioctl(ddvd_fdvideo, VIDEO_GET_PTS, &pts) < 0)
			Perror("VIDEO_GET_PTS");
		// pts+10 to avoid decoder time rounding errors. Seen vpts=11555 and pts=11554 ...
		signed long long spudiff = pts+10 - spupts;
#endif
		if (ddvd_playmode & STEP && pts > steppts) { // finish step
			if (ioctl(ddvd_fdaudio, AUDIO_PAUSE) < 0)
				Perror("AUDIO_PAUSE");
			if (ioctl(ddvd_fdvideo, VIDEO_FREEZE) < 0)
				Perror("VIDEO_FREEZE");
			Debug(3, "STEP mode done: go to PAUSE on %lld now %lld diff %lld %d:%02d:%02d/%02d\n", steppts, pts, pts - steppts,
					(int)(pts/90000/3600), (int)(pts/90000/60)%60, (int)(pts/90000)%60,
					(int)((pts%90000 + 1) * playerconfig->last_framerate.framerate / 90000 / 1000));
			ddvd_playmode = PAUSE;
			msg = DDVD_SHOWOSD_STATE_PAUSE;
			safe_write(message_pipe, &msg, sizeof(int));
			ddvd_wait_for_user = 1; // don't waste cpu during pause
		}

		if (have_still_event && ddvd_iframesend <= 0 && pts <= vpts + 3600) {
			/* It seems stills have a separate vpts (e.g. starting from 0 again, or a separate PGC)
			 * Reached the time for a still frame. Start a timer to wait the amount of time specified by the
			 * still's length while still handling user input to make menus and other interactive stills work.
			 * A length of 0xff means an indefinite still which has to be skipped indirectly by some user interaction.
			 */
			if (!ddvd_wait_timer_active)
				Debug(2, "DVDNAV_STILL_FRAME: activate: length=%d vpts=%llu pts=%llu\n", still_event.length, vpts, pts);
			if (still_event.length < 0xff) {
				if (!ddvd_wait_timer_active) {
					ddvd_wait_timer_active = 1;
					ddvd_wait_timer_end = now + still_event.length * 1000; //ms
				}
			}
			else
				ddvd_wait_for_user = 1;
			have_still_event = 0;
		}
		/*
		 * When vpts > pts we are still in a normal stream so check on spudif is enough.
		 * But when vpts < pts, libdvdnav already is working on a new video fragment (PGC) possible sending out PSU.
		 * In that case we should only display the SPU if the spupts is still from the previous video (spupts > vpts)
		 * and the SPU is within 2 seconds of the pts (spudiff < 2*90000).
		 * (FIXME: why do we need this last check?, maybe doing this last check is just enough...)
		 * Or when vpts < pts check that the previous_spupts < spupts ...
		 */
		if (ddvd_spu_play < ddvd_spu_ind && spudiff >= 0 && (vpts > pts || spupts+5 > vpts && spudiff < 2*90000)) {
			memset(ddvd_lbb, 0, 720 * 576); // Clear decode buffer
			cur_spu_return = ddvd_spu_decode_data(ddvd_lbb, ddvd_spu[ddvd_spu_play % NUM_SPU_BACKBUFFER], spupts); // decode
			pci = ddvd_pci[ddvd_spu_play % NUM_SPU_BACKBUFFER];
			Debug(2, "SPU current=%d pts=%llu spupts=%llu bbox: %dx%d %dx%d btns=%d highlight=%d displaytime=%d %s\n",
				ddvd_spu_play, pts, spupts,
				cur_spu_return.x_start, cur_spu_return.y_start,
				cur_spu_return.x_end, cur_spu_return.y_end,
				pci->hli.hl_gi.btn_ns, have_highlight, cur_spu_return.display_time,
				cur_spu_return.force_hide == SPU_HIDE ? "hide" : cur_spu_return.force_hide == SPU_FORCE ? "force" : "show");
			ddvd_spu_play++;

			// process spu data
			if (cur_spu_return.force_hide == SPU_FORCE) {
				// highlight/button
				int buttonN;
				if (!have_highlight) {
					// got a Highlight SPU but no highlight event yet. Force getting one
					dvdnav_get_current_highlight(dvdnav, &buttonN);
					if (buttonN == 0)
						buttonN = 1;
					if (buttonN > pci->hli.hl_gi.btn_ns)
						buttonN = pci->hli.hl_gi.btn_ns;
					dvdnav_button_select(dvdnav, pci, buttonN);
					ddvd_wait_highlight = 1; // still frame might already have set 'wait_for_user'. This will first wait for the highlight to be drawn
					Debug(2, "FORCE highlight button %d\n", buttonN);
				}
				else
					buttonN = highlight_event.buttonN;
				in_menu = 1;
				Debug(2, "Update highlight buttons - %d of %d, switching to menu\n", buttonN, pci->hli.hl_gi.btn_ns);
			}
			else if (cur_spu_return.force_hide == SPU_SHOW) {
				// subtitle
				// overlapping spu timers not supported yet, so clear the screen in that case
				if (ddvd_spu_timer_active || last_spu_return.display_time < 0) {
					ddvd_clear_screen = 1;
					Debug(3, "clear p_lfb, physical screen, new SPU, vpts=%llu pts=%llu spts=%llu highlight=%d spu_timer_active=%d lastsputime=%d\n", vpts, pts, spupts, have_highlight, ddvd_spu_timer_active, last_spu_return.display_time);
				}
				// dont display SPU if displaytime is <= 0 or the actual SPU track is marked as hide (bit 7)
				if (cur_spu_return.display_time <= 0 || ((dvdnav_get_active_spu_stream(dvdnav) & 0x80) && !spu_lock)) {
					ddvd_spu_timer_active = 0;
					Debug(2, "do not display this spu: active stream=%u spulock=%d\n", dvdnav_get_active_spu_stream(dvdnav), spu_lock);
				}
				else {
					// set timer and prepare backbuffer
					ddvd_spu_timer_active = 1;
					ddvd_spu_timer_end = now + cur_spu_return.display_time * 10; //ms
					Debug(3, "    drawing subtitle, vpts=%llu pts=%llu highlight=%d\n", vpts, pts, have_highlight);
					if (ddvd_screeninfo_bypp == 1) {
						struct ddvd_color colnew;
						int ctmp;
						msg = DDVD_COLORTABLE_UPDATE;
						safe_write(message_pipe, &msg, sizeof(int));
						for (ctmp = 0; ctmp < 4; ctmp++) {
							colnew.blue = ddvd_bl[ctmp + 252];
							colnew.green = ddvd_gn[ctmp + 252];
							colnew.red = ddvd_rd[ctmp + 252];
							colnew.trans = ddvd_tr[ctmp + 252];
							safe_write(message_pipe, &colnew, sizeof(struct ddvd_color));
						}
						msg = DDVD_NULL;
						memcpy(ddvd_lbb2, ddvd_lbb, 720 * 576);
					}
					else {
						ddvd_resize_pixmap = (ddvd_screeninfo_xres > 720) ?    // Set resize function
										&ddvd_resize_pixmap_xbpp : &ddvd_resize_pixmap_xbpp_smooth;
						memset(ddvd_lbb2, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);    // Clear backbuffer ..
						int i = 0;
						for (i = cur_spu_return.y_start; i < cur_spu_return.y_end; ++i)
							ddvd_blit_to_argb(ddvd_lbb2 + (i * 720 + cur_spu_return.x_start) * ddvd_screeninfo_bypp,
												ddvd_lbb + i * 720 + cur_spu_return.x_start,
												cur_spu_return.x_end - cur_spu_return.x_start);
					}

					blit_area.x_start = cur_spu_return.x_start;
					blit_area.x_end = cur_spu_return.x_end;
					blit_area.y_start = cur_spu_return.y_start;
					blit_area.y_end = cur_spu_return.y_end;

					draw_osd = 1;
				}
			}
			else // if (cur_spu_return.force_hide == SPU_HIDE)
				Debug(2, "Weird: SPU_HIDE as base SPU type. Expect this only as part of a SPU_SHOW packet!!!\n");
		}

		// highlight/button handling
		if (in_menu && have_highlight) {
			Debug(3, "HIGHLIGHT DRAW Selected button=%d mode=%d, bpts=%u vpts=%llu pts=%llu\n", highlight_event.buttonN, highlight_event.display, highlight_event.pts, vpts, pts);
			dvdnav_highlight_area_t hl;
			have_highlight = 0;
			ddvd_wait_highlight = 0; // no need to hold 'wait_for_user' any longer
			ddvd_clear_screen = 1;
			blit_area.x_start = blit_area.x_end = blit_area.y_start = blit_area.y_end = 0;

			if (!pci) // should not happen, is set when SPU is processed
				pci = dvdnav_get_current_nav_pci(dvdnav);

			btni_t *btni = NULL;
			if (pci->hli.hl_gi.btngr_ns) {
				Debug(3, "DOBUTTON %d data from buttongroup\n", highlight_event.buttonN);
				int btns_per_group = 36 / pci->hli.hl_gi.btngr_ns;
				int modeMask = 1 << tv_scale;

				if (     pci->hli.hl_gi.btngr_ns >= 1 && (pci->hli.hl_gi.btngr1_dsp_ty & modeMask))
					btni = &pci->hli.btnit[0 * btns_per_group + highlight_event.buttonN - 1];
				else if (pci->hli.hl_gi.btngr_ns >= 2 && (pci->hli.hl_gi.btngr2_dsp_ty & modeMask))
					btni = &pci->hli.btnit[1 * btns_per_group + highlight_event.buttonN - 1];
				else if (pci->hli.hl_gi.btngr_ns >= 3 && (pci->hli.hl_gi.btngr3_dsp_ty & modeMask))
					btni = &pci->hli.btnit[2 * btns_per_group + highlight_event.buttonN - 1];
				else
					btni = &pci->hli.btnit[highlight_event.buttonN - 1];
			}
			if (btni) {
				Debug(3, "DOBUTTON btni\n");
				hl.palette = btni->btn_coln == 0 ? 0 : pci->hli.btn_colit.btn_coli[btni->btn_coln - 1][0];
				hl.sy = btni->y_start;
				hl.ey = btni->y_end;
				hl.sx = btni->x_start;
				hl.ex = btni->x_end;

				// get and set clut for actual button
				int i;
				if (ddvd_screeninfo_bypp == 1) {
					msg = DDVD_COLORTABLE_UPDATE;
					safe_write(message_pipe, &msg, sizeof(int));
				}
				else
					ddvd_resize_pixmap = &ddvd_resize_pixmap_xbpp; // set resize function

				//CHANGE COLORMAP from highlight data, used in ddvd_blit_to_argb()
				for (i = 0; i < 4; i++) {
					unsigned char tmp, tmp2;
					struct ddvd_color colnew;
					tmp = ((hl.palette) >> (16 + 4 * i)) & 0xf;
					tmp2 = ((hl.palette) >> (4 * i)) & 0xf;
					colnew.blue = ddvd_bl[i + 252] = ddvd_bl[tmp];
					colnew.green = ddvd_gn[i + 252] = ddvd_gn[tmp];
					colnew.red = ddvd_rd[i + 252] = ddvd_rd[tmp];
					colnew.trans = ddvd_tr[i + 252] = (0xF - tmp2) * 0x1111;
					if (ddvd_screeninfo_bypp == 1)
						safe_write(message_pipe, &colnew, sizeof(struct ddvd_color));
				}
				msg = DDVD_NULL;

				memset(ddvd_lbb2, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear backbuffer ..
				Debug(4, "        clear ddvd_lbb2, backbuffer, new button to come\n");
				//copy button into screen
				for (i = hl.sy; i < hl.ey; i++) {
					if (ddvd_screeninfo_bypp == 1)
						memcpy(ddvd_lbb2 + hl.sx + 720 * i,
								ddvd_lbb + hl.sx + 720 * i, hl.ex - hl.sx);
					else
						ddvd_blit_to_argb(ddvd_lbb2 + (hl.sx + 720 * i) * ddvd_screeninfo_bypp,
											ddvd_lbb + hl.sx + 720 * i, hl.ex - hl.sx);
				}
				blit_area.x_start = hl.sx;
				blit_area.x_end = hl.ex;
				blit_area.y_start = hl.sy;
				blit_area.y_end = hl.ey;
				draw_osd = 1;
				Debug(3, "BUT new bbox: %dx%d %dx%d\n",
						blit_area.x_start, blit_area.y_start,
						blit_area.x_end, blit_area.y_end);
			}
			else {
				Debug(3, "DOBUTTON no info - no drawing!\n");
				draw_osd = 0;
			}
		}

		if (ddvd_clear_screen) {
			Debug(3, "DODRAW DRAW clear screen area: %dx%d %dx%d\n",
					last_blit_area.x_start, last_blit_area.y_start,
					last_blit_area.x_end, last_blit_area.y_end);
			memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);	//clear screen ..
			msg = DDVD_SCREEN_UPDATE;
			safe_write(message_pipe, &msg, sizeof(int));
			safe_write(message_pipe, &last_blit_area, sizeof(struct ddvd_resize_return));
			ddvd_clear_screen = 0;
		}
		if (draw_osd) {
			Debug(3, "DODRAW DRAW button/subtitle at: %dx%d %dx%d\n", blit_area.x_start, blit_area.y_start, blit_area.x_end, blit_area.y_end);
			int y_source = ddvd_have_ntsc ? 480 : 576; // correct ntsc overlay
			int x_offset = calc_x_scale_offset(dvd_aspect, tv_mode, tv_mode2, tv_aspect);
			int y_offset = calc_y_scale_offset(dvd_aspect, tv_mode, tv_mode2, tv_aspect);
			int resized = 0;

			if ((x_offset != 0 || y_offset != 0 || y_source != ddvd_screeninfo_yres ||
				ddvd_screeninfo_xres != 720) && !playerconfig->canscale) {
				// decide which resize routine we should use
				// on 4bpp mode we use bicubic resize for sd skins because we get much better results
				// with subtitles and the speed is ok for hd skins we use nearest neighbor resize because
				// upscaling to hd is too slow with bicubic resize
				//uint64_t start = ddvd_get_time(); // only to print resize stats later on
				resized = 1;
				blit_area = ddvd_resize_pixmap(ddvd_lbb2, 720, y_source, ddvd_screeninfo_xres, ddvd_screeninfo_yres,
												x_offset, y_offset, blit_area.x_start, blit_area.x_end,
												blit_area.y_start, blit_area.y_end, ddvd_screeninfo_bypp);
				//Debug(4, "needed time for resizing: %d ms\n", (int)(ddvd_get_time() - start));
				Debug(4, "resized to: %dx%d %dx%d\n", blit_area.x_start, blit_area.y_start, blit_area.x_end, blit_area.y_end);
			}

			if (resized) {
				blit_area.x_offset = 0;
				blit_area.y_offset = 0;
				blit_area.width = 720;
				blit_area.height = 576;
			}
			else {
				blit_area.x_offset = x_offset;
				blit_area.y_offset = y_offset;
				blit_area.width = ddvd_screeninfo_xres;
				blit_area.height = ddvd_screeninfo_yres;
			}
			memcpy(p_lfb, ddvd_lbb2, ddvd_screeninfo_xres * ddvd_screeninfo_yres * ddvd_screeninfo_bypp); //copy backbuffer into screen
			Debug(4, "fill p_lfb from ddvd_lbb2, backbuffer with new button/subtitle\n");
			int msg_old = msg; // Save and restore msg it may not be empty
			msg = DDVD_SCREEN_UPDATE;
			safe_write(message_pipe, &msg, sizeof(int));
			safe_write(message_pipe, &blit_area, sizeof(struct ddvd_resize_return));
			memcpy(&last_blit_area, &blit_area, sizeof(struct ddvd_resize_return)); // safe blit area for next wipe
			msg = msg_old;
			draw_osd = 0;
			last_spu_return = cur_spu_return;
		}

		// final menu status check
		if (in_menu && !playerconfig->in_menu && (dvdnav_is_domain_vmgm(dvdnav) || dvdnav_is_domain_vtsm(dvdnav))) {
			int bla = DDVD_MENU_OPENED;
			safe_write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu = 1;
			Debug(3, "MENU_OPENED vpts=%llu, pts=%llu highlight=%d!!!\n", vpts, pts, have_highlight);
		}
		else if (playerconfig->in_menu && !(dvdnav_is_domain_vmgm(dvdnav) || dvdnav_is_domain_vtsm(dvdnav))) {
			int bla = DDVD_MENU_CLOSED;
			safe_write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu = 0;
			in_menu = 0;
			Debug(3, "MENU_CLOSED vpts=%llu, pts=%llu highlight=%d!!!\n", vpts, pts, have_highlight);
		}

		// report audio info
		if (report_audio_info) {
			if (playerconfig->audio_format[audio_id] > -1) {
				uint16_t audio_lang = 0xFFFF;
				int audio_id_logical;
				audio_id_logical = dvdnav_get_audio_logical_stream(dvdnav, audio_id);
				audio_lang = dvdnav_audio_stream_to_lang(dvdnav, audio_id_logical);
				if (audio_lang == 0xFFFF)
					audio_lang = 0x2D2D;
				int msg_old = msg; // Save and restore msg it may not bee empty
				msg = DDVD_SHOWOSD_AUDIO;
				safe_write(message_pipe, &msg, sizeof(int));
				safe_write(message_pipe, &audio_id, sizeof(int));
				safe_write(message_pipe, &audio_lang, sizeof(uint16_t));
				safe_write(message_pipe, &playerconfig->audio_format[audio_id], sizeof(int));
				msg = msg_old;
				report_audio_info = 0;
			}
		}

		//Userinput
		if (ddvd_wait_for_user && !ddvd_wait_highlight) {
			Debug(3, "Waiting for keypress - %spaused, vpts=%llu pts=%llu menu=%d playermenu=%d\n", ddvd_playmode & PAUSE ? "" : "not ", vpts, pts, in_menu, playerconfig->in_menu);
			struct pollfd pfd[1];	// Make new pollfd array
			pfd[0].fd = key_pipe;
			pfd[0].events = POLLIN | POLLPRI | POLLERR;
			poll(pfd, 1, -1);
			if (!(ddvd_playmode & PAUSE)) // start looping again
				ddvd_wait_for_user = 0;
		}
		if (ddvd_readpipe(key_pipe, &rccode, sizeof(int), 0) == sizeof(int)) {
			int keydone = 1;
			Debug(2, "Got key %d menu %d playermenu %d\n", rccode, in_menu, playerconfig->in_menu);
			switch (rccode) { // Actions inside and outside of menu
				case DDVD_SET_MUTE:
					ismute = 1;
					break;
				case DDVD_UNSET_MUTE:
					ismute = 0;
					break;
				case DDVD_KEY_MENU: // Dream
				case DDVD_KEY_AUDIOMENU: // Audio
					if (dvdnav_menu_call(dvdnav, rccode == DDVD_KEY_MENU ? DVD_MENU_Root : DVD_MENU_Audio) == DVDNAV_STATUS_OK) {
						ddvd_play_empty(TRUE);
						ddvd_spu_play = ddvd_spu_ind; // Skip remaining subtitles
						ddvd_playmode = PLAY;
						goto key_play;
					}
					break;
				default:
					keydone = 0;
					break;
			}
			if (ddvd_playmode & PAUSE) {
				switch (rccode) {    // Actions in PAUSE mode
					case DDVD_KEY_PLAY:
					case DDVD_KEY_PAUSE:
					case DDVD_KEY_EXIT:
					case DDVD_KEY_OK:
					case DDVD_KEY_SLOWFWD:
					case DDVD_KEY_SLOWBWD:
						break;
					case DDVD_KEY_UP:
					case DDVD_KEY_DOWN:
					case DDVD_KEY_PREV_CHAPTER: // <
					case DDVD_KEY_NEXT_CHAPTER: // >
						// Unpause for a while (two to three frames) and pause again
						steppts = pts;
						Debug(3, "STEP mode on. play till %lld now %lld\n", steppts, pts);
						msg = DDVD_SHOWOSD_STATE_PLAY;
						safe_write(message_pipe, &msg, sizeof(int));
						ddvd_wait_for_user = 0;
						ddvd_playmode = STEP;
						keydone = 1;
						if (ioctl(ddvd_fdaudio, AUDIO_CONTINUE) < 0)
							Perror("AUDIO_CONTINUE");
						if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
							Perror("VIDEO_CONTINUE");
						break;
					case DDVD_KEY_FASTFWD:
					case DDVD_KEY_FASTBWD:
					case DDVD_SKIP_FWD:
					case DDVD_SKIP_BWD:
					case DDVD_SET_TITLE:
					case DDVD_SET_CHAPTER:
						// we must empty the pipe here... and fall through
						ddvd_readpipe(key_pipe, &keydone, sizeof(int), 1);
					default:
						keydone = 1;
						break;
				}
			}

			if (!keydone && playerconfig->in_menu) {
				if (!pci) // should not happen! Must be set when SPU is processed
					pci = dvdnav_get_current_nav_pci(dvdnav);
				switch (rccode) {	// Actions inside a Menu
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
						Debug(3, "'OK' clear screen, clear buttons\n");
						ddvd_wait_for_user = 0;
						ddvd_play_empty(TRUE);
						if (ddvd_wait_timer_active)
							ddvd_wait_timer_active = 0;
						dvdnav_button_activate(dvdnav, pci);
						in_menu = 0; // expext a new SPU packet to enable menus again
						break;
					case DDVD_KEY_EXIT:	//Exit
						Debug(3, "DDVD_KEY_EXIT (menu)\n");
						playerconfig->should_resume = 0;
						playerconfig->resume_title = 0;
						playerconfig->resume_chapter = 0;
						playerconfig->resume_block = 0;
						playerconfig->resume_audio_id = 0;
						playerconfig->resume_audio_lock = 0;
						playerconfig->resume_spu_id = 0;
						playerconfig->resume_spu_lock = 0;
						finished = 1;
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
			}
			else if (!keydone) {	// Actions inside a Movie
				switch (rccode) {	// Main Actions
					case DDVD_SET_CHAPTER:
					case DDVD_KEY_PREV_CHAPTER:	// <
					case DDVD_KEY_NEXT_CHAPTER:	// >
					case DDVD_KEY_LEFT:
					case DDVD_KEY_RIGHT:
					{
						int titleNo, totalChapters, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &totalChapters);
						if (rccode == DDVD_SET_CHAPTER)
							ddvd_readpipe(key_pipe, &chapterNo, sizeof(int), 1);
						else if (rccode == DDVD_KEY_PREV_CHAPTER || rccode == DDVD_KEY_LEFT)
							chapterNo--;
						else
							chapterNo++;
						if (chapterNo > totalChapters)
							chapterNo = 1;
						if (chapterNo <= 0)
							chapterNo = totalChapters;
						Debug(1, "DDVD_SET_CHAPTER %d/%d in title %d\n", chapterNo, totalChapters, titleNo);
						ddvd_play_empty(TRUE);
						dvdnav_part_play(dvdnav, titleNo, chapterNo);
						msg = DDVD_SHOWOSD_TIME;
						Debug(1, "                 clr spu frame spu_nr=%d->%d\n", ddvd_spu_play, ddvd_spu_ind);
						ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
						break;
					}
					case DDVD_SET_TITLE:
					case DDVD_KEY_PREV_TITLE:
					case DDVD_KEY_NEXT_TITLE:
					case DDVD_KEY_DOWN:
					case DDVD_KEY_UP:
					{
						int titleNo, totalTitles, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_titles(dvdnav, &totalTitles);
						if (rccode == DDVD_SET_TITLE)
							ddvd_readpipe(key_pipe, &titleNo, sizeof(int), 1);
						else if (rccode == DDVD_KEY_PREV_TITLE || rccode == DDVD_KEY_DOWN)
							titleNo--;
						else
							titleNo++;
						if (titleNo > totalTitles)
							titleNo = 1;
						if (titleNo <= 0)
							titleNo = totalTitles;
						Debug(1, "DDVD_SET_TITLE %d/%d\n", titleNo, totalTitles);
						ddvd_play_empty(TRUE);
						dvdnav_part_play(dvdnav, titleNo, 1);
						msg = DDVD_SHOWOSD_TIME;
						Debug(1, "                 clr spu frame spu_nr=%d->%d\n", ddvd_spu_play, ddvd_spu_ind);
						ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
						break;
					}
					case DDVD_KEY_PAUSE:	// Pause
					{
						Debug(3, "DDVD_KEY_PAUSE on %s\n", ddvd_playmode & PLAY ? "play" : "pause");
						if (ddvd_playmode == PLAY) { // no bitfield check, need real pause here
							ddvd_playmode = PAUSE;
							if (ioctl(ddvd_fdaudio, AUDIO_PAUSE) < 0)
								Perror("AUDIO_PAUSE");
							if (ioctl(ddvd_fdvideo, VIDEO_FREEZE) < 0)
								Perror("VIDEO_FREEZE");
							if (ddvd_trickmode != TOFF) {
								if (ddvd_trickmode & (FASTFW|FASTBW))
									if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, 0))
										Perror("VIDEO_FAST_FORWARD");
								if (ddvd_trickmode & (SLOWFW|SLOWBW))
									if (ioctl(ddvd_fdvideo, VIDEO_SLOWMOTION, 0) < 0)
										Perror("VIDEO_SLOWMOTION");
								if (!ismute)
									if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 0) < 0)
										Perror("AUDIO_SET_MUTE");
								ddvd_trickmode = TOFF;
								ddvd_trickspeed = 0;
							}

							msg = DDVD_SHOWOSD_STATE_PAUSE;
							safe_write(message_pipe, &msg, sizeof(int));
							ddvd_wait_for_user = 1; // don't waste cpu during pause
							break;
						}
						else if (ddvd_playmode != PAUSE) // no bitfield check, need may be play+pause
							break;
						// fall through to PLAY
					}
					case DDVD_KEY_PLAY:	// Play
					{
						Debug(3, "DDVD_KEY_PLAY on %s\n", ddvd_playmode & PLAY ? "play" : "pause fallthrough");
						if (ddvd_playmode & PAUSE || ddvd_trickmode != TOFF) {
							if (ddvd_playmode & PAUSE)
								ddvd_wait_for_user = 0;
							ddvd_playmode = PLAY;
key_play:
#if CONFIG_API_VERSION == 1
							ddvd_device_clear();
#endif
							if (ddvd_trickmode & (FASTFW|FASTBW)) {
								Debug(3, "DDVD_KEY_PLAY reset fast forward\n");
								if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, 0))
									Perror("VIDEO_FAST_FORWARD");
							}
							if (ddvd_trickmode & (SLOWFW|SLOWBW)) {
								Debug(3, "DDVD_KEY_PLAY reset slow motion\n");
								if (ioctl(ddvd_fdvideo, VIDEO_SLOWMOTION, 0) < 0)
									Perror("VIDEO_SLOWMOTION");
							}
							if (ddvd_trickmode != TOFF && !ismute)
								if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 0) < 0)
									Perror("AUDIO_SET_MUTE");
							if (ddvd_playmode & PLAY || ddvd_trickmode & (FASTFW|FASTBW|SLOWFW|SLOWBW)) {
								Debug(3, "DDVD_KEY_PLAY cont audio and video\n");
								if (ioctl(ddvd_fdaudio, AUDIO_CONTINUE) < 0)
									Perror("AUDIO_CONTINUE");
								if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
									Perror("VIDEO_CONTINUE");
								msg = DDVD_SHOWOSD_STATE_PLAY;
								safe_write(message_pipe, &msg, sizeof(int));
							}
							ddvd_trickmode = TOFF;
							ddvd_trickspeed = 0;
							msg = DDVD_SHOWOSD_TIME;
						}
						break;
					}
					case DDVD_KEY_EXIT:	//Exit
					{
						Debug(1, "DDVD_KEY_EXIT (save resume info)\n");
						int resume_title, resume_chapter; //safe resume info
						uint32_t resume_block, total_block;
						if (dvdnav_current_title_info(dvdnav, &resume_title, &resume_chapter) &&
							resume_title != 0 &&
							dvdnav_get_position (dvdnav, &resume_block, &total_block) == DVDNAV_STATUS_OK) {
								playerconfig->resume_title = resume_title;
								playerconfig->resume_chapter = resume_chapter;
								playerconfig->resume_block = resume_block;
								playerconfig->resume_audio_id = audio_id;
								playerconfig->resume_audio_lock = audio_lock;
								playerconfig->resume_spu_id = spu_active_id;
								playerconfig->resume_spu_lock = spu_lock;
						}
						else
							Debug(1, "    failed to get resume position\n");
						finished = 1;
						break;
					}
					case DDVD_KEY_SLOWFWD:
					case DDVD_KEY_SLOWBWD:
						ddvd_readpipe(key_pipe, &ddvd_trickspeed, sizeof(int), 1);
						if (ddvd_trickspeed <= 0) // no slow backward yet
							goto key_play;
						if (!(ddvd_trickmode & SLOWFW)) {
							if (ddvd_trickmode & (FASTFW|FASTBW)) {
								if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, 0))
									Perror("VIDEO_FAST_FORWARD");
							}
							if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 1) < 0)
								Perror("AUDIO_SET_MUTE");
							ddvd_trickmode = SLOWFW;
						}
						if (ioctl(ddvd_fdvideo, VIDEO_SLOWMOTION, ddvd_trickspeed) < 0)
							Debug(1, "VIDEO_SLOWMOTION(%d) failed\n", ddvd_trickspeed);
						if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
							Perror("VIDEO_CONTINUE");
						if (ddvd_playmode == PAUSE) {
							ddvd_playmode = PLAY;
							ddvd_wait_for_user = 0;
							if (ioctl(ddvd_fdaudio, AUDIO_CONTINUE) < 0)
								Perror("AUDIO_CONTINUE");
						}
						Debug(3, "SLOW%cWD speed %dx\n", ddvd_trickmode & SLOWFW ? 'F' : 'B', ddvd_trickspeed);
						msg = ddvd_trickmode & (SLOWFW) ? DDVD_SHOWOSD_STATE_SFWD : DDVD_SHOWOSD_STATE_SBWD;
						break;
					case DDVD_KEY_FASTFWD:
					case DDVD_KEY_FASTBWD:
						ddvd_readpipe(key_pipe, &ddvd_trickspeed, sizeof(int), 1);
						Debug(3, "FAST%cWD speed %dx\n", ddvd_trickmode & (TRICKFW|FASTFW) ? 'F' : 'B', ddvd_trickspeed);
						if (ddvd_trickspeed == 0)
							goto key_play;
						if (!(ddvd_trickmode & (FASTFW|FASTBW|TRICKFW|TRICKBW))) {
							if (ddvd_trickmode & (SLOWFW|SLOWBW)) {
								if (ioctl(ddvd_fdvideo, VIDEO_SLOWMOTION, 0) < 0)
									Perror("VIDEO_SLOWMOTION");
							}
							if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 1) < 0)
								Perror("AUDIO_SET_MUTE");
						}
						// determine if flip to/from driver (smooth) or trick fast forward
						if (ddvd_trickspeed > 0 && ddvd_trickspeed < 7) { // higher speeds cannot be handled reliably by driver
							ddvd_trickmode = FASTFW;
							if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, ddvd_trickspeed) < 0)
								Perror("VIDEO_FAST_FORWARD");
							if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
								Perror("VIDEO_CONTINUE");
						}
						else {
							if (ddvd_trickmode & FASTFW) {
								if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, 0) < 0)
									Perror("VIDEO_FAST_FORWARD");
								if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
									Perror("VIDEO_CONTINUE");
							}
							ddvd_trickmode = (ddvd_trickspeed < 0 ? TRICKBW : TRICKFW);
						}
						Debug(3, "FAST%cWD speed %dx\n", ddvd_trickmode & (TRICKFW|FASTFW) ? 'F' : 'B', ddvd_trickspeed);
						msg = ddvd_trickmode & (TRICKBW|FASTBW) ? DDVD_SHOWOSD_STATE_FBWD : DDVD_SHOWOSD_STATE_FFWD;
						break;
					case DDVD_KEY_FFWD:	//FastForward
					case DDVD_KEY_FBWD:	//FastBackward
					{
						if (ddvd_trickmode == TOFF) {
							if (ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 1) < 0)
								Perror("AUDIO_SET_MUTE");
							ddvd_trickspeed = 2;
							ddvd_trickmode = (rccode == DDVD_KEY_FBWD ? TRICKBW : FASTFW);
						}
						else if (ddvd_trickmode & (rccode == DDVD_KEY_FBWD ? (TRICKFW|FASTFW) : TRICKBW)) {
							ddvd_trickspeed /= 2;
							if (ddvd_trickspeed == 1) {
								ddvd_trickspeed = 0;
								goto key_play;
							}
						}
						else if (ddvd_trickspeed < 64)
							ddvd_trickspeed *= 2;

						Debug(3, "FAST%cWD speed %dx\n", ddvd_trickmode & (TRICKFW|FASTFW) ? 'F' : 'B', ddvd_trickspeed);
						if (ddvd_trickmode & (TRICKFW|FASTFW)) {
							if (ddvd_trickspeed < 7) { // higher speeds cannot be handled reliably by driver
								ddvd_trickmode = FASTFW; // Driver fast forward
								if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, ddvd_trickspeed) < 0)
									Perror("VIDEO_FAST_FORWARD");
								if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
									Perror("VIDEO_CONTINUE");
							}
							else {
								if (ddvd_trickmode & FASTFW) {
									if (ioctl(ddvd_fdvideo, VIDEO_FAST_FORWARD, 0) < 0)
										Perror("VIDEO_FAST_FORWARD");
									if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
										Perror("VIDEO_CONTINUE");
								}
								ddvd_trickmode = TRICKFW; // Trick fast forward
							}
						}
						msg = ddvd_trickmode & (TRICKBW|FASTBW) ? DDVD_SHOWOSD_STATE_FBWD : DDVD_SHOWOSD_STATE_FFWD;
						break;
					}
					case DDVD_SKIP_FWD:
					case DDVD_SKIP_BWD:
					{
						int skip;
						ddvd_readpipe(key_pipe, &skip, sizeof(int), 1);
						if (ddvd_trickmode != (TRICKFW|TRICKBW)) {
							uint32_t pos, len;
							dvdnav_get_position(dvdnav, &pos, &len);
							// 90000 = 1 Sek.
							if (!len)
								len = 1;
							int64_t newpos = pos + (skip * 90000L + (int64_t)(vpts > pts ? pts - vpts : 0)) * (int64_t)len / ddvd_lastCellEventInfo.pgc_length;
							Debug(3, "DDVD_SKIP skip=%d oldpos=%u len=%u pgc=%lld newpos=%lld vpts=%llu pts=%llu\n", skip, pos, len, ddvd_lastCellEventInfo.pgc_length, newpos, vpts, pts);
							if (newpos >= len) {	// reached end of movie
								newpos = len - 250;
								reached_eof = 1;
							}
							else if (newpos < 0) {
								newpos = 0;
								reached_sof = 1;
							}
							dvdnav_sector_search(dvdnav, newpos, SEEK_SET);
							ddvd_play_empty(1);
							msg = DDVD_SHOWOSD_TIME;
							Debug(1, "                 clr spu frame spu_nr=%d->%d\n", ddvd_spu_play, ddvd_spu_ind);
							ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
						}
						break;
					}
					case DDVD_KEY_AUDIO:	//jump to next audio track
					case DDVD_SET_AUDIO:	//change to given audio track
					{
						int count = 1;
						Debug(1, "DDVD_SET_AUDIO CURRENT %i\n", audio_id);
						if (rccode == DDVD_SET_AUDIO) {
							ddvd_readpipe(key_pipe, &count, sizeof(int), 1);
							if (count < MAX_AUDIO && playerconfig->audio_format[count] != -1)
								audio_id = count;
						}
						else {
							do {
								audio_id++;
								if (audio_id >= MAX_AUDIO)
									audio_id = 0;
							} while (playerconfig->audio_format[audio_id] == -1 && count++ < MAX_AUDIO);
						}
						Debug(1, "DDVD_SET_AUDIO %i\n", audio_id);
						report_audio_info = 1;
						ddvd_play_empty(TRUE);
						audio_lock = 1;
						ddvd_lpcm_count = 0;
						break;
					}
					case DDVD_KEY_SUBTITLE:	//jump to next spu track
					case DDVD_SET_SUBTITLE:	//change to given spu track
					{
						uint16_t spu_lang = 0xFFFF;
						int old_active_id = spu_active_id;
						if (rccode == DDVD_KEY_SUBTITLE)
							spu_index++;
						else
							ddvd_readpipe(key_pipe, &spu_index, sizeof(int), 1);
						if (spu_index < MAX_SPU && playerconfig->spu_map[spu_index].logical_id > -1) {
							spu_active_id = playerconfig->spu_map[spu_index].stream_id;
							spu_lang = playerconfig->spu_map[spu_index].lang;
						}

						if (spu_lang == 0xFFFF) {
							spu_lang = 0x2D2D;	// SPU "off"
							spu_active_id = -1;
							spu_index = -1;
							ddvd_spu_play = ddvd_spu_ind; // skip remaining subtitles
						}
						Debug(1, "DDVD_SET_SUBTITLE CURRENT ind=%d act=%d prevact=%d - %c%c\n", spu_index, spu_active_id, old_active_id, spu_lang >> 8, spu_lang & 0xFF);
						spu_lock = 1;
						playerconfig->last_spu_id = spu_index;
						msg = DDVD_SHOWOSD_SUBTITLE;
						safe_write(message_pipe, &msg, sizeof(int));
						safe_write(message_pipe, &spu_index, sizeof(int));
						safe_write(message_pipe, &spu_lang, sizeof(uint16_t));
						break;
					}
					case DDVD_KEY_ANGLE: //change angle
					case DDVD_GET_ANGLE: //frontend wants angle info
					{
						int num = 0, current = 0;
						dvdnav_get_angle_info(dvdnav, &current, &num);
						if (rccode == DDVD_KEY_ANGLE) {
							if (num != 0) {
								current++;
								if (current > num)
									current = 1;
								dvdnav_angle_change(dvdnav, current);
							}
						}
						msg = DDVD_SHOWOSD_ANGLE;
						safe_write(message_pipe, &msg, sizeof(int));
						safe_write(message_pipe, &current, sizeof(int));
						safe_write(message_pipe, &num, sizeof(int));
						break;
					}
					case DDVD_GET_TIME:	// frontend wants actual time
						msg = DDVD_SHOWOSD_TIME;
						break;
					default:
						break;
				}
			}
		}
	}

err_dvdnav:
	/* destroy dvdnav handle */
	if (dvdnav_close(dvdnav) != DVDNAV_STATUS_OK)
		Debug(1, "Error on dvdnav_close: %s\n", dvdnav_err_to_string(dvdnav));

err_dvdnav_open:
	ddvd_device_clear();
	if (ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_DEMUX) < 0)
		Perror("VIDEO_SELECT_SOURCE");
	if (ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_DEMUX) < 0)
		Perror("AUDIO_SELECT_SOURCE");
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
	blit_area.x_start = blit_area.y_start = 0;
	blit_area.x_end = ddvd_screeninfo_xres - 1;
	blit_area.y_end = ddvd_screeninfo_yres - 1;
	blit_area.x_offset = 0;
	blit_area.y_offset = 0;
	blit_area.width = ddvd_screeninfo_xres;
	blit_area.height = ddvd_screeninfo_yres;
	memset(p_lfb, 0, ddvd_screeninfo_stride * ddvd_screeninfo_yres);
	msg = DDVD_SCREEN_UPDATE;
	safe_write(message_pipe, &msg, sizeof(int));
	safe_write(message_pipe, &blit_area, sizeof(struct ddvd_resize_return));

err_malloc:
	// clean up
	for (i = 0; i < NUM_SPU_BACKBUFFER; i++) {
		if (ddvd_spu[i] != NULL)
			free(ddvd_spu[i]);
		if (ddvd_pci[i] != NULL)
			free(ddvd_pci[i]);
	}
	if (ddvd_lbb != NULL)
		free(ddvd_lbb);
	if (ddvd_lbb2 != NULL)
		free(ddvd_lbb2);
	if (last_iframe != NULL)
		free(last_iframe);

#if CONFIG_API_VERSION == 3
	ddvd_unset_pcr_offset();
#endif
	Debug(1, "EXIT\n");
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
			/* else if (errno == ????) // FIXME: determine error when pipe closes...
			   break; */
			Debug(1, "unhandled read error %d(%m)\n", errno);
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

		info.pos_seconds = pos_s % 60;
		info.pos_minutes = (pos_s / 60) % 60;
		info.pos_hours = pos_s / 3600;
		info.end_seconds = len_s % 60;
		info.end_minutes = (len_s / 60) % 60;
		info.end_hours = len_s / 3600;

		info.pos_title = titleNo;
	}

	return info;
}

// video out aspect/scale
static int ddvd_check_aspect(int dvd_aspect, int dvd_scale_perm, int tv_aspect, int tv_mode)
{
	int tv_scale = 0; // widescreen spu

	if (dvd_aspect == 0) { // dvd 4:3
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
	Debug(3, "ddvd_play_empty clear=%d\n", device_clear);
	ddvd_wait_for_user = 0;
	ddvd_lpcm_count = 0;
	ddvd_iframerun = 0;
	ddvd_still_frame = 0;
	ddvd_iframesend = 0;
	ddvd_last_iframe_len = 0;
	ddvd_spu_ptr = 0;

	ddvd_wait_timer_active = 0;
	ddvd_wait_timer_end = 0;

	ddvd_spu_timer_active = 0;
	ddvd_spu_timer_end = 0;

	ddvd_clear_screen = 1;

	if (device_clear)
		ddvd_device_clear();
}

// Empty Device Buffers
static void ddvd_device_clear(void)
{
	Debug(3, "device_clear: clear audio and video buffers\n");

	if (ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER) < 0)
		Perror("AUDIO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdaudio, AUDIO_PLAY) < 0)
		Perror("AUDIO_PLAY");
	if (ioctl(ddvd_fdaudio, AUDIO_CONTINUE) < 0)
		Perror("AUDIO_CONTINUE");

	if (ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER) < 0)
		Perror("VIDEO_CLEAR_BUFFER");
	if (ioctl(ddvd_fdvideo, VIDEO_PLAY) < 0)
		Perror("VIDEO_PLAY");
	if (ioctl(ddvd_fdvideo, VIDEO_CONTINUE) < 0)
		Perror("VIDEO_CONTINUE");

	if (ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1) < 0)
		Perror("AUDIO_SET_AV_SYNC");
}

// SPU Decoder
static struct ddvd_spu_return ddvd_spu_decode_data(char *spu_buf, const uint8_t * buffer, unsigned long long pts)
{
	int x1spu, x2spu, y1spu, y2spu, xspu, yspu;
	int offset[2], param_len;
	int size, datasize, controlsize, aligned, id;
	int menubutton = 0;
	int display_time = -1;
	int force_hide = SPU_NOP;

	size = (buffer[0] << 8 | buffer[1]);
	datasize = (buffer[2] << 8 | buffer[3]);
	controlsize = (buffer[datasize + 2] << 8 | buffer[datasize + 3]);

	Debug(4, "SPU_dec: Size: %X Datasize: %X Controlsize: %X\n", size, datasize, controlsize);
	// parse header
	int i = datasize + 4;

	while (i < size && buffer[i] != 0xFF) {
		switch (buffer[i]) {
			case 0x00:	// force
				force_hide = SPU_FORCE; // Highlight mask SPU
				Debug(4, "force\n");
				i++;
				break;
			case 0x01:	// show
				force_hide = SPU_SHOW; // Subtitle SPU
				Debug(4, "show\n");
				i++;
				break;
			case 0x02:	// hide
				force_hide = SPU_HIDE; // Probably only as second control block in Subtitle SPU. See scan for display_time below
				Debug(4, "hide\n");
				i++;
				break;
			case 0x03:	// palette
			{
				ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer + i + 1);
				Debug(4, "update palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

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

				i += 3;
				break;
			}
			case 0x04:	// transparency palette
			{
				ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer + i + 1);
				Debug(4, "update transp palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

				ddvd_tr[0 + 252] = (0xF - clut->entry3) * 0x1111;
				ddvd_tr[1 + 252] = (0xF - clut->entry2) * 0x1111;
				ddvd_tr[2 + 252] = (0xF - clut->entry1) * 0x1111;
				ddvd_tr[3 + 252] = (0xF - clut->entry0) * 0x1111;

				i += 3;
				break;
			}
			case 0x05:	// image coordinates
				xspu = x1spu = (((unsigned int)buffer[i + 1]) << 4) + (buffer[i + 2] >> 4);
				yspu = y1spu = (((unsigned int)buffer[i + 4]) << 4) + (buffer[i + 5] >> 4);
				x2spu = (((buffer[i + 2] & 0x0f) << 8) + buffer[i + 3]);
				y2spu = (((buffer[i + 5] & 0x0f) << 8) + buffer[i + 6]);
				Debug(4, "image coords: %dx%d,%dx%d\n", xspu, yspu, x2spu, y2spu);
				i += 7;
				break;
			case 0x06:	// image 1 / image 2 offsets
				offset[0] = (((unsigned int)buffer[i + 1]) << 8) + buffer[i + 2];
				offset[1] = (((unsigned int)buffer[i + 3]) << 8) + buffer[i + 4];
				Debug(4, "image offsets %x,%x\n", offset[0], offset[1]);
				i += 5;
				break;
			case 0x07:	// change color for a special area so overlays with more than 4 colors are possible - NOT IMPLEMENTET YET
				Debug(4, "change color packet - not implemented\n");
				param_len = (buffer[i + 1] << 8 | buffer[i + 2]);
				i += param_len + 1;
				break;
			default:
				i++;
				break;
		}
	}

	// get display time - actually a plain control block
	if (i + 6 <= size) {
		if (buffer[i + 5] == 0x02 && buffer[i + 6] == 0xFF) {
			display_time = ((buffer[i + 1] << 8) + buffer[i + 2]);
			Debug(4, "Display Time: %d\n", display_time);
		}
	}

	// parse picture
	aligned = 1;
	id = 0;

	while (offset[1] < datasize + 2 && yspu <= 575) {	// there are some special cases the decoder tries to write more than 576 lines in our buffer and we dont want this ;)
		u_int len;
		u_int code;

		code = (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
		aligned ^= 1;

		if (code < 0x0004) {
			code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
			aligned ^= 1;
			if (code < 0x0010) {
				code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
				aligned ^= 1;
				if (code < 0x0040) {
					code = (code << 4) | (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
					aligned ^= 1;
				}
			}
		}

		len = code >> 2;
		if (len == 0)
			len = (x2spu - xspu) + 1;

		memset(spu_buf + xspu + 720 * (yspu), (code & 3) + 252, len);	// drawpixel into backbuffer
		xspu += len;
		if (xspu > x2spu) {
			if (!aligned) {
				code = (aligned ? (buffer[offset[id]++] >> 4) : (buffer[offset[id] - 1] & 0xF));
				aligned = aligned ? 0 : 1;
			}
			xspu = x1spu;	// next line
			yspu++;
			id ^= 1;
		}
	}
	struct ddvd_spu_return return_code;
	return_code.display_time = display_time;
	return_code.x_start = x1spu;
	return_code.x_end = x2spu;
	return_code.y_start = y1spu;
	return_code.y_end = y2spu;
	return_code.force_hide = force_hide;
	return_code.pts = pts;

	return return_code;
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
		}
		else {
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
struct ddvd_resize_return ddvd_resize_pixmap_xbpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors)
{
	int x_ratio = (int)((xsource << 16) / (xdest - 2 * xoffset));
	int y_ratio = (int)(((ysource - 2 * yoffset) << 16) / ydest);
	int yoffset2 = (yoffset << 16) / y_ratio;

	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource * ysource * colors);
	memcpy(pixmap_tmp, pixmap, xsource * ysource * colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	struct ddvd_resize_return return_code;

	return_code.x_start = (xstart << 16) / x_ratio; // transform input resize area to destination area
	return_code.x_end = (xend << 16) / x_ratio;
	return_code.y_start = ((ystart << 16) / y_ratio) - yoffset2;
	return_code.y_end = ((yend << 16) / y_ratio) - yoffset2;
	return_code.y_start = return_code.y_start < 0 ? 0 : return_code.y_start;
	return_code.y_end = return_code.y_end > ydest ? ydest : return_code.y_end;

	int x2, y2, c, i ,j;
	for (i = return_code.y_start; i <= return_code.y_end; i++) {
		for (j = return_code.x_start; j <= return_code.x_end; j++) {
			x2 = ((j * x_ratio) >> 16);
			y2 = ((i * y_ratio) >> 16) + yoffset;
			for (c = 0; c < colors; c++)
				pixmap[((i * xdest) + j) * colors + c + xoffset * colors] = pixmap_tmp[((y2 * xsource) + x2) * colors + c];
		}
	}
	free(pixmap_tmp);

	return_code.x_start += xoffset; // correct xoffset
	return_code.x_end += xoffset;

	return return_code;
}

// bicubic picture resize
struct ddvd_resize_return ddvd_resize_pixmap_xbpp_smooth(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors)
{
	unsigned int xs, ys, xd, yd, dpixel, fx, fy;
	unsigned int c, tmp_i;
	int x, y, t, t1;

	xs = xsource; // x-resolution source
	ys = ysource - 2 * yoffset; // y-resolution source
	xd = xdest - 2 * xoffset; // x-resolution destination
	yd = ydest; // y-resolution destination
	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource * ysource * colors);
	memcpy(pixmap_tmp, pixmap, xsource * ysource * colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	struct ddvd_resize_return return_code;

	int yoffset2 = (yoffset << 16) / ((ys << 16) / yd);
	return_code.x_start = (xstart << 16) / ((xs << 16) / xd); // transform input resize area to destination area
	return_code.x_end = (xend << 16) / ((xs << 16) / xd);
	return_code.y_start = ((ystart << 16) / ((ys << 16) / yd)) - yoffset2;
	return_code.y_end = ((yend << 16) / ((ys << 16) / yd)) - yoffset2;
	return_code.y_start = return_code.y_start < 0 ? 0 : return_code.y_start;
	return_code.y_end = return_code.y_end > ydest ? ydest : return_code.y_end;

	// get x scale factor, use bitshifting to get rid of floats
	fx = ((xs - 1) << 16) / xd;

	// get y scale factor, use bitshifting to get rid of floats
	fy = ((ys - 1) << 16) / yd;

	unsigned int sx1[xd], sx2[xd], sy1, sy2;

	// pre calculating sx1/sx2 for faster resizing
	for (x = return_code.x_start; x <= return_code.x_end; x++) {
		// first x source pixel for calculating destination pixel
		sx1[x] = (fx * x) >> 16; //floor()

		// last x source pixel for calculating destination pixel
		sx2[x] = sx1[x] + (fx >> 16);
		if (fx & 0x7FFF) //ceil()
			sx2[x]++;
	}

	// Scale
	for (y = return_code.y_start; y <= return_code.y_end; y++) {
		// first y source pixel for calculating destination pixel
		sy1 = (fy * y) >> 16; //floor()

		// last y source pixel for calculating destination pixel
		sy2 = sy1 + (fy >> 16);
		if (fy & 0x7FFF) //ceil()
			sy2++;

		for (x = return_code.x_start; x <= return_code.x_end; x++) {
			// we do this for every color
			for (c = 0; c < colors; c++) {
				// calculating destination pixel
				tmp_i = 0;
				dpixel = 0;
				for (t1 = sy1; t1 < sy2; t1++) {
					for (t = sx1[x]; t <= sx2[x]; t++) {
						tmp_i += (int)pixmap_tmp[(t * colors) + c + ((t1 + yoffset) * xs * colors)];
						dpixel++;
					}
				}
				// writing calculated pixel into destination pixmap
				pixmap[((x + xoffset) * colors) + c + (y * (xd + 2 * xoffset) * colors)] = tmp_i / dpixel;
			}
		}
	}
	free(pixmap_tmp);

	return_code.x_start += xoffset; // correct xoffset
	return_code.x_end += xoffset;

	return return_code;
}

// very simple linear resize used for 1bypp mode
struct ddvd_resize_return ddvd_resize_pixmap_1bpp(unsigned char *pixmap, int xsource, int ysource, int xdest, int ydest, int xoffset, int yoffset, int xstart, int xend, int ystart, int yend, int colors)
{
	unsigned char *pixmap_tmp;
	pixmap_tmp = (unsigned char *)malloc(xsource * ysource * colors);
	memcpy(pixmap_tmp, pixmap, xsource * ysource * colors);
	memset(pixmap, 0, xdest * ydest * colors);	//clear screen ..
	struct ddvd_resize_return return_code;

	int i, fx, fy, tmp;

	// precalculate scale factor, use factor 10 to get rid of floats
	fx = xsource * 10 / (xdest - 2 * xoffset);
	fy = (ysource - 2 * yoffset) * 10 / ydest;
	int yoffset2 = (yoffset * 10) / fy;

	return_code.x_start = (xstart * 10) / fx; // transform input resize area to destination area
	return_code.x_end = (xend * 10) / fx;
	return_code.y_start = (ystart * 10) / fy;
	return_code.y_end = (yend * 10) / fy;
	return_code.y_start = return_code.y_start < 0 ? 0 : return_code.y_start;
	return_code.y_end = return_code.y_end > ydest ? ydest : return_code.y_end;

	// scale x
	for (i = return_code.x_start; i <= return_code.x_end; i++)
		pixmap[i + xoffset] = pixmap_tmp[((fx * i) / 10)];

	// scale y
	for (i = return_code.y_start; i <= return_code.y_end; i++) {
		tmp = (fy * i) / 10;
		if (tmp != i)
			memcpy(pixmap + (i*xdest), pixmap + (tmp + yoffset) * xdest, xdest);
	}
	free(pixmap_tmp);

	return_code.x_start += xoffset; // correct xoffset
	return_code.x_end += xoffset;

	return return_code;
}

