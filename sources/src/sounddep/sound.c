 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Support for the Mute sound system
  *
  * Copyright 1997 Bernd Schmidt
  * Copyright 2003 Richard Drummond
  */



#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#ifndef __CELLOS_LV2__ 
#include "memory.h"
#endif
#include "custom.h"
#include "audio.h"
#include "gensound.h"

#include "driveclick.h"
#include "sounddep/sound.h"

#define LOG_MSG(x) printf(x);
#define LOG_MSG2(...)

uae_u16 *paula_sndbuffer = NULL;
uae_u16 *paula_sndbufpt;
int paula_sndbufsize;
int sound_initialized = 0;
int soundcheck = 0;
int have_sound = 0;

unsigned long last_time = 0;
unsigned long now_time;

unsigned long stat_time;
unsigned long stat_count;

static unsigned long scaled_sample_evtime;

void sound_mute (int newmute)
{
}

void set_volume (int volume, int mute)
{
	config_changed = 1;
}

static int setget_master_volume_linux (int setvolume, int *volume, int *mute)
{
	unsigned int ok = 0;

	if (setvolume) {
		;//set
	} else {
		;//get
	}

	return ok;
}

static int set_master_volume (int volume, int mute)
{
	return setget_master_volume_linux (1, &volume, &mute);
}

static int get_master_volume (int *volume, int *mute)
{
    *volume = 0;
    *mute = 0;
	return setget_master_volume_linux (0, volume, mute);
}

void master_sound_volume (int dir)
{
    int vol, mute, r;

    r = get_master_volume (&vol, &mute);
    if (!r)
            return;
    if (dir == 0)
            mute = mute ? 0 : 1;
    vol += dir * (65536 / 10);
    if (vol < 0)
            vol = 0;
    if (vol > 65535)
            vol = 65535;
    set_master_volume (vol, mute);
    config_changed = 1;
}

void restart_sound_buffer (void)
{
}

int init_sound (void)
{
    if (sndbuffer != NULL) {
#ifdef DRIVESOUND
        driveclick_init();
#endif	
        return 1;
    }

    if (sound_initialized) {
        return 1;
    }
 
    //LOG_MSG(("INIT SOUND\n"));
    sndbuffer = (uae_u16*) malloc(DEFAULT_SOUND_MINB);
    if (sndbuffer == NULL) {
        LOG_MSG(("sound init failed\n"));
        return 0;
    }
   
    sndbufsize = DEFAULT_SOUND_MINB;
    obtainedfreq = DEFAULT_SOUND_FREQ;
    sndbufpt = sndbuffer;
    sample_handler =  sample16s_handler;
    sound_initialized = 1;

    //init_sound_table16();
    //scaled_sample_evtime = (unsigned long)(MAXHPOS_PAL * MAXVPOS_PAL * VBLANK_HZ_PAL + rate - 1) / DEFAULT_SOUND_FREQ;
    //LOG_MSG2("bufsize=%i\n", sndbufsize);

#ifdef DRIVESOUND
	driveclick_init();
#endif
    return 1;
}

int setup_sound (void)
{
    //LOG_MSG(("setup_sound\n"));
    sound_available = 1;
    have_sound = 1;
    return 1;
}

void close_sound (void)
{
}

void reset_sound (void)
{
}

void pause_sound (void)
{
}

void resume_sound (void)
{
}

void sound_volume (int dir)
{
}

/*
 * Handle audio specific cfgfile options
 */
void audio_default_options (struct uae_prefs *p)
{
}

int audio_parse_option (struct uae_prefs *p, const char *option, const char *value)
{
    return 0;
}

void audio_save_options (FILE *f, const struct uae_prefs *p)
{
}
