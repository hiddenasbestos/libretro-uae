#include "libretro-glue.h"

#include "vkbd_def.h"
#include "graph.h"

extern int NPAGE;
extern int SHOWKEYPOS;
extern int SHIFTON;
extern int vkflag[6];
extern int video_config;

void virtual_kbd(unsigned short int *pixels,int vx,int vy)
{
   int y,x,page   = (NPAGE== -1) ? 0 : NPLGN*NLIGN;
   uint16_t  *pix = &pixels[0];

   int maxchr			= 8;
   int XKEY, YKEY, XTEXT, YTEXT, YOFFSET;
   int BKG_COLOR;
   int BKG_COLOR_NORMAL = RGB565(24, 24, 24);
   int BKG_COLOR_ALT	= RGB565(20, 20, 20);
   int BKG_COLOR_SEL	= RGB565(10, 10, 10);
   int BKG_COLOR_BORDER	= RGB565(1, 1, 1);
   int BKG_PADDING_X	= 0;
   int BKG_PADDING_Y	= 4;
   int FONT_WIDTH		= 1;
   int FONT_HEIGHT		= 1.6;
   int FONT_COLOR  		= RGB565(1,1,1);
   int FONT_COLOR_SEL	= RGB565(254,254,254);

   if(video_config & 0x04) // PUAE_VIDEO_HIRES
      YOFFSET			= 200;
   else
   {
      YOFFSET			= 100;
      FONT_WIDTH		= 1;
      FONT_HEIGHT		= 1;
      BKG_PADDING_X		= -2;
      BKG_PADDING_Y		= 1;
      maxchr			= 4;
   }

   int XSIDE			= ((CROP_WIDTH-XOFFSET)/NPLGN);
   int YSIDE  			= ((CROP_HEIGHT-YOFFSET)/NLIGN);


   /* Alternate color keys */
   char *alt_keys[] = {
      "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
      "LShift", "LAlt", "LAmiga", "RAmiga", "RAlt", "RShift",
      "Esc", "`", "Tab", "Ctrl", "CapsLock", "Return", "<-", "Del", "Help"
   };
   int alt_keys_len = sizeof(alt_keys)/sizeof(alt_keys[0]);

   for(x=0;x<NPLGN;x++)
   {
      for(y=0;y<NLIGN;y++)
      {
         /* Default key color */
         BKG_COLOR = BKG_COLOR_NORMAL;

         /* Alternate key color */
         for(int alt_key = 0; alt_key < alt_keys_len; ++alt_key)
             if(!strcmp(alt_keys[alt_key], MVk[(y*NPLGN)+x].norml))
                 BKG_COLOR = BKG_COLOR_ALT;

         /* Key positions */
         XKEY  = XBASE3+x*XSIDE;
         XTEXT = XBASE0+BKG_PADDING_X+x*XSIDE;
         if(SHOWKEYPOS==-1) {
            YKEY  = YBASE3+y*YSIDE;
            YTEXT = YBASE0+BKG_PADDING_Y+YSIDE*y;
         } else {
            YKEY  = YBASE3A+y*YSIDE;
            YTEXT = YBASE0A+BKG_PADDING_Y+YSIDE*y;
         }

         /* Key background */
         DrawFBoxBmp(pix, XKEY,YKEY, XSIDE,YSIDE, BKG_COLOR);

         /* Key border */
         DrawBoxBmp(pix, XKEY,YKEY, XSIDE,YSIDE, BKG_COLOR_BORDER);

         /* Key text */
         Draw_text(pix, XTEXT, YTEXT, FONT_COLOR,BKG_COLOR, FONT_WIDTH,FONT_HEIGHT, maxchr,
               SHIFTON==-1?MVk[(y*NPLGN)+x+page].norml:MVk[(y*NPLGN)+x+page].shift);	
      }
   }

   /* Key positions */
   XKEY  = XBASE3+1+vx*XSIDE;
   XTEXT = XBASE0+BKG_PADDING_X+vx*XSIDE;
   if(SHOWKEYPOS==-1) {
      YKEY  = YBASE3+1+vy*YSIDE;
      YTEXT = YBASE0+BKG_PADDING_Y+YSIDE*vy;
   } else {
      YKEY  = YBASE3A+1+vy*YSIDE;
      YTEXT = YBASE0A+BKG_PADDING_Y+YSIDE*vy;
   }

   /* Pressed key background */
   if(vkflag[4]==1) {
      BKG_COLOR_SEL = RGB565(4,4,4);
   }

   /* Selected key background */
   DrawFBoxBmp(pix, XKEY,YKEY, XSIDE-1,YSIDE-1, BKG_COLOR_SEL);

   /* Selected key border */
   //DrawBoxBmp(pix, XBASE3+vx*XSIDE,YBASE3+vy*YSIDE, XSIDE,YSIDE, BKG_COLOR);

   /* Selected key text */
   Draw_text(pix, XTEXT,YTEXT, FONT_COLOR_SEL,BKG_COLOR_SEL, FONT_WIDTH,FONT_HEIGHT, maxchr,
         (SHIFTON == -1) ? MVk[(vy*NPLGN)+vx+page].norml : MVk[(vy*NPLGN)+vx+page].shift);	
}

int check_vkey2(int x,int y)
{
   /*check which key is press */
   int page = (NPAGE == -1) ? 0 : NPLGN*NLIGN;
   return   MVk[y*NPLGN+x+page].val;
}
