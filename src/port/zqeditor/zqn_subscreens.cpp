// Subscreens (SUBS) of a quest made in 2.53.1 or older, saved as 2.55.17 (section version 12) or
// 3.0 Nightly (21). See zq_writers.h for the newer-version save.
//
// Both versions have the new subscreen system: lists of active, passive, overlay (and in 3.0, map)
// subscreens, each made of pages of widgets. When they open an old quest, every old custom
// subscreen becomes a one-page subscreen whose widgets are converted from the old objects
// (read_old_subscreens, ZCSubscreen::load_old and the widgets' load_old), and a quest without
// custom subscreens gets the default ones, built the same way from that version's own default
// object lists (setupsubscreens; zqn_subscreen_defaults.h, generated from their subscr.cpp).
// After the load, DMaps whose subscreen number the old engine couldn't reach make the converted
// list grow by a copy (fixup_old_dmap_subscreen_indices).
//
// The old subscreens are taken as upstream reads them: the file's own SUBS part, byte by byte, so
// names and texts come out exactly as its reader makes them. If the user changed the subscreens
// in ZQuest, they are taken from ZQuest's data, as upstream would read them from a 2.53 save.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "subscr.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqw_init.h"
#include "zqn_subscreens.h"
#include "zqn_out.h"

extern dmap *DMaps;
extern zinitdata zinit;
extern zquestheader header;

using zqwe::Buf;
using zqwe::Src;

namespace
{

struct ZqnDefObj
{
    int type, pos, x, y, w, h, ct1, c1, ct2, c2, ct3, c3;
    long d[10];
    int frames, speed, delay, frame;
    const char *text;
};

#include "zqn_subscreen_defaults.h"

// The old object types (both versions number them as 2.53 does).
enum
{
    OT_NULL, OT_NONE, OT_FRAME, OT_TEXT, OT_LINE, OT_RECT, OT_BSTIME, OT_TIME, OT_SSTIME, OT_MAGICMETER, OT_LIFEMETER,
    OT_BUTTONITEM, OT_ICON, OT_COUNTER, OT_COUNTERS, OT_MINIMAPTITLE, OT_MINIMAP, OT_LARGEMAP, OT_CLEAR,
    OT_CURRENTITEM, OT_ITEM, OT_TRIFRAME, OT_MCGUFFIN, OT_TILEBLOCK, OT_MINITILE, OT_SELECTOR1, OT_SELECTOR2,
    OT_MAGICGAUGE, OT_LIFEGAUGE, OT_TEXTBOX, OT_CURRENTITEMTILE, OT_SELECTEDITEMTILE, OT_CURRENTITEMTEXT,
    OT_CURRENTITEMNAME, OT_SELECTEDITEMNAME, OT_CURRENTITEMCLASSTEXT, OT_CURRENTITEMCLASSNAME, OT_SELECTEDITEMCLASSNAME
};

// The widget types the conversion makes (widg* in new_subscr.h).
enum
{
    W_NULL, W_FRAME, W_TEXT, W_LINE, W_RECT, W_TIME, W_MMETER, W_LMETER, W_BTNITM, W_COUNTER,
    W_OLDCTR, W_MMAPTITLE, W_MMAP, W_LMAP, W_BGCOLOR, W_ITEMSLOT, W_MCGUFF_FRAME, W_MCGUFF, W_TILEBLOCK,
    W_MINITILE, W_SELECTOR, W_LGAUGE, W_MGAUGE, W_TEXTBOX, W_SELECTEDTEXT
};

const int SST_ACTIVE = 0, SST_PASSIVE = 1, SST_MAP = 3;
const int SSCT_SYSTEM = 0xFE, SSCT_MISC = 0xFF, SSCT_TEXT = 0, SSCT_DEFAULT = 18;
const int SSF_MAX = 66;
const int CR_NONE = -1, CR_MONEY = 1, CR_LIFE = 0, CR_BOMBS = 2, CR_ARROWS = 3, CR_MAGIC = 4, CR_SBOMBS = 6;
const int CR_CUSTOM1 = 7, CR_CUSTOM26 = 32;
const int SSC_GENKEYMAGIC = -9, SSC_GENKEYNOMAGIC = -8, SSC_LEVKEYMAGIC = -7, SSC_LEVKEYNOMAGIC = -6;
const int SSC_ANYKEYMAGIC = -5, SSC_ANYKEYNOMAGIC = -4, SSC_MAXHP = -3, SSC_MAXMP = -2;
const int OLD_MAX_GROUPS = 128, OLD_MAX_OBJECTS = 256;

// An old subscreen object as upstream's reader leaves it (subscreen_object in new_subscr.h).
struct OldObj
{
    int type, pos, x, y, w, h, ct1, c1, ct2, c2, ct3, c3;
    long d[10];
    int frames, speed, delay, frame;
    bool has_text;
    std::string text;

    OldObj() : type(0), pos(0), x(0), y(0), w(0), h(0), ct1(0), c1(0), ct2(0), c2(0), ct3(0), c3(0),
        frames(0), speed(0), delay(0), frame(0), has_text(false)
    {
        memset(d, 0, sizeof(d));
    }
};

struct OldGroup
{
    int ss_type;
    std::string name;
    std::vector<OldObj> objs;       // the first numsub objects the file lists

    OldGroup() : ss_type(0) {}
};

int s16(long v) { return (short)(v & 0xFFFF); }

bool is_text_type(int t)
{
    return t == OT_TEXT || t == OT_TEXTBOX || t == OT_CURRENTITEMTEXT || t == OT_CURRENTITEMCLASSTEXT;
}

// The reader's text buffer (read_one_old_subscreen's tempdp1). An empty text copies whatever its
// first byte still holds from the text before it, which is what upstream ends up with.
struct TextBuf
{
    char b[1024];
    TextBuf() { memset(b, 0, sizeof(b)); }
};

// Conversions the reader makes after reading an object, by the section version.
void reader_fixups(OldObj &o, int v)
{
    if(v < 7)
    {
        if(o.type == OT_MAGICGAUGE && !o.d[8])
            o.d[8] = -1;        // -1 now means 'always'
        else if(o.type == OT_LIFEGAUGE)
            o.d[8] = 0;
    }

    if(o.type == OT_COUNTER && v < 3)
    {
        o.d[5] = (o.d[5] ? 1 : 0) + (o.d[7] ? 2 : 0);
        o.d[7] = 0;
    }
}

// read_one_old_subscreen (2.55.17 and 3.0 read it the same way), for section versions 6 and 7:
// older versions are taken from ZQuest's data, which 2.53's reader converted the same way.
bool read_group(Src &s, int v, bool hc24, TextBuf &tb, OldGroup &g)
{
    char tempname[64];

    if(!s.has(64))
        return false;

    memcpy(tempname, s.at(), 64);
    s.skip(64);
    int ss = v > 1 ? (int)s.c() : 0;
    int numsub = v < 4 ? (int)s.c() : (int)s.w();

    for(int j = 0; j < OLD_MAX_OBJECTS && j < numsub && !s.bad; ++j)
    {
        OldObj o;
        o.type = (int)s.c();
        o.pos = (int)s.c();
        o.x = s16(s.w());
        o.y = s16(s.w());
        o.w = (int)s.w();
        o.h = (int)s.w();
        o.ct1 = (int)s.c();
        o.c1 = s16(s.w());
        o.ct2 = (int)s.c();
        o.c2 = s16(s.w());
        o.ct3 = (int)s.c();
        o.c3 = s16(s.w());

        for(int k = 0; k < 10; ++k)
            o.d[k] = s.l();

        if(v < 2)
        {
            o.speed = (int)(s.l() & 0xFF);
            o.delay = (int)(s.l() & 0xFF);
            o.frame = (int)(s.l() & 0xFFFF);
        }
        else
        {
            o.speed = (int)s.c();
            o.delay = (int)s.c();
            o.frame = (int)s.w();
        }

        if(is_text_type(o.type))
        {
            long size = s.w();

            if(size + 2 > (long)sizeof(tb.b))
                return false;

            tb.b[size + 1] = 0;

            if(size)
            {
                if(!s.has((size_t)size + 1))
                    return false;

                memcpy(tb.b, s.at(), (size_t)size + 1);
                s.skip((size_t)size + 1);
            }

            o.has_text = true;
            o.text = tb.b;
        }
        else
        {
            if(o.type == OT_LIFEMETER && hc24)
                o.d[2] = 1;

            s.c();      // dp1's byte
        }

        reader_fixups(o, v);
        g.name = std::string(tempname, strnlen(tempname, 63));
        g.ss_type = ss;
        g.objs.push_back(o);
    }

    return !s.bad;
}

// The same, from ZQuest's data, as 2.53's writer would save it (section version 6).
void group_from_zquest(const subscreen_group &cs, TextBuf &tb, OldGroup &g)
{
    int numsub = 0;

    while(numsub < MAXSUBSCREENITEMS && cs.objects[numsub].type != OT_NULL)
        ++numsub;

    for(int j = 0; j < numsub; ++j)
    {
        const subscreen_object &z = cs.objects[j];
        OldObj o;
        o.type = z.type;
        o.pos = z.pos;
        o.x = s16(z.x);
        o.y = s16(z.y);
        o.w = z.w;
        o.h = z.h;
        o.ct1 = z.colortype1;
        o.c1 = z.color1;
        o.ct2 = z.colortype2;
        o.c2 = z.color2;
        o.ct3 = z.colortype3;
        o.c3 = z.color3;
        o.d[0] = z.d1; o.d[1] = z.d2; o.d[2] = z.d3; o.d[3] = z.d4; o.d[4] = z.d5;
        o.d[5] = z.d6; o.d[6] = z.d7; o.d[7] = z.d8; o.d[8] = z.d9; o.d[9] = z.d10;
        o.speed = z.speed;
        o.delay = z.delay;
        o.frame = z.frame;

        if(is_text_type(o.type))
        {
            const char *t = (const char *)z.dp1;
            size_t n = t ? strlen(t) : 0;

            if(n + 2 > sizeof(tb.b))
                n = sizeof(tb.b) - 2;

            tb.b[n + 1] = 0;

            if(n)
            {
                memcpy(tb.b, t, n);
                tb.b[n] = 0;
            }

            o.has_text = true;
            o.text = tb.b;
        }

        reader_fixups(o, 6);
        g.objs.push_back(o);
    }

    g.name = std::string(cs.name, strnlen(cs.name, 63));
    g.ss_type = cs.ss_type;
}

OldObj from_default(const ZqnDefObj &d)
{
    OldObj o;
    o.type = d.type; o.pos = d.pos; o.x = d.x; o.y = d.y; o.w = d.w; o.h = d.h;
    o.ct1 = d.ct1; o.c1 = d.c1; o.ct2 = d.ct2; o.c2 = d.c2; o.ct3 = d.ct3; o.c3 = d.c3;

    for(int k = 0; k < 10; ++k)
        o.d[k] = d.d[k];

    o.frames = d.frames; o.speed = d.speed; o.delay = d.delay; o.frame = d.frame;
    o.has_text = d.text != NULL;
    o.text = d.text ? d.text : "";
    return o;
}

// ---- widgets -------------------------------------------------------------------------------

int to_real_font(long ssf)
{
    // to_real_font, the same in 2.55.17 and 3.0
    static const int table[SSF_MAX] = { 0, 12, 13, 14, 15, 8, 9, 11, 2, 16, 1, 17, 18, 19, 20, 21, 22, 71, 23, 24, 25,
        26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70 };
    return (ssf >= 0 && ssf < SSF_MAX) ? table[ssf] : 0;
}

int old_ssc_to_new_ctr(long ssc)
{
    switch(ssc)
    {
    case 0: return CR_MONEY;
    case 1: return CR_BOMBS;
    case 2: return CR_SBOMBS;
    case 3: return CR_ARROWS;
    case 4: return SSC_GENKEYMAGIC;
    case 5: return SSC_GENKEYNOMAGIC;
    case 6: return SSC_LEVKEYMAGIC;
    case 7: return SSC_LEVKEYNOMAGIC;
    case 8: return SSC_ANYKEYMAGIC;
    case 9: return SSC_ANYKEYNOMAGIC;
    case 35: return CR_LIFE;
    case 36: return CR_MAGIC;
    case 37: return SSC_MAXHP;
    case 38: return SSC_MAXMP;
    }

    if(ssc < 0)
        return (int)ssc;
    if(ssc >= 10 && ssc <= 34)
        return CR_CUSTOM1 + (int)(ssc - 10);
    if(ssc >= 39 && ssc <= 104)
        return CR_CUSTOM26 + (int)(ssc - 39);
    return CR_NONE;
}

int widget_type(int t)
{
    switch(t)
    {
    case OT_FRAME: return W_FRAME;
    case OT_TEXT: return W_TEXT;
    case OT_LINE: return W_LINE;
    case OT_RECT: return W_RECT;
    case OT_BSTIME: case OT_TIME: case OT_SSTIME: return W_TIME;
    case OT_MAGICMETER: return W_MMETER;
    case OT_LIFEMETER: return W_LMETER;
    case OT_BUTTONITEM: return W_BTNITM;
    case OT_COUNTER: return W_COUNTER;
    case OT_COUNTERS: return W_OLDCTR;
    case OT_MINIMAPTITLE: return W_MMAPTITLE;
    case OT_MINIMAP: return W_MMAP;
    case OT_LARGEMAP: return W_LMAP;
    case OT_CLEAR: return W_BGCOLOR;
    case OT_CURRENTITEM: return W_ITEMSLOT;
    case OT_TRIFRAME: return W_MCGUFF_FRAME;
    case OT_MCGUFFIN: return W_MCGUFF;
    case OT_TILEBLOCK: return W_TILEBLOCK;
    case OT_MINITILE: return W_MINITILE;
    case OT_SELECTOR1: case OT_SELECTOR2: return W_SELECTOR;
    case OT_LIFEGAUGE: return W_LGAUGE;
    case OT_MAGICGAUGE: return W_MGAUGE;
    case OT_TEXTBOX: return W_TEXTBOX;
    case OT_SELECTEDITEMNAME: return W_SELECTEDTEXT;
    }

    // ssoITEM and ssoICON only become widgets in replay tests; the rest never do
    return W_NULL;
}

void flag(unsigned long &flags, unsigned long bit, bool on)
{
    if(on)
        flags |= bit;
    else
        flags &= ~bit;
}

void color(Buf &o, int type, int c)
{
    o.c(type);
    o.w(c);
}

void cstr(Buf &o, const std::string &s)
{
    size_t n = s.size() < 255 ? s.size() : 255;
    o.c((long)n);
    o.put(s.data(), n);
}

void wstr(Buf &o, const std::string &s)
{
    size_t n = s.size() < 65535 ? s.size() : 65535;
    o.w((long)n);
    o.put(s.data(), n);
}

// A script_config with no script: the script, the 8 InitD (an array: its count first) and no
// instance values.
void empty_scrconfig(Buf &o)
{
    o.w(0);
    o.w(8);

    for(int q = 0; q < 8; ++q)
        o.l(0);

    o.w(0);
}

// A transition of type 0 (instant): no arguments.
void empty_transition(Buf &o, int target)
{
    o.c(0);

    if(target == ZQ_NEWER_30)
        o.w(0);
    else
        o.c(0);

    o.w(0);
    o.c(0);
}

// SubscrWidget::write: what every widget starts with.
void widget_base(Buf &o, int target, int wtype, const OldObj &old, unsigned long genflags, unsigned long flags,
                 const long *selpos)
{
    o.c(wtype);
    o.c(old.type);
    o.c(old.pos & 0x7);
    o.w(old.x);
    o.w(old.y);
    o.w(old.w);
    o.w(old.h);
    o.l((long)genflags);
    o.l((long)flags);
    o.w(0);                                         // label
    o.c((unsigned long)old.d[0] >= (unsigned long)SSF_MAX ? 1 : 0);    // SUBSCRCOMPAT_FONT_RAND

    if(genflags & 1)                                // SUBSCRFLAG_SELECTABLE
    {
        for(int k = 0; k < 5; ++k)
            o.l(selpos[k]);

        o.w(0);                                     // override text

        if(target == ZQ_NEWER_30)
        {
            o.c(0);                                 // generic script buttons
            empty_scrconfig(o);
        }
        else
            o.w(0);                                 // generic script

        o.c(0);                                     // page mode
    }

    if(target == ZQ_NEWER_30)
    {
        // the display conditions: none
        o.w(0);                 // owned items
        o.w(0);                 // unowned items
        o.w(0xFFFF);            // counter (crNONE)
        o.w(0);                 // counter value
        o.c(0);                 // condition type
        o.w(0);                 // level items
        o.w(0xFFFF);            // level item level
        o.c(0);                 // disabled
        o.c(0);                 // dmap floors
        o.w(0);                 // dmap levels
        o.w(0);                 // dmaps
        o.w(0);                 // maps
        o.c(0);                 // screens
        o.w(0);                 // screen state map
        o.w(0);                 // screen state screen
        o.l(0);                 // screen state
        o.l(0);                 // ex state
        o.l(0);                 // level state
        o.w(0xFFFF);            // level state level
    }
}

// SW_GaugePiece::write for a gauge converted from an old one: no animation, grid or
// infinite item flags, so those fields aren't there.
void gauge(Buf &o, int target, const OldObj &old, unsigned long flags, int frames, int speed, int delay)
{
    static const long *none = NULL;
    widget_base(o, target, old.type == OT_LIFEGAUGE ? W_LGAUGE : W_MGAUGE, old, 0, flags, none);
    long tiles[4] = { old.d[1], old.d[2], old.d[3], old.d[4] };
    int csets[4] = { old.ct1, old.c1 & 0xFF, old.ct2, old.c2 & 0xFF };

    for(int q = 0; q < 4; ++q)
    {
        // SubscrMTInfo: setTileCrn then getTileCrn
        long tile = tiles[q] >> 2;
        int crn = (int)((byte)(tiles[q] % 4));
        o.l((long)(((dword)tile << 2) | (dword)(crn % 4)));
        o.c(csets[q]);
    }

    o.w(frames);

    if(frames > 1)
    {
        o.w(speed);
        o.w(delay);
    }

    o.w(old.d[0] & 0xFFFF);     // container
    o.c(0);                     // unit per frame
    o.c(0);                     // gauge width
    o.c(0);                     // gauge height
}

// A widget converted from an old object (each widget's load_old, then its write).
void write_widget(Buf &o, int target, const OldObj &old)
{
    int wt = widget_type(old.type);
    unsigned long flags = 0;
    const long *nosel = NULL;
    const long *d = old.d;

    switch(wt)
    {
    case W_FRAME:
        flag(flags, 1, d[3] != 0);
        flag(flags, 2, d[2] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(d[0]);
        color(o, old.ct1, old.c1);
        break;

    case W_TEXT:
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        wstr(o, old.has_text ? old.text : std::string());
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        break;

    case W_LINE:
        flag(flags, 1, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        color(o, old.ct1, old.c1);
        break;

    case W_RECT:
        flag(flags, 1, d[1] != 0);
        flag(flags, 2, d[0] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        color(o, old.ct2, old.c2);
        color(o, old.ct1, old.c1);
        break;

    case W_TIME:
        flag(flags, 1, old.type == OT_BSTIME);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        break;

    case W_MMETER:
        widget_base(o, target, wt, old, 0, flags, nosel);
        break;

    case W_LMETER:
        flag(flags, 1, d[1] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.c(d[2] ? 3 : 2);
        break;

    case W_BTNITM:
        flag(flags, 1, d[1] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.c(d[0]);
        break;

    case W_COUNTER:
    {
        int ctrs[3] = { old_ssc_to_new_ctr(d[6]), old_ssc_to_new_ctr(d[7]), old_ssc_to_new_ctr(d[8]) };

        for(int q = 0; q < 3; ++q)
            for(int p = 0; p < q; ++p)
                if(ctrs[p] == ctrs[q])
                    ctrs[q] = CR_NONE;

        if(ctrs[1] == CR_MONEY)
            ctrs[1] = CR_NONE;
        if(ctrs[2] == CR_MONEY)
            ctrs[2] = CR_NONE;

        flag(flags, 1, (d[5] & 1) != 0);
        flag(flags, 2, (d[5] & 2) != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        o.l(ctrs[0]);
        o.l(ctrs[1]);
        o.l(ctrs[2]);
        o.c(d[3]);              // min digits
        o.c(0);                 // max digits
        o.l(d[9]);              // infinite item
        o.c(d[4]);              // infinite character
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        break;
    }

    case W_OLDCTR:
        flag(flags, 1, d[1] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[2]);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        o.c(d[3]);
        o.l(d[9]);
        o.c(d[4]);
        break;

    case W_MMAPTITLE:
        flag(flags, 1, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        o.c(4);                 // tab size
        break;

    case W_MMAP:
        flag(flags, 1, d[0] != 0);
        flag(flags, 2, d[1] != 0);
        flag(flags, 4, d[2] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);

        if(target == ZQ_NEWER_30)
            o.w(1);             // compass level items: the McGuffin

        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);

        if(target == ZQ_NEWER_30)
        {
            color(o, SSCT_SYSTEM, -1);
            color(o, SSCT_MISC, SSCT_DEFAULT);
            color(o, SSCT_MISC, SSCT_DEFAULT);
        }

        break;

    case W_LMAP:
        flag(flags, 1, d[0] != 0);
        flag(flags, 2, d[1] != 0);
        flag(flags, 4, d[2] != 0);
        flag(flags, 8, d[9] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        break;

    case W_BGCOLOR:
        widget_base(o, target, wt, old, 0, flags, nosel);
        color(o, old.ct1, old.c1);
        break;

    case W_ITEMSLOT:
    {
        long iid = d[7] - 1;
        long maxitems = target == ZQ_NEWER_30 ? 1024 : 256;

        if((unsigned long)iid >= (unsigned long)maxitems)
            iid = -1;

        long sel[5] = { d[2], d[3], d[4], d[5], d[6] };
        flag(flags, 1, !(d[1] & 1));
        flag(flags, 2, (d[1] & 2) != 0);
        widget_base(o, target, wt, old, sel[0] >= 0 ? 1 : 0, flags, sel);
        o.l(d[0]);
        o.l(iid);
        break;
    }

    case W_MCGUFF_FRAME:
        flag(flags, 1, d[4] != 0);
        flag(flags, 2, d[5] != 0);
        flag(flags, 4, d[6] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(d[0]);              // frame tile
        o.l(d[2]);              // piece tile
        o.c(d[1]);              // frame cset
        o.c(d[3]);              // piece cset
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        break;

    case W_MCGUFF:
        flag(flags, 1, d[2] != 0);
        flag(flags, 2, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(d[0]);              // tile
        o.l(d[4]);              // number
        o.c(d[1]);              // flip
        color(o, old.ct1, old.c1);
        break;

    case W_TILEBLOCK:
        flag(flags, 1, d[2] != 0);
        flag(flags, 2, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(d[0]);
        o.c(d[1]);
        color(o, old.ct1, old.c1);
        break;

    case W_MINITILE:
    {
        long tile;
        int crn;

        if(d[0] == -1)
        {
            tile = -1;
            crn = (int)(d[2] & 0xFF);
        }
        else
        {
            tile = d[0] >> 2;
            crn = (int)(d[0] & 3);
        }

        flag(flags, 1, d[4] != 0);
        flag(flags, 2, d[5] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(tile);
        o.l(d[1]);              // special tile
        o.c(crn);
        o.c(d[3]);              // flip
        color(o, old.ct1, old.c1);
        break;
    }

    case W_SELECTOR:
        flag(flags, 1, d[3] != 0);
        flag(flags, 2, d[4] != 0);
        flag(flags, 4, old.type == OT_SELECTOR2);
        widget_base(o, target, wt, old, 0, flags, nosel);
        break;

    case W_LGAUGE:
    case W_MGAUGE:
    {
        flag(flags, 0x01, (d[9] & 0x01) != 0);
        flag(flags, 0x02, (d[9] & 0x02) != 0);
        flag(flags, 0x04, (d[9] & 0x04) != 0);
        flag(flags, 0x08, (d[9] & 0x08) != 0);
        flag(flags, 0x10, (d[9] & 0x10) != 0);

        if(wt == W_LGAUGE)
            gauge(o, target, old, flags, 1, 0, 0);
        else
        {
            gauge(o, target, old, flags, (int)(d[5] & 0xFFFF), (int)(d[6] & 0xFFFF), (int)(d[7] & 0xFFFF));
            o.w(d[8] & 0xFFFF);     // shown drain rate
        }

        break;
    }

    case W_TEXTBOX:
        flag(flags, 1, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        o.c(d[4]);              // tab size
        wstr(o, old.has_text ? old.text : std::string());
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        break;

    case W_SELECTEDTEXT:
        flag(flags, 1, d[3] != 0);
        widget_base(o, target, wt, old, 0, flags, nosel);
        o.l(to_real_font(d[0]));
        o.c(d[1]);
        o.c(d[2]);
        o.c(d[4]);
        color(o, old.ct1, old.c1);
        color(o, old.ct2, old.c2);
        color(o, old.ct3, old.c3);
        break;
    }
}

// A converted subscreen: one page holding the old objects' widgets.
struct NewSub
{
    int sub_type;
    std::string name;
    std::vector<OldObj> objs;       // only those that become widgets
};

// ZCSubscreen::load_old: the objects up to the first empty one, those that become widgets.
NewSub convert(int sub_type, const std::string &name, const std::vector<OldObj> &objs)
{
    NewSub s;
    s.sub_type = sub_type;
    s.name = name;

    for(size_t i = 0; i < objs.size() && (int)i < OLD_MAX_OBJECTS && objs[i].type != OT_NULL; ++i)
        if(widget_type(objs[i].type) != W_NULL)
            s.objs.push_back(objs[i]);

    return s;
}

void write_sub(Buf &o, int target, const NewSub &s)
{
    cstr(o, s.name);
    o.c(s.sub_type);
    o.l(0);                     // flags
    bool active = s.sub_type == SST_ACTIVE || (target == ZQ_NEWER_30 && s.sub_type == SST_MAP);

    if(active)
    {
        for(int q = 0; q < 4; ++q)
            o.w(0xFF);          // no default page for any button

        o.c(0);                 // page buttons
        o.c(0);
        empty_transition(o, target);
        empty_transition(o, target);

        if(target == ZQ_NEWER_30)
            empty_scrconfig(o);
        else
            o.w(0);             // script
    }

    o.c(1);                     // one page
    o.l(0);                     // its cursor position
    o.w((long)s.objs.size());

    for(size_t i = 0; i < s.objs.size(); ++i)
        write_widget(o, target, s.objs[i]);

    if(target == ZQ_NEWER_30)
        o.c(0);                 // page transitions
}

// ---- what upstream holds after the load -----------------------------------------------------

struct Lists
{
    std::vector<NewSub> active, passive;
    bool dmaps_fixed;                   // the DMaps' subscreen numbers changed with the fixup
    std::vector<int> dm_active, dm_passive;

    Lists() : dmaps_fixed(false) {}
};

// read_old_subscreens and fixup_old_dmap_subscreen_indices.
void from_old_groups(const std::vector<OldGroup> &groups, Lists &l)
{
    l.dm_active.resize(MAXDMAPS);
    l.dm_passive.resize(MAXDMAPS);

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        l.dm_active[i] = DMaps[i].active_subscreen;
        l.dm_passive[i] = DMaps[i].passive_subscreen;
    }

    bool hit_empty = false, has_old = false;
    int reach[2] = { 0, 0 };
    int fallback_type = -1, fallback_index = -1;

    for(size_t i = 0; i < groups.size(); ++i)
    {
        const OldGroup &g = groups[i];

        if(g.objs.empty() || g.objs[0].type == OT_NULL)
        {
            hit_empty = true;
            continue;
        }

        bool passive = g.ss_type == SST_PASSIVE;
        std::vector<NewSub> &vec = passive ? l.passive : l.active;
        vec.push_back(convert(g.ss_type, g.name, g.objs));

        if(!hit_empty)
        {
            reach[passive ? 1 : 0] = (int)vec.size();
            fallback_type = passive ? 1 : 0;
            fallback_index = (int)vec.size() - 1;
            has_old = true;
        }
    }

    if(!has_old || fallback_type < 0)
        return;

    int fallback[2] = { -1, -1 };
    fallback[fallback_type] = fallback_index;

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        for(int type = 0; type < 2; ++type)
        {
            int &idx = type == 0 ? l.dm_active[i] : l.dm_passive[i];

            if(idx < reach[type])
                continue;

            if(fallback[type] < 0)
            {
                // the old engine drew a group of the other type: a copy of it goes into this list
                std::vector<NewSub> &src = fallback_type == 1 ? l.passive : l.active;
                std::vector<NewSub> &dest = type == 1 ? l.passive : l.active;
                NewSub copy = src[fallback_index];
                copy.sub_type = type;
                dest.push_back(copy);
                fallback[type] = (int)dest.size() - 1;
            }

            if(idx != fallback[type])
                l.dmaps_fixed = true;

            idx = fallback[type];
        }
    }
}

void default_list(const ZqnDefObj *arr, size_t n, std::vector<OldObj> &out)
{
    out.clear();

    for(size_t i = 0; i < n; ++i)
        out.push_back(from_default(arr[i]));
}

// setupsubscreens, with the target version's own default object lists.
bool from_defaults(int target, int mode, Lists &l)
{
    if(mode >= 8)
        mode = 0;

    struct Def
    {
        const ZqnDefObj *a[4];
        size_t n[4];
    };

#define ZQN_DEF(v, m) { { zqn##v##_default_subscreen_active_##m##_0, zqn##v##_default_subscreen_active_##m##_1, \
        zqn##v##_default_subscreen_passive_##m##_0, zqn##v##_default_subscreen_passive_##m##_1 }, \
        { sizeof(zqn##v##_default_subscreen_active_##m##_0) / sizeof(ZqnDefObj), \
          sizeof(zqn##v##_default_subscreen_active_##m##_1) / sizeof(ZqnDefObj), \
          sizeof(zqn##v##_default_subscreen_passive_##m##_0) / sizeof(ZqnDefObj), \
          sizeof(zqn##v##_default_subscreen_passive_##m##_1) / sizeof(ZqnDefObj) } }

    static const Def defs30[8] = { ZQN_DEF(30, 0), ZQN_DEF(30, 1), ZQN_DEF(30, 2), ZQN_DEF(30, 3), ZQN_DEF(30, 4),
        ZQN_DEF(30, 5), ZQN_DEF(30, 6),
        { { zqn30_z3_active_a, zqn30_z3_active_ab, zqn30_z3_passive_a, zqn30_z3_passive_ab },
          { sizeof(zqn30_z3_active_a) / sizeof(ZqnDefObj), sizeof(zqn30_z3_active_ab) / sizeof(ZqnDefObj),
            sizeof(zqn30_z3_passive_a) / sizeof(ZqnDefObj), sizeof(zqn30_z3_passive_ab) / sizeof(ZqnDefObj) } } };
    static const Def defs255[8] = { ZQN_DEF(255, 0), ZQN_DEF(255, 1), ZQN_DEF(255, 2), ZQN_DEF(255, 3),
        ZQN_DEF(255, 4), ZQN_DEF(255, 5), ZQN_DEF(255, 6),
        { { zqn255_z3_active_a, zqn255_z3_active_ab, zqn255_z3_passive_a, zqn255_z3_passive_ab },
          { sizeof(zqn255_z3_active_a) / sizeof(ZqnDefObj), sizeof(zqn255_z3_active_ab) / sizeof(ZqnDefObj),
            sizeof(zqn255_z3_passive_a) / sizeof(ZqnDefObj), sizeof(zqn255_z3_passive_ab) / sizeof(ZqnDefObj) } } };
#undef ZQN_DEF

    const Def &d = (target == ZQ_NEWER_30 ? defs30 : defs255)[mode];
    static const char *const names[4] = { "Active Subscreen (Triforce)", "Active Subscreen (Dungeon Map)",
        "Passive Subscreen (Magic)", "Passive Subscreen (No Magic)" };

    for(int q = 0; q < 4; ++q)
    {
        std::vector<OldObj> objs;
        default_list(d.a[q], d.n[q], objs);
        // the Z3 subscreens keep the empty name a new subscreen has
        NewSub s = convert(q < 2 ? SST_ACTIVE : SST_PASSIVE, mode == 7 ? std::string() : names[q], objs);
        (q < 2 ? l.active : l.passive).push_back(s);
    }

    return true;
}

// ---- ZQuest's subscreens as loaded, to tell whether the user changed them -------------------

struct SnapObj
{
    subscreen_object o;
    std::string text;
};

struct SnapGroup
{
    int ss_type;
    std::string name;
    std::vector<SnapObj> objs;
};

std::vector<SnapGroup> g_snap;

bool same_object(const subscreen_object &a, const subscreen_object &b)
{
    return a.type == b.type && a.pos == b.pos && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
           a.colortype1 == b.colortype1 && a.color1 == b.color1 && a.colortype2 == b.colortype2 && a.color2 == b.color2 &&
           a.colortype3 == b.colortype3 && a.color3 == b.color3 && a.d1 == b.d1 && a.d2 == b.d2 && a.d3 == b.d3 &&
           a.d4 == b.d4 && a.d5 == b.d5 && a.d6 == b.d6 && a.d7 == b.d7 && a.d8 == b.d8 && a.d9 == b.d9 && a.d10 == b.d10 &&
           a.frames == b.frames && a.speed == b.speed && a.delay == b.delay && a.frame == b.frame;
}

void snap_group(const subscreen_group &cs, SnapGroup &g)
{
    g.ss_type = cs.ss_type;
    g.name.assign(cs.name, 64);
    g.objs.clear();

    for(int j = 0; j < MAXSUBSCREENITEMS; ++j)
    {
        SnapObj s;
        s.o = cs.objects[j];
        s.o.dp1 = NULL;

        if(is_text_type(cs.objects[j].type) && cs.objects[j].dp1)
            s.text = (const char *)cs.objects[j].dp1;

        g.objs.push_back(s);
    }
}

void take_snapshot()
{
    g_snap.resize(MAXCUSTOMSUBSCREENS);

    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
        snap_group(custom_subscreen[i], g_snap[i]);
}

ZqLoadHookRegistration g_hook(take_snapshot);

bool subscreens_changed()
{
    if((int)g_snap.size() != MAXCUSTOMSUBSCREENS)
        return true;

    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
    {
        SnapGroup now;
        snap_group(custom_subscreen[i], now);
        const SnapGroup &was = g_snap[i];

        if(now.ss_type != was.ss_type || now.name != was.name)
            return true;

        for(int j = 0; j < MAXSUBSCREENITEMS; ++j)
            if(!same_object(now.objs[j].o, was.objs[j].o) || now.objs[j].text != was.objs[j].text)
                return true;
    }

    return false;
}

// qr_24HC as the old-subscreen reader sees it (deprecated_rules bit 12): the file's rule, kept
// only when the rules section is older than version 8.
bool rule_24hc()
{
    const ZqQuestPart *p = zq_find_part(ID_RULES);
    Src s;

    if(!p || !zqwe::load_part(*p, s))
        return false;

    long v = s.w();
    s.skip(2 + 4);
    long b = s.c();
    b = s.c();
    return !s.bad && v < 8 && (b & 0x10) != 0;
}

void set_error(const char *what)
{
    zqw_last_error = what;
}

// ZQN_FORCE_ZQUEST=1 (for testing): take the subscreens from ZQuest's data, as if the user had
// changed them, to check that path against upstream's conversion of the same quest.
bool force_zquest()
{
    const char *e = getenv("ZQN_FORCE_ZQUEST");
    return e && *e == '1';
}

// The subscreen lists upstream holds after opening the quest (and the DMaps' numbers).
bool held_lists(int target, Lists &l)
{
    const ZqQuestPart *part = zq_find_part(ID_SUBSCREEN);
    word version = 0, cversion = 0;
    bool changed = subscreens_changed() || force_zquest();
    TextBuf tb;

    if(part)
        zq_part_versions(*part, &version, &cversion);

    if(part && !changed && version >= 6 && version < 8)
    {
        Src s;
        zqwe::Head h;

        if(!zqwe::load_part(*part, s) || !zqwe::read_head(s, 0, h))
        {
            set_error("the subscreens as loaded couldn't be read again");
            return false;
        }

        bool hc24 = rule_24hc();
        std::vector<OldGroup> groups(OLD_MAX_GROUPS);

        for(int i = 0; i < OLD_MAX_GROUPS; ++i)
            if(!read_group(s, h.version, hc24, tb, groups[i]))
            {
                set_error("the subscreens as loaded don't read the way upstream reads them");
                return false;
            }

        from_old_groups(groups, l);
    }
    else if(part || changed)
    {
        // as upstream reads a 2.53 save of ZQuest's subscreens (2.53's reader converted older
        // section versions the way upstream does)
        std::vector<OldGroup> groups(OLD_MAX_GROUPS);

        for(int i = 0; i < OLD_MAX_GROUPS; ++i)
            group_from_zquest(custom_subscreen[i], tb, groups[i]);

        from_old_groups(groups, l);
    }
    else
    {
        // No custom subscreens: the defaults, which a sectioned quest only gets with its init data.
        if(zq_quest_file.zelda_version >= 0x193 && (!zq_find_part(ID_INITDATA) || zinit.subscreen == ssdtMAX))
        {
            set_error("the quest has neither custom subscreens nor a subscreen style to build them from");
            return false;
        }

        from_defaults(target, zinit.subscreen, l);
    }

    return true;
}

int zqn_subscreens(PACKFILE *f, const ZqNewerContext &ctx)
{
    zqn::wg_edit_test_once();

    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    Lists l;

    if(!held_lists(ctx.target, l))
        return 1;

    // ZQN_WG_TRACE=file (for testing): notes the quests whose DMaps the load renumbers
    const char *trace = getenv("ZQN_WG_TRACE");

    if(trace && *trace && l.dmaps_fixed)
    {
        FILE *tf = fopen(trace, "a");

        if(tf)
        {
            fprintf(tf, "%s: dmap subscreens renumbered: %d active, %d passive subscreens\n", header.title,
                    (int)l.active.size(), (int)l.passive.size());
            fclose(tf);
        }
    }

    Buf body;
    body.c((long)(l.active.size() & 0xFF));

    for(size_t i = 0; i < l.active.size(); ++i)
        write_sub(body, ctx.target, l.active[i]);

    body.c((long)(l.passive.size() & 0xFF));

    for(size_t i = 0; i < l.passive.size(); ++i)
        write_sub(body, ctx.target, l.passive[i]);

    body.c(0);                  // overlay subscreens

    if(ctx.target == ZQ_NEWER_30)
        body.c(0);              // map subscreens

    word version = ctx.target == ZQ_NEWER_30 ? 21 : 12;
    word cversion = ctx.target == ZQ_NEWER_30 ? 21 : 3;
    Buf head;
    head.w(version);
    head.w(cversion);
    head.l((long)body.size());

    if(pack_mputl((long)ID_SUBSCREEN, f) == EOF || pack_fwrite(&head.b[0], (long)head.size(), f) != (long)head.size() ||
       (!body.b.empty() && pack_fwrite(&body.b[0], (long)body.size(), f) != (long)body.size()))
    {
        set_error("writing the subscreens failed");
        return 1;
    }

    return ZQW_OK;
}

ZqNewerRegistration g_reg(ID_SUBSCREEN, zqn_subscreens, "subscreens for 2.55.17 and 3.0");

} // namespace

bool zqn_dmap_subscreens(std::vector<int> &active, std::vector<int> &passive)
{
    Lists l;

    if(zq_quest_file.zelda_version >= 0x255 || !held_lists(ZQ_NEWER_30, l) || !l.dmaps_fixed)
        return false;

    active = l.dm_active;
    passive = l.dm_passive;
    return true;
}
