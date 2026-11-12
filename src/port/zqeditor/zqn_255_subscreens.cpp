// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the subscreens (SUBS), and the DMaps'
// subscreen numbers that go with them (zqn255_dmap_subscreens).
//
// 3.0 holds four lists of subscreens (active, passive, overlay, map), each made of pages of
// widgets, and writes them as SUBS version 21. What it holds after loading a 2.55 quest comes from
// three sections, in file order:
// - DMAP: each DMap's active, passive, overlay and map subscreen numbers;
// - INIT older than version 37 stores a subscreen style. When it isn't ssdtMAX (8), the init data
//   reader makes the default subscreens (setupsubscreens) and gives every DMap the active subscreen
//   its type calls for (0 on the overworld, 1 elsewhere) and passive subscreen 0 with
//   qr_ENABLEMAGIC, 1 without;
// - SUBS replaces the lists. Versions 6 and 7 are the old format: 3.0 converts each old custom
//   subscreen to a one-page subscreen (zqn_subscreens.cpp does that part), and at the end of the
//   load DMaps whose numbers the old engine couldn't reach get the subscreen it fell back to,
//   which can add a copy to a list (fixup_old_dmap_subscreen_indices). Versions 8-21 are the new
//   format, read widget by widget (ZCSubscreen::read, SubscrPage::read and each widget's read in
//   new_subscr.cpp) with what each version lacks taking its default, and written back as 3.0 holds
//   them (convert_new below).
// When a DMap is written, a number past its list's end becomes 0 (-1 for the map subscreen,
// validate_subscreens), so the DMap writer must use the same lists (zqn255_dmap_subscreens).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "zqn_255.h"
#include "zqn_subscreens.h"

namespace
{

const int SSDT_MAX = 8;             // ssdtMAX: no subscreen style (the quest's own subscreens)
const int SST_ACTIVE = 0, SST_MAP = 3;
const int ITEMS_30 = 1024;          // 3.0's MAXITEMS (wrap_iid)
const int WIDG_MAX = 30;            // widgMAX

typedef std::vector<byte> Bytes;

int part_index(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return (int)i;

    return -1;
}

// A part's section as the quest's own-version save writes it now.
bool own_section(dword id, Zqn255Section &s)
{
    ZqNewerContext c;
    c.target = ZQ_NEWER_30;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(id);
    c.src_version = c.src_cversion = 0;

    if(!c.src_part)
        return false;

    zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);
    return zqn255_section(c, s);
}

// ---- the new format (versions 8-21), read as 3.0 reads it and written as it writes it ---------

// p_getwstr: the characters up to the first 0 (the count's bytes are all read)
std::string get_wstr(Zqn255In &r)
{
    const size_t n = r.u16();
    Bytes b = r.bytes(n);
    size_t k = 0;

    while(k < b.size() && b[k])
        ++k;

    return std::string(b.begin(), b.begin() + k);
}

void put_wstr(Zqn255Out &o, const std::string &s)
{
    const size_t n = s.size() < 65535 ? s.size() : 65535;
    o.w((int64_t)n);
    o.put(s.data(), n);
}

// SubscrColorInfo: a type byte and a colour word, as they are
void color(Zqn255In &r, Zqn255Out &o)
{
    o.c(r.u8());
    o.w(r.u16());
}

void colors3(Zqn255In &r, Zqn255Out &o)
{
    color(r, o);
    color(r, o);
    color(r, o);
}

struct Conv
{
    Zqn255In r;
    int v;
    std::string why;    // set when 3.0 couldn't read it (or this can't tell what it would hold)

    Conv(const Bytes &d, int version) : r(d), v(version) {}

    bool fail(const char *what)
    {
        if(why.empty())
            why = what;

        return false;
    }
    bool ok() const { return why.empty() && !r.bad; }

    // SubscrTransition: version 17 made the sound a word; the writer writes as many arguments as
    // the type has (num_args), those the file didn't have as 0.
    void transition(Zqn255Out &o)
    {
        const byte type = r.u8();
        const word sfx = v < 17 ? r.u8() : r.u16();
        const word flags = r.u16();
        const int args = r.u8();
        long arg[4] = { 0, 0, 0, 0 };

        if(args > 4)
            fail("a subscreen transition has more arguments than 3.0 holds");

        for(int q = 0; q < args && q < 4; ++q)
            arg[q] = r.s32();

        const int n = type == 1 ? 2 : type == 2 ? 4 : 0;
        o.c(type);
        o.w(sfx);
        o.w(flags);
        o.c(n);

        for(int q = 0; q < n; ++q)
            o.l(arg[q]);
    }

    // SubscrSelectorInfo: the position and size, then a count of tile settings, of which 3.0
    // keeps two (written always two, those missing zeros)
    void selector(Zqn255Out &o)
    {
        for(int q = 0; q < 4; ++q)
            o.w(r.u16());

        const int n = r.u8();
        Bytes info[2] = { Bytes(12, 0), Bytes(12, 0) };

        for(int q = 0; q < n; ++q)
        {
            Bytes b = r.bytes(12);

            if(q < 2 && b.size() == 12)
                info[q] = b;
        }

        o.c(2);
        o.put(info[0]);
        o.put(info[1]);
    }

    // script_config as version 21 stores it (p_getvar): the script, the run arguments (a counted
    // array of 8) and the instance values (a counted map, written in key order)
    void script_config(Zqn255Out &o)
    {
        const word script = r.u16();
        const int n = r.u16();
        long args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        if(n > 8)
            fail("a script has more than 8 InitD values");

        for(int q = 0; q < n && q < 8; ++q)
            args[q] = r.s32();

        const int m = r.u16();
        std::map<word, long> inst;

        for(int q = 0; q < m && !r.bad; ++q)
        {
            const word key = r.u16();
            inst[key] = r.s32();
        }

        put_script(o, script, args, &inst);
    }

    // script_config as older versions store it: the script, then (with a script) for a widget
    // the buttons it runs on, and the 8 InitDs
    void old_script(Zqn255Out &o, bool btns)
    {
        const word script = r.u16();
        byte buttons = 0;
        long args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        if(script)
        {
            if(btns)
                buttons = r.u8();

            for(int q = 0; q < 8; ++q)
                args[q] = r.s32();
        }

        if(btns)
            o.c(buttons);

        put_script(o, script, args, NULL);
    }

    static void put_script(Zqn255Out &o, word script, const long *args, const std::map<word, long> *inst)
    {
        o.w(script);
        o.w(8);

        for(int q = 0; q < 8; ++q)
            o.l(args[q]);

        o.w(inst ? (int64_t)inst->size() : 0);

        if(inst)
            for(std::map<word, long>::const_iterator it = inst->begin(); it != inst->end(); ++it)
            {
                o.w(it->first);
                o.l(it->second);
            }
    }

    // a set of word values (items: bytes before version 18), counted by a word; written in order
    void word_set(Zqn255Out &o, bool bytes)
    {
        const int n = r.u16();
        std::set<word> s;

        for(int q = 0; q < n && !r.bad; ++q)
            s.insert(bytes ? (word)r.u8() : r.u16());

        o.w((int64_t)s.size());

        for(std::set<word>::const_iterator it = s.begin(); it != s.end(); ++it)
            o.w(*it);
    }

    // a set of byte values counted by a byte (p_getcset / p_putcset: at most 255 written)
    void byte_set(Zqn255Out &o)
    {
        const int n = r.u8();
        std::set<byte> s;

        for(int q = 0; q < n && !r.bad; ++q)
            s.insert(r.u8());

        const int put = s.size() < 255 ? (int)s.size() : 255;
        o.c(put);
        int k = 0;

        for(std::set<byte>::const_iterator it = s.begin(); it != s.end() && k < put; ++it, ++k)
            o.c(*it);
    }

    // SW_GaugePiece: some fields only follow with the flags that use them; version 18 made the
    // infinite item a dword
    void gauge(Zqn255Out &o, dword flags)
    {
        for(int q = 0; q < 4; ++q)
        {
            o.l(r.s32());   // SubscrMTInfo: tile and corner, the same once read and written
            o.c(r.u8());
        }

        const word frames = r.u16();
        o.w(frames);
        const bool frcond = frames <= 1;
        const bool acond = frcond || !(flags & 0xC0);
        const bool frcond2 = frcond || (!acond && frames <= 2 && (flags & 0x200));
        const bool infcond = !(flags & 0xC00);

        if(!frcond2)
        {
            o.w(r.u16());   // speed
            o.w(r.u16());   // delay
        }

        o.w(r.u16());       // container
        o.c(r.u8());        // unit per frame
        const byte wid = r.u8(), hei = r.u8();
        o.c(wid);
        o.c(hei);

        if(wid || hei)
        {
            o.c(r.u8());
            o.c(r.u8());
            o.w(r.u16());
            o.w(r.u16());
        }

        if(!acond)
            o.w(r.u16());   // animation value

        if(!infcond)
            o.l(v < 18 ? (long)r.s16() : r.s32());
    }

    // font, alignment (most), shadow style
    void text_style(Zqn255Out &o, bool align)
    {
        o.l(r.s32());

        if(align)
            o.c(r.u8());

        o.c(r.u8());
    }

    // A widget: SubscrWidget::readWidg, then its write.
    void widget(Zqn255Out &o)
    {
        const int type = r.u8();

        if(r.bad)
            return;

        if(type <= 0 || type >= WIDG_MAX)
        {
            // newType has no widget for it (a null widget only in replay tests): 3.0 can't load it
            fail("a subscreen widget's type isn't one 3.0 has");
            return;
        }

        o.c(type);
        o.c(r.u8());            // the old object type
        o.c(r.u8());            // posflags
        o.w(r.u16());           // x, y, w, h
        o.w(r.u16());
        o.w(r.u16());
        o.w(r.u16());
        const dword genflags = r.u32();
        const dword flags = r.u32();
        o.l(genflags);
        o.l(flags);
        put_wstr(o, v >= 9 ? get_wstr(r) : std::string());     // label
        o.c(v >= 11 ? r.u8() : 0);                              // compat flags

        if(genflags & 1)        // SUBSCRFLAG_SELECTABLE
        {
            for(int q = 0; q < 5; ++q)
                o.l(r.s32());   // pos, up, down, left, right

            put_wstr(o, get_wstr(r));   // override text

            if(genflags & 4)    // SUBSCRFLAG_SELOVERRIDE
                selector(o);

            if(v >= 21)
            {
                o.c(r.u8());
                script_config(o);
            }
            else
                old_script(o, true);

            const byte pg_mode = r.u8();
            o.c(pg_mode);

            if(pg_mode)
            {
                o.c(r.u8());    // buttons
                o.c(r.u8());    // target
                transition(o);
            }
        }

        // the display conditions (version 14), with what older versions lack at its default
        if(v >= 14)
        {
            word_set(o, v < 18);
            word_set(o, v < 18);
            o.w(r.u16());       // counter
            o.w(r.u16());       // counter value
            o.c(r.u8());        // condition type
            o.w(v >= 15 ? r.u16() : r.u8());    // level items
            o.w(r.u16());       // level item level
            o.c(r.u8() ? 1 : 0);                // disabled
        }
        else
        {
            o.w(0);
            o.w(0);
            o.w(0xFFFF);        // crNONE
            o.w(0);
            o.c(0);
            o.w(0);
            o.w(0xFFFF);
            o.c(0);
        }

        if(v >= 16)
        {
            byte_set(o);        // floors
            word_set(o, false); // levels
            word_set(o, false); // dmaps
            word_set(o, false); // maps
            byte_set(o);        // screens
            o.w(r.u16());       // screen state map, screen
            o.w(r.u16());
            o.l(r.u32());       // screen, extra and level states
            o.l(r.u32());
            o.l(r.u32());
            o.w(r.u16());       // level state level
        }
        else
        {
            o.c(0);
            o.w(0);
            o.w(0);
            o.w(0);
            o.c(0);
            o.w(0);
            o.w(0);
            o.l(0);
            o.l(0);
            o.l(0);
            o.w(0xFFFF);
        }

        own_data(o, type, flags);
    }

    // each widget type's own fields
    void own_data(Zqn255Out &o, int type, dword flags)
    {
        switch(type)
        {
        case 1:     // widgFRAME
            o.l(r.s32());
            color(r, o);
            break;

        case 2:     // widgTEXT
            text_style(o, true);
            put_wstr(o, get_wstr(r));
            colors3(r, o);
            break;

        case 3:     // widgLINE
            color(r, o);
            break;

        case 4:     // widgRECT
            color(r, o);
            color(r, o);
            break;

        case 5:     // widgTIME
            text_style(o, true);
            colors3(r, o);
            break;

        case 6:     // widgMMETER
        case 20:    // widgSELECTOR
            break;

        case 7:     // widgLMETER
        case 8:     // widgBTNITM
            o.c(r.u8());
            break;

        case 9:     // widgCOUNTER
        {
            text_style(o, true);
            Zqn255Out c;
            colors3(r, c);
            o.put(c.b);

            for(int q = 0; q < 3; ++q)
                o.l(r.s32());   // counters

            o.c(r.u8());        // min digits
            o.c(r.u8());        // max digits
            o.l(r.s32());       // infinite item
            o.c(r.u8());        // infinite character

            if(v >= 12)
                colors3(r, o);
            else
                o.put(c.b);     // the second colours are the first's

            break;
        }

        case 10:    // widgOLDCTR
            text_style(o, false);
            colors3(r, o);
            o.c(r.u8());
            o.l(r.s32());
            o.c(r.u8());
            break;

        case 11:    // widgMMAPTITLE
            text_style(o, true);
            colors3(r, o);
            o.c(v >= 10 ? r.u8() : 4);  // tab size
            break;

        case 12:    // widgMMAP
            if(v >= 15)
                o.w(r.u16());   // compass level items
            else if(v >= 13)
                o.w(r.u8());
            else
                o.w(1);         // the McGuffin

            colors3(r, o);

            if(v >= 19)
                color(r, o);    // visited room
            else
            {
                o.c(0xFE);      // ssctSYSTEM, -1
                o.w(0xFFFF);
            }

            if(v >= 20)
            {
                color(r, o);
                color(r, o);
            }
            else
            {
                o.c(0xFF);      // ssctMISC, ssctDEFAULT
                o.w(18);
                o.c(0xFF);
                o.w(18);
            }

            break;

        case 13:    // widgLMAP
            color(r, o);
            color(r, o);
            break;

        case 14:    // widgBGCOLOR
            color(r, o);
            break;

        case 15:    // widgITEMSLOT
        {
            o.l(r.s32());       // item class
            long iid = r.s32();

            if((unsigned long)iid >= (unsigned long)ITEMS_30)
                iid = -1;       // wrap_iid

            o.l(iid);
            break;
        }

        case 16:    // widgMCGUFF_FRAME
            o.l(r.s32());
            o.l(r.s32());
            o.c(r.u8());
            o.c(r.u8());
            color(r, o);
            color(r, o);
            break;

        case 17:    // widgMCGUFF
            o.l(r.s32());
            o.l(r.s32());
            o.c(r.u8());
            color(r, o);
            break;

        case 18:    // widgTILEBLOCK
            o.l(r.s32());
            o.c(r.u8());
            color(r, o);
            break;

        case 19:    // widgMINITILE
            o.l(r.s32());
            o.l(r.s32());
            o.c(r.u8());
            o.c(r.u8());
            color(r, o);
            break;

        case 21:    // widgLGAUGE
            gauge(o, flags);
            break;

        case 22:    // widgMGAUGE
            gauge(o, flags);
            o.w(r.u16());       // shown at drain
            break;

        case 23:    // widgTEXTBOX
            text_style(o, true);
            o.c(r.u8());        // tab size
            put_wstr(o, get_wstr(r));
            colors3(r, o);
            break;

        case 24:    // widgSELECTEDTEXT
            text_style(o, true);
            o.c(r.u8());
            colors3(r, o);
            break;

        case 25:    // widgMISCGAUGE
            gauge(o, flags);
            o.w(r.u16());       // counter
            o.w(r.u16());       // per container
            break;

        case 26:    // widgBTNCOUNTER
        {
            text_style(o, true);
            Zqn255Out c;
            colors3(r, c);
            o.put(c.b);

            for(int q = 0; q < 5; ++q)
                o.c(r.u8());    // min, max digits, infinite character, button, cost

            if(v >= 12)
                colors3(r, o);
            else
                o.put(c.b);

            break;
        }

        case 27:    // widgCOUNTERPERCBAR
            o.w(r.u16());
            color(r, o);
            color(r, o);
            break;

        case 28:    // widgITMCOOLDOWNGAUGE
            gauge(o, flags);
            o.l(r.s32());
            o.l(r.s32());
            o.c(r.u8());
            o.l(r.s32());
            o.l(r.s32());
            break;

        case 29:    // widgITMCOOLDOWNTEXT
            text_style(o, true);
            colors3(r, o);
            o.l(r.s32());
            o.l(r.s32());
            o.c(r.u8());
            break;
        }
    }

    // SubscrPage: the cursor position (3.0 keeps it in a byte), the widgets, and from version 16
    // the page's change transitions
    void page(Zqn255Out &o)
    {
        const dword cursor = r.u32();
        o.l(cursor & 0xFF);
        const int n = r.u16();
        o.w(n);

        for(int q = 0; q < n && ok(); ++q)
            widget(o);

        const int trs = v >= 16 ? r.u8() : 0;
        o.c(trs);

        for(int q = 0; q < trs && ok(); ++q)
        {
            o.c(r.u8());        // buttons
            o.c(r.u8());        // mode
            o.c(r.u8());        // target
            o.c(r.u8());        // flags
            transition(o);
        }
    }

    // ZCSubscreen::read, then write
    void subscreen(Bytes &out)
    {
        Zqn255Out o;
        const size_t nlen = r.u8();
        o.c((int64_t)nlen);   // p_getcstr keeps every character
        o.put(r.bytes(nlen));
        const byte sub_type = r.u8();
        const dword flags = r.u32();
        o.c(sub_type);
        o.l(flags);
        const bool active = sub_type == SST_ACTIVE || sub_type == SST_MAP;
        Zqn255Out sel, trans;
        word def_btns[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

        if(active)
        {
            if(flags & 2)       // SUBFLAG_ACT_OVERRIDESEL
                selector(sel);

            for(int q = 0; q < 4; ++q)
                def_btns[q] = r.u16();

            trans.c(r.u8());    // page left, right buttons
            trans.c(r.u8());
            transition(trans);
            transition(trans);

            if(v >= 21)
                script_config(trans);
            else
                old_script(trans, false);
        }

        const int pages = r.u8();
        std::vector<Bytes> pg;

        for(int q = 0; q < pages && ok(); ++q)
        {
            Zqn255Out p;
            page(p);
            pg.push_back(p.b);
        }

        if(active)
        {
            o.put(sel.b);

            // a page past the subscreen's pages: none
            for(int q = 0; q < 4; ++q)
                o.w((def_btns[q] & 0xFF) >= pages ? 0xFF : def_btns[q]);

            o.put(trans.b);
        }

        // only active subscreens have more than one page
        const int put = pages && !active ? 1 : pages;
        o.c(put);

        for(int q = 0; q < put && q < (int)pg.size(); ++q)
            o.put(pg[q]);

        out.swap(o.b);
    }
};

// The lists of a new-format section, each subscreen as 3.0 writes it.
bool convert_new(const Bytes &data, int v, std::vector<Bytes> lists[4], std::string &why)
{
    Conv c(data, v);
    const int nl = v >= 16 ? 4 : 3;

    for(int l = 0; l < nl && c.ok(); ++l)
    {
        const int n = c.r.u8();

        for(int q = 0; q < n && c.ok(); ++q)
        {
            Bytes b;
            c.subscreen(b);
            lists[l].push_back(b);
        }
    }

    if(!c.ok())
    {
        why = c.why.empty() ? "its subscreens aren't laid out as expected" : c.why;
        return false;
    }

    return true;
}

// ---- what 3.0 holds after the load ----------------------------------------------------------

struct Held
{
    std::vector<Bytes> lists[4];        // active, passive, overlay, map
};

// readinitdata: the subscreen style init data older than version 37 stores (ssdtMAX otherwise,
// and with no init data, which leaves the subscreens alone).
bool init_style(int *mode)
{
    *mode = SSDT_MAX;

    if(!zq_find_part(ID_INITDATA))
        return true;

    Zqn255Section s;

    if(!own_section(ID_INITDATA, s))
        return false;

    if(s.version >= 37)
        return true;

    Tape t;
    TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), 0, &t);

    if(!zq_tape_init(p, s.version) || p.bad || !t.find("subscreen"))
        return false;

    *mode = (int)t.get("subscreen");
    return true;
}

// The DMaps as the DMAP section stores them; past its count (or without one) as 3.0 holds unread
// DMaps: type 0, subscreens 0, no map subscreen.
bool stored_dmaps(std::vector<Zqn255DmapSubs> &dm)
{
    Zqn255DmapSubs blank = { 0, 0, 0, 0, -1 };
    dm.assign(MAXDMAPS, blank);

    if(!zq_find_part(ID_DMAPS))
        return true;

    Zqn255Section s;

    if(!own_section(ID_DMAPS, s) || s.data.size() < 2)
        return false;

    const int count = s.data[0] | (s.data[1] << 8);
    size_t pos = 2;

    for(int i = 0; i < count && i < MAXDMAPS; ++i)
    {
        Tape t;
        TapeParser p(&s.data[0], s.data.size(), pos, &t);

        if(!zq_tape_dmap(p, s.version) || p.bad)
            return false;

        pos = p.pos;
        dm[i].type = (int)t.get("type");
        dm[i].active = (int)t.get("active_subscreen");
        dm[i].passive = (int)t.get("passive_subscreen");
        dm[i].overlay = (int)t.get("overlay_subscreen", 0);
        dm[i].map = (int)t.get("map_subscreen", -1);
    }

    return true;
}

// The lists 3.0 holds after loading the quest. dm: every DMap as the DMAP section stores it on the
// way in, with its numbers as 3.0 writes them on the way out.
bool held_lists(int target, std::vector<Zqn255DmapSubs> &dm, Held &h, std::string &why)
{
    if(target != ZQ_NEWER_30 || dm.size() != (size_t)MAXDMAPS)
    {
        why = "the DMaps' subscreens are only worked out for 3.0";
        return false;
    }

    int mode;

    if(!init_style(&mode))
    {
        why = "its init data (the subscreen style) isn't laid out as expected";
        return false;
    }

    // In file order: the init data (when it sets a style, the default lists, and every DMap's
    // numbers from its type if the DMaps were read before it) and the subscreens; the later of
    // the two leaves its lists.
    const int pd = part_index(ID_DMAPS), pi = part_index(ID_INITDATA), ps = part_index(ID_SUBSCREEN);
    const bool init_sets = pi >= 0 && mode != SSDT_MAX;

    if(init_sets && pi > pd)
    {
        std::vector<byte> rules;

        if(!zqn255_rules_loaded(target, rules) || rules.size() < 100)
        {
            why = "its quest rules (qr_ENABLEMAGIC) couldn't be worked out";
            return false;
        }

        const bool magic = (rules[qr_ENABLEMAGIC >> 3] >> (qr_ENABLEMAGIC & 7)) & 1;

        for(int i = 0; i < MAXDMAPS; ++i)
        {
            const int type = dm[i].type & dmfTYPE;
            dm[i].active = (type == dmOVERW || type == dmBSOVERW) ? 0 : 1;
            dm[i].passive = magic ? 0 : 1;
        }
    }

    if(ps >= 0 && (!init_sets || ps > pi))
    {
        Zqn255Section s;

        if(!own_section(ID_SUBSCREEN, s))
        {
            why = "its subscreens couldn't be read again";
            return false;
        }

        if(s.version >= 8 && s.version <= 21)
        {
            if(!convert_new(s.data, s.version, h.lists, why))
                return false;
        }
        else if(s.version >= 6 && s.version <= 7)
        {
            // the old subscreens' fixup runs at the end of the load, on the DMaps' numbers then
            std::vector<int> act(MAXDMAPS), pas(MAXDMAPS);

            for(int i = 0; i < MAXDMAPS; ++i)
            {
                act[i] = dm[i].active & 0xFF;
                pas[i] = dm[i].passive & 0xFF;
            }

            if(!zqn_old_subscreens_30(s.data, s.version, act, pas, h.lists[0], h.lists[1]))
            {
                why = "its old-format subscreens don't read the way 3.0 reads them";
                return false;
            }

            for(int i = 0; i < MAXDMAPS; ++i)
            {
                dm[i].active = act[i];
                dm[i].passive = pas[i];
            }
        }
        else
        {
            why = "its subscreen section version isn't one this converts";
            return false;
        }
    }
    else if(init_sets)
        zqn_default_subscreens_30(mode, h.lists[0], h.lists[1]);

    // validate_subscreens, as each DMap is written
    const size_t na = h.lists[0].size(), np = h.lists[1].size(), no = h.lists[2].size(), nm = h.lists[3].size();

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        Zqn255DmapSubs &d = dm[i];
        d.active &= 0xFF;
        d.passive &= 0xFF;
        d.overlay &= 0xFF;

        if((size_t)d.active >= na)
            d.active = 0;

        if((size_t)d.passive >= np)
            d.passive = 0;

        if((size_t)d.overlay >= no)
            d.overlay = 0;

        const short m = (short)d.map;

        if(m < 0 || (size_t)m >= nm || nm == 0)
            d.map = -1;
        else
            d.map = m;
    }

    return true;
}

int write_subscreens(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_30)
        return ZQW_UNSUPPORTED;

    if(ctx.src_part && (ctx.src_version < 6 || ctx.src_version > 21))
        return ZQW_UNSUPPORTED;

    std::vector<Zqn255DmapSubs> dm;

    if(!stored_dmaps(dm))
    {
        zqw_last_error = "its DMaps (their subscreens) aren't laid out as expected";
        return 2;
    }

    Held h;
    std::string why;

    if(!held_lists(ctx.target, dm, h, why))
    {
        zqw_last_error = why;
        return 2;
    }

    // ZQN255_DMAP_SUBS_TRACE=<file> (tests only): every DMap's numbers as zqn255_dmap_subscreens
    // gives them for the DMap writer, to check against upstream's DMAP section
    const char *trace = getenv("ZQN255_DMAP_SUBS_TRACE");

    if(trace && *trace)
    {
        std::vector<Zqn255DmapSubs> td;
        FILE *tf = fopen(trace, "w");

        if(tf)
        {
            int mode = -1;
            init_style(&mode);
            fprintf(tf, "# style %d, SUBS v%d, lists %d %d %d %d\n", mode, (int)ctx.src_version, (int)h.lists[0].size(),
                    (int)h.lists[1].size(), (int)h.lists[2].size(), (int)h.lists[3].size());

            if(stored_dmaps(td) && zqn255_dmap_subscreens(ctx.target, td))
                for(size_t i = 0; i < td.size(); ++i)
                    fprintf(tf, "%d %d %d %d %d\n", (int)i, td[i].active, td[i].passive, td[i].overlay, td[i].map);
            else
                fprintf(tf, "failed\n");

            fclose(tf);
        }
    }

    // writesubscreens: each list's count as a byte, and that many of its subscreens
    Zqn255Out o;

    for(int l = 0; l < 4; ++l)
    {
        const size_t n = h.lists[l].size() & 0xFF;
        o.c((int64_t)n);

        for(size_t q = 0; q < n; ++q)
            o.put(h.lists[l][q]);
    }

    std::vector<byte> none;
    return zqn255_emit(f, ID_SUBSCREEN, 21, 21, none, o.b);
}

ZqNewerRegistration g_reg_subscreens(ID_SUBSCREEN, write_subscreens, "2.55 subscreens as 3.0");

} // namespace

bool zqn255_dmap_subscreens(int target, std::vector<Zqn255DmapSubs> &dmaps)
{
    if(zq_quest_file.zelda_version < 0x255 || target != ZQ_NEWER_30 || dmaps.size() != (size_t)MAXDMAPS)
        return false;

    Held h;
    std::string why;
    std::vector<Zqn255DmapSubs> dm = dmaps;

    if(!held_lists(target, dm, h, why))
        return false;

    dmaps = dm;
    return true;
}
