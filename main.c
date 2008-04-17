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

#include <poll.h>

/*
 * external functions 
 */

// create ddvd handle and set defaults

struct ddvd *ddvd_create(void)
{
	struct ddvd *pconfig;
	pconfig = (struct ddvd*)malloc(sizeof(struct ddvd));
	memset(pconfig, 0, sizeof(*pconfig));
		
	// defaults
	pconfig->ac3thru=0;
	pconfig->aspect=DDVD_4_3_LETTERBOX;
	memcpy(pconfig->language,"en",2);
	strcpy(pconfig->dvd_path,"/dev/cdroms/cdrom0");
	pconfig->tv_system=DDVD_PAL;
	pconfig->lfb_set=0;
	pconfig->xres=720;
	pconfig->yres=576;
	pconfig->stride=720;
	pconfig->bypp=1;
	pconfig->next_time_update=0;
	strcpy(pconfig->title_string,"");
	
	// open pipes
	pipe(pconfig->key_pipe);
	fcntl(pconfig->key_pipe[0],F_SETFL,O_NONBLOCK);

	pipe(pconfig->message_pipe);
	fcntl(pconfig->message_pipe[0],F_SETFL,O_NONBLOCK);
	
	return pconfig;
}

// destroy ddvd handle

void ddvd_close(struct ddvd *pconfig)
{
	close(pconfig->message_pipe[0]);
	close(pconfig->message_pipe[1]);
	close(pconfig->key_pipe[0]);
	close(pconfig->key_pipe[1]);
	free(pconfig);
}

// get message_pipe fd for polling functions in the host app

int ddvd_get_messagepipe_fd(struct ddvd *pconfig)
{
	return pconfig->message_pipe[0];
}

// set framebuffer options

void ddvd_set_lfb(struct ddvd *pconfig, unsigned char *lfb, int xres, int yres, int bypp, int stride)
{
	pconfig->lfb=lfb;
	pconfig->lfb_set=1;
	pconfig->xres=xres;
	pconfig->yres=yres;
	pconfig->stride=stride;
	pconfig->bypp=bypp;
}

// set path to dvd block device or file structure/iso

void ddvd_set_dvd_path(struct ddvd *pconfig, const char *path)
{
	strcpy(pconfig->dvd_path,path);
}

// set language

void ddvd_set_language(struct ddvd *pconfig, const char lang[2])
{
	memcpy(pconfig->language,lang,2);
}

// set internal ac3 decoding (needs liba52 which will be dynamically loaded)

void ddvd_set_ac3thru(struct ddvd *pconfig, int ac3thru)
{
	pconfig->ac3thru=ac3thru;
}

// set video options 

void ddvd_set_video(struct ddvd *pconfig, int aspect, int tv_system)
{
	pconfig->aspect=aspect;
	pconfig->tv_system=tv_system;
}

// send commands/keys to the main player

void ddvd_send_key(struct ddvd *pconfig, int key)
{
	write(pconfig->key_pipe[1], &key, sizeof(int));
}

void debug_send_key(struct ddvd *pconfig, int key, const char *file, int line)
{
	printf("ddvd_send_key %d at line %d in file %s\n", key, line, file);
	ddvd_send_key(pconfig, key);
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
	DDVD_SEND_KEY(pconfig, DDVD_SET_TITLE);
	DDVD_SEND_KEY(pconfig, title);
}

// jump to beginning of given chapter
void ddvd_set_chapter(struct ddvd *pconfig, int chapter)
{
	DDVD_SEND_KEY(pconfig, DDVD_SET_CHAPTER);
	DDVD_SEND_KEY(pconfig, chapter);
}

// get and process the next message from the main player

int ddvd_get_next_message(struct ddvd*pconfig, int blocked)
{
	int res;
	if(ddvd_readpipe(pconfig->message_pipe[0], &res, sizeof(int),blocked) != sizeof(int))
		res=DDVD_NULL;
	
	switch(res) // more data to process ?
	{
		case DDVD_COLORTABLE_UPDATE:
		{
			int i=0;
			while (i < 4)
				ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_col[i++], sizeof(struct ddvd_color),1);
			break;
		}
		case DDVD_SHOWOSD_TIME:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time),1);
			break;
		case DDVD_SHOWOSD_STATE_FFWD:
		{
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_trickspeed, sizeof(pconfig->last_trickspeed),1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time),1);
			break;
		}
		case DDVD_SHOWOSD_STATE_FBWD:
		{
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_trickspeed, sizeof(pconfig->last_trickspeed),1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_time, sizeof(pconfig->last_time),1);			
			break;
		}
		case DDVD_SHOWOSD_STRING:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_string, sizeof(pconfig->last_string),1);
			break;
		case DDVD_SHOWOSD_AUDIO:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_id, sizeof(int),1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_lang, sizeof(uint16_t),1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_audio_type, sizeof(int),1);
			break;
		case DDVD_SHOWOSD_SUBTITLE:
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_spu_id, sizeof(int),1);
			ddvd_readpipe(pconfig->message_pipe[0], &pconfig->last_spu_lang, sizeof(uint16_t),1);
			break;
		default:
			break;
	}

	return res;
}

// get last colortable for 8bit mode (4 colors)

void ddvd_get_last_colortable(struct ddvd*pconfig, void *colortable)
{
	memcpy(colortable,pconfig->last_col,sizeof(pconfig->last_col));
}

// get last received playing time as struct ddvd_time

void ddvd_get_last_time(struct ddvd*pconfig, void *timestamp)
{
	memcpy(timestamp,&pconfig->last_time,sizeof(pconfig->last_time));
}

// get the actual trickspeed (2-64x) when in trickmode

void ddvd_get_last_trickspeed(struct ddvd*pconfig, void *trickspeed)
{
	memcpy(trickspeed,&pconfig->last_trickspeed,sizeof(pconfig->last_trickspeed));
}

// get last text message from player

void ddvd_get_last_string(struct ddvd*pconfig, void *text)
{
	memcpy(text,pconfig->last_string,sizeof(pconfig->last_string));
}

// get the active audio track

void ddvd_get_last_audio(struct ddvd*pconfig, void *id, void *lang, void *type)
{
	memcpy(id,&pconfig->last_audio_id,sizeof(pconfig->last_audio_id));
	memcpy(lang,&pconfig->last_audio_lang,sizeof(pconfig->last_audio_lang));
	memcpy(type,&pconfig->last_audio_type,sizeof(pconfig->last_audio_type));
}

// get the active SPU track

void ddvd_get_last_spu(struct ddvd*pconfig, void *id, void *lang)
{
	memcpy(id,&pconfig->last_spu_id,sizeof(pconfig->last_spu_id));
	memcpy(lang,&pconfig->last_spu_lang,sizeof(pconfig->last_spu_lang));
}

void ddvd_get_title_string(struct ddvd*pconfig, char *title_string)
{
	memcpy(title_string,&pconfig->title_string,sizeof(pconfig->title_string));
}

// the main player loop

void ddvd_run(struct ddvd *playerconfig) {
	
	if (playerconfig->lfb_set == 0)
	{
		printf("Frame/backbuffer not given to libdreamdvd. Will not start the player !\n");
		return;
	}
	ddvd_screeninfo_xres=playerconfig->xres;
	ddvd_screeninfo_yres=playerconfig->yres;
	ddvd_screeninfo_stride=playerconfig->stride;
	int ddvd_screeninfo_bypp=playerconfig->bypp;
	
	int message_pipe=playerconfig->message_pipe[1];
	int key_pipe=playerconfig->key_pipe[0];
	
	unsigned char *p_lfb=playerconfig->lfb;
	
	int msg;
	
	// try to load liba52.so.0 for softdecoding
	int have_liba52=ddvd_load_liba52();
	
	// init backbuffer (SPU)
	if(!(ddvd_lbb = malloc(720*576))) // the spu backbuffer is always max DVD PAL 720x576 pixel (NTSC 720x480)
	{
		printf("SPU-Backbuffer <mem allocation failed>\n");
		return;
	}
	memset(ddvd_lbb, 0, ddvd_screeninfo_xres*ddvd_screeninfo_yres);

	memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres); //clear screen
	msg=DDVD_SCREEN_UPDATE;
	write(message_pipe, &msg, sizeof(int));

	printf("Opening output...\n");
#if CONFIG_API_VERSION == 1	
	ddvd_output_fd =open("/dev/video", O_WRONLY);
	if (ddvd_output_fd == -1) 
	{
		printf("Error opening output video\n");
		return;
	}
	ddvd_fdvideo = open("/dev/dvb/card0/video0", O_RDWR );
	if (ddvd_fdvideo  == -1) 
	{
		printf("Error opening output video0\n");
		return;
	}
	ddvd_fdaudio = open("/dev/dvb/card0/audio0", O_RDWR );
	if (ddvd_fdaudio  == -1) 
	{
		printf("Error opening output audio0\n");
		return;
	}
	ddvd_ac3_fd = open("/dev/sound/dsp1", O_RDWR );
	if (ddvd_ac3_fd == -1) 
	{
		printf("Error opening output /dev/sound/dsp1\n");
		return;
	}
	ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY );
	ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER);
	ioctl(ddvd_fdvideo, VIDEO_PLAY);
	ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY );
	ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER);
	ioctl(ddvd_fdaudio, AUDIO_PLAY);
#elif CONFIG_API_VERSION == 3
	ddvd_output_fd = ddvd_fdvideo = open("/dev/dvb/adapter0/video0", O_RDWR );
	if (ddvd_fdvideo  == -1) 
	{
		printf("Error opening output video0\n");
		return;
	}
	ddvd_ac3_fd = ddvd_fdaudio = open("/dev/dvb/adapter0/audio0", O_RDWR );
	if (ddvd_fdaudio  == -1) 
	{
		printf("Error opening output audio0\n");
		return;
	}
	ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY );
	ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER);
	ioctl(ddvd_fdvideo, VIDEO_PLAY);

	ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_MEMORY );
	ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER);
	ioctl(ddvd_fdaudio, AUDIO_PLAY);
#else
#error please define CONFIG_API_VERSION to be 1 or 3
#endif

// show startup screen
#if SHOW_START_SCREEN == 1
#if CONFIG_API_VERSION == 1					
		int i=0;
		while (i++<10) //that really sucks but there is no other way
			write(ddvd_output_fd, ddvd_startup_logo, sizeof(ddvd_startup_logo));
#else
		unsigned char ifbuffer[sizeof(ddvd_startup_logo)*2+9];
		ifbuffer[0]=0;
		ifbuffer[1]=0;
		ifbuffer[2]=1;
		ifbuffer[3]=0xE0;
		ifbuffer[4]=(sizeof(ddvd_startup_logo)*2)>>8;
		ifbuffer[5]=(sizeof(ddvd_startup_logo)*2)&0xFF;
		ifbuffer[6]=0x80;
		ifbuffer[7]=0;
		ifbuffer[8]=0;
		memcpy(ifbuffer+9,ddvd_startup_logo,sizeof(ddvd_startup_logo));
		memcpy(ifbuffer+9+sizeof(ddvd_startup_logo),ddvd_startup_logo,sizeof(ddvd_startup_logo));
		write(ddvd_output_fd, ifbuffer, sizeof(ddvd_startup_logo)*2+9);
#endif
#endif
	
	int audio_type=DDVD_AC3;
	
#if CONFIG_API_VERSION == 3
	ddvd_set_pcr_offset();
#endif
	
	uint8_t mem[DVD_VIDEO_LB_LEN];

	int result, event, len;
	uint8_t *buf = mem;
	uint8_t *last_iframe;
	last_iframe = (uint8_t *)malloc(320*1024);

	unsigned char lpcm_data[2048*6*6/*4608*/];
	int mpa_header_length;
	int mpa_count,mpa_count2;
	uint8_t mpa_data[2048*4];
	int ac3_len;
	int16_t ac3_tmp[2048*6*6];
	
	ddvd_mpa_init(48000,192000); //init MPA Encoder with 48kHz and 192k Bitrate
	
	int ac3thru=1;
	if (have_liba52)
	{
		state = a52_init(0); //init AC3 Decoder 
		ac3thru=playerconfig->ac3thru;
	}
			
	
	unsigned char osdtext[512];
	strcpy(osdtext,"");
	
	int tv_aspect=playerconfig->aspect; //0-> 4:3 lb 1-> 4:3 ps 2-> 16:9 3-> always 16:9
	int dvd_aspect=0; //0-> 4:3 2-> 16:9
	int dvd_scale_perm=0;
	int tv_scale=0; //0-> off 1-> letterbox 2-> panscan
	int spu_active_id=-1;
	int finished = 0;
	int audio_id;
	
	ddvd_trickmode=TOFF;
	ddvd_trickspeed=0;
	
	int rccode;
	int ismute=0;
	
	if (ac3thru)
	{
		ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
		ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 3 );
	}
	else
	{
		ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
		ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1 );
	}
	
	// set video system
	int pal_ntsc=playerconfig->tv_system;
	int saa;
	if (pal_ntsc == 1)
		saa=SAA_NTSC;
	else
		saa=SAA_PAL;
	int saafd=open("/dev/dbox/saa0", O_RDWR);
	ioctl(saafd,SAAIOSENC,&saa);
	close(saafd);
	
	/* open dvdnav handle */
	printf("Opening DVD...%s\n",playerconfig->dvd_path);
	if (dvdnav_open(&dvdnav, playerconfig->dvd_path) != DVDNAV_STATUS_OK) 
	{
		printf("Error on dvdnav_open\n");
		sprintf(osdtext,"Error: Cant open DVD Source: %s",playerconfig->dvd_path);
		msg=DDVD_SHOWOSD_STRING;
		write(message_pipe, &msg, sizeof(int));
		write(message_pipe, &osdtext, sizeof(osdtext));
		return;
	}

	/* set read ahead cache usage to no */
	if (dvdnav_set_readahead_flag(dvdnav, 0) != DVDNAV_STATUS_OK) 
	{
		printf("Error on dvdnav_set_readahead_flag: %s\n", dvdnav_err_to_string(dvdnav));
		return;
	}

	/* set the language */
	if (dvdnav_menu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
	dvdnav_audio_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK ||
	dvdnav_spu_language_select(dvdnav, playerconfig->language) != DVDNAV_STATUS_OK) 
	{
		printf("Error on setting languages: %s\n", dvdnav_err_to_string(dvdnav));
		return;
	}

	/* set the PGC positioning flag to have position information relatively to the
	* whole feature instead of just relatively to the current chapter */
	if (dvdnav_set_PGC_positioning_flag(dvdnav, 1) != DVDNAV_STATUS_OK) 
	{
		printf("Error on dvdnav_set_PGC_positioning_flag: %s\n", dvdnav_err_to_string(dvdnav));
		return;
	}

	int audio_lock=0;
	int spu_lock=0;
	int audio_format[8]={-1,-1,-1,-1,-1,-1,-1,-1};
	
	unsigned long long vpts,apts,spts,pts;
	
	audio_id=dvdnav_get_active_audio_stream(dvdnav);
	ddvd_playmode=PLAY;
	
	uint8_t *spu_buffer;
	spu_buffer = (uint8_t *)malloc(2*(128*1024));
	ddvd_lbb_changed=0;
	uint8_t *spu_backbuffer;
	spu_backbuffer = (uint8_t *)malloc(3*2*(128*1024));
	unsigned long long spu_backpts[3];

	ddvd_play_empty(FALSE);
	ddvd_get_time(); //set timestamp

	playerconfig->in_menu=0;

	const char *dvd_titlestring;
	if (dvdnav_get_title_string(dvdnav, &dvd_titlestring) == DVDNAV_STATUS_OK)
	{
		memcpy(&playerconfig->title_string,dvd_titlestring,strlen(dvd_titlestring)*sizeof(char));
	}
	msg=DDVD_SHOWOSD_TITLESTRING;
	write(message_pipe, &msg, sizeof(int));


	/* the read loop which regularly calls dvdnav_get_next_block
	* and handles the returned events */
	int reached_eof=0;
	int reached_sof=0;
	while (!finished) 
	{
		pci_t *pci=0;
		dsi_t *dsi=0;
		int buttonN=-1;
		int in_menu=0;

		/* the main reading function */
		if(ddvd_playmode == PLAY) //skip when not in play mode
		{
			// trickmode
			if (ddvd_trickmode) 
			{
				if (ddvd_trick_timer_end <= ddvd_get_time())
				{
					if (ddvd_trickmode == FASTBW) //jump back ?
					{
						uint32_t pos, len;
						dvdnav_get_position(dvdnav, &pos, &len);
						//90000 = 1 Sek. -> 45000 = 0.5 Sek. -> Speed Faktor=2
						int64_t posneu=((pos*ddvd_lastCellEventInfo.pgc_length)/len)-(45000*2*ddvd_trickspeed);
						int64_t posneu2=posneu <= 0 ? 0 : (posneu*len)/ddvd_lastCellEventInfo.pgc_length;
						dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
						if (posneu < 0) // reached begin of movie
						{
							reached_sof=1;
							msg=DDVD_SHOWOSD_TIME;
						}
						else
							msg=DDVD_SHOWOSD_STATE_FBWD;
					} 
					else if (ddvd_trickmode == FASTFW) //jump forward ?
					{
						uint32_t pos, len;
						dvdnav_get_position(dvdnav, &pos, &len);
						//90000 = 1 Sek. -> 22500 = 0.25 Sek. -> Speed Faktor=2
						int64_t posneu=((pos*ddvd_lastCellEventInfo.pgc_length)/len)+(22500*2*ddvd_trickspeed);
						int64_t posneu2=(posneu*len)/ddvd_lastCellEventInfo.pgc_length;
						if (posneu2 && len && posneu2 >= len) // reached end of movie
						{
							posneu2 = len - 250;
							reached_eof=1;
							msg=DDVD_SHOWOSD_TIME;
						}
						else
							msg=DDVD_SHOWOSD_STATE_FFWD;
						dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
					}
					ddvd_trick_timer_end=ddvd_get_time()+300;
					ddvd_lpcm_count=0;
				}
			}

			result = dvdnav_get_next_block(dvdnav, buf, &event, &len);

			if (result == DVDNAV_STATUS_ERR) 
			{
				printf("Error getting next block: %s\n", dvdnav_err_to_string(dvdnav));
				sprintf(osdtext,"Error: Getting next block: %s",dvdnav_err_to_string(dvdnav));
				msg=DDVD_SHOWOSD_STRING;
				write(message_pipe, &msg, sizeof(int));
				write(message_pipe, &osdtext, sizeof(osdtext));
				return;
			}

send_message:
			// send OSD Data
			if (msg>0)
			{
				switch(msg)
				{
					case DDVD_SHOWOSD_TIME:
					{
						struct ddvd_time info;
						info=ddvd_get_osd_time(playerconfig);
						write(message_pipe, &msg, sizeof(int));
						write(message_pipe, &info, sizeof(struct ddvd_time));
						break;
					}
					case DDVD_SHOWOSD_STATE_FFWD:
					{
						struct ddvd_time info;
						info=ddvd_get_osd_time(playerconfig);
						write(message_pipe, &msg, sizeof(int));
						write(message_pipe, &ddvd_trickspeed, sizeof(int));
						write(message_pipe, &info, sizeof(struct ddvd_time));
						break;
					}
					case DDVD_SHOWOSD_STATE_FBWD:
					{
						struct ddvd_time info;
						info=ddvd_get_osd_time(playerconfig);
						write(message_pipe, &msg, sizeof(int));
						write(message_pipe, &ddvd_trickspeed, sizeof(int));
						write(message_pipe, &info, sizeof(struct ddvd_time));
						break;
					}
					default:
						break;
				}
				msg=0;
			}

			if (reached_eof)
			{
				msg = DDVD_EOF_REACHED;
				write(message_pipe, &msg, sizeof(int));
				reached_eof=0;
			}

			if (reached_sof)
			{
				msg = DDVD_SOF_REACHED;
				write(message_pipe, &msg, sizeof(int));
				reached_sof=0;
			}

			if (ddvd_get_time() > playerconfig->next_time_update)
			{
				msg = DDVD_SHOWOSD_TIME;
				goto send_message;
			}

			// send iFrame
			if (ddvd_iframesend<0)
			{
#if CONFIG_API_VERSION == 1		
				ddvd_device_clear();
#endif
				ddvd_iframesend=0;
			}
			if (ddvd_iframesend>0)
			{
#if CONFIG_API_VERSION == 1		
				ddvd_device_clear();
#endif
				if (ddvd_still_frame && ddvd_last_iframe_len)
				{
#if 0
					static int ifnum = 0;
					static char ifname[255];
					snprintf(ifname, 255, "/tmp/dvd.iframe.%3.3d.asm.pes", ifnum++);
					FILE *f = fopen(ifname, "wb");
					fwrite(last_iframe, 1, ddvd_last_iframe_len, f);
					fclose(f);
#endif
					
#if CONFIG_API_VERSION == 1					
					int i=0;
					while (i++<10) //that really sucks but there is no other way
						write(ddvd_output_fd, last_iframe, ddvd_last_iframe_len);
#else
					unsigned char ifbuffer[ddvd_last_iframe_len*2+9];
					ifbuffer[0]=0;
					ifbuffer[1]=0;
					ifbuffer[2]=1;
					ifbuffer[3]=0xE0;
					ifbuffer[4]=(ddvd_last_iframe_len*2)>>8;
					ifbuffer[5]=(ddvd_last_iframe_len*2)&0xFF;
					ifbuffer[6]=0x80;
					ifbuffer[7]=0;
					ifbuffer[8]=0;
					memcpy(ifbuffer+9,last_iframe,ddvd_last_iframe_len);
					memcpy(ifbuffer+9+ddvd_last_iframe_len,last_iframe,ddvd_last_iframe_len);
					write(ddvd_output_fd, ifbuffer, ddvd_last_iframe_len*2+9);
#endif					
					//printf("Show iframe with size: %d\n",ddvd_last_iframe_len);
					ddvd_last_iframe_len=0;
				}
				ddvd_iframesend=-1;
			}

			// wait timer
			if (ddvd_wait_timer_active)
			{
				if ( ddvd_wait_timer_end <= ddvd_get_time() )
				{
					ddvd_wait_timer_active=0;
					dvdnav_still_skip(dvdnav);
					//printf("wait timer done\n");
				}
			}
			// SPU timer
			if (ddvd_spu_timer_active)
			{
				if ( ddvd_spu_timer_end <= ddvd_get_time() )
				{
					ddvd_spu_timer_active=0;
					memset(ddvd_lbb, 0, ddvd_screeninfo_xres*ddvd_screeninfo_yres); //clear SPU backbuffer
					ddvd_lbb_changed=1;
					//printf("spu timer done\n");
				}
			}

			switch (event) 
			{
				case DVDNAV_BLOCK_OK:
					/* We have received a regular block of the currently playing MPEG stream.
					* So we do some demuxing and decoding. */
				
					// select audio data
					if (((buf[14+3]) & 0xF0) == 0xC0)
						audio_format[(buf[14+3])-0xC0]=DDVD_MPEG;
					if ((buf[14+3])==0xBD && ((buf[14+buf[14+8]+9]) & 0xF8)==0x80)
						audio_format[(buf[14+buf[14+8]+9])-0x80]=DDVD_AC3;
					if ((buf[14+3])==0xBD && ((buf[14+buf[14+8]+9]) & 0xF8)==0x88)
						audio_format[(buf[14+buf[14+8]+9])-0x88]=DDVD_DTS;
					if ((buf[14+3])==0xBD && ((buf[14+buf[14+8]+9]) & 0xF8)==0xA0)
						audio_format[(buf[14+buf[14+8]+9])-0xA0]=DDVD_LPCM;
					
					if ((buf[14+3]&0xF0)==0xE0) // video
					{
						if(buf[14+7]&128)
						{
							/* damn gcc bug */
							vpts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							vpts |=   buf[14+10] << 22;
							vpts |=  (buf[14+11]>>1) << 15;
							vpts |=   buf[14+12] << 7;
							vpts |=  (buf[14+14]>>1);
							//printf("VPTS? %X\n",(int)vpts);
						}
#if CONFIG_API_VERSION == 1	
						// Eliminate 00 00 01 B4 sequence error packet because it breaks the pallas mpeg decoder
						// This is very strange because the 00 00 01 B4 is partly inside the header extension ...
						if (buf[21] == 0x00 && buf[22] == 0x00 && buf[23] == 0x01 && buf[24] == 0xB4)
						{
							buf[21]=0x01;
						}
						if (buf[22] == 0x00 && buf[23] == 0x00 && buf[24] == 0x01 && buf[25] == 0xB4)
						{
							buf[22]=0x01;
						}
#endif						
						// If DVD Aspect is 16:9 Zoom (3) and TV is 16:9 then patch the mpeg stream
						// up to 16:9 anamorph because the decoder dont support the 16:9 zoom mode
						if (dvd_aspect == 3 && tv_aspect >= 2)
						{
							if (buf[33] == 0 && buf[33+1] == 0 && buf[33+2] == 1 && buf[33+3] == 0xB3)
							{
								buf[33+7]=(buf[33+7]&0xF)+0x30;
							}
							if (buf[36] == 0 && buf[36+1] == 0 && buf[36+2] == 1 && buf[36+3] == 0xB3)
							{
								buf[36+7]=(buf[36+7]&0xF)+0x30;
							}
						}

						write(ddvd_output_fd, buf+14,2048-14);
						
						// 14+8 header_length
						// 14+(header_length)+3  -> start mpeg header
						// buf[14+buf[14+8]+3] start mpeg header

						int datalen=(buf[19]+(buf[18]<<8)+6)-buf[14+8]; // length mpeg packet
						int data=buf[14+buf[14+8]+3]; // start mpeg packet(header)

						int do_copy = (ddvd_iframerun == 0x01) && !(buf[data] == 0 && buf[data+1] == 0 && buf[data+2] == 1)?1:0;
						int have_pictureheader=0;
						int haveslice=0;
						int setrun=0;
						
						while (datalen > 6)
						{
							if (buf[data] == 0 && buf[data+1] == 0 && buf[data+2] == 1)
							{
								if ( buf[data+3] == 0x00 ) //picture
								{
									if (!setrun)
									{
										ddvd_iframerun=((buf[data+5] >> 3) & 0x07);
										setrun=1;
									}
							
									if (ddvd_iframerun < 0x01 || 0x03 < ddvd_iframerun) 
									{
										data++;
										datalen--;
										continue;
									}
									have_pictureheader=1;
									data+=5;
									datalen-=5;
										datalen=6;								
								} 
								else if ( buf[data+3] == 0xB3 && datalen >= 8 ) //sequence header
								{
									ddvd_last_iframe_len=0; // clear iframe buffer
									data+=7;
									datalen-=7;
								}
								else if ( buf[data+3] == 0xBE ) //padding stream
								{
									break;
								}
								else if ( 0x01 <= buf[data+3] && buf[data+3] <= 0xaf)  //slice ?
								{
									if ( !have_pictureheader && ddvd_last_iframe_len == 0 )
										haveslice=1;
								}
							}
							data++;
							datalen--;
						}
						if ( (ddvd_iframerun <= 0x01 || do_copy) && ddvd_still_frame )
						{
							if ( haveslice )
								ddvd_iframerun=0xFF;
							else if (ddvd_last_iframe_len < (320*1024)-(buf[19]+(buf[18]<<8)+6))
							{
								memcpy(last_iframe+ddvd_last_iframe_len, buf+14 , buf[19]+(buf[18]<<8)+6);
								ddvd_last_iframe_len+=buf[19]+(buf[18]<<8)+6;	
							}
						}
					}
					if ((buf[14+3])==0xC0+audio_id) // mpeg audio
					{
						if (audio_type != DDVD_MPEG)
						{
							//printf("Switch to MPEG Audio\n");
							ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
							ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1 );
							audio_type = DDVD_MPEG;
						}
						
						if(buf[14+7]&128)
						{
							/* damn gcc bug */
							apts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							apts |=   buf[14+10] << 22;
							apts |=  (buf[14+11]>>1) << 15;
							apts |=   buf[14+12] << 7;
							apts |=  (buf[14+14]>>1);
							//printf("APTS? %X\n",(int)apts);
						}

						write(ddvd_ac3_fd, buf+14 , buf[19]+(buf[18]<<8)+6);
					}
					if ((buf[14+3])==0xBD && (buf[14+buf[14+8]+9])==0xA0+audio_id) // lpcm audio
					{
						if (audio_type != DDVD_LPCM)
						{
							//printf("Switch to LPCM Audio\n");
							ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
							ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1 );
							audio_type = DDVD_LPCM;
							ddvd_lpcm_count=0;
						}
						if(buf[14+7]&128)
						{
							/* damn gcc bug */
							apts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							apts |=   buf[14+10] << 22;
							apts |=  (buf[14+11]>>1) << 15;
							apts |=   buf[14+12] << 7;
							apts |=  (buf[14+14]>>1);
							//printf("APTS? %X\n",(int)apts);
						}		
						int i=0; 
						char abuf[(((buf[18]<<8)|buf[19])-buf[22]-14)];
#if BYTE_ORDER == BIG_ENDIAN
						// just copy, byte order is correct on ppc machines
						memcpy(abuf,buf+14+buf[14+8]+9+7,(((buf[18]<<8)|buf[19])-buf[22]-14));
						i=(((buf[18]<<8)|buf[19])-buf[22]-14);
#else
						// byte swapping .. we become the wrong byteorder on lpcm on the 7025
						while (i< (((buf[18]<<8)|buf[19])-buf[22]-14) )
						{
							abuf[i+0]=(buf[14+buf[14+8]+9+7+i+1]);
							abuf[i+1]=(buf[14+buf[14+8]+9+7+i+0]);
							i+=2;
						}
#endif
						// we will encode the raw lpcm data to mpeg audio and send them with pts
						// information to the decoder to get a sync. playing the pcm data via
						// oss will break the pic/sound sync. So believe it or not, this is the 
						// smartest way to get a synced lpcm track ;-)
						if (ddvd_lpcm_count == 0) // save mpeg header with pts
						{
							memcpy(mpa_data,buf+14,buf[14+8]+9);
							mpa_header_length=buf[14+8]+9;
						}
						if (ddvd_lpcm_count+i >= 4608) //we have to send 4608 bytes to the encoder
						{
							memcpy(lpcm_data+ddvd_lpcm_count,abuf,4608-ddvd_lpcm_count);
							//encode
							mpa_count=ddvd_mpa_encode_frame(mpa_data+mpa_header_length,4608,lpcm_data);							
							//patch pes__packet_length
							mpa_count=mpa_count+mpa_header_length-6;
							mpa_data[4]=mpa_count>>8;
							mpa_data[5]=mpa_count&0xFF;
							//patch header type to mpeg
							mpa_data[3]=0xC0;
							//write
							write(ddvd_ac3_fd,mpa_data,mpa_count+mpa_header_length);
							memcpy(lpcm_data,abuf+(4608-ddvd_lpcm_count),i-(4608-ddvd_lpcm_count));
							ddvd_lpcm_count=i-(4608-ddvd_lpcm_count);
							memcpy(mpa_data,buf+14,buf[14+8]+9);
							mpa_header_length=buf[14+8]+9;							
						} else
						{
							memcpy(lpcm_data+ddvd_lpcm_count,abuf,i);
							ddvd_lpcm_count+=i;						
						}

						//write(ddvd_ac3_fd, buf+14 , buf[19]+(buf[18]<<8)+6);
					}
					if ((buf[14+3])==0xBD && (buf[14+buf[14+8]+9])==0x88+audio_id) // dts audio
					{
						if (audio_type != DDVD_DTS)
						{
							//printf("Switch to DTS Audio (thru)\n");
							ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
							ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 5 );
							audio_type = DDVD_DTS;
						}
						
						if(buf[14+7]&128)
						{
							/* damn gcc bug */
							apts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							apts |=   buf[14+10] << 22;
							apts |=  (buf[14+11]>>1) << 15;
							apts |=   buf[14+12] << 7;
							apts |=  (buf[14+14]>>1);
							//printf("APTS? %X\n",(int)apts);
						}						
						
						write(ddvd_ac3_fd, buf+14, buf[19]+(buf[18]<<8)+6); // not working yet ....
					}
					if ((buf[14+3])==0xBD && (buf[14+buf[14+8]+9])==0x80+audio_id) // ac3 audio
					{
						if (audio_type != DDVD_AC3)
						{
							//printf("Switch to AC3 Audio\n");
							if (ac3thru)
							{
								ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
								ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 3 );
							}
							else
							{
								ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
								ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1 );
							}
							audio_type = DDVD_AC3;
						}
						
						if(buf[14+7]&128)
						{
							/* damn gcc bug */
							apts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							apts |=   buf[14+10] << 22;
							apts |=  (buf[14+11]>>1) << 15;
							apts |=   buf[14+12] << 7;
							apts |=  (buf[14+14]>>1);
							//printf("APTS? %X\n",(int)apts);
						}
						
						if (ac3thru || !have_liba52) // !have_liba52 and !ac3thru should never happen, but who knows ;)
						{
							write(ddvd_ac3_fd, buf+14, buf[19]+(buf[18]<<8)+6);
							//fwrite(buf+buf[22]+27, 1, ((buf[18]<<8)|buf[19])-buf[22]-7, fac3); //debugwrite
						}
						else
						{
							// a bit more funny than lpcm sound, because we do a complete recoding here
							// we will decode the ac3 data to plain lpcm and will then encode to mpeg
							// audio and send them with pts information to the decoder to get a sync.
							
							// decode and convert ac3 to raw lpcm
							ac3_len=ddvd_ac3_decode(buf+buf[22]+27, ((buf[18]<<8)|buf[19])-buf[22]-7, ac3_tmp);
							
							// save the pes header incl. PTS
							memcpy(mpa_data,buf+14,buf[14+8]+9);
							mpa_header_length=buf[14+8]+9;

							//apts-=(((unsigned long long)(ddvd_lpcm_count)*90)/192);

							//mpa_data[14]=(int)((apts<<1)&0xFF);
							//mpa_data[12]=(int)((apts>>7)&0xFF);
							//mpa_data[11]=(int)(((apts<<1)>>15)&0xFF);
							//mpa_data[10]=(int)((apts>>22)&0xFF);
							
							// copy lpcm data into buffer for encoding
							memcpy(lpcm_data+ddvd_lpcm_count,ac3_tmp,ac3_len);
							ddvd_lpcm_count+=ac3_len;						

							// encode the whole packet to mpa
							mpa_count2=mpa_count=0;
							while(ddvd_lpcm_count >= 4608)
							{
								mpa_count=ddvd_mpa_encode_frame(mpa_data+mpa_header_length+mpa_count2,4608,lpcm_data);							
								mpa_count2+=mpa_count;
								ddvd_lpcm_count-=4608;
								memcpy(lpcm_data,lpcm_data+4608,ddvd_lpcm_count);
							}
							
							// patch pes__packet_length
							mpa_count=mpa_count2+mpa_header_length-6;
							mpa_data[4]=mpa_count>>8;
							mpa_data[5]=mpa_count&0xFF;
						
							// patch header type to mpeg
							mpa_data[3]=0xC0;

							// write to decoder
							write(ddvd_ac3_fd,mpa_data,mpa_count2+mpa_header_length);
							
						}
					}
					if ( (buf[14+3])==0xBD && ((buf[14+buf[14+8]+9])&0xE0) == 0x20 &&  ((buf[14+buf[14+8]+9])&0x1F) == spu_active_id ) // SPU packet
					{
						memcpy(spu_buffer+ddvd_spu_ptr,buf+buf[22]+14+10,2048-(buf[22]+14+10));
						ddvd_spu_ptr+=2048-(buf[22]+14+10);

						if(buf[14+7]&128)
						{	
							/* damn gcc bug */
#if CONFIG_API_VERSION == 3
							spts  = ((unsigned long long)(((buf[14+9] >> 1) & 7))) << 30;
							spts |=   buf[14+10] << 22;
							spts |=  (buf[14+11]>>1) << 15;
							spts |=   buf[14+12] << 7;
							spts |=  (buf[14+14]>>1);
#else							
							spts = (buf[14+9] >> 1) << 29; // need a corrected "spts" because vulcan/pallas will give us a 32bit pts instead of 33bit
							spts |= buf[14+10] << 21;
							spts |=(buf[14+11] >> 1) << 14;
							spts |= buf[14+12] << 6;
							spts |= buf[14+12] >> 2;
#endif
							//printf("SPTS? %X\n",(int)spts);
						}
						
						if ( ddvd_spu_ptr >= (spu_buffer[0] << 8 | spu_buffer[1]) ) // SPU packet complete ?
						{
							if (ddvd_spu_backnr == 3) // backbuffer already full ?
							{
								int tmplen=(spu_backbuffer[0] << 8 | spu_backbuffer[1]);
								memcpy(spu_backbuffer,spu_backbuffer+tmplen,ddvd_spu_backptr-tmplen); // delete oldest SPU packet
								spu_backpts[0]=spu_backpts[1];
								spu_backpts[1]=spu_backpts[2];
								ddvd_spu_backnr=2;
								ddvd_spu_backptr-=tmplen;
							}
							
							memcpy(spu_backbuffer+ddvd_spu_backptr, spu_buffer, (spu_buffer[0] << 8 | spu_buffer[1])); // copy into backbuffer
							spu_backpts[ddvd_spu_backnr++]=spts; // store pts
							ddvd_spu_backptr+=(spu_buffer[0] << 8 | spu_buffer[1]); // increase ptr
							
							ddvd_spu_ptr=0;
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
							ddvd_iframesend=1;

						dvdnav_still_event_t *still_event = (dvdnav_still_event_t *)buf;
						if (still_event->length < 0xff)
						{
							if (!ddvd_wait_timer_active)
							{
								ddvd_wait_timer_active=1;
								ddvd_wait_timer_end=ddvd_get_time()+(still_event->length*1000); //ms
							}
						}
						else
							ddvd_wait_for_user=1;
					}
					break;
					
				case DVDNAV_WAIT:
					/* We have reached a point in DVD playback, where timing is critical.
					* We dont use readahead, so we can simply skip the wait state. */
					{
						dvdnav_wait_skip(dvdnav);
					}
					break;
				
				case DVDNAV_SPU_CLUT_CHANGE:
					/* We received a new color lookup table so we read and store
					* it */
					{
						int i=0, i2=0;
						uint8_t pal[16*4];
#if BYTE_ORDER == BIG_ENDIAN
						memcpy(pal,buf, 16 * sizeof(uint32_t));
#else
						for (;i<16;++i)
							*(int*)(pal + i * 4) = htonl(*(int*)(buf + i * 4));
						i=0;
#endif
						while (i<16*4) {
							int y=buf[i+1];
							signed char cr=buf[i+2]; //v
							signed char cb=buf[i+3]; //u
							//printf("%d %d %d ->", y, cr, cb);
							y=pal[i+1];
							cr=pal[i+2]; //v
							cb=pal[i+3]; //u
							//printf(" %d %d %d\n", y, cr, cb);
							i+=4;
						}
						i=0;

						while (i<16*4)
						{
							int y=pal[i+1];
							signed char cr=pal[i+2]; //v
							signed char cb=pal[i+3]; //u
						
							y=76310*(y-16); //yuv2rgb
							cr-=128;
							cb-=128;
							int r=CLAMP((y + 104635*cr)>>16);
							int g=CLAMP((y - 53294*cr - 25690*cb)>>16);
							int b=CLAMP((y + 132278*cb)>>16);
							
							ddvd_bl[i2]=b<<8;
							ddvd_gn[i2]=g<<8;
							ddvd_rd[i2]=r<<8;
							i+=4;
							i2++;
						}
						//CHANGE COLORMAP
					}
					break;
				
				case DVDNAV_SPU_STREAM_CHANGE:
					/* We received a new SPU stream ID */
					{
						if (!spu_lock)
						{
							dvdnav_spu_stream_change_event_t *ev = (dvdnav_spu_stream_change_event_t *)buf;
							switch(tv_scale)
							{
								case 0: //off
									spu_active_id=ev->physical_wide;
									break;
								case 1: //letterbox
									spu_active_id=ev->physical_letterbox;
									break;
								case 2: //panscan
									spu_active_id=ev->physical_pan_scan;
									break;
								default: // should not happen
									spu_active_id=ev->physical_wide;
									break;
							}
							//printf("SPU Stream change: w %X l: %X p: %X active: %X\n",ev->physical_wide,ev->physical_letterbox,ev->physical_pan_scan,spu_active_id);
						}
					}
					break;
				
				case DVDNAV_AUDIO_STREAM_CHANGE:
					/* We received a new Audio stream ID  */
					if (!audio_lock)
						audio_id=dvdnav_get_active_audio_stream(dvdnav);
					break;
				
				case DVDNAV_HIGHLIGHT:
					/* Lets display some Buttons */
					if (ddvd_clear_buttons == 0)
					{
						dvdnav_highlight_event_t *highlight_event = (dvdnav_highlight_event_t *)buf;

						pci = dvdnav_get_current_nav_pci(dvdnav);
						dsi = dvdnav_get_current_nav_dsi(dvdnav);
						dvdnav_highlight_area_t hl;
						
						int libdvdnav_workaround=0;

						if (pci->hli.hl_gi.btngr_ns) 
						{
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

							if (btni && btni->btn_coln != 0) 
							{
								// get and set clut for actual button
								unsigned char tmp,tmp2;
								struct ddvd_color colneu;
								int i;
								msg=DDVD_COLORTABLE_UPDATE;
								if (ddvd_screeninfo_bypp == 1)
									write(message_pipe, &msg, sizeof(int));
								for (i = 0;i < 4; i++) 
								{
									tmp=((pci->hli.btn_colit.btn_coli[btni->btn_coln-1][0])>>(16+4*i)) & 0xf;
									tmp2=((pci->hli.btn_colit.btn_coli[btni->btn_coln-1][0])>>(4*i)) & 0xf;
									colneu.blue=ddvd_bl[i+252]=ddvd_bl[tmp];
									colneu.green=ddvd_gn[i+252]=ddvd_gn[tmp];
									colneu.red=ddvd_rd[i+252]=ddvd_rd[tmp];
									colneu.trans=ddvd_tr[i+252]=(0xF-tmp2)*0x1111;
									if (ddvd_screeninfo_bypp == 1)
										write(message_pipe, &colneu, sizeof(struct ddvd_color));
								}
								msg=DDVD_NULL;
								//CHANGE COLORMAP

								memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres); //clear screen ..
								//copy button into screen
								for (i = btni->y_start; i < btni->y_end; i++)
								{
									if (ddvd_screeninfo_bypp == 1)
										memcpy(p_lfb+btni->x_start+ddvd_screeninfo_xres*(i), ddvd_lbb+btni->x_start+ddvd_screeninfo_xres*(i), btni->x_end-btni->x_start);
									else
										ddvd_blit_to_argb(p_lfb+btni->x_start*ddvd_screeninfo_bypp+ddvd_screeninfo_stride*i, ddvd_lbb+btni->x_start+ddvd_screeninfo_xres*(i), btni->x_end-btni->x_start);
								}

								libdvdnav_workaround=1;
							}
						}
						if (!libdvdnav_workaround && dvdnav_get_highlight_area(pci, highlight_event->buttonN, 0, &hl) == DVDNAV_STATUS_OK) 
						{
							// get and set clut for actual button
							unsigned char tmp,tmp2;
							struct ddvd_color colneu;
							int i;
							msg=DDVD_COLORTABLE_UPDATE;
							if (ddvd_screeninfo_bypp == 1)
								write(message_pipe, &msg, sizeof(int));							
							for (i = 0;i < 4; i++) 
							{
								tmp=((hl.palette)>>(16+4*i)) & 0xf;
								tmp2=((hl.palette)>>(4*i)) & 0xf;
								colneu.blue=ddvd_bl[i+252]=ddvd_bl[tmp];
								colneu.green=ddvd_gn[i+252]=ddvd_gn[tmp];
								colneu.red=ddvd_rd[i+252]=ddvd_rd[tmp];
								colneu.trans=ddvd_tr[i+252]=(0xF-tmp2)*0x1111;
								if (ddvd_screeninfo_bypp == 1)
									write(message_pipe, &colneu, sizeof(struct ddvd_color));
							}
							msg=DDVD_NULL;
							//CHANGE COLORMAP
							
							memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres); //clear screen ..
							//copy button into screen
							for (i = hl.sy; i < hl.ey; i++)
							{
								if (ddvd_screeninfo_bypp == 1)
									memcpy(p_lfb+hl.sx+ddvd_screeninfo_xres*(i), ddvd_lbb+hl.sx+ddvd_screeninfo_xres*(i), hl.ex-hl.sx);
								else
									ddvd_blit_to_argb(p_lfb+hl.sx*ddvd_screeninfo_bypp+ddvd_screeninfo_stride*i, ddvd_lbb+hl.sx+ddvd_screeninfo_xres*(i), hl.ex-hl.sx);
							}
							libdvdnav_workaround=1;
						}
						if (!libdvdnav_workaround)
							memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres); //clear screen .. 
						
						msg=DDVD_SCREEN_UPDATE;
						write(message_pipe, &msg, sizeof(int));
					} 
					else {
						ddvd_clear_buttons=0;
						//printf("clear buttons\n");
					}
					break;
				
				case DVDNAV_VTS_CHANGE:
					/* Some status information like video aspect and video scale permissions do
					* not change inside a VTS. Therefore we will set it new at this place */
					ddvd_play_empty(TRUE);
					audio_lock=0; // reset audio & spu lock
					spu_lock=0;
					audio_format[0]=audio_format[1]=audio_format[2]=audio_format[4]=audio_format[4]=audio_format[5]=audio_format[6]=audio_format[7]=-1;
					
					dvd_aspect = dvdnav_get_video_aspect(dvdnav);
					dvd_scale_perm = dvdnav_get_video_scale_permission(dvdnav);
					tv_scale=ddvd_check_aspect(dvd_aspect, dvd_scale_perm, tv_aspect);
					//printf("DVD Aspect: %d TV Aspect: %d Scale: %d Allowed: %d\n",dvd_aspect,tv_aspect,tv_scale,dvd_scale_perm);
					break;
				
				case DVDNAV_CELL_CHANGE:
					/* Store new cell information */
					{
						memcpy(&ddvd_lastCellEventInfo, buf, sizeof(dvdnav_cell_change_event_t));
						
						if ((ddvd_still_frame & CELL_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
							ddvd_iframesend=1;

						ddvd_still_frame = (dvdnav_get_next_still_flag(dvdnav) != 0) ? CELL_STILL : 0;
					}
					break;
					
				case DVDNAV_NAV_PACKET:
					/* A NAV packet provides PTS discontinuity information, angle linking information and
					* button definitions for DVD menus. We have to handle some stilframes here */
					{
						pci = dvdnav_get_current_nav_pci(dvdnav);
						dsi = dvdnav_get_current_nav_dsi(dvdnav);

						if ((ddvd_still_frame & NAV_STILL) && ddvd_iframesend == 0 && ddvd_last_iframe_len)
							ddvd_iframesend=1;

						if (dsi->vobu_sri.next_video == 0xbfffffff)
							ddvd_still_frame |= NAV_STILL;//|= 1;
						else
							ddvd_still_frame &= ~NAV_STILL;//&= 1;
					}
					break;
					
				case DVDNAV_HOP_CHANNEL:
					/* This event is issued whenever a non-seamless operation has been executed.
					* So we drop our buffers */
					ddvd_play_empty(TRUE);
					break;
				
				case DVDNAV_STOP:
					/* Playback should end here. */
					printf("DVDNAV_STOP\n");
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
		ioctl(ddvd_output_fd, VIDEO_GET_PTS, &tpts);
		pts=(unsigned long long)tpts;
		signed long long diff=spts-pts;
		if (ddvd_spu_backnr > 0 && diff<=0xFF) // we only have a 32bit pts on vulcan/pallas (instead of 33bit) so we need some tolerance on syncing SPU for menus
										  // so on non animated menus the buttons will be displayed to soon, but we we have to accept it
#else
		ioctl(ddvd_fdvideo, VIDEO_GET_PTS, &pts);
		if (ddvd_spu_backnr > 0 && pts >= spu_backpts[0])
#endif			
		{
			int tmplen=(spu_backbuffer[0] << 8 | spu_backbuffer[1]);

			ddvd_display_time=ddvd_spu_decode_data(spu_backbuffer, tmplen); // decode
			ddvd_lbb_changed=1;

			struct ddvd_color colneu;
			int ctmp;
			msg=DDVD_COLORTABLE_UPDATE;
			if (ddvd_screeninfo_bypp == 1)
				write(message_pipe, &msg, sizeof(int));
			for (ctmp = 0;ctmp < 4; ctmp++) 
			{
				colneu.blue=ddvd_bl[ctmp+252];
				colneu.green=ddvd_gn[ctmp+252];
				colneu.red=ddvd_rd[ctmp+252];
				colneu.trans=ddvd_tr[ctmp+252];
				if (ddvd_screeninfo_bypp == 1)
					write(message_pipe, &colneu, sizeof(struct ddvd_color));
			}
			msg=DDVD_NULL;
			
			memcpy(spu_backbuffer,spu_backbuffer+tmplen,ddvd_spu_backptr-tmplen); // delete SPU packet
			spu_backpts[0]=spu_backpts[1];
			spu_backpts[1]=spu_backpts[2];
			ddvd_spu_backnr--;
			ddvd_spu_backptr-=tmplen;
			
			// set timer
			if (ddvd_display_time > 0)
			{
				ddvd_spu_timer_active=1;
				ddvd_spu_timer_end=ddvd_get_time()+(ddvd_display_time*10); //ms
			} 
			else
				ddvd_spu_timer_active=0;

			pci = dvdnav_get_current_nav_pci(dvdnav); //update highlight buttons
			dsi = dvdnav_get_current_nav_dsi(dvdnav);
			if(pci->hli.hl_gi.btn_ns > 0)
			{
				dvdnav_get_current_highlight(dvdnav, &buttonN);
				dvdnav_button_select(dvdnav, pci, buttonN);
				ddvd_lbb_changed=0;
				in_menu = 1;
			}
		}

		if (!in_menu)
		{
			if (!pci)
				pci = dvdnav_get_current_nav_pci(dvdnav);

			in_menu = pci && pci->hli.hl_gi.btn_ns > 0;
		}

		if ( in_menu && !playerconfig->in_menu)
		{
			int bla = DDVD_MENU_OPENED;
			write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu=1;
		}
		else if (!in_menu && playerconfig->in_menu)
		{
			int bla = DDVD_MENU_CLOSED;
			write(message_pipe, &bla, sizeof(int));
			playerconfig->in_menu=0;
		}

		if (ddvd_wait_for_user)
		{
			struct pollfd pfd[1];  // make new pollfd array
			pfd[0].fd = key_pipe;
			pfd[0].events = POLLIN|POLLPRI|POLLERR;
			poll(pfd, 1, -1);
		}

		//Userinput
		if (ddvd_readpipe(key_pipe, &rccode, sizeof(int),0) == sizeof(int))
		{
			int keydone=1;

			if (!dsi)
				dsi = dvdnav_get_current_nav_dsi(dvdnav);

			if (buttonN == -1)
				dvdnav_get_current_highlight(dvdnav, &buttonN);

			switch (rccode) // actions inside and outside of menu
			{
				case DDVD_SET_TITLE:
				{
					int title, totalTitles;
					playerconfig->in_menu=0;
					ddvd_readpipe(key_pipe, &title, sizeof(int),1);
					printf("DDVD_SET_TITLE %d\n",title);
					dvdnav_get_number_of_titles(dvdnav, &totalTitles);
					if ( title <= totalTitles )
					{
						if (in_menu)
						{
							playerconfig->in_menu=0;
							dvdnav_reset(dvdnav);
							dvdnav_title_play(dvdnav, title);
						}
						else
						{
							ddvd_play_empty(TRUE);
							dvdnav_part_play(dvdnav, title, 1);
						}
						msg=DDVD_SHOWOSD_TIME;
					}
					break;
				}
				case DDVD_SET_CHAPTER:
				{
					int chapter, totalChapters, chapterNo, titleNo;
					ddvd_readpipe(key_pipe, &chapter, sizeof(int),1);
					printf("DDVD_SET_CHAPTER %d\n",chapter);
					dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
					dvdnav_get_number_of_parts(dvdnav, titleNo, &totalChapters);
					if ( chapter <= totalChapters )
					{
						if (in_menu)
						{
							playerconfig->in_menu=0;
							dvdnav_reset(dvdnav);
							dvdnav_title_play(dvdnav, titleNo);
						}
						else
							ddvd_play_empty(TRUE);
						dvdnav_part_play(dvdnav, titleNo, chapter);
						msg=DDVD_SHOWOSD_TIME;
					}
					break;
				}
				case DDVD_SET_MUTE:
					ismute=1;
					break;
				case DDVD_UNSET_MUTE:
					ismute=0;
					break;
				default:
					keydone=0;
					break;
			}

			if(!keydone && in_menu)
			{
				switch (rccode) //Actions inside a Menu
				{
					case DDVD_KEY_UP: //Up
						dvdnav_upper_button_select(dvdnav, pci);
						break;
					case DDVD_KEY_DOWN: //Down
						dvdnav_lower_button_select(dvdnav, pci);
						break;
					case DDVD_KEY_LEFT: //left
						dvdnav_left_button_select(dvdnav, pci);
						break;
					case DDVD_KEY_RIGHT: //right
						dvdnav_right_button_select(dvdnav, pci);
						break;
					case DDVD_KEY_OK: //OK
						{
							ddvd_wait_for_user=0;
							memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres); //clear screen ..
							memset(ddvd_lbb, 0, ddvd_screeninfo_xres*ddvd_screeninfo_yres); //clear backbuffer
							msg=DDVD_SCREEN_UPDATE;
							write(message_pipe, &msg, sizeof(int));
							ddvd_clear_buttons=1;
							dvdnav_button_activate(dvdnav, pci);
							ddvd_play_empty(TRUE);
							if (ddvd_wait_timer_active)
								ddvd_wait_timer_active=0;
						}
						break;
					case DDVD_KEY_EXIT: //Exit
						printf("DDVD_KEY_EXIT (menu)\n");
						finished = 1;
						break;
					case DDVD_KEY_MENU: //Dream
						if (dvdnav_menu_call(dvdnav, DVD_MENU_Root) == DVDNAV_STATUS_OK)
							ddvd_play_empty(TRUE);
						break;
					case DDVD_KEY_AUDIOMENU: //Audio
						if (dvdnav_menu_call(dvdnav, DVD_MENU_Audio) == DVDNAV_STATUS_OK)
							ddvd_play_empty(TRUE);
						break;
					case DDVD_SKIP_FWD:
					case DDVD_SKIP_BWD:
						// we must empty the pipe here...
						ddvd_readpipe(key_pipe, &keydone, sizeof(int),1);
					default:
						break;
				}
			} 
			else if (!keydone) //Actions inside a Movie
			{
				switch (rccode) //Main Actions
				{
					case DDVD_KEY_PREV_CHAPTER: //left
					{
						int titleNo, chapterNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &chapterNumber);
						chapterNo--;
						if(chapterNo>chapterNumber) chapterNo=1;
						if(chapterNo<=0) chapterNo=chapterNumber;
						dvdnav_part_play(dvdnav, titleNo, chapterNo);
						ddvd_play_empty(TRUE);
						msg=DDVD_SHOWOSD_TIME;
						break;
					}
					case DDVD_KEY_NEXT_CHAPTER: //right
					{
						int titleNo, chapterNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_parts(dvdnav, titleNo, &chapterNumber);
						chapterNo++;
						if(chapterNo>chapterNumber) chapterNo=1;
						if(chapterNo<=0) chapterNo=chapterNumber;
						dvdnav_part_play(dvdnav, titleNo, chapterNo);
						ddvd_play_empty(TRUE);
						msg=DDVD_SHOWOSD_TIME;
						break;
					}
					case DDVD_KEY_PREV_TITLE:
					{
						int titleNo, titleNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_titles(dvdnav, &titleNumber);
						titleNo--;
						if(titleNo>titleNumber) titleNo=1;
						if(titleNo<=0) titleNo=titleNumber;
						dvdnav_part_play(dvdnav, titleNo, 1);
						ddvd_play_empty(TRUE);
						msg=DDVD_SHOWOSD_TIME;
						break;
					}
					case DDVD_KEY_NEXT_TITLE:
					{
						int titleNo, titleNumber, chapterNo;
						dvdnav_current_title_info(dvdnav, &titleNo, &chapterNo);
						dvdnav_get_number_of_titles(dvdnav, &titleNumber);
						titleNo++;
						if(titleNo>titleNumber) titleNo=1;
						if(titleNo<=0) titleNo=titleNumber;
						dvdnav_part_play(dvdnav, titleNo, 1);
						ddvd_play_empty(TRUE);
						msg=DDVD_SHOWOSD_TIME;
						break;
					}
					case DDVD_KEY_PAUSE: // Pause
					{
						if (ddvd_playmode == PLAY)
						{
							ddvd_playmode=PAUSE;
							ioctl(ddvd_fdaudio, AUDIO_PAUSE);
							ioctl(ddvd_fdvideo, VIDEO_FREEZE);
							msg=DDVD_SHOWOSD_STATE_PAUSE;
							write(message_pipe, &msg, sizeof(int));
							break;
						}
						else if (ddvd_playmode != PAUSE)
							break;
						// fall through to PLAY
					}
					case DDVD_KEY_PLAY: // Play
					{
						if (ddvd_playmode == PAUSE || ddvd_trickmode)
						{
							ddvd_playmode=PLAY;
key_play:
#if CONFIG_API_VERSION == 1								
							ddvd_device_clear();
#endif								
							if (ddvd_trickmode && !ismute)
								ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 0);
							ddvd_trickmode=TOFF;
							if (ddvd_playmode == PLAY)
							{
								ioctl(ddvd_fdaudio, AUDIO_CONTINUE);
								ioctl(ddvd_fdvideo, VIDEO_CONTINUE);
								msg=DDVD_SHOWOSD_STATE_PLAY;
								write(message_pipe, &msg, sizeof(int));
							}
							msg=DDVD_SHOWOSD_TIME;
						}
						break;
					}
					case DDVD_KEY_MENU: //Dream
						if (dvdnav_menu_call(dvdnav, DVD_MENU_Root) == DVDNAV_STATUS_OK)
							ddvd_play_empty(TRUE);
						break;
					case DDVD_KEY_AUDIOMENU: //Audio
						if (dvdnav_menu_call(dvdnav, DVD_MENU_Audio) == DVDNAV_STATUS_OK)
							ddvd_play_empty(TRUE);
						break;
					case DDVD_KEY_EXIT: //EXIT 
					{
						printf("DDVD_KEY_EXIT\n");
						finished = 1;
						break;
					}
					case DDVD_KEY_FFWD: //FastForward
					case DDVD_KEY_FBWD: //FastBackward
					{
						if (ddvd_trickmode == TOFF)
						{
							ioctl(ddvd_fdaudio, AUDIO_SET_MUTE, 1);
							ddvd_trickspeed = 2;
							ddvd_trickmode = (rccode == DDVD_KEY_FBWD ? FASTBW : FASTFW);
						}
						else if (ddvd_trickmode == (rccode == DDVD_KEY_FBWD ? FASTFW : FASTBW))
						{
							ddvd_trickspeed /= 2;
							if (ddvd_trickspeed==1)
							{
								ddvd_trickspeed=0;
								goto key_play;
							}
						}
						else if (ddvd_trickspeed < 64)
							ddvd_trickspeed *= 2;
						break;
					}
					case DDVD_KEY_AUDIO: //change audio track 
					{
						uint16_t audio_lang=0xFFFF;
						while (audio_lang == 0xFFFF)
						{
							audio_id++;
							audio_lang=dvdnav_audio_stream_to_lang(dvdnav, audio_id);
							if (audio_lang == 0xFFFF)
								audio_id=-1;
						}
						ddvd_play_empty(TRUE);
						audio_lock=1;
						ddvd_lpcm_count=0;
						msg=DDVD_SHOWOSD_AUDIO;
						write(message_pipe, &msg, sizeof(int));
						write(message_pipe, &audio_id, sizeof(int));
						write(message_pipe, &audio_lang, sizeof(uint16_t));
						write(message_pipe, &audio_format[audio_id], sizeof(int));
						break;
					}
					case DDVD_KEY_SUBTITLE: //change spu track 
					{
						uint16_t spu_lang=0xFFFF;
						while (spu_lang == 0xFFFF)
						{
							spu_active_id++;
							spu_lang=dvdnav_spu_stream_to_lang(dvdnav, spu_active_id);
							if (spu_lang == 0xFFFF)
							{
								spu_lang=0x2D2D; // SPU "off" 
								spu_active_id=-1;
							}
						}				
						spu_lock=1;
						msg=DDVD_SHOWOSD_SUBTITLE;
						write(message_pipe, &msg, sizeof(int));
						write(message_pipe, &spu_active_id, sizeof(int));
						write(message_pipe, &spu_lang, sizeof(uint16_t));
						break;
					}
					case DDVD_GET_TIME: // frontend wants actual time
						msg=DDVD_SHOWOSD_TIME;
						break;	
					case DDVD_SKIP_FWD:
					case DDVD_SKIP_BWD:
					{
						int skip;
						ddvd_readpipe(key_pipe, &skip, sizeof(int),1);
						if (ddvd_trickmode == TOFF)
						{
							uint32_t pos, len;
							dvdnav_get_position(dvdnav, &pos, &len);
							printf("DDVD_SKIP pos=%u len=%u \n",pos,len);
							//90000 = 1 Sek.
							if (!len)
								len=1;
							int64_t posneu=((pos*ddvd_lastCellEventInfo.pgc_length)/len)+(90000*skip);
							printf("DDVD_SKIP posneu1=%lld\n",posneu);
							int64_t posneu2=posneu <= 0 ? 0 : (posneu*len)/ddvd_lastCellEventInfo.pgc_length;
							printf("DDVD_SKIP posneu2=%lld\n",posneu2);
							if (len && posneu2 && posneu2 >= len) // reached end of movie
							{
								posneu2 = len - 250;
								reached_eof=1;
							}
							dvdnav_sector_search(dvdnav, posneu2, SEEK_SET);
							ddvd_lpcm_count=0;
							msg=DDVD_SHOWOSD_TIME;
						}
						break;
					}
					default:
						break;
				}
			}
		}
		
			
		// spu handling
		if (ddvd_lbb_changed == 1)
		{
			if (ddvd_screeninfo_bypp == 1)
				memcpy(p_lfb,ddvd_lbb,ddvd_screeninfo_xres*ddvd_screeninfo_yres); //copy SPU backbuffer into screen
			else {
				int i=0;
				for (; i < ddvd_screeninfo_yres; ++i)
					ddvd_blit_to_argb(p_lfb+i*ddvd_screeninfo_stride,ddvd_lbb+i*ddvd_screeninfo_xres,ddvd_screeninfo_xres);
			}
			int msg_old=msg; // save and restore msg it may not bee empty
			msg=DDVD_SCREEN_UPDATE;
			write(message_pipe, &msg, sizeof(int));
			msg=msg_old;
			ddvd_lbb_changed=0;
		}

	}

	/* destroy dvdnav handle */
	if (dvdnav_close(dvdnav) != DVDNAV_STATUS_OK) 
	{
		printf("Error on dvdnav_close: %s\n", dvdnav_err_to_string(dvdnav));
		return;
	}
	
	ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER);
	ioctl(ddvd_fdvideo, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_DEMUX );
	ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER);
	ioctl(ddvd_fdaudio, AUDIO_SELECT_SOURCE, AUDIO_SOURCE_DEMUX );

	ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 ); // restore AudioDecoder State
	ioctl(ddvd_fdaudio, AUDIO_SET_BYPASS_MODE, 1 );	
	close(ddvd_output_fd);
	close(ddvd_fdaudio);
	close(ddvd_fdvideo);
	close(ddvd_ac3_fd);
	
	if (have_liba52)
	{
		a52_free (state);
		ddvd_close_liba52();
	}
	
	//Clear Screen
	memset(p_lfb, 0, ddvd_screeninfo_stride*ddvd_screeninfo_yres);
	msg=DDVD_SCREEN_UPDATE;
	write(message_pipe, &msg, sizeof(int));
	
	// clean up
	free(ddvd_lbb);
	free(last_iframe);
	free(spu_buffer);
	free(spu_backbuffer);
	
#if CONFIG_API_VERSION == 3
	ddvd_unset_pcr_offset();
#endif
	
} 

/*
 * internal functions 
 */

// reading from pipe

int ddvd_readpipe(int pipefd, void *dest, int bytes, int blocked_read)
{
	int bytes_completed = 0;
	while(1)
	{
		int rd = read(pipefd, dest+bytes_completed, bytes-bytes_completed);
		if (rd < 0)
		{
			int error = errno;
			if (error = -EAGAIN)
			{
				if (blocked_read || bytes_completed) {
					usleep(1);
					continue;
				}
				break; // leave while loop
			}
			/* else if (error == ????) // hier sollte evtl noch geschaut werden welcher error code kommt wenn die pipe geschlossen wurde... 
				break; */
			printf("unhandled read error %d(%m)\n", error);
		}
		bytes_completed += rd;
		if (bytes_completed == bytes) // all bytes read
			break;
		if (!blocked_read && !bytes_completed)
			break;
	}
	return bytes_completed;
}

// get actual playing time

struct ddvd_time ddvd_get_osd_time(struct ddvd *playerconfig)
{
	int titleNo;
	struct ddvd_time info;
	uint32_t pos, len;
	info.pos_minutes=info.pos_hours=info.pos_seconds=info.pos_chapter=info.pos_title=0;
	info.end_minutes=info.end_hours=info.end_seconds=info.end_chapter=0;
	dvdnav_get_number_of_titles(dvdnav, &info.end_title);

	dvdnav_current_title_info(dvdnav, &titleNo, &info.pos_chapter);
	if (titleNo)
	{
		dvdnav_get_number_of_parts(dvdnav, titleNo, &info.end_chapter);
		dvdnav_get_position_in_title(dvdnav, &pos, &len);
		
		uint64_t len_s=ddvd_lastCellEventInfo.pgc_length/90000;
		uint64_t pos_s=((ddvd_lastCellEventInfo.pgc_length/len)*pos)/90000;
		
		info.pos_minutes=pos_s/60;
		info.pos_hours=info.pos_minutes/60;
		info.pos_minutes=info.pos_minutes-(info.pos_hours*60);
		info.pos_seconds=pos_s-((info.pos_hours*60)+info.pos_minutes)*60;
		info.end_minutes=len_s/60;
		info.end_hours=info.end_minutes/60;
		info.end_minutes=info.end_minutes-(info.end_hours*60);
		info.end_seconds=len_s-((info.end_hours*60)+info.end_minutes)*60;

		info.pos_title = titleNo;
	}
	playerconfig->next_time_update = ddvd_get_time() + 1000;
	
	return info;
}

// video out aspect/scale

int ddvd_check_aspect(int dvd_aspect, int dvd_scale_perm, int tv_aspect)
{
	char aspect[20],policy[20],policy2[20],saa2[20];
	int tv_scale=0;
	int saa;
	if (tv_aspect == 2) // tv 16:9
	{
		if (dvd_aspect == 0)
		{
			sprintf(aspect,"4:3");
			sprintf(policy,"VIDEO_PAN_SCAN"); //no scaling needed
			sprintf(policy2,"panscan");
			tv_scale=0; //off
			saa=SAA_WSS_43F;
			sprintf(saa2,"4:3_full_format");
		}
		else 
		{
			sprintf(aspect,"16:9");
			sprintf(policy,"VIDEO_CENTER_CUT_OUT"); //no scaling neededVIDEO_CENTER_CUT_OUT
			sprintf(policy2,"panscan");
			tv_scale=0; //off
			saa=SAA_WSS_169F;
			sprintf(saa2,"16:9_full_format");
		}
	}
	else if (tv_aspect == 3) // tv always 16:9
	{
		sprintf(aspect,"16:9");
		sprintf(policy,"VIDEO_CENTER_CUT_OUT"); //no scaling needed
		sprintf(policy2,"panscan");
		tv_scale=0; //off
		saa=SAA_WSS_169F;
		sprintf(saa2,"16:9_full_format");
	}
	else // tv 4:3
	{
		sprintf(aspect,"4:3");
		saa=SAA_WSS_43F;
		sprintf(saa2,"4:3_full_format");
		if (dvd_aspect == 0) // dvd 4:3 ...
		{
			sprintf(policy,"VIDEO_PAN_SCAN"); //no scaling needed
			sprintf(policy2,"panscan");
			tv_scale=0; //off
		}
		else // dvd 16:9 ... need scaling prefer letterbox (tv_aspect 0)
		{
			if (!(dvd_scale_perm & 1)) // letterbox allowed
			{
				sprintf(policy,"VIDEO_LETTER_BOX");
				sprintf(policy2,"letterbox");
				tv_scale=1; //letterbox
			}
			else if (!(dvd_scale_perm & 2)) // panscan allowed
			{
				sprintf(policy,"VIDEO_PAN_SCAN");
				sprintf(policy2,"panscan");
				tv_scale=2; //panscan
			}
			else // should not happen
			{
				sprintf(policy,"VIDEO_LETTER_BOX");
				sprintf(policy2,"letterbox");
				tv_scale=1; //off
			}
		}
		if (tv_aspect == 1 && tv_scale == 1 && !(dvd_scale_perm & 2)) // prefer panscan if allowed (tv_aspect 1)
		{
			sprintf(policy,"VIDEO_PAN_SCAN");
			sprintf(policy2,"panscan");
			tv_scale=2; //panscan
		}
	}
	
	ioctl(ddvd_fdvideo, VIDEO_SET_DISPLAY_FORMAT, policy);
	
	int saafd=open("/dev/dbox/saa0", O_RDWR);
	ioctl(saafd,SAAIOSWSS,&saa);
	close(saafd);

	char filename[128];	

	// switch video aspect
	snprintf(filename, 128, "/proc/stb/video/aspect");
	FILE *f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, aspect);//aspect
		fclose(f);
	}
	// switch video scaling
	snprintf(filename, 128, "/proc/stb/video/policy");
	f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, policy2);
		fclose(f);
	}
	// switch wss
	snprintf(filename, 128, "/proc/stb/denc/0/wss");
	f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, saa2);
		fclose(f);
	}
	return tv_scale;
}

// get timestamp

uint64_t ddvd_get_time(void)
{
	static time_t t0 = 0;
	struct timeval t;
	if (gettimeofday(&t, NULL) == 0) {
		if (t0 == 0)
			t0 = t.tv_sec; // this avoids an overflow (we only work with deltas)
		return (uint64_t)(t.tv_sec - t0) * 1000 + (uint64_t)(t.tv_usec / 1000);
	}
	return 0;
}

// Empty all Buffers

void ddvd_play_empty (int device_clear)
{
	ddvd_wait_for_user = 0;
	ddvd_clear_buttons = 0;
	ddvd_lpcm_count=0;
	ddvd_iframerun=0;
	ddvd_still_frame=0;
	ddvd_iframesend=0;
	ddvd_last_iframe_len=0;
	ddvd_spu_ptr=0;
	ddvd_spu_backnr=0;
	ddvd_spu_backptr=0;
	
	ddvd_wait_timer_active=0;
	ddvd_wait_timer_end=0;

	ddvd_spu_timer_active=0;
	ddvd_spu_timer_end=0;
	
	//memset(ddvd_lbb, 0, ddvd_screeninfo_xres*ddvd_screeninfo_yres); //clear SPU backbuffer
	//ddvd_lbb_changed=1;
	
	if (device_clear)
	{
		ddvd_device_clear();
	}
}

// Empty Device Buffers

void ddvd_device_clear (void)
{
	ioctl(ddvd_fdaudio, AUDIO_STOP);
	ioctl(ddvd_fdaudio, AUDIO_CLEAR_BUFFER);
	ioctl(ddvd_fdaudio, AUDIO_PLAY);
	
	ioctl(ddvd_fdvideo, VIDEO_CLEAR_BUFFER);
	ioctl(ddvd_fdvideo, VIDEO_PLAY);
	
	ioctl(ddvd_fdaudio, AUDIO_SET_AV_SYNC, 1 );
}

// SPU Decoder

int ddvd_spu_decode_data( uint8_t * buffer, int len )
{

	int x1spu,x2spu,y1spu,y2spu,xspu,yspu;
	int offset[2];
	int size,datasize,controlsize,aligned,id;
	int menubutton=0;
	int display_time=-1;
	
	size=(buffer[0] << 8 | buffer[1]);
	datasize=(buffer[2] << 8 | buffer[3]);
	controlsize=(buffer[datasize+2] << 8 | buffer[datasize+3]);
	
	//printf("SPU_dec: Size: %X Datasize: %X Controlsize: %X\n",size,datasize,controlsize);

	// parse header
	
	int i = datasize + 4;

	while (i < size && buffer[i] != 0xFF)
	{
		switch (buffer[i])
		{
			case 0x00: /* menu button special color handling */
				menubutton=1;
				memset(ddvd_lbb, 0, ddvd_screeninfo_xres*ddvd_screeninfo_yres); //clear backbuffer
				i++;
				break;
			case 0x01: /* show */
				i++;
				break;
			case 0x02: /* hide */
				i++;
				break;
			case 0x03: /* palette */
				{
					ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer+i+1);
					//printf("update palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

					ddvd_bl[3+252]=ddvd_bl[clut->entry0];
					ddvd_gn[3+252]=ddvd_gn[clut->entry0];
					ddvd_rd[3+252]=ddvd_rd[clut->entry0];

					ddvd_bl[2+252]=ddvd_bl[clut->entry1];
					ddvd_gn[2+252]=ddvd_gn[clut->entry1];
					ddvd_rd[2+252]=ddvd_rd[clut->entry1];

					ddvd_bl[1+252]=ddvd_bl[clut->entry2];
					ddvd_gn[1+252]=ddvd_gn[clut->entry2];
					ddvd_rd[1+252]=ddvd_rd[clut->entry2];

					ddvd_bl[0+252]=ddvd_bl[clut->entry3];
					ddvd_gn[0+252]=ddvd_gn[clut->entry3];
					ddvd_rd[0+252]=ddvd_rd[clut->entry3];

					//CHANGE COLORMAP
					
					i += 3;
				}
				break;
			case 0x04: /* transparency palette */
				{
					ddvd_spudec_clut_t *clut = (ddvd_spudec_clut_t *) (buffer+i+1);
					//printf("update transp palette %d %d %d %d\n", clut->entry0, clut->entry1, clut->entry2, clut->entry3);

					ddvd_tr[0+252]=(0xF-clut->entry3)*0x1111;
					ddvd_tr[1+252]=(0xF-clut->entry2)*0x1111;
					ddvd_tr[2+252]=(0xF-clut->entry1)*0x1111;
					ddvd_tr[3+252]=(0xF-clut->entry0)*0x1111;

					//CHANGE COLORMAP
					
					i += 3;
				}
				break;
			case 0x05: /* image coordinates */
				//printf("image coords\n");
				xspu = x1spu = (((unsigned int)buffer[i+1]) << 4) + (buffer[i+2] >> 4);
				yspu = y1spu = (((unsigned int)buffer[i+4]) << 4) + (buffer[i+5] >> 4);
				x2spu = (((buffer[i+2] & 0x0f) << 8) + buffer[i+3] );
				y2spu = (((buffer[i+5] & 0x0f) << 8) + buffer[i+6] );
				//printf("%d %d %d %d\n", xspu, yspu, x2spu, y2spu);
				i += 7;
				break;				
			case 0x06: /* image 1 / image 2 offsets */
				//printf("image offsets\n");
				offset[0] = (((unsigned int)buffer[i+1]) << 8) + buffer[i+2];
				offset[1] = (((unsigned int)buffer[i+3]) << 8) + buffer[i+4];
				//printf("%d %d\n", offset[0], offset[1]);
				i += 5;
				break;
			default:
				i++;
				break;
		}
	}
	
	//get display time
	if (i+6 <= size)
	{
		if (buffer[i+5] == 0x02 && buffer[i+6] == 0xFF)
		{
			display_time=((buffer[i+1]<<8)+buffer[i+2]);
			//printf("Display Time: %d\n",ddvd_display_time);
		}
	}
	
	//printf("SPU_dec: Image coords x1: %d y1: %d x2: %d y2: %d\n",x1spu,y1spu,x2spu,y2spu);
	//printf("Offset[0]: %X Offset[1]: %X\n",offset[0],offset[1]);
	
	// parse picture
	
	aligned=1;
	id=0;
	
	while (offset[1] < datasize + 2 && yspu <= 575) // there are some special cases the decoder tries to write more than 576 lines in our buffer and we dont want this ;)
	{
		u_int len;
		u_int code;

		code = (aligned?(buffer[offset[id]++]>>4):(buffer[offset[id]-1]&0xF));aligned=aligned?0:1;

		if (code < 0x0004) 
		{
			code = (code << 4) | (aligned?(buffer[offset[id]++]>>4):(buffer[offset[id]-1]&0xF));aligned=aligned?0:1;
			if (code < 0x0010) 
			{
				code = (code << 4) | (aligned?(buffer[offset[id]++]>>4):(buffer[offset[id]-1]&0xF));aligned=aligned?0:1;
				if (code < 0x0040) 
				{
					code = (code << 4) | (aligned?(buffer[offset[id]++]>>4):(buffer[offset[id]-1]&0xF));aligned=aligned?0:1;
				}
			}
		}

		len   = code >> 2;

		if (len == 0)
		  len = x2spu-xspu;

		memset(ddvd_lbb + xspu + ddvd_screeninfo_xres*(yspu), (code & 3)+252, len); //drawpixel into backbuffer
		xspu+=len;
		if (xspu >= x2spu)
		{
			if (!aligned)
			{
				code = (aligned?(buffer[offset[id]++]>>4):(buffer[offset[id]-1]&0xF));aligned=aligned?0:1;
			}
			xspu = x1spu; //next line
			yspu++;
			id=id?0:1;
		}
	}	

	return display_time;	
}

// blit to argb in 32bit mode

void ddvd_blit_to_argb(void *_dst, void *_src, int pix)
{
	unsigned long *dst = _dst;
	unsigned char *src = _src;
	while (pix--)
	{
		int p = (*src++);
		int a, r, g, b;
		if (p == 0)
		{
			r=g=b=a=0; //clear screen (transparency)
		} 
		else
		{
			a = 0xFF - (ddvd_tr[p]>>8);
			r = ddvd_rd[p]>>8;
			g = ddvd_gn[p]>>8;
			b = ddvd_bl[p]>>8;
		}
		*dst++ = (a << 24) | (r << 16) | (g << 8) | (b << 0);
	}
}


#if CONFIG_API_VERSION == 3

// set decoder buffer offsets to a minimum

void ddvd_set_pcr_offset()
{
	char filename[128];	
	snprintf(filename, 128, "/proc/stb/pcr/pcr_stc_offset");
	FILE *f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, "200");
		fclose(f);
	}
	snprintf(filename, 128, "/proc/stb/vmpeg/0/sync_offset");
	f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, "200");
		fclose(f);
	}
}

// reset decoder buffer offsets 

void ddvd_unset_pcr_offset()
{
	char filename[128];	
	snprintf(filename, 128, "/proc/stb/pcr/pcr_stc_offset");
	FILE *f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, "2710");
		fclose(f);
	}
	snprintf(filename, 128, "/proc/stb/vmpeg/0/sync_offset");
	f = fopen(filename, "w");
	if (f)
	{
		fprintf(f, "2710");
		fclose(f);
	}
}

#endif
