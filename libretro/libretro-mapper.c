#include "libretro.h"
#include "libretro-glue.h"
#include "keyboard.h"
#include "libretro-keymap.h"
#include "graph.h"
#include "libretro-mapper.h"

#include "uae_types.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"

#include "gui.h"
#include "xwin.h"
#include "disk.h"

#ifdef __CELLOS_LV2__
#include "sys/sys_time.h"
#include "sys/timer.h"
#include <sys/time.h>
#include <time.h>
#define usleep  sys_timer_usleep

void gettimeofday (struct timeval *tv, void *blah)
{
   int64_t time = sys_time_get_system_time();

   tv->tv_sec  = time / 1000000;
   tv->tv_usec = time - (tv->tv_sec * 1000000);  // implicit rounding will take care of this for us
}

#else
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#endif

int NPAGE=-1;
int SHIFTON=-1,ALTON=-1;
int MOUSEMODE=-1,SHOWKEY=-1,SHOWKEYPOS=-1,SHOWKEYTRANS=-1,STATUSON=-1,LEDON=-1;

char RPATH[512];

int slowdown=0;
extern int pix_bytes;
extern bool fake_ntsc;
extern bool real_ntsc;

static int jflag[4][16]={0};
static int mflag[2][16]={0};
static int jbt[2][24]={0};
static int kbt[16]={0};

extern void reset_drawing(void);
extern void retro_key_up(int);
extern void retro_key_down(int);
extern void retro_mouse(int, int, int);
extern void retro_mouse_button(int, int, int);
extern void retro_joystick(int, int, int);
extern void retro_joystick_button(int, int, int);
extern unsigned int uae_devices[2];
extern int video_config;
extern int video_config_aspect;
extern int zoom_mode_id;
extern bool request_update_av_info;

int STAT_BASEY;
int STAT_DECX=4;
int FONT_WIDTH=1;
int FONT_HEIGHT=1;
int BOX_PADDING=2;
int BOX_Y;
int BOX_WIDTH;
int BOX_HEIGHT=11;

extern char key_state[512];
extern char key_state2[512];

static retro_input_state_t input_state_cb;
static retro_input_poll_t input_poll_cb;

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

#define MDEBUG
#ifdef MDEBUG
#define mprintf printf
#else
#define mprintf(...)
#endif

#ifdef WIIU
#include <features_cpu.h>
#endif

/* in milliseconds */
long GetTicks(void)
{
#ifdef _ANDROID_
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   return (now.tv_sec*1000000 + now.tv_nsec/1000)/1000;
#elif defined(WIIU)
   return (cpu_features_get_time_usec())/1000;
#else
   struct timeval tv;
   gettimeofday (&tv, NULL);
   return (tv.tv_sec*1000000 + tv.tv_usec)/1000;
#endif
}

char* joystick_value_human(int val[16])
{
    static char str[4];
    sprintf(str, "%3s", "   ");

    if (val[RETRO_DEVICE_ID_JOYPAD_UP])
        str[1] = '^';

    if (val[RETRO_DEVICE_ID_JOYPAD_DOWN])
        str[1] = 'v';

    if (val[RETRO_DEVICE_ID_JOYPAD_LEFT])
        str[0] = '<';

    if (val[RETRO_DEVICE_ID_JOYPAD_RIGHT])
        str[2] = '>';

    if (val[RETRO_DEVICE_ID_JOYPAD_B])
        str[1] = '1';

    if (val[RETRO_DEVICE_ID_JOYPAD_A])
        str[1] = '2';

    if (val[RETRO_DEVICE_ID_JOYPAD_B] && val[RETRO_DEVICE_ID_JOYPAD_A])
        str[1] = '3';

    str[1] = (val[RETRO_DEVICE_ID_JOYPAD_B] || val[RETRO_DEVICE_ID_JOYPAD_A]) ? (str[1] | 0x80) : str[1];
    return str;
}

bool flag_empty(int val[16])
{
   for (int x=0; x<16; x++)
   {
      if (val[x])
         return false;
   }
   return true;
}

void Screen_SetFullUpdate(void)
{
   reset_drawing();
}

int retro_button_to_uae_button(int i)
{
   int uae_button = -1;
   switch (i)
   {
      case RETRO_DEVICE_ID_JOYPAD_B:
         uae_button = 0;
         break;
      case RETRO_DEVICE_ID_JOYPAD_A:
         uae_button = 1;
         break;
      case RETRO_DEVICE_ID_JOYPAD_Y:
         uae_button = 2;
         break;
      case RETRO_DEVICE_ID_JOYPAD_X:
         uae_button = 3;
         break;
      case RETRO_DEVICE_ID_JOYPAD_L:
         uae_button = 4;
         break;
      case RETRO_DEVICE_ID_JOYPAD_R:
         uae_button = 5;
         break;
      case RETRO_DEVICE_ID_JOYPAD_START:
         uae_button = 6;
         break;
   }
   return uae_button;
}

void ProcessController(int retro_port, int i)
{
   int uae_button = -1;

   if (i>3 && i<8) // Directions, need to fight around presses on the same axis
   {
      if (i==RETRO_DEVICE_ID_JOYPAD_UP || i==RETRO_DEVICE_ID_JOYPAD_DOWN)
      {
         if (i==RETRO_DEVICE_ID_JOYPAD_UP && SHOWKEY==-1)
         {
            if (input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i)
            && !input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
            {
               retro_joystick(retro_port, 1, -1);
               jflag[retro_port][i]=1;
            }
         }
         else
         if (i==RETRO_DEVICE_ID_JOYPAD_DOWN && SHOWKEY==-1)
         {
            if (input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i)
            && !input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
            {
               retro_joystick(retro_port, 1, 1);
               jflag[retro_port][i]=1;
            }
         }

         if (!input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)
         && jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_UP]==1)
         {
            retro_joystick(retro_port, 1, 0);
            jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_UP]=0;
         }
         else
         if (!input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)
         && jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_DOWN]==1)
         {
            retro_joystick(retro_port, 1, 0);
            jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_DOWN]=0;
         }
      }

      if (i==RETRO_DEVICE_ID_JOYPAD_LEFT || i==RETRO_DEVICE_ID_JOYPAD_RIGHT)
      {
         if (i==RETRO_DEVICE_ID_JOYPAD_LEFT && SHOWKEY==-1)
         {
            if (input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i)
            && !input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
            {
               retro_joystick(retro_port, 0, -1);
               jflag[retro_port][i]=1;
            }
         }
         else
         if (i==RETRO_DEVICE_ID_JOYPAD_RIGHT && SHOWKEY==-1)
         {
            if (input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i)
            && !input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
            {
               retro_joystick(retro_port, 0, 1);
               jflag[retro_port][i]=1;
            }
         }

         if (!input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)
         && jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_LEFT]==1)
         {
            retro_joystick(retro_port, 0, 0);
            jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_LEFT]=0;
         }
         else
         if (!input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)
         && jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_RIGHT]==1)
         {
            retro_joystick(retro_port, 0, 0);
            jflag[retro_port][RETRO_DEVICE_ID_JOYPAD_RIGHT]=0;
         }
      }
   }
   else // Buttons
   {
      uae_button = retro_button_to_uae_button(i);
      if (uae_button != -1)
      {
         if (input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i) && jflag[retro_port][i]==0 && SHOWKEY==-1)
         {
            retro_joystick_button(retro_port, uae_button, 1);
            jflag[retro_port][i]=1;
         }
         else
         if (!input_state_cb(retro_port, RETRO_DEVICE_JOYPAD, 0, i) && jflag[retro_port][i]==1)
         {
            retro_joystick_button(retro_port, uae_button, 0);
            jflag[retro_port][i]=0;
         }
      }
   }
}

void ProcessKey()
{
   int i;
   for (i=0;i<320;i++)
   {
      key_state[i]=input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, i) ? 0x80 : 0;

      /* CapsLock */
      if (keyboard_translation[i]==AK_CAPSLOCK)
      {
         if (key_state[i] && key_state2[i]==0)
         {
            retro_key_down(keyboard_translation[i]);
            retro_key_up(keyboard_translation[i]);
            SHIFTON=-SHIFTON;
            Screen_SetFullUpdate();
            key_state2[i]=1;
         }
         else if (!key_state[i] && key_state2[i]==1)
            key_state2[i]=0;
      }
      /* Special key (Right Alt) for overriding RetroPad cursor override */
      else if (keyboard_translation[i]==AK_RALT)
      {
         if (key_state[i] && key_state2[i]==0)
         {
            ALTON=1;
            retro_key_down(keyboard_translation[i]);
            key_state2[i]=1;
         }
         else if (!key_state[i] && key_state2[i]==1)
         {
            ALTON=-1;
            retro_key_up(keyboard_translation[i]);
            key_state2[i]=0;
         }
      }
      else
      {
         if (key_state[i] && keyboard_translation[i]!=-1 && key_state2[i]==0)
         {
            if (SHIFTON==1)
               retro_key_down(keyboard_translation[RETROK_LSHIFT]);

            retro_key_down(keyboard_translation[i]);
            key_state2[i]=1;
         }
         else if (!key_state[i] && keyboard_translation[i]!=-1 && key_state2[i]==1)
         {
            retro_key_up(keyboard_translation[i]);
            key_state2[i]=0;

            if (SHIFTON==1)
               retro_key_up(keyboard_translation[RETROK_LSHIFT]);
         }
      }
   }
}

void update_input()
{
   // RETRO    B   Y   SLT STA UP  DWN LFT RGT A   X   L   R   L2  R2  L3  R3  LR  LL  LD  LU  RR  RL  RD  RU
   // INDEX    0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23

   static int i, j, mk;
   static int oldi=-1;

   input_poll_cb();

   ProcessKey();
}

void retro_poll_event()
{
	update_input();

   if (ALTON==-1) /* retro joypad take control over keyboard joy */
   /* override keydown, but allow keyup, to prevent key sticking during keyboard use, if held down on opening keyboard */
   /* keyup allowing most likely not needed on actual keyboard presses even though they get stuck also */
   {
      static float mouse_multiplier=1;
      static int uae_mouse_x[2],uae_mouse_y[2];
      static int uae_mouse_l[2]={0},uae_mouse_r[2]={0},uae_mouse_m[2]={0};
      static int mouse_lmb[2]={0},mouse_rmb[2]={0},mouse_mmb[2]={0};
      static int16_t mouse_x[2]={0},mouse_y[2]={0};
      static int i=0,j=0;

		int retro_port;
		for (retro_port = 0; retro_port <= 1; retro_port++)
		{
			switch (uae_devices[retro_port])
			{
			case RETRO_DEVICE_JOYPAD:
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_UP );
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_DOWN );
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_LEFT );
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_RIGHT );
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_B );
				ProcessController( retro_port, RETRO_DEVICE_ID_JOYPAD_A );
				break;
			}
		}

      // Mouse control
      uae_mouse_l[0]=uae_mouse_r[0]=uae_mouse_m[0]=0;
      uae_mouse_l[1]=uae_mouse_r[1]=uae_mouse_m[0]=0;
      uae_mouse_x[0]=uae_mouse_y[0]=0;
      uae_mouse_x[1]=uae_mouse_y[1]=0;

      // Real mouse buttons
      if (!uae_mouse_l[0] && !uae_mouse_r[0])
      {
         uae_mouse_l[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
         uae_mouse_r[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
         uae_mouse_m[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE);
      }

      // Real mouse movement
      if (!uae_mouse_x[0] && !uae_mouse_y[0])
      {
         mouse_x[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
         mouse_y[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

         if (mouse_x[0] || mouse_y[0])
         {
            uae_mouse_x[0] = mouse_x[0];
            uae_mouse_y[0] = mouse_y[0];
         }
      }

      // Ports 1 & 2
      for (j = 0; j < 2; j++)
      {
         // Mouse buttons to UAE
         if (mouse_lmb[j]==0 && uae_mouse_l[j])
         {
            mouse_lmb[j]=1;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_B]=1;
            retro_mouse_button(j, 0, 1);
         }
         else if (mouse_lmb[j]==1 && !uae_mouse_l[j])
         {
            mouse_lmb[j]=0;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_B]=0;
            retro_mouse_button(j, 0, 0);
         }

         if (mouse_rmb[j]==0 && uae_mouse_r[j])
         {
            mouse_rmb[j]=1;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_A]=1;
            retro_mouse_button(j, 1, 1);
         }
         else if (mouse_rmb[j]==1 && !uae_mouse_r[j])
         {
            mouse_rmb[j]=0;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_A]=0;
            retro_mouse_button(j, 1, 0);
         }

         if (mouse_mmb[j]==0 && uae_mouse_m[j])
         {
            mouse_mmb[j]=1;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_Y]=1;
            retro_mouse_button(j, 2, 1);
         }
         else if (mouse_mmb[j]==1 && !uae_mouse_m[j])
         {
            mouse_mmb[j]=0;
            mflag[j][RETRO_DEVICE_ID_JOYPAD_Y]=0;
            retro_mouse_button(j, 2, 0);
         }

         // Mouse movements to UAE
         if (uae_mouse_y[j]<0 && mflag[j][RETRO_DEVICE_ID_JOYPAD_UP]==0)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_UP]=1;
         if (uae_mouse_y[j]>-1 && mflag[j][RETRO_DEVICE_ID_JOYPAD_UP]==1)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_UP]=0;

         if (uae_mouse_y[j]>0 && mflag[j][RETRO_DEVICE_ID_JOYPAD_DOWN]==0)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_DOWN]=1;
         if (uae_mouse_y[j]<1 && mflag[j][RETRO_DEVICE_ID_JOYPAD_DOWN]==1)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_DOWN]=0;

         if (uae_mouse_x[j]<0 && mflag[j][RETRO_DEVICE_ID_JOYPAD_LEFT]==0)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_LEFT]=1;
         if (uae_mouse_x[j]>-1 && mflag[j][RETRO_DEVICE_ID_JOYPAD_LEFT]==1)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_LEFT]=0;

         if (uae_mouse_x[j]>0 && mflag[j][RETRO_DEVICE_ID_JOYPAD_RIGHT]==0)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_RIGHT]=1;
         if (uae_mouse_x[j]<1 && mflag[j][RETRO_DEVICE_ID_JOYPAD_RIGHT]==1)
            mflag[j][RETRO_DEVICE_ID_JOYPAD_RIGHT]=0;

         if (uae_mouse_x[j] || uae_mouse_y[j])
            retro_mouse(j, uae_mouse_x[j], uae_mouse_y[j]);
      }
   }
}
