//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  aglogo.cc
//
//  Armageddon games logo with fire field generator.
//  Adapted from code written by Midnight and included in
//  SPHINX C-- examples.
//  (Tweaked by Jeremy Craner.)
//
//--------------------------------------------------------

#include "precompiled.h" //always first

#include "zc_alleg.h"
#include "zdefs.h"
#include "zeldadat.h"
#include "zc_malloc.h"
int logovolume = 0;

extern DATAFILE* data;

extern bool sbig;
extern int screen_scale;
extern int joystick_index;

extern FONT* dsphantompfont;

static void SetCols(RGB* pal)
{

    byte firepal[256*3];
    word DI,BX=0;
    
    firepal[0]=0;
    firepal[1]=0;
    firepal[2]=0;
    
    do
    {
        word AX = BX * 64 / 85;
        byte DL = AX;
        
        DI = BX+BX+BX;
        firepal[DI+3] = DL;
        firepal[DI+3+1] = 0;
        
        if(BX<75)
            firepal[DI+3+2] = BX<8 ? (BX<<1)+12 : 20;
        else firepal[DI+3+2] = BX-75;
        
        firepal[DI+3+85+85+85] = 63;
        firepal[DI+3+85+85+85+1] = DL;
        firepal[DI+3+85+85+85+2] = 0;
        
        firepal[DI+3+85+85+85+85+85+85] = 63;
        firepal[DI+3+85+85+85+85+85+85+1] = 63;
        firepal[DI+3+85+85+85+85+85+85+2] = DL;
        
        ++BX;
    }
    while(BX < 85);
    
    firepal[255*3]=0;
    firepal[255*3+1]=0;
    firepal[255*3+2]=0;
    DI=0;
    
    for(int i=0; i<256; i++)
    {
        pal[i].r = firepal[DI++];
        pal[i].g = firepal[DI++];
        pal[i].b = firepal[DI++];
    }
}

// srcrow: the row the flames are fed from (2 below the last visible row).
static void AddFire(BITMAP* firebuf,word N,int srcrow)
{
    while(N)
    {
        int cx = (rand()%10+1)<<1;
        int x = rand()%314;
        byte color = (rand()&127) + 128;

        while(cx)
        {
            firebuf->line[srcrow][x]=color;
            firebuf->line[srcrow+2][x]=color;
            ++x;
            --cx;
        }
        
        --N;
    }
}

static void CopyAvg(BITMAP* fire,int srcrow)
{
    for(int y=4; y<srcrow+4; y+=2)
    {
        for(int x=2; x<336; x+=2)
        {
            byte* si=&(fire->line[y][x-2]);
            int AX = (*si + *(si+2) + *(si+4) + fire->line[y+2][x]) >> 2;
            AX = (AX + fire->line[y-2][x]) >> 1;
            
            if(AX>128)
                AX-=2;
            else if(AX>12)
                AX-=4;
            else if(AX>8)
                AX-=2;
            else if(AX>0)
                --AX;
                
            AX=(AX<<8)+AX;
            ((word*)fire->line[y-4])[x>>1]=AX;
            ((word*)fire->line[y-3])[x>>1]=AX;
        }
    }
}

//--------------------------------------------------------
// Logo modes. The zelda.dat object at RLE_AGTEXT is either
//  - an 8-bit RLE sprite: the classic "Armageddon Games" text, drawn
//    over the fire in a 320x198 frame; or
//  - an 8-bit bitmap up to 320x224 whose palette indices run dark to
//    bright (the full-screen logo in later 2.53 data files): shown in
//    greyscale in a 320x224 frame, with the fire blended over it so the
//    flames are translucent.
//--------------------------------------------------------

#define AGLOGO_W      320
#define AGLOGO_H_RLE  198
#define AGLOGO_H_BMP  224

static const byte bayer4[4][4] =
{
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

// Quantize an 8-bit value to 16 levels with 4x4 ordered dithering,
// which hides the banding of having only 16 levels.
static inline int DitherLevel16(int v, int x, int y)
{
    v += bayer4[y&3][x&3] - 8;

    if(v < 0)
        return 0;

    if(v > 255)
        return 15;

    return v >> 4;
}

// Build the 16x16 blend palette: index = fire_level*16 + logo_level.
// Each entry screen-blends that logo grey with that fire level's colour,
//   out = 1 - (1 - grey) * (1 - strength * fire)
// so the logo is untouched where there is no fire, the flames are vivid
// over black, and the logo shows through the flames everywhere else.
static void BuildBlendPalette(const RGB *firepal, RGB *out)
{
    const double strength = 0.9;

    for(int f=0; f<16; f++)
    {
        // Level 0 is "no fire" (black); others use the middle colour of their range.
        const RGB &fc = firepal[f ? f*16 + 8 : 0];
        double fr = strength * fc.r / 63.0;
        double fg = strength * fc.g / 63.0;
        double fb = strength * fc.b / 63.0;

        for(int l=0; l<16; l++)
        {
            double g = l / 15.0;
            out[f*16+l].r = (unsigned char)(63.0 * (1.0 - (1.0-g)*(1.0-fr)) + 0.5);
            out[f*16+l].g = (unsigned char)(63.0 * (1.0 - (1.0-g)*(1.0-fg)) + 0.5);
            out[f*16+l].b = (unsigned char)(63.0 * (1.0 - (1.0-g)*(1.0-fb)) + 0.5);
        }
    }
}

// Precompute the dithered grey level (0-15) of the logo for every pixel of
// the frame: the logo is centred and its left/right edges fade into black.
static BITMAP *BuildLogoLevels(BITMAP *logo, int h)
{
    BITMAP *lv = create_bitmap_ex(8, AGLOGO_W, h);

    if(!lv)
        return NULL;

    clear_bitmap(lv);

    const int ox = (AGLOGO_W - logo->w) / 2;
    const int oy = (h - logo->h) / 2;
    const int fade = 24;

    for(int y=0; y<logo->h; y++)
    {
        for(int x=0; x<logo->w; x++)
        {
            int edge = MIN(x, logo->w - 1 - x);
            int v = logo->line[y][x];

            if(edge < fade)
                v = v * edge / fade;

            lv->line[y+oy][x+ox] = (byte)DitherLevel16(v, x+ox, y+oy);
        }
    }

    return lv;
}

int aglogo(BITMAP *frame, BITMAP *firebuf, int resx, int resy)
{
    // frame should be at least 320x224, firebuf at least 340x232
    PALETTE firepal;
    SetCols(firepal);
    PALETTE pal;
    PALETTE workpal;

    // Choose the logo mode from what zelda.dat actually holds. Anything
    // unexpected gets the fire alone instead of being misread as garbage.
    DATAFILE *logoobj = &data[RLE_AGTEXT];
    RLE_SPRITE *agtext = NULL;
    BITMAP *logolevels = NULL;

    if(logoobj->type == DAT_RLE_SPRITE)
    {
        agtext = (RLE_SPRITE*)logoobj->dat;
    }
    else if(logoobj->type == DAT_BITMAP)
    {
        BITMAP *logo = (BITMAP*)logoobj->dat;

        if(bitmap_color_depth(logo) == 8 && logo->w <= AGLOGO_W && logo->h <= AGLOGO_H_BMP)
            logolevels = BuildLogoLevels(logo, AGLOGO_H_BMP);
    }

    const int h = logolevels ? AGLOGO_H_BMP : AGLOGO_H_RLE; // visible frame height
    const int firerow = h + 2;                               // row the flames are fed from
    const int textcolor = logolevels ? 0 : 2;                // black / near-black

    if(logolevels)
        BuildBlendPalette(firepal, pal);
    else
        memcpy(pal, firepal, sizeof(PALETTE));

    set_palette(black_palette);
    clear_bitmap(frame);
    clear_bitmap(firebuf);
    clear_bitmap(screen);

    for(int f=0; f<128; f++)
    {
        AddFire(firebuf,(f>>3)+1,firerow);
        CopyAvg(firebuf,firerow);
    }

    int fadecnt=0;
    bool blackout=false;

    static const char *celebrate_text = "Celebrating 27 Years";
    static const char *release_text   = "A very special release by Deviance <3";
    // Stack both lines up from the bottom of the visible frame.
    const int line_h      = text_height(dsphantompfont);
    const int release_x   = (AGLOGO_W - text_length(dsphantompfont, release_text)) / 2;
    const int release_y   = h - 6 - line_h;
    const int celebrate_x = (AGLOGO_W - text_length(dsphantompfont, celebrate_text)) / 2;
    const int celebrate_y = release_y - line_h - 3;

    logovolume = get_config_int("zeldadx","logo_volume",255);
    play_sample((SAMPLE*)data[WAV_00_AGFIRE].dat,logovolume,128,1000,true);

    do
    {
        AddFire(firebuf,17,firerow);
        CopyAvg(firebuf,firerow);

        if(logolevels)
        {
            // Each pixel's colour index is (fire level << 4) | logo level.
            for(int y=0; y<h; y++)
            {
                byte *dst  = frame->line[y];
                byte *fire = firebuf->line[y] + 8;
                byte *lv   = logolevels->line[y];

                for(int x=0; x<AGLOGO_W; x++)
                    dst[x] = (byte)((DitherLevel16(fire[x], x, y) << 4) | lv[x]);
            }
        }
        else
        {
            blit(firebuf,frame,8,0,0,0,AGLOGO_W,h);

            if(agtext)
                draw_rle_sprite(frame,agtext,24,90);
        }

        textout_ex(frame, dsphantompfont, celebrate_text, celebrate_x, celebrate_y, textcolor, -1);
        textout_ex(frame, dsphantompfont, release_text, release_x, release_y, textcolor, -1);

        vsync();

        if(sbig)
            stretch_blit(frame,screen, 0,0,AGLOGO_W,h, (resx-(AGLOGO_W*screen_scale))>>1, (resy-(h*screen_scale))>>1, AGLOGO_W*screen_scale,h*screen_scale);
        else
            blit(frame,screen, 0,0,(resx-AGLOGO_W)>>1, (resy-h)>>1, AGLOGO_W,h);
            
        poll_joystick();
        
        if((keypressed()||joy[joystick_index].button[0].b||joy[joystick_index].button[1].b)&&fadecnt>=32)
            blackout=true;
            
        if(!blackout)
        {
            if(fadecnt<64) //I get some problems here when fade_interpolate takes invalid parameters.
            {
                if(!(++fadecnt < 0))
                    fade_interpolate(black_palette,pal,workpal,fadecnt,0,255);
                    
                set_palette_range(workpal,0,255,false);
            }
        }
        else
        {
            if(!(--fadecnt < 0))
                fade_interpolate(black_palette,pal,workpal,fadecnt,0,255);
                
            set_palette_range(workpal,0,255,false);
        }
    }
    while(fadecnt>0);
    
    stop_sample((SAMPLE*)data[WAV_00_AGFIRE].dat);

    if(logolevels)
        destroy_bitmap(logolevels);

    clear_keybuf();
    return 0;
}

