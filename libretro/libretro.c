#include "libretro.h"
#include "retrodep/retroglue.h"
#include "libretro-mapper.h"
#include "libretro-glue.h"
#include "retro_files.h"
#include "retro_strings.h"
#include "retro_disk_control.h"
#include "uae_types.h"

#include "sysdeps.h"
#include "uae.h"
#include "options.h"
#include "inputdevice.h"
#include "savestate.h"
#include "custom.h"

extern void m68k_go (int, int resume);

// LOG
static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
}

retro_log_printf_t log_cb = fallback_log;

#define EMULATOR_DEF_WIDTH 720
#define EMULATOR_DEF_HEIGHT 568
#define EMULATOR_MAX_WIDTH 1024
#define EMULATOR_MAX_HEIGHT 1024

#define UAE_HZ_PAL 49.9201
#define UAE_HZ_NTSC 59.8251

#if EMULATOR_DEF_WIDTH < 0 || EMULATOR_DEF_WIDTH > EMULATOR_MAX_WIDTH || EMULATOR_DEF_HEIGHT < 0 || EMULATOR_DEF_HEIGHT > EMULATOR_MAX_HEIGHT
#error EMULATOR_DEF_WIDTH || EMULATOR_DEF_HEIGHT
#endif

int defaultw = EMULATOR_DEF_WIDTH;
int defaulth = EMULATOR_DEF_HEIGHT;
int retrow = 0;
int retroh = 0;
char key_state[512];
char key_state2[512];
bool opt_use_whdload_hdf = false;
int pix_bytes = 2;
static bool pix_bytes_initialized = false;
bool fake_ntsc = false;
bool real_ntsc = false;
bool request_update_av_info = false;

#if defined(NATMEM_OFFSET)
extern uae_u8 *natmem_offset;
extern uae_u32 natmem_size;
#endif
unsigned short int bmp[EMULATOR_MAX_WIDTH*EMULATOR_MAX_HEIGHT];
extern int SHIFTON;
extern char RPATH[512];
static int firstpass = 1;
extern int prefs_changed;
unsigned int video_config = 0;
unsigned int video_config_old = 0;
unsigned int video_config_geometry = 0;
unsigned int video_config_allow_hz_change = 0;
unsigned int inputdevice_finalized = 0;

static char *uae_argv[] = { "puae", RPATH };

extern void retro_poll_event(void);
unsigned int uae_devices[2];
extern int cd32_pad_enabled[NORMAL_JPORTS];

static char buf[64][4096]={0};

#ifdef WIN32
#define DIR_SEP_STR "\\"
#else
#define DIR_SEP_STR "/"
#endif

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;

const char *retro_save_directory;
const char *retro_system_directory;
const char *retro_content_directory;

// Disk control context
static dc_storage* dc;

// Amiga models
// chipmem_size 1 = 0.5MB, 2 = 1MB, 4 = 2MB
// bogomem_size 2 = 0.5MB, 4 = 1MB, 6 = 1.5MB, 7 = 1.8MB

#define A500 "\
cpu_type=68000\n\
chipmem_size=1\n\
bogomem_size=2\n\
chipset_compatible=A500\n\
chipset=ocs\n"

#define A500OG "\
cpu_type=68000\n\
chipmem_size=1\n\
bogomem_size=0\n\
chipset_compatible=A500\n\
chipset=ocs\n"

#define A500PLUS "\
cpu_type=68000\n\
chipmem_size=2\n\
bogomem_size=4\n\
chipset_compatible=A500+\n\
chipset=ecs\n"

#define A600 "\
cpu_type=68000\n\
chipmem_size=4\n\
fastmem_size=8\n\
chipset_compatible=A600\n\
chipset=ecs\n"

#define A1200 "\
cpu_type=68ec020\n\
chipmem_size=4\n\
fastmem_size=8\n\
chipset_compatible=A1200\n\
chipset=aga\n"

#define A1200OG "\
cpu_type=68ec020\n\
chipmem_size=4\n\
fastmem_size=0\n\
chipset_compatible=A1200\n\
chipset=aga\n"


// Amiga kickstarts
#define A500_ROM    "kick34005.A500"
#define A500KS2_ROM "kick37175.A500"
#define A600_ROM    "kick40063.A600"
#define A1200_ROM   "kick40068.A1200"

#define PUAE_VIDEO_PAL 		0x01
#define PUAE_VIDEO_NTSC 	0x02
#define PUAE_VIDEO_HIRES 	0x04

#define PUAE_VIDEO_PAL_HI 	PUAE_VIDEO_PAL|PUAE_VIDEO_HIRES
#define PUAE_VIDEO_NTSC_HI 	PUAE_VIDEO_NTSC|PUAE_VIDEO_HIRES

static char uae_machine[256];
static char uae_kickstart[16];
static char uae_config[1024];

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_controller_description p1_controllers[] ={
      { "Joystick", RETRO_DEVICE_JOYPAD },
      { "None", RETRO_DEVICE_NONE },
   };
   static const struct retro_controller_description p2_controllers[] = {
      { "Joystick", RETRO_DEVICE_JOYPAD },
      { "None", RETRO_DEVICE_NONE },
   };

   static const struct retro_controller_info ports[] = {
      { p1_controllers, 2 }, // port 1
      { p2_controllers, 2 }, // port 2
      { NULL, 0 }
   };

   static struct retro_core_option_definition core_options[] =
   {
      /*{
         "puae_model",
         "Model",
         "Restart required.",
         {
            { "A500", "A500 (512KB Chip + 512KB Slow)" },
            { "A500OG", "A500 (512KB Chip)" },
            { "A500PLUS", "A500+ (1MB Chip + 1MB Slow)" },
            { "A600", "A600 (2MB Chip + 8MB Fast)" },
            { "A1200", "A1200 (2MB Chip + 8MB Fast)" },
            { "A1200OG", "A1200 (2MB Chip)" },
            { NULL, NULL },
         },
         "A500"
      },*/
      {
         "puae_video_standard",
         "Video Standard",
         "",
         {
            { "PAL", NULL },
            { "NTSC", NULL },
            { NULL, NULL },
         },
         "PAL"
      },
      /*{
         "puae_gfx_colors",
         "Color Depth",
         "24-bit is slower and not available on all platforms. Restart required.",
         {
            { "16bit", "Thousands (16-bit)" },
            { "24bit", "Millions (24-bit)" },
            { NULL, NULL },
         },
         "16bit"
      },*/
      /*{
         "puae_collision_level",
         "Collision Level",
         "'Sprites and Playfields' is recommended.",
         {
            { "playfields", "Sprites and Playfields" },
            { "sprites", "Sprites only" },
            { "full", "Full" },
            { "none", "None" },
            { NULL, NULL },
         },
         "playfields"
      },*/
      /*{
         "puae_cpu_compatibility",
         "CPU Compatibility",
         "",
         {
            { "normal", "Normal" },
            { "compatible", "More compatible" },
            { "exact", "Cycle-exact" },
            { NULL, NULL },
         },
         "exact"
      },*/
      /*{
         "puae_use_whdload",
         "Use WHDLoad.hdf",
         "Restart required.",
         {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },*/
      { NULL, NULL, NULL, {{0}}, NULL },
   };

   environ_cb = cb;
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   unsigned version = 0;
   if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version == 1))
      cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, core_options);
   else
   {
		/* Fallback for older API */
		static struct retro_variable variables[64] = { 0 };
		int i = 0;
		while (core_options[i].key)
		{
			buf[i][0] = 0;
			variables[i].key = core_options[i].key;
			strcpy(buf[i], core_options[i].desc);
			strcat(buf[i], "; ");
//			strcat(buf[i], core_options[i].default_value);
			int j = 0;
			while (core_options[i].values[j].value && j < RETRO_NUM_CORE_OPTION_VALUES_MAX)
			{
				if ( j > 0 )
					strcat(buf[i], "|");
				strcat(buf[i], core_options[i].values[j].value);
				++j;
			};
			variables[i].value = buf[i];
			++i;
		};
		variables[i].key = NULL;
		variables[i].value = NULL;
		cb( RETRO_ENVIRONMENT_SET_VARIABLES, variables);
   }

   static bool allowNoGameMode;
   allowNoGameMode = true;
   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allowNoGameMode);
}

static void update_variables(void)
{
   uae_machine[0] = '\0';
   uae_config[0] = '\0';

   struct retro_variable var = {0};

#if FORCE_MACHINE == 500
	strcat(uae_machine, A500);
	strcpy(uae_kickstart, A500_ROM);
#endif // FORCE_MACHINE

/*
   var.key = "puae_model";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "A500") == 0)
      {
         strcat(uae_machine, A500);
         strcpy(uae_kickstart, A500_ROM);
      }
      if (strcmp(var.value, "A500OG") == 0)
      {
         strcat(uae_machine, A500OG);
         strcpy(uae_kickstart, A500_ROM);
      }
      if (strcmp(var.value, "A500PLUS") == 0)
      {
         strcat(uae_machine, A500PLUS);
         strcpy(uae_kickstart, A500KS2_ROM);
      }
      if (strcmp(var.value, "A600") == 0)
      {
         strcat(uae_machine, A600);
         strcpy(uae_kickstart, A600_ROM);
      }
      if (strcmp(var.value, "A1200") == 0)
      {
         strcat(uae_machine, A1200);
         strcpy(uae_kickstart, A1200_ROM);
      }
      if (strcmp(var.value, "A1200OG") == 0)
      {
         strcat(uae_machine, A1200OG);
         strcpy(uae_kickstart, A1200_ROM);
      }
   }
*/
   var.key = "puae_video_standard";
   var.value = NULL;

	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
	{
		/* video_config change only at start */
		if (video_config_old == 0)
		{
			if (strcmp(var.value, "PAL") == 0)
			{
				video_config |= PUAE_VIDEO_PAL;
				video_config &= ~PUAE_VIDEO_NTSC;
				strcat(uae_config, "ntsc=false\n");
			}
			else
			{
				video_config |= PUAE_VIDEO_NTSC;
				video_config &= ~PUAE_VIDEO_PAL;
				strcat(uae_config, "ntsc=true\n");
				real_ntsc = true;
			}
        }
	}

	strcat(uae_config, "cpu_compatible=true\n");
	strcat(uae_config, "cycle_exact=true\n");

/*
   var.key = "puae_cpu_compatibility";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	 if (strcmp(var.value, "normal") == 0)
	 {
		strcat(uae_config, "cpu_compatible=false\n");
		strcat(uae_config, "cycle_exact=false\n");
	 }
	 else if (strcmp(var.value, "compatible") == 0)
	 {
		strcat(uae_config, "cpu_compatible=true\n");
		strcat(uae_config, "cycle_exact=false\n");
	 }
	 else if (strcmp(var.value, "exact") == 0)
	 {
		strcat(uae_config, "cpu_compatible=true\n");
		strcat(uae_config, "cycle_exact=true\n");
	 }
   }
*/

	strcat(uae_config, "sound_output=3\n"); // exact
	strcat(uae_config, "sound_filter=1\n"); // FILTER_SOUND_EMUL
	strcat(uae_config, "sound_filter_type=0\n"); // A500
//	strcat(uae_config, "sound_filter_type=1\n"); // A1200
	strcat(uae_config, "sound_interpol=0\n"); // 'none' (see changed_prefs.sound_interpol for others)
	strcat(uae_config, "floppy_volume=100\n"); /* 100 is mute, 0 is max */
	strcat(uae_config, "floppy0sound=1\n");
	strcat(uae_config, "floppy1sound=1\n");
	strcat(uae_config, "floppy2sound=1\n");
	strcat(uae_config, "floppy3sound=1\n");

	strcat(uae_config, "collision_level=2\n"); // sprites&playfields (recommended default)
/*
   var.key = "puae_collision_level";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      strcat(uae_config, "collision_level=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (firstpass != 1)
      {
         if (strcmp(var.value, "none") == 0) changed_prefs.collision_level=0;
         else if (strcmp(var.value, "sprites") == 0) changed_prefs.collision_level=1;
         else if (strcmp(var.value, "playfields") == 0) changed_prefs.collision_level=2;
         else if (strcmp(var.value, "full") == 0) changed_prefs.collision_level=3;
      }
   }
*/
	changed_prefs.gfx_framerate=1; // no frameskip

   /*var.key = "puae_gfx_colors";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      // Only allow screenmode change after restart
      if (!pix_bytes_initialized)
      {
         if (strcmp(var.value, "16bit") == 0) pix_bytes=2;
         else if (strcmp(var.value, "24bit") == 0) pix_bytes=4;
         pix_bytes_initialized = true;
      }
   }*/

   var.key = "puae_use_whdload";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
        opt_use_whdload_hdf = true;
      if (strcmp(var.value, "disabled") == 0)
        opt_use_whdload_hdf = false;
   }


   // Setting resolution
   // According to PUAE configuration.txt :
   //
   // To emulate a high-resolution, fully overscanned PAL screen - either
   // non-interlaced with line-doubling, or interlaced - you need to use a
   // display of at least 720 by 568 pixels. If you specify a smaller size,
   // E-UAE's display will be clipped to fit (and you can use the gfx_center_*
   // options - see below - to centre the clipped region of the display).
   // Similarly, to fully display an over-scanned lo-res PAL screen, you need a
   // display of 360 by 284 pixels.
   //
   // So, here are the standard resolutions :
   // - **360x284**: PAL Low resolution
   // - **360x240**: NTSC Low resolution
   // - **720x568**: PAL High resolution
   // - **720x480**: NTSC High resolution
   switch(video_config)
   {
		case PUAE_VIDEO_PAL_HI:
			defaultw = 720;
			defaulth = 568;
			strcat(uae_config, "gfx_lores=false\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;

		case PUAE_VIDEO_NTSC_HI:
			defaultw = 720;
			defaulth = 480;
			strcat(uae_config, "gfx_lores=false\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;
   }

   /* Always update av_info geometry */
   request_update_av_info = true;

   /* Always trigger audio and custom change */
   config_changed = 1;
   check_prefs_changed_audio();
   check_prefs_changed_custom();
   check_prefs_changed_cpu();
   config_changed = 0;
}

//*****************************************************************************
//*****************************************************************************
// Disk control
extern void DISK_check_change(void);
extern void disk_eject (int num);

static bool disk_set_eject_state(bool ejected)
{
	if (dc)
	{
		if (dc->eject_state == ejected)
			return true;
		else
			dc->eject_state = ejected;

		if (dc->eject_state)
		{
			changed_prefs.floppyslots[0].df[0] = 0;
			DISK_check_change();
			disk_eject(0);
		}
		else
		{
			if (strlen(dc->files[dc->index]) > 0)
			{
				if (file_exists(dc->files[dc->index]))
				{
					if (currprefs.nr_floppies-1 < 0 )
						currprefs.nr_floppies = 1;

					//check whether drive is enabled
					if (currprefs.floppyslots[0].dfxtype < 0)
					{
						changed_prefs.floppyslots[0].dfxtype = 0;
						DISK_check_change();
					}
					changed_prefs = currprefs;
					strcpy (changed_prefs.floppyslots[0].df,dc->files[dc->index]);
					DISK_check_change();
				}
			}
		}
	}
	return true;
}

static bool disk_get_eject_state(void)
{
	if (dc)
		return dc->eject_state;

	return true;
}

static unsigned disk_get_image_index(void)
{
	if (dc)
		return dc->index;

	return 0;
}

static bool disk_set_image_index(unsigned index)
{
	// Insert disk
	if (dc)
	{
		// Same disk...
		// This can mess things in the emu
		if (index == dc->index)
			return true;

		if ((index < dc->count) && (dc->files[index]))
		{
			dc->index = index;
			log_cb(RETRO_LOG_INFO, "Disk(%d) into drive DF0: %s\n", dc->index+1, dc->files[dc->index]);
			return true;
		}
	}

	return false;
}

static unsigned disk_get_num_images(void)
{
	if (dc)
		return dc->count;

	return 0;
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
	if (dc)
	{
		if (index >= dc->count)
			return false;

		if (dc->files[index])
		{
			free(dc->files[index]);
			dc->files[index] = NULL;
		}

		// TODO : Handling removing of a disk image when info = NULL

		if (info != NULL)
			dc->files[index] = strdup(info->path);
	}

    return false;
}

static bool disk_add_image_index(void)
{
	if (dc)
	{
		if (dc->count <= DC_MAX_SIZE)
		{
			dc->files[dc->count] = NULL;
			dc->count++;
			return true;
		}
	}

    return false;
}

static struct retro_disk_control_callback disk_interface = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

//*****************************************************************************
//*****************************************************************************
// Init
void retro_init(void)
{
	// Init log
	struct retro_log_callback log;
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
		log_cb = log.log;
	else
		log_cb = fallback_log;

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   const char *system_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
     // if defined, use the system directory
     retro_system_directory=system_dir;
   }

   const char *content_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
     // if defined, use the system directory
     retro_content_directory=content_dir;
   }

   const char *save_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
     // If save directory is defined use it, otherwise use system directory
     retro_save_directory = *save_dir ? save_dir : retro_system_directory;
   }
   else
   {
     // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
     retro_save_directory=retro_system_directory;
   }

   //log_cb(RETRO_LOG_INFO, "Retro SYSTEM_DIRECTORY %s\n",retro_system_directory);
   //log_cb(RETRO_LOG_INFO, "Retro SAVE_DIRECTORY %s\n",retro_save_directory);
   //log_cb(RETRO_LOG_INFO, "Retro CONTENT_DIRECTORY %s\n",retro_content_directory);

   // Disk control interface
   dc = dc_create();
   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   // Savestates
   static uint32_t quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE | RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE | RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE;
   environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);

   // Inputs
#define RETRO_DESCRIPTOR_BLOCK( _user )                                            \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A / 2nd fire / Blue" },\
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B / Fire / Red" },  \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X / Yellow" },      \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y / Green" },       \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },     \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start / Play" },\
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },       \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R / Forward" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L / Rewind" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },             \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },               \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },               \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },             \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" }

   static struct retro_input_descriptor input_descriptors[] =
   {
      RETRO_DESCRIPTOR_BLOCK( 0 ),
      RETRO_DESCRIPTOR_BLOCK( 1 ),
      RETRO_DESCRIPTOR_BLOCK( 2 ),
      RETRO_DESCRIPTOR_BLOCK( 3 ),
      { 0 },
   };

#undef RETRO_DESCRIPTOR_BLOCK

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptors);


   memset(key_state, 0, sizeof(key_state));
   memset(key_state2, 0, sizeof(key_state2));

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_ERROR, "[libretro-uae]: RGB565 is not supported. Abort!\n");
      exit(0);//return false;
   }

   memset(bmp, 0, sizeof(bmp));

   update_variables();
}

void retro_deinit(void)
{
	log_cb(RETRO_LOG_INFO, "[libretro-uae]: Eject disk\n");
	disk_eject(0);

	log_cb(RETRO_LOG_INFO, "[libretro-uae]: UAE 'leave_program' call\n");
	leave_program();

	// Clean the m3u storage
	if (dc)
		dc_free(dc);
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (port<2)
   {
      uae_devices[port]=device;
      int uae_port = (port==0) ? 1 : 0;
      cd32_pad_enabled[uae_port]=0;
      switch(device)
      {
         case RETRO_DEVICE_JOYPAD:
            log_cb(RETRO_LOG_INFO, "Controller %u: Joystick\n", (port+1));
            break;

         case RETRO_DEVICE_NONE:
            log_cb(RETRO_LOG_INFO, "Controller %u: Unplugged\n", (port+1));
            break;
      }

      /* After startup input_get_default_joystick will need to be refreshed for cd32<>joystick change to work.
         Doing updateconfig straight from boot will crash, hence inputdevice_finalized */
      if (inputdevice_finalized)
         inputdevice_updateconfig(NULL, &currprefs);
   }
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
#if FORCE_MACHINE==500
   info->library_name     = "PUAE (A500)";
#endif // FORCE_MACHINE
   info->library_version  = "2.6.1 " GIT_VERSION;
   info->need_fullpath    = true;
   info->block_extract    = false;
   info->valid_extensions = "adf|m3u";
}

bool retro_update_av_info(bool change_geometry, bool change_timing, bool isntsc)
{
   request_update_av_info = false;
   float hz = currprefs.chipset_refreshrate;
//   log_cb(RETRO_LOG_ERROR, "[libretro-uae]: Trying to update AV geometry:%d timing:%d, to: ntsc:%d hz:%0.4f, from video_config:%d, video_aspect:%d\n", change_geometry, change_timing, isntsc, hz, video_config, 0);

   /* Change PAL/NTSC with a twist, thanks to Dyna Blaster

      Early Startup switch looks proper:
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         NTSC mode V=59.8859Hz H=15590.7473Hz (227x262+1) IDX=11 (NTSC) D=0 RTG=0/0
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0

      Dyna Blaster switch looks unorthodox:
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         PAL mode V=59.4106Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
   */

   video_config_old = video_config;
   video_config_geometry = video_config;

   /* When timing & geometry is changed */
   if (change_timing)
   {
      /* Change to NTSC if not NTSC */
      if (isntsc && (video_config & PUAE_VIDEO_PAL) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_NTSC;
         video_config &= ~PUAE_VIDEO_PAL;
      }
      /* Change to PAL if not PAL */
      else if (!isntsc && (video_config & PUAE_VIDEO_NTSC) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_PAL;
         video_config &= ~PUAE_VIDEO_NTSC;
      }

      /* Main video config will be changed too */
      video_config_geometry = video_config;
   }

   /* Do nothing if timing has not changed, unless Hz switched without isntsc */
   if (video_config_old == video_config && change_timing)
   {
      /* Dyna Blaster and the like stays at fake NTSC to prevent pointless switching back and forth */
      if (!isntsc && hz > 55)
      {
         video_config |= PUAE_VIDEO_NTSC;
         video_config &= ~PUAE_VIDEO_PAL;
         video_config_geometry = video_config;
         fake_ntsc=true;
      }

      /* If still no change */
      if (video_config_old == video_config)
      {
//         log_cb(RETRO_LOG_INFO, "[libretro-uae]: Already at wanted AV\n");
         change_timing = false; // Allow other calculations but don't alter timing
      }
   }

   /* Geometry dimensions */
   switch(video_config_geometry)
   {
      case PUAE_VIDEO_PAL_HI:
         retrow = 720;
         retroh = 568;
         break;

      case PUAE_VIDEO_NTSC_HI:
         retrow = 720;
         retroh = 480;
         break;
   }

   /* When the actual dimensions change and not just the view */
   if (change_timing)
   {
      defaultw = retrow;
      defaulth = retroh;
   }

   static struct retro_system_av_info new_av_info;
   new_av_info.geometry.base_width = retrow;
   new_av_info.geometry.base_height = retroh;

   if (video_config_geometry & PUAE_VIDEO_NTSC)
      new_av_info.geometry.aspect_ratio=(float)retrow/(float)retroh * 44.0/52.0;
   else
      new_av_info.geometry.aspect_ratio=(float)retrow/(float)retroh;

   /* Disable Hz change if not allowed */
   if (!video_config_allow_hz_change)
      change_timing = 0;

   /* Logging */
   if (change_geometry && change_timing)
   {
      log_cb(RETRO_LOG_INFO, "[libretro-uae]: Update av_info: %dx%d %0.4fHz, video_config:%d\n", retrow, retroh, hz, video_config_geometry);
   }
   else if (change_geometry && !change_timing)
   {
      log_cb(RETRO_LOG_INFO, "[libretro-uae]: Update geometry: %dx%d, video_config:%d\n", retrow, retroh, video_config_geometry);
   }
   else if (!change_geometry && change_timing)
   {
      log_cb(RETRO_LOG_INFO, "[libretro-uae]: Update timing: %0.4fHz, video_config:%d\n", hz, video_config_geometry);
   }

   if (change_timing) {
      struct retro_system_av_info new_timing;
      retro_get_system_av_info(&new_timing);
      new_timing.timing.fps = hz;
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_timing);
   }

   if (change_geometry) {
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);
   }

   /* No need to check changed gfx at startup */
   if (firstpass != 1) {
      prefs_changed = 1; // Triggers check_prefs_changed_gfx() in vsync_handle_check()
   }

   return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   /* need to do this here because core option values are not available in retro_init */
   if (pix_bytes == 4)
   {
      enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
      if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      {
         log_cb(RETRO_LOG_INFO, "[libretro-uae]: XRGB8888 is not supported. Trying RGB565\n");
         fmt = RETRO_PIXEL_FORMAT_RGB565;
         pix_bytes = 2;
         if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
         {
            log_cb(RETRO_LOG_ERROR, "[libretro-uae]: RGB565 is not supported. Aborting.\n");
            exit(0);//return false;
         }
      }
   }

   static struct retro_game_geometry geom;
   geom.base_width=retrow;
   geom.base_height=retroh;
   geom.max_width=EMULATOR_MAX_WIDTH;
   geom.max_height=EMULATOR_MAX_HEIGHT;

   if (retro_get_region() == RETRO_REGION_NTSC)
      geom.aspect_ratio=(float)retrow/(float)retroh * 44.0/52.0;
   else
      geom.aspect_ratio=(float)retrow/(float)retroh;
   info->geometry = geom;

   info->timing.sample_rate = 44100.0;
   info->timing.fps = (retro_get_region() == RETRO_REGION_NTSC) ? UAE_HZ_NTSC : UAE_HZ_PAL;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_shutdown_uae(void)
{
   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

void retro_reset(void)
{
   fake_ntsc=false;
   uae_reset(1, 1); /* hardreset, keyboardreset */
}

void retro_audio_cb(short l, short r)
{
   audio_cb(l,r);
}

void retro_run(void)
{
   // Core options
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   // AV info change is requested
   if (request_update_av_info)
      retro_update_av_info(1, 0, 0);

	if (firstpass)
	{
		log_cb(RETRO_LOG_INFO, "[libretro-uae] firstpass complete. Starting UAE %s\n", RPATH );
		firstpass=0;

		video_cb(bmp, retrow, retroh, retrow << (pix_bytes / 2));

		// init emulation
		umain(2, uae_argv);

		log_cb(RETRO_LOG_INFO, "[libretro-uae] UAE ready.\n" );

		// run emulation first pass.
		m68k_go( 1, 0 );
	}
	else
	{
		retro_poll_event();
		video_cb(bmp, retrow, retroh, retrow << (pix_bytes / 2));

		// resume emulation for a frame.
		m68k_go( 1, 1 );
	}
}

#define ADF_FILE_EXT "adf"
#define FDI_FILE_EXT "fdi"
#define DMS_FILE_EXT "dms"
#define IPF_FILE_EXT "ipf"
#define ZIP_FILE_EXT "zip"
#define HDF_FILE_EXT "hdf"
#define HDZ_FILE_EXT "hdz"
#define UAE_FILE_EXT "uae"
#define M3U_FILE_EXT "m3u"
#define LIBRETRO_PUAE_CONF "puae_libretro.uae"
#define WHDLOAD_HDF "WHDLoad.hdf"

bool retro_load_game(const struct retro_game_info *info)
{
   int w = 0, h = 0;

   RPATH[0] = '\0';

   if (info)
   {
      const char *full_path = (const char*)info->path;

	  // If argument is a disk or hard drive image file
	  if (  strendswith(full_path, ADF_FILE_EXT)
         || strendswith(full_path, FDI_FILE_EXT)
         || strendswith(full_path, DMS_FILE_EXT)
         || strendswith(full_path, IPF_FILE_EXT)
         || strendswith(full_path, ZIP_FILE_EXT)
         || strendswith(full_path, HDF_FILE_EXT)
         || strendswith(full_path, HDZ_FILE_EXT)
         || strendswith(full_path, M3U_FILE_EXT))
	  {
	     log_cb(RETRO_LOG_INFO, "Game '%s' is a disk, a hard drive image or a m3u file.\n", full_path);

	     path_join((char*)&RPATH, retro_save_directory, LIBRETRO_PUAE_CONF);
	     log_cb(RETRO_LOG_INFO, "Generating temporary uae config file '%s'.\n", (const char*)&RPATH);

	     // Open tmp config file
	     FILE * configfile;
	     if (configfile = fopen(RPATH, "w"))
	     {
	        char kickstart[RETRO_PATH_MAX];

            // If a machine was specified in the name of the game
            if (strstr(full_path, "(A1200OG)") != NULL || strstr(full_path, "(A1200NF)") != NULL)
            {
               // Use A1200 barebone
               log_cb(RETRO_LOG_INFO, "Found '(A1200OG)' or '(A1200NF)' in filename '%s'. Booting A1200 NoFast with Kickstart 3.1 r40.068 rom.\n", full_path);
               fprintf(configfile, A1200OG);
               path_join((char*)&kickstart, retro_system_directory, A1200_ROM);
            }
            else if (strstr(full_path, "(A1200)") != NULL || strstr(full_path, "(AGA)") != NULL)
            {
               // Use A1200
               log_cb(RETRO_LOG_INFO, "Found '(A1200)' or '(AGA)' in filename '%s'. Booting A1200 with Kickstart 3.1 r40.068 rom.\n", full_path);
               fprintf(configfile, A1200);
               path_join((char*)&kickstart, retro_system_directory, A1200_ROM);
            }
            else if (strstr(full_path, "(A600)") != NULL || strstr(full_path, "(ECS)") != NULL)
            {
               // Use A600
               log_cb(RETRO_LOG_INFO, "Found '(A600)' or '(ECS)' in filename '%s'. Booting A600 with Kickstart 3.1 r40.063 rom.\n", full_path);
               fprintf(configfile, A600);
               path_join((char*)&kickstart, retro_system_directory, A600_ROM);
            }
            else if (strstr(full_path, "(A500+)") != NULL || strstr(full_path, "(A500PLUS)") != NULL)
            {
               // Use A500+
               log_cb(RETRO_LOG_INFO, "Found '(A500+)' or '(A500PLUS)' in filename '%s'. Booting A500+ with Kickstart 2.04 r37.175.\n", full_path);
               fprintf(configfile, A500PLUS);
               path_join((char*)&kickstart, retro_system_directory, A500KS2_ROM);
            }
            else if (strstr(full_path, "(A500OG)") != NULL || strstr(full_path, "(512K)") != NULL)
            {
               // Use A500 barebone
               log_cb(RETRO_LOG_INFO, "Found '(A500OG)' or '(512K)' in filename '%s'. Booting A500 512K with Kickstart 1.3 r34.005.\n", full_path);
               fprintf(configfile, A500OG);
               path_join((char*)&kickstart, retro_system_directory, A500_ROM);
            }
            else if (strstr(full_path, "(A500)") != NULL || strstr(full_path, "(OCS)") != NULL)
            {
               // Use A500
               log_cb(RETRO_LOG_INFO, "Found '(A500)' or '(OCS)' in filename '%s'. Booting A500 with Kickstart 1.3 r34.005.\n", full_path);
               fprintf(configfile, A500);
               path_join((char*)&kickstart, retro_system_directory, A500_ROM);
            }
            else
            {
               // No machine specified, we will use the configured one
               log_cb(RETRO_LOG_INFO, "No machine specified in filename '%s'. Booting default configuration.\n", full_path);
               fprintf(configfile, uae_machine);
               path_join((char*)&kickstart, retro_system_directory, uae_kickstart);
            }

            // Write common config
            fprintf(configfile, uae_config);

            // If region was specified in the name of the game
            if (strstr(full_path, "(NTSC)") != NULL)
            {
               log_cb(RETRO_LOG_INFO, "Found '(NTSC)' in filename '%s'\n", full_path);
               fprintf(configfile, "ntsc=true\n");
            }
            else if (strstr(full_path, "(PAL)") != NULL)
            {
               log_cb(RETRO_LOG_INFO, "Found '(PAL)' in filename '%s'\n", full_path);
               fprintf(configfile, "ntsc=false\n");
            }

            // Verify kickstart
            if (!file_exists(kickstart))
            {
               // Kickstart rom not found
               log_cb(RETRO_LOG_ERROR, "Kickstart rom '%s' not found.\n", (const char*)&kickstart);
               log_cb(RETRO_LOG_ERROR, "You must have a correct kickstart file ('%s') in your RetroArch system directory.\n", kickstart);
               fclose(configfile);
               return false;
            }

            fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

            // If argument is a hard drive image file
            if (  strendswith(full_path, HDF_FILE_EXT)
               || strendswith(full_path, HDZ_FILE_EXT))
            {
               if (opt_use_whdload_hdf)
               {
                  // Init WHDLoad
                  char whdload[RETRO_PATH_MAX];
                  path_join((char*)&whdload, retro_system_directory, WHDLOAD_HDF);

                  // Verify WHDLoad
                  if (file_exists(whdload))
                  {
                     fprintf(configfile, "hardfile=read-write,32,1,2,512,%s\n", (const char*)&whdload);
			      }
                  else
                  {
                     log_cb(RETRO_LOG_ERROR, "WHDLoad image file '%s' not found.\n", (const char*)&whdload);
			      }
               }
               fprintf(configfile, "hardfile=read-write,32,1,2,512,%s\n", full_path);
            }
            else
            {
               // If argument is a m3u playlist
               if (strendswith(full_path, M3U_FILE_EXT))
               {
                  // Parse the m3u file
                  dc_parse_m3u(dc, full_path);

                  // Some debugging
                  log_cb(RETRO_LOG_INFO, "M3U file parsed, %d file(s) found\n", dc->count);
                  for (unsigned i = 0; i < dc->count; i++)
                  {
                     log_cb(RETRO_LOG_INFO, "File %d: %s\n", i+1, dc->files[i]);
				  }
               }
               else
               {
                  // Add the file to disk control context
                  // Maybe, in a later version of retroarch, we could add disk on the fly (didn't find how to do this)
                  dc_add_file(dc, full_path);
               }

               // Init first disk
               dc->index = 0;
               dc->eject_state = false;
               log_cb(RETRO_LOG_INFO, "Disk (%d) inserted into drive DF0: %s\n", dc->index+1, dc->files[dc->index]);
               fprintf(configfile, "floppy0=%s\n", dc->files[0]);

               // Append rest of the disks to the config if m3u is a MultiDrive-m3u
               if (strstr(full_path, "(MD)") != NULL)
               {
                  for (unsigned i = 1; i < dc->count; i++)
                  {
                     dc->index = i;
                     if (i <= 3)
                     {
                        log_cb(RETRO_LOG_INFO, "Disk (%d) inserted into drive DF%d: %s\n", dc->index+1, i, dc->files[dc->index]);
                        fprintf(configfile, "floppy%d=%s\n", i, dc->files[i]);

                        // By default only DF0: is enabled, so floppyXtype needs to be set on the extra drives
                        if (i > 0)
                           fprintf(configfile, "floppy%dtype=%d\n", i, 0); // 0 = 3.5" DD
                     }
                     else
                     {
                        fprintf(stderr, "Too many disks for MultiDrive!\n");
                        fclose(configfile);
                        return false;
                     }
                  }
               }
            }
            fclose(configfile);
         }
         else
         {
            // Error
            log_cb(RETRO_LOG_ERROR, "Error while writing '%s' file.\n", (const char*)&RPATH);
            return false;
         }
      }
      // If argument is an uae file
	  else if (strendswith(full_path, UAE_FILE_EXT))
	  {
	     log_cb(RETRO_LOG_INFO, "Game '%s' is an UAE config file.\n", full_path);

	     // Prepend default config
	     path_join((char*)&RPATH, retro_save_directory, LIBRETRO_PUAE_CONF);
	     log_cb(RETRO_LOG_INFO, "Generating temporary uae config file '%s'.\n", (const char*)&RPATH);

	     // Open tmp config file
	     FILE * configfile;

	     if (configfile = fopen(RPATH, "w"))
	     {
	        char kickstart[RETRO_PATH_MAX];

	        fprintf(configfile, uae_machine);
	        path_join((char*)&kickstart, retro_system_directory, uae_kickstart);

	        // Write common config
	        fprintf(configfile, uae_config);
	        fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

	        // Iterate parsed file and append all rows to the temporary config
	        FILE * configfile_custom;

	        char filebuf[4096];
	        if (configfile_custom = fopen (full_path, "r"))
	        {
	           while (fgets(filebuf, sizeof(filebuf), configfile_custom))
	           {
	              fprintf(configfile, filebuf);
               }
               fclose(configfile_custom);
            }
            fclose(configfile);
         }
         else
         {
            // Error
            log_cb(RETRO_LOG_ERROR, "Error while writing '%s' file.\n", (const char*)&RPATH);
            return false;
         }
      }
	  // Other extensions
	  else
	  {
	     // Unsupported file format
	     log_cb(RETRO_LOG_ERROR, "Content '%s'. Unsupported file format.\n", full_path);
	     return false;
	  }
   }
   // Empty content
   else
   {
      path_join((char*)&RPATH, retro_save_directory, LIBRETRO_PUAE_CONF);
      log_cb(RETRO_LOG_INFO, "Generating temporary uae config file '%s'.\n", (const char*)&RPATH);

      // Open tmp config file
      FILE * configfile;
      if (configfile = fopen(RPATH, "w"))
      {
         char kickstart[RETRO_PATH_MAX];

         // No machine specified we will use the configured one
         printf("No machine specified. Booting default configuration.\n");
         fprintf(configfile, uae_machine);
         path_join((char*)&kickstart, retro_system_directory, uae_kickstart);

         // Write common config
         fprintf(configfile, uae_config);

         // Verify kickstart
         if (!file_exists(kickstart))
         {
            // Kickstart rom not found
            log_cb(RETRO_LOG_ERROR, "Kickstart rom '%s' not found.\n", (const char*)&kickstart);
            log_cb(RETRO_LOG_ERROR, "You must have a correct kickstart file ('%s') in your RetroArch system directory.\n", kickstart);
            fclose(configfile);
            return false;
         }

         fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);
         fclose(configfile);
      }
   }

   if (w<=0 || h<=0 || w>EMULATOR_MAX_WIDTH || h>EMULATOR_MAX_HEIGHT)
   {
      w = defaultw;
      h = defaulth;
   }

   log_cb(RETRO_LOG_INFO, "[libretro-uae]: Resolution selected: %dx%d (default: %dx%d)\n", w, h, defaultw, defaulth);

   retrow = w;
   retroh = h;
   memset(bmp, 0, sizeof(bmp));
   Screen_SetFullUpdate();
   return true;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return (video_config & PUAE_VIDEO_NTSC) ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   if (firstpass != 1)
   {
      snprintf(savestate_fname, sizeof(savestate_fname), "%s%suae_tempsave.uss", retro_save_directory, DIR_SEP_STR);
      if (save_state(savestate_fname, "retro") >= 0)
      {
         FILE *file = fopen(savestate_fname, "rb");
         if (file)
         {
            size_t size = 0;
            fseek(file, 0L, SEEK_END);
            size = ftell(file);
            fclose(file);
            return size;
         }
      }
   }
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   if (firstpass != 1)
   {
      snprintf(savestate_fname, sizeof(savestate_fname), "%s%suae_tempsave.uss", retro_save_directory, DIR_SEP_STR);
      if (save_state(savestate_fname, "retro") >= 0)
      {
         FILE *file = fopen(savestate_fname, "rb");
         if (file)
         {
            if (fread(data_, size, 1, file) == 1)
            {
               fclose(file);
               return true;
            }
            fclose(file);
         }
      }
   }
   return false;
}

bool retro_unserialize(const void *data_, size_t size)
{
   if (firstpass != 1)
   {
      snprintf(savestate_fname, sizeof(savestate_fname), "%s%suae_tempsave.uss", retro_save_directory, DIR_SEP_STR);
      FILE *file = fopen(savestate_fname, "wb");
      if (file)
      {
         if (fwrite(data_, size, 1, file) == 1)
         {
            fclose(file);
            savestate_state = STATE_DORESTORE;
            return true;
         }
         else
            fclose(file);
      }
   }
   return false;
}

void *retro_get_memory_data(unsigned id)
{
#if defined(NATMEM_OFFSET)
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return natmem_offset;
#endif
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
#if defined(NATMEM_OFFSET)
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return natmem_size;
#endif
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

#ifdef ANDROID
#include <sys/timeb.h>

int ftime(struct timeb *tb)
{
    struct timeval  tv;
    struct timezone tz;

    if (gettimeofday (&tv, &tz) < 0)
        return -1;

    tb->time    = tv.tv_sec;
    tb->millitm = (tv.tv_usec + 500) / 1000;

    if (tb->millitm == 1000)
    {
        ++tb->time;
        tb->millitm = 0;
    }
    tb->timezone = tz.tz_minuteswest;
    tb->dstflag  = tz.tz_dsttime;

    return 0;
}
#endif
