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


/*
 * main struct for ddvd handle
 */
struct ddvd; 

struct ddvd_resume {
	int title;
	int chapter;
	unsigned long int block;
	int audio_id;
	int audio_lock;
	int spu_id;
	int spu_lock;
};

enum ddvd_result {
	DDVD_OK = 0,
	DDVD_INVAL,
	DDVD_NOMEM,
	DDVD_BUSY,
	DDVD_FAIL_OPEN,
	DDVD_FAIL_PREFS,
	DDVD_FAIL_READ,
	DDVD_RESULT_MAX,
};

/* 
 * functions for initialization and setting options DONT USE THIS FUNCTIONS AFTER STARTING WITH ddvd_run !!!
 */

// create a ddvd handle with default options PAL, 4:3 LB, EN, AC3 decoding internal (if liba52 is available)
struct ddvd *ddvd_create(void);

// get message_pipe fd to make polling possible
int ddvd_get_messagepipe_fd(struct ddvd *pconfig);

// set framebuffer and options libdreamdvd should render buttons and subtitles
// until this option ist set, ddvd_run will not start playing
// lfb-> needs a pointer to the real framebuffer or to a backbuffer
// xres, yres-> screen resolution, normally 720x576 libdreamdvd will scale inside to the given resolution
// bypp-> bytes per pixel, only 1 (8bit) or 4 (32bit argb) is supported
// stride-> line length in bytes, normally xres*bypp but not always like on the DM7025 framebuffer
// canscale-> caller supports ddvd_get_blit_destination
void ddvd_set_lfb(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride);
void ddvd_set_lfb_ex(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride, int canscale);

// set path to a dvd block device, a dvd file structure or an dvd iso-file ("/dev/dvd" ...)
void ddvd_set_dvd_path(struct ddvd *pconfig, const char *path);

// set preferred dvd language in 2 letter iso code (en,de, ...)
void ddvd_set_language(struct ddvd *pconfig, const char lang[2]);

// set external/internal AC3 decoding 0-> external 1-> internal
// internal decoding needs liba52 which will be loaded dynamically if available
// if set to "internal" and liba52 will not be found, the AC3 data will be passed thru 
void ddvd_set_ac3thru(struct ddvd *pconfig, int ac3thru);

// set video options for aspect and the tv system, see enums for possible options
void ddvd_set_video(struct ddvd *pconfig, int aspect, int tv_mode, int tv_system);
void ddvd_set_video_ex(struct ddvd *pconfig, int aspect, int tv_mode, int tv_mode2, int tv_system);

// set resume postion for dvd start
void ddvd_set_resume_pos(struct ddvd *pconfig, struct ddvd_resume resume_info);

// directly set given audio stream id (alternative to iteration through the streams with the DDVD_KEY_AUDIO)
void ddvd_set_audio(struct ddvd *pconfig, int audio_id);

// directly set given subtitle stream id (alternative to iteration through the streams with the DDVD_KEY_SUBTITLE)
void ddvd_set_spu(struct ddvd *pconfig, int spu_id);

/* 
 * functions for starting the dvd player
 */

// starting playback, this function should be started inside a thread, because it only comes back after
// stopping the dvd player
enum ddvd_result ddvd_run(struct ddvd *pconfig);


/* 
 * functions for controlling the player while running, these functions are THREAD SAFE (or lets say I hope so ;-)
 */

// send a remote control key or command to the player, see send_key enum for possible commands
void ddvd_send_key(struct ddvd *pconfig, int key);

// skip n seconds in playing n>0 forward - n<0 backward
void ddvd_skip_seconds(struct ddvd *pconfig, int seconds);

// jump to beginning of given title
void ddvd_set_title(struct ddvd *pconfig, int title);

// jump to beginning of given chapter
void ddvd_set_chapter(struct ddvd *pconfig, int chapter);

// get and process the next message from the main player
// use blocked=1 if the function should wait till a message has received, blocked=0 will return
// immediatly and give you DDVD_NULL if there was no messag in the pipe
// blocked=0 will be usefull if the main programm runs a loop, blocked=1 if you use fd-polling 
int ddvd_get_next_message(struct ddvd*pconfig, int blocked);

// get last colortable for 8bit mode (4 colors)
// will give you 4 color structs as array, remember to use the offset (see state enum)
// struct ddvd_color colortable[4] 
void ddvd_get_last_colortable(struct ddvd*pconfig, void *colortable);

// get last area to update overlay after DDVD_SCREEN_UPDATE
void ddvd_get_last_blit_area(struct ddvd *pconfig, int *x_start, int *x_end, int *y_start, int *y_end);

#define DDVD_SUPPORTS_16_10_SCALING 1
#define DDVD_SUPPORTS_GET_BLIT_DESTINATION 1
// get parameters used for blit
void ddvd_get_blit_destination(struct ddvd *pconfig, int *x_offset, int *y_offset, int *width, int *height);

// get last received playing time
// struct ddvd_time timestamp
void ddvd_get_last_time(struct ddvd*pconfig, void *timestamp);

// get actual angle info after DDVD_SHOWOSD_ANGLE
void ddvd_get_angle_info(struct ddvd*pconfig, int *current, int *num);

// get the actual trickspeed (2-64x) when in trickmode
// int trickspeed
void ddvd_get_last_trickspeed(struct ddvd*pconfig, void *trickspeed);

// get last text message from player
// char text[512]
void ddvd_get_last_string(struct ddvd*pconfig, void *text);

// get the active audio track
// int id -> logical track number
// uint16_t lang -> audio language in 2 letter iso code
// int type -> audio type, see audio type enum (ac3,mpeg,...)
void ddvd_get_last_audio(struct ddvd*pconfig, void *id, void *lang, void *type);

// get audio track details for given audio track id
void ddvd_get_audio_byid(struct ddvd *pconfig, int audio_id, void *lang, void *type);

// get the number of available audio tracks
void ddvd_get_audio_count(struct ddvd *pconfig, void *count);

// get the active subtitle track
// int id -> logical track number
// uint16_t lang -> subtitle language in 2 letter iso code
// id=-1 means no subtitle track active
void ddvd_get_last_spu(struct ddvd*pconfig, void *id, void *lang);

// get track details for given subtitle track id
void ddvd_get_spu_byid(struct ddvd *pconfig, int spu_id, void *lang);

// get the number of available subtitle tracks
void ddvd_get_spu_count(struct ddvd *pconfig, void *count);

// get dvd title string
void ddvd_get_title_string(struct ddvd*pconfig, char *title_string);

// get last received position for resume
void ddvd_get_resume_pos(struct ddvd *pconfig, struct ddvd_resume *resume_info);

#define DDVD_SUPPORTS_PICTURE_INFO 1
void ddvd_get_last_size(struct ddvd *pconfig, int *width, int *height, int *aspect);
void ddvd_get_last_framerate(struct ddvd *pconfig, int *frate);
void ddvd_get_last_progressive(struct ddvd *pconfig, int *progressive);

/* 
 * functions for clean up AFTER the player had stopped
 */

// destroy ddvd handle, do NOT call this while the player is still running
// to stop the player you can send the DDVD_KEY_EXIT command via ddvd_send_key
void ddvd_close(struct ddvd *pconfig);


/* 
 * messages recieved from ddvd_get_next_message
 */

enum { // state
	DDVD_NULL, 					// nothing to do
	DDVD_COLORTABLE_UPDATE, 	// if we are in 8bit graphics mode we have to renew the color table, the color table can be grabbed with
								// ddvd_get_last_colortable function, we need an offset of 252d, means the first struct in the array should be set to 252d in 
								// the real colortable, the second struct to 253d and so on, this message will never been send in 32bit mode 
								// ATTENTION to clear screen, libdreamdvd uses color 0, so be sure to set color 0 in the host-application to full transparency
	DDVD_SCREEN_UPDATE,			// libdreamdvd rendered something to the given framebuffer, so if we are working with a backbuffer in the host app,
								// we have to update our screen. can be ignored if libdreamdvd renders directly to the real framebuffer
								// the rect that have to be updated can be fetched with ddvd_get_last_blit_area
	DDVD_SHOWOSD_STATE_PLAY,	// we should display a state icon or text (play, pause, ...) on osd 
	DDVD_SHOWOSD_STATE_PAUSE,
	DDVD_SHOWOSD_TIME, 			// we should display the playing time on osd you can get the time with ddvd_get_last_time
	DDVD_SHOWOSD_STATE_FFWD,	// we should display FFWD/FBWD trickmode on osd you can grab the actual time with ddvd_get_last_time
	DDVD_SHOWOSD_STATE_FBWD,	// and the trickspeed with ddvd_get_last_trickspeed
	DDVD_SHOWOSD_STRING,		// we should display a text on osd (errors, ...) the text can be read with ddvd_get_last_string
	DDVD_SHOWOSD_AUDIO,			// new audio track selected you can get the audio id, language, type with ddvd_get_last_audio
	DDVD_SHOWOSD_SUBTITLE,		// new subtitle track selected you can get the spu id, language with ddvd_get_last_spu
	DDVD_SHOWOSD_TITLESTRING,	
	DDVD_EOF_REACHED,
	DDVD_SOF_REACHED,
	DDVD_MENU_OPENED,
	DDVD_MENU_CLOSED,
	DDVD_SHOWOSD_ANGLE,			// show angle info, you can get it with ddvd_get_angle_info	
	DDVD_SIZE_CHANGED,
	DDVD_PROGRESSIVE_CHANGED,
	DDVD_FRAMERATE_CHANGED,
};


/* 
 * key/commands to send with ddvd_send_key
 */

enum { // send_key
	DDVD_KEY_NULL,				// will be ignored
	DDVD_KEY_EXIT,				// stop end exit the player as fast as possible
	
								/* menus*/
	DDVD_KEY_LEFT,				// cursor control
	DDVD_KEY_RIGHT,
	DDVD_KEY_UP,
	DDVD_KEY_DOWN,
	DDVD_KEY_OK,				// activate button

								/* inside movie */
	DDVD_KEY_PLAY,				// resume playing if we are "paused" or used any ffwd/fbwd before
	DDVD_KEY_PAUSE,				// pause playing (still picture)
	DDVD_KEY_NEXT_CHAPTER,			// jump to next chapter
	DDVD_KEY_PREV_CHAPTER,			// jump to previous chapter
	DDVD_KEY_NEXT_TITLE,			// jump to next title
	DDVD_KEY_PREV_TITLE,			// jump to previous title
	DDVD_KEY_FFWD,				// start or speed up fast forward mode 
	DDVD_KEY_FBWD,				// start or speed up fast backward mode 
	DDVD_KEY_MENU,				// jump into the dvd menu 
	DDVD_KEY_AUDIOMENU,			// jump into the dvd audio menu
	DDVD_KEY_AUDIO,				// change audio track on the fly 
	DDVD_KEY_SUBTITLE,			// change subtitle track on the fly 
	DDVD_GET_TIME,				// get actual playing time (see struct ddvd_time) 
	DDVD_SKIP_FWD,				// jump forward in playing SHOULD NOT BE USED DIRECTLY, USE ddvd_skip_seconds FOR SKIPPING
	DDVD_SKIP_BWD,				// jump backward in playing SHOULD NOT BE USED DIRECTLY, USE ddvd_skip_seconds FOR SKIPPING 
	DDVD_SET_TITLE,				// jump to given title
	DDVD_SET_CHAPTER,			// jump to given chapter
	DDVD_SEEK_ABS,				// seek to given absolute seconds (from beginning of current title)
	DDVD_SET_MUTE,				// just telling dreamdvd that the sound has been muted, libdreamdvd does not mute for you, but has to know
								// the mute state for sound handling on ffwd/fbwd trick mode
	DDVD_UNSET_MUTE,			// sound is not muted any more (see DDVD_SET_MUTE)
	DDVD_KEY_ANGLE,				// change angle on the fly
	DDVD_GET_ANGLE,				// get actual angle info
	DDVD_SET_AUDIO,				// set given audio track id
	DDVD_SET_SUBTITLE,			// set given subtitle track id
};

// if you use the same keys for different functions in different contexts (menu/movie) just send both commands, the player will 
// choose the right one and ignore the other. For example you want to use the right cursor key for "right" in menu and "next chapter" in movie,
// so you have to send both when you received the key event for your "right cursor key": DDVD_KEY_RIGHT and DDVD_KEY_NEXT_CHAPTER


/* 
 * config and state enums
 */

enum { // audio types
	DDVD_UNKNOWN,
	DDVD_AC3,
	DDVD_MPEG,
	DDVD_DTS,
	DDVD_LPCM,
};

enum { // tv system
	DDVD_PAL,
	DDVD_NTSC,
};

enum { // aspect
	DDVD_4_3,
	DDVD_16_9,
	DDVD_16_10,
};

enum { // tv mode
	DDVD_LETTERBOX,
	DDVD_PAN_SCAN,
	DDVD_JUSTSCALE,
};


/* 
 * structs for color palette and osd time and resume info
 */

struct ddvd_color { 
	unsigned short red;	
	unsigned short green;
	unsigned short blue;
	unsigned short trans;
};

struct ddvd_time {
	int pos_hours;				// pos_hours:pos_minutes:pos_seconds -> time already played
	int pos_minutes;			// end_hours:end_minutes:end_seconds -> total time of the playing programm
	int pos_seconds;			// pos_chapter -> chapter no. we are just playing
	int pos_chapter;			// end_chapter -> total chapters of the playing programm
	int pos_title;
	int end_hours;
	int end_minutes;
	int end_seconds;
	int end_chapter;
	int end_title;
};
