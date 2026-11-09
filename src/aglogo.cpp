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

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <thread>
#include <vector>

#include "zcmusic.h"

#ifdef ZC_PORT
#include "port/game/zcmusic_port.h"
#endif

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

// The intros keep their own time: windowed vsync() returns at once, and full screen
// follows the monitor, so without this they would run at whatever speed the PC manages.
class IntroPace
{
    typedef std::chrono::steady_clock Clock;

    Clock::duration step;
    Clock::time_point next;

public:
    explicit IntroPace(int hz) : step(std::chrono::microseconds(1000000 / hz)), next(Clock::now() + step) {}

    void wait()
    {
        std::this_thread::sleep_until(next);
        next += step;

        const Clock::time_point now = Clock::now();

        if(next < now)
            next = now + step;
    }
};

//--------------------------------------------------------
// The 80s arcade title shown before the logo, drawn entirely in code. Odd rows are drawn
// with colours 128-255, a dimmed copy of 0-127, which gives the CRT scanlines.
//--------------------------------------------------------

enum
{
    ARC_W = 320, ARC_H = 224, ARC_HORIZON = 150, ARC_FPS = 60, ARC_HOLD = 330, ARC_FADE = 30,
    ARC_SCALE = 3, ARC_LINES = 3, ARC_STARS = 90,
    ARC_SKY = 1, ARC_SKY_N = 31, ARC_SUN = 32, ARC_SUN_N = 24, ARC_FLOOR = 56, ARC_FLOOR_N = 8,
    ARC_NEON = 64, ARC_NEON_N = 8, ARC_STAR = 72, ARC_STAR_N = 8, ARC_CHROME = 80, ARC_CHROME_N = 32,
    ARC_GLOW = 112, ARC_GLOW_N = 8, ARC_WHITE = 120, ARC_EDGE = 121
};

static const char *const arcade_lines[ARC_LINES] = { "Made for", "Zelda Fans", "Like You! <3" };

static void ArcadeMix(RGB &out, const int *a, const int *b, double t)
{
    out.r = (unsigned char)(a[0] + (b[0] - a[0]) * t + 0.5);
    out.g = (unsigned char)(a[1] + (b[1] - a[1]) * t + 0.5);
    out.b = (unsigned char)(a[2] + (b[2] - a[2]) * t + 0.5);
}

// A colour along a list of evenly spaced stops.
static void ArcadeRamp(RGB &out, const int stops[][3], int count, double t)
{
    const double at = t * (count - 1);
    const int n = at >= count - 1 ? count - 2 : (int)at;
    ArcadeMix(out, stops[n], stops[n + 1], at - n);
}

static void ArcadePalette(RGB *pal, int tick)
{
    static const int sky[][3]    = { { 2, 1, 12 }, { 10, 3, 28 }, { 28, 6, 42 }, { 56, 14, 40 } };
    static const int sun[][3]    = { { 63, 60, 20 }, { 63, 38, 10 }, { 63, 12, 36 } };
    static const int floorc[][3] = { { 12, 2, 24 }, { 3, 1, 10 } };
    static const int chrome[][3] = { { 63, 63, 63 }, { 34, 58, 63 }, { 10, 24, 56 }, { 4, 6, 26 },
                                     { 63, 42, 14 }, { 63, 60, 34 }, { 63, 26, 44 } };
    static const int magenta[3]  = { 63, 8, 56 };
    static const int cyan[3]     = { 8, 56, 63 };

    memset(pal, 0, sizeof(RGB) * 256);

    for(int i = 0; i < ARC_SKY_N; ++i)
        ArcadeRamp(pal[ARC_SKY + i], sky, 4, i / (double)(ARC_SKY_N - 1));

    for(int i = 0; i < ARC_SUN_N; ++i)
        ArcadeRamp(pal[ARC_SUN + i], sun, 3, i / (double)(ARC_SUN_N - 1));

    for(int i = 0; i < ARC_FLOOR_N; ++i)
        ArcadeRamp(pal[ARC_FLOOR + i], floorc, 2, i / (double)(ARC_FLOOR_N - 1));

    for(int i = 0; i < ARC_CHROME_N; ++i)
        ArcadeRamp(pal[ARC_CHROME + i], chrome, 7, i / (double)(ARC_CHROME_N - 1));

    const double pulse = (sin(tick * 0.035) + 1.0) / 2.0;

    for(int i = 0; i < ARC_NEON_N; ++i)
    {
        const double level = 0.35 + 0.65 * i / (ARC_NEON_N - 1);
        RGB c;
        ArcadeMix(c, magenta, cyan, pulse);
        pal[ARC_NEON + i].r = (unsigned char)(c.r * level);
        pal[ARC_NEON + i].g = (unsigned char)(c.g * level);
        pal[ARC_NEON + i].b = (unsigned char)(c.b * level);

        ArcadeMix(c, cyan, magenta, pulse);
        pal[ARC_GLOW + i].r = (unsigned char)(c.r * level);
        pal[ARC_GLOW + i].g = (unsigned char)(c.g * level);
        pal[ARC_GLOW + i].b = (unsigned char)(c.b * level);
    }

    for(int i = 0; i < ARC_STAR_N; ++i)
    {
        const int v = 10 + 53 * i / (ARC_STAR_N - 1);
        pal[ARC_STAR + i].r = (unsigned char)(v * 3 / 4);
        pal[ARC_STAR + i].g = (unsigned char)(v * 7 / 8);
        pal[ARC_STAR + i].b = (unsigned char)v;
    }

    pal[ARC_WHITE].r = pal[ARC_WHITE].g = pal[ARC_WHITE].b = 63;
    pal[ARC_EDGE].r = 3;
    pal[ARC_EDGE].g = 1;
    pal[ARC_EDGE].b = 14;

    for(int i = 0; i < 128; ++i)
    {
        pal[128 + i].r = (unsigned char)(pal[i].r * 62 / 100);
        pal[128 + i].g = (unsigned char)(pal[i].g * 62 / 100);
        pal[128 + i].b = (unsigned char)(pal[i].b * 62 / 100);
    }
}

// The lettering, already coloured: big blocky chrome letters with a dark edge and a neon glow.
// Colour 0 is see-through. line_top gets each line's first row, and one past the last.
static BITMAP *ArcadeTextMap(int *line_top)
{
    const int line_h = text_height(dsphantompfont) * ARC_SCALE;
    const int gap = 2;
    const int top = (ARC_HORIZON - (line_h * ARC_LINES + gap * (ARC_LINES - 1))) / 2 + 4;
    BITMAP *out = create_bitmap_ex(8, ARC_W, ARC_H);
    BITMAP *line_bmp = create_bitmap_ex(8, ARC_W, text_height(dsphantompfont) + 2);

    if(!out || !line_bmp)
    {
        if(out)
            destroy_bitmap(out);

        if(line_bmp)
            destroy_bitmap(line_bmp);

        return NULL;
    }

    clear_bitmap(out);

    for(int n = 0; n < ARC_LINES; ++n)
    {
        const int width = text_length(dsphantompfont, arcade_lines[n]) * ARC_SCALE;
        const int ox = (ARC_W - width) / 2, oy = top + n * (line_h + gap);

        clear_bitmap(line_bmp);
        textout_ex(line_bmp, dsphantompfont, arcade_lines[n], 0, 0, 1, 0);
        line_top[n] = oy - 5;

        for(int y = 0; y < line_h; ++y)
        {
            const int colour = ARC_CHROME + y * ARC_CHROME_N / line_h;

            for(int x = 0; x < width; ++x)
                if(line_bmp->line[y / ARC_SCALE][x / ARC_SCALE] && ox + x >= 0 && ox + x < ARC_W)
                    out->line[oy + y][ox + x] = (byte)colour;
        }
    }

    line_top[ARC_LINES] = top + ARC_LINES * (line_h + gap) + 5;
    destroy_bitmap(line_bmp);

    // edge and glow, measured from the nearest letter pixel
    BITMAP *copy = create_bitmap_ex(8, ARC_W, ARC_H);

    if(copy)
    {
        blit(out, copy, 0, 0, 0, 0, ARC_W, ARC_H);

        for(int y = 5; y < ARC_H - 5; ++y)
        {
            for(int x = 5; x < ARC_W - 5; ++x)
            {
                if(copy->line[y][x])
                    continue;

                int nearest = 99;

                for(int dy = -5; dy <= 5; ++dy)
                    for(int dx = -5; dx <= 5; ++dx)
                        if(copy->line[y + dy][x + dx] && dx * dx + dy * dy < nearest)
                            nearest = dx * dx + dy * dy;

                if(nearest <= 5)
                    out->line[y][x] = ARC_EDGE;
                else if(nearest <= 25)
                    out->line[y][x] = (byte)(ARC_GLOW + (25 - nearest) * (ARC_GLOW_N - 1) / 20);
            }
        }

        destroy_bitmap(copy);
    }

    return out;
}

static void ArcadeDraw(BITMAP *frame, BITMAP *text, const int *line_top, const short (*stars)[3], int tick)
{
    const int cx = ARC_W / 2, radius = 58;
    const int rise = tick < 90 ? (90 - tick) * (90 - tick) * 70 / 8100 : 0;
    const int sun_y = ARC_HORIZON - 12 + rise;

    // sky and sun
    for(int y = 0; y < ARC_HORIZON; ++y)
    {
        memset(frame->line[y], ARC_SKY + y * ARC_SKY_N / ARC_HORIZON, ARC_W);
        const int dy = y - sun_y;

        if(dy * dy >= radius * radius)
            continue;

        // the classic stripes: gaps that widen toward the bottom and drift downward
        if(dy > -8 && ((dy + tick / 4) & 15) < 3 + (dy + 8) / 14)
            continue;

        const int half = (int)sqrt((double)(radius * radius - dy * dy));
        memset(frame->line[y] + cx - half, ARC_SUN + (dy + radius) * ARC_SUN_N / (radius * 2), half * 2);
    }

    // stars, behind the sun
    for(int n = 0; n < ARC_STARS; ++n)
    {
        const int x = stars[n][0], y = stars[n][1];
        const int dy = y - sun_y, dx = x - cx;

        if(dx * dx + dy * dy > radius * radius)
            frame->line[y][x] = (byte)(ARC_STAR + (((tick + stars[n][2]) >> 3) & (ARC_STAR_N - 1)));
    }

    // the floor, with the grid rushing toward the viewer
    const int floor_h = ARC_H - ARC_HORIZON;

    for(int y = ARC_HORIZON; y < ARC_H; ++y)
        memset(frame->line[y], ARC_FLOOR + (y - ARC_HORIZON) * ARC_FLOOR_N / floor_h, ARC_W);

    for(int k = -14; k <= 14; ++k)
        line(frame, cx + k * 9, ARC_HORIZON, cx + k * 62, ARC_H - 1, ARC_NEON + 5);

    const double scroll = fmod(tick / 40.0, 1.0);

    for(int n = 0; n < 12; ++n)
    {
        const double z = (n + scroll) / 12.0;
        const int y = ARC_HORIZON + (int)(z * z * floor_h);

        if(y < ARC_H)
            hline(frame, 0, y, ARC_W - 1, ARC_NEON + (int)(z * (ARC_NEON_N - 1)));
    }

    hline(frame, 0, ARC_HORIZON, ARC_W - 1, ARC_NEON + ARC_NEON_N - 1);

    // the lettering wipes in a line at a time, then a shine sweeps across it now and then
    const int shine = (tick - 150) % 170 * 6 - 40;

    for(int n = 0; n < ARC_LINES; ++n)
    {
        const int reveal = (tick - (40 + n * 26)) * 16;

        if(reveal <= 0)
            continue;

        for(int y = line_top[n]; y < line_top[n + 1] && y < ARC_H; ++y)
        {
            for(int x = 0; x < ARC_W && x < reveal; ++x)
            {
                const byte c = text->line[y][x];

                if(!c)
                    continue;

                const bool letter = c >= ARC_CHROME && c < ARC_CHROME + ARC_CHROME_N;
                const int band = x + y / 2 - shine;

                if(letter && (reveal - x < 14 || (tick > 150 && band >= 0 && band < 12)))
                    frame->line[y][x] = ARC_WHITE;
                else
                    frame->line[y][x] = c;
            }
        }
    }

    // scanlines
    for(int y = 1; y < ARC_H; y += 2)
        for(int x = 0; x < ARC_W; ++x)
            frame->line[y][x] |= 128;
}

static void ArcadeIntro(BITMAP *frame, int resx, int resy)
{
    int line_top[ARC_LINES + 1];
    BITMAP *text = ArcadeTextMap(line_top);

    if(!text)
        return;

    short stars[ARC_STARS][3];
    unsigned int seed = 0x1986;

    for(int n = 0; n < ARC_STARS; ++n)
    {
        seed = seed * 1103515245u + 12345u;
        stars[n][0] = (short)((seed >> 8) % ARC_W);
        seed = seed * 1103515245u + 12345u;
        stars[n][1] = (short)((seed >> 8) % (ARC_HORIZON - 8));
        seed = seed * 1103515245u + 12345u;
        stars[n][2] = (short)((seed >> 8) % 64);
    }

    set_palette(black_palette);
    clear_bitmap(screen);

    IntroPace pace(ARC_FPS);
    PALETTE pal, shown;
    int tick = 0, fade = 0;
    bool leaving = false;

    do
    {
        ArcadeDraw(frame, text, line_top, stars, tick);
        ArcadePalette(pal, tick);

        for(int i = 0; i < 256; ++i)
        {
            shown[i].r = (unsigned char)(pal[i].r * fade / ARC_FADE);
            shown[i].g = (unsigned char)(pal[i].g * fade / ARC_FADE);
            shown[i].b = (unsigned char)(pal[i].b * fade / ARC_FADE);
        }

        set_palette_range(shown, 0, 255, false);
        vsync();

        if(sbig)
            stretch_blit(frame, screen, 0, 0, ARC_W, ARC_H, (resx - (ARC_W * screen_scale)) >> 1, (resy - (ARC_H * screen_scale)) >> 1, ARC_W * screen_scale, ARC_H * screen_scale);
        else
            blit(frame, screen, 0, 0, (resx - ARC_W) >> 1, (resy - ARC_H) >> 1, ARC_W, ARC_H);

        poll_joystick();

        if(tick >= ARC_HOLD || (tick >= 20 && (keypressed() || joy[joystick_index].button[0].b || joy[joystick_index].button[1].b)))
            leaving = true;

        if(leaving)
            --fade;
        else if(fade < ARC_FADE)
            ++fade;

        ++tick;
        pace.wait();
    }
    while(fade > 0);

    destroy_bitmap(text);
    set_palette(black_palette);
    clear_keybuf();
}

//--------------------------------------------------------
// "Celebrating 27 Years" over the flames: big gold letters that drop in one at a time, then
// ride a slow wave while a shine sweeps across them and the 27 throws off sparkles.
//--------------------------------------------------------

struct CelebrateGlyph
{
    BITMAP *bmp;    // 1 where the letter has ink
    int x, w, scale;
    bool big;
};

static const char celebrate_text[] = "Celebrating 27 Years";
enum { CELEB_LEN = sizeof(celebrate_text) - 1, CELEB_SCALE = 2, CELEB_BIG = 3, CELEB_GAP = 1 };

static void CelebrateBuild(CelebrateGlyph *g)
{
    const int th = text_height(dsphantompfont);
    char one[2] = { 0, 0 };
    int total = 0;

    for(int i = 0; i < CELEB_LEN; ++i)
    {
        one[0] = celebrate_text[i];
        g[i].big = i == 12 || i == 13;      // the 27
        g[i].scale = g[i].big ? CELEB_BIG : CELEB_SCALE;
        g[i].w = text_length(dsphantompfont, one);
        g[i].bmp = create_bitmap_ex(8, g[i].w + 1, th + 1);

        if(g[i].bmp)
        {
            clear_bitmap(g[i].bmp);
            textout_ex(g[i].bmp, dsphantompfont, one, 0, 0, 1, 0);
        }

        g[i].x = total;
        total += g[i].w * g[i].scale + CELEB_GAP;
    }

    const int left = (AGLOGO_W - total) / 2;

    for(int i = 0; i < CELEB_LEN; ++i)
        g[i].x += left;
}

static void CelebrateFree(CelebrateGlyph *g)
{
    for(int i = 0; i < CELEB_LEN; ++i)
        if(g[i].bmp)
            destroy_bitmap(g[i].bmp);
}

static inline void CelebratePut(BITMAP *frame, int h, int x, int y, int colour)
{
    if(x >= 0 && x < AGLOGO_W && y >= 0 && y < h)
        frame->line[y][x] = (byte)colour;
}

static void CelebrateDraw(BITMAP *frame, int h, const CelebrateGlyph *g, int tick, bool blended)
{
    const int th = text_height(dsphantompfont);
    const int baseline = h - 7;
    const int white = blended ? 255 : 248;
    const int shine = (tick % 200) * 3 - 60;

    // pass 0 draws the dark edge of every letter, pass 1 the letters, so no edge cuts a neighbour
    for(int pass = 0; pass < 2; ++pass)
    {
        for(int i = 0; i < CELEB_LEN; ++i)
        {
            if(!g[i].bmp)
                continue;

            const int age = tick - (30 + i * 5);

            if(age < 0)
                continue;

            int offset = (int)(sin(tick * 0.075 + i * 0.55) * (g[i].big ? 3.0 : 2.0));

            // falling in, with one small bounce
            if(age < 16)
                offset -= (16 - age) * (16 - age) * 70 / 256;
            else if(age < 26)
                offset -= (int)(sin((age - 16) * 3.14159 / 10.0) * 5.0);

            const int s = g[i].scale;
            const int top = baseline - th * s + offset;

            for(int cy = 0; cy < th; ++cy)
            {
                for(int cx = 0; cx < g[i].w; ++cx)
                {
                    if(!g[i].bmp->line[cy][cx])
                        continue;

                    const int bx = g[i].x + cx * s, by = top + cy * s;

                    if(pass == 0)
                    {
                        for(int y = by - 2; y < by + s + 3; ++y)
                            for(int x = bx - 2; x < bx + s + 2; ++x)
                                CelebratePut(frame, h, x, y, 0);

                        continue;
                    }

                    for(int y = 0; y < s; ++y)
                    {
                        // white at the top of each letter, down through orange to deep red, to stand out from the fire
                        const int level = 15 - (cy * s + y) * 12 / (th * s);
                        const int band = bx + cy * s - shine;
                        const int fire = band >= 0 && band < 16 ? white : (blended ? (level << 4) | 3 : level * 16 + 8);

                        for(int x = 0; x < s; ++x)
                            CelebratePut(frame, h, bx + x, by + y, fire);
                    }
                }
            }
        }
    }

    // sparkles around the 27
    if(tick < 30 + 13 * 5 + 26)
        return;

    const int left = g[12].x - 8, width = g[13].x + g[13].w * CELEB_BIG + 8 - left;

    for(int k = 0; k < 7; ++k)
    {
        const int phase = tick + k * 19;
        const int life = phase % 44;

        if(life >= 22)
            continue;

        unsigned int seed = (unsigned int)(phase / 44 + k * 977) * 2654435761u;
        const int cx = left + (seed >> 8) % width;
        seed = seed * 2654435761u + 17u;
        const int cy = baseline - th * CELEB_BIG - 6 + (seed >> 8) % (th * CELEB_BIG + 8);
        const int reach = (life < 11 ? life : 22 - life) / 2;

        for(int d = -reach; d <= reach; ++d)
        {
            CelebratePut(frame, h, cx + d, cy, white);
            CelebratePut(frame, h, cx, cy + d, white);
        }

        if(reach >= 3)
        {
            CelebratePut(frame, h, cx - 1, cy - 1, white);
            CelebratePut(frame, h, cx + 1, cy - 1, white);
            CelebratePut(frame, h, cx - 1, cy + 1, white);
            CelebratePut(frame, h, cx + 1, cy + 1, white);
        }
    }
}

//--------------------------------------------------------
// The ZC 3.0 Nightly intro: the logo crawls up a deep blue screen without its sword, the sword
// thrusts in from the left and slots through the letters, and the screen flashes on impact.
// Timings are upstream's. The art and music are in assets/zc, with their terms and credits.
//--------------------------------------------------------

extern ZCMUSIC *zcmusic;
extern int emusic_volume;
extern FONT *zfont;
bool try_zcmusic(char *filename, int track, int midi);

static void Zc3Show(BITMAP *frame, int resx, int resy)
{
    vsync();

    if(sbig)
        stretch_blit(frame, screen, 0, 0, AGLOGO_W, AGLOGO_H_BMP, (resx - (AGLOGO_W * screen_scale)) >> 1, (resy - (AGLOGO_H_BMP * screen_scale)) >> 1, AGLOGO_W * screen_scale, AGLOGO_H_BMP * screen_scale);
    else
        blit(frame, screen, 0, 0, (resx - AGLOGO_W) >> 1, (resy - AGLOGO_H_BMP) >> 1, AGLOGO_W, AGLOGO_H_BMP);

    poll_joystick();
}

// Returns false, having shown nothing, if the intro's files aren't there.
static bool Zc3Intro(BITMAP *frame, int resx, int resy)
{
    enum { CRAWL = 150, THRUST = 16, FLASH = 12, HIT = CRAWL + THRUST, LEAVE = 30, WHITE_INDEX = 255 };

    char exe[2048], path[2048];
    get_executable_name(exe, sizeof(exe));
    replace_filename(path, exe, "assets/zc/intro_logo.dat", sizeof(path));

    FILE *f = fopen(path, "rb");

    if(!f)
        return false;

    std::vector<byte> file;
    byte chunk[4096];

    for(size_t got; (got = fread(chunk, 1, sizeof(chunk), f)) > 0; )
        file.insert(file.end(), chunk, chunk + got);

    fclose(f);

    if(file.size() < 10 || memcmp(&file[0], "ZC3L", 4))
        return false;

    const int w = file[4] | (file[5] << 8), h = file[6] | (file[7] << 8), colours = file[8] | (file[9] << 8);
    const size_t pixels = 10 + (size_t)colours * 3;

    if(colours > 250 || file.size() < pixels + (size_t)w * h * 3)
        return false;

    PALETTE pal, shown;
    memset(pal, 0, sizeof(PALETTE));
    pal[0].r = 0x07 >> 2;           // upstream's deep blue backdrop
    pal[0].g = 0x2A >> 2;
    pal[0].b = 0xAB >> 2;
    pal[WHITE_INDEX].r = pal[WHITE_INDEX].g = pal[WHITE_INDEX].b = 63;

    for(int i = 0; i < colours; ++i)
    {
        pal[i + 1].r = file[10 + i * 3] >> 2;
        pal[i + 1].g = file[11 + i * 3] >> 2;
        pal[i + 1].b = file[12 + i * 3] >> 2;
    }

    BITMAP *layer[3];

    for(int n = 0; n < 3; ++n)
    {
        layer[n] = create_bitmap_ex(8, w, h);

        if(layer[n])
            for(int y = 0; y < h; ++y)
                memcpy(layer[n]->line[y], &file[pixels + (size_t)n * w * h + (size_t)y * w], w);
    }

    if(!layer[0] || !layer[1] || !layer[2])
    {
        for(int n = 0; n < 3; ++n)
            if(layer[n])
                destroy_bitmap(layer[n]);

        return false;
    }

    replace_filename(path, exe, "assets/zc/ZC_Sword_Woosh.wav", sizeof(path));
    SAMPLE *woosh = load_wav(path);

    const int volume = emusic_volume * 7 / 10;
    bool music = try_zcmusic((char *)"assets/zc/ZC_Forever_HD.mp3", 0, -1000);

#ifdef ZC_PORT
    if(music)
        zcmusic_set_volume(zcmusic, volume);
#endif

    const int x = (AGLOGO_W - w) / 2;
    const double start_y = AGLOGO_H_BMP + h, final_y = (AGLOGO_H_BMP - h) / 2;
    const char *prompt = "PRESS ANYTHING TO START";

    IntroPace pace(60);
    int counter = 0;
    double y = start_y;

    set_palette(pal);

    while(true)
    {
        zcmusic_poll();
        y = start_y + (final_y - start_y) * MIN(1.0, (double)counter / CRAWL);
        clear_to_color(frame, 0);

        if(counter == CRAWL && woosh)
            play_sample(woosh, 255, 128, 1000, 0);

        if(counter >= CRAWL && counter < HIT)
        {
            const double from = -w, r = (double)(counter - CRAWL + 1) / THRUST;
            draw_sprite(frame, layer[2], (int)(from + (x - from) * r), (int)y);
        }

        draw_sprite(frame, layer[counter >= HIT ? 0 : 1], x, (int)y);

        if(counter >= HIT + 45)
            textout_ex(frame, zfont, prompt, (AGLOGO_W - text_length(zfont, prompt)) / 2, AGLOGO_H_BMP - 20, WHITE_INDEX, -1);

        // the flash on impact lifts every colour toward white
        const double flash = counter >= HIT ? 0.85 * MAX(0.0, 1.0 - (double)(counter - HIT) / FLASH) : 0.0;

        for(int i = 0; i < 256; ++i)
        {
            shown[i].r = (unsigned char)(pal[i].r + (63 - pal[i].r) * flash);
            shown[i].g = (unsigned char)(pal[i].g + (63 - pal[i].g) * flash);
            shown[i].b = (unsigned char)(pal[i].b + (63 - pal[i].b) * flash);
        }

        set_palette_range(shown, 0, 255, false);
        Zc3Show(frame, resx, resy);

        if(counter >= 10 && (keypressed() || joy[joystick_index].button[0].b || joy[joystick_index].button[1].b))
            break;

        if(counter < HIT + FLASH + 60)
            ++counter;

        pace.wait();
    }

    set_palette_range(pal, 0, 255, false);

    // the logo rises off to the top while the music fades
    const double from_y = y;

    for(int c = 0; c <= LEAVE; ++c)
    {
        const double ratio = (double)c / LEAVE;

#ifdef ZC_PORT
        if(music)
            zcmusic_set_volume(zcmusic, (int)(volume - volume * sqrt(ratio)));
#endif

        zcmusic_poll();
        clear_to_color(frame, 0);
        draw_sprite(frame, layer[0], x, (int)(from_y * (1.0 - ratio)));
        Zc3Show(frame, resx, resy);
        pace.wait();
    }

    if(music)
    {
        zcmusic_stop(zcmusic);
        zcmusic_unload_file(zcmusic);
        zcmusic = NULL;
    }

    if(woosh)
        destroy_sample(woosh);

    for(int n = 0; n < 3; ++n)
        destroy_bitmap(layer[n]);

    set_palette(black_palette);
    clear_keybuf();
    return true;
}

int aglogo(BITMAP *frame, BITMAP *firebuf, int resx, int resy)
{
    // The arcade title always plays. Settings > Intro picks what follows it: the flames
    // below, or ZC 3.0's. A random pick goes by the clock, because rand() belongs to the game.
    ArcadeIntro(frame, resx, resy);

    enum { NEXT_RANDOM, NEXT_FLAMES, NEXT_ZC3, NEXT_COUNT };
    extern int intro_choice;
    int next = intro_choice;

    if(next == NEXT_RANDOM)
        next = NEXT_FLAMES + (int)((std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % (NEXT_COUNT - NEXT_FLAMES));

    if(next == NEXT_ZC3 && Zc3Intro(frame, resx, resy))
        return 0;

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

    CelebrateGlyph celebrate[CELEB_LEN];
    CelebrateBuild(celebrate);

    logovolume = get_config_int("zeldadx","logo_volume",255);
    play_sample((SAMPLE*)data[WAV_00_AGFIRE].dat,logovolume,128,1000,true);

    // 60 frames a second; the flames advance once every this many frames.
    enum { FIRE_FRAMES = 2 };
    int fireclk = 0;
    IntroPace pace(60);

    do
    {
        if(fireclk++ % FIRE_FRAMES == 0)
        {
            AddFire(firebuf,17,firerow);
            CopyAvg(firebuf,firerow);
        }

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

        CelebrateDraw(frame, h, celebrate, fireclk, logolevels != NULL);

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

        pace.wait();
    }
    while(fadecnt>0);
    
    stop_sample((SAMPLE*)data[WAV_00_AGFIRE].dat);
    CelebrateFree(celebrate);

    if(logolevels)
        destroy_bitmap(logolevels);

    clear_keybuf();
    return 0;
}

