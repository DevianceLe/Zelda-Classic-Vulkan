// ZQuest's writers for the misc colours (MCLR), game icons (ICON), palettes (CSET), tiles
// (TILE), weapon sprites (WPN), sound effects (SFX) and tunes (MIDI) of a quest saved in the
// version it was made in (zq_writers.h). They take every section version the loaders read:
// src/qst.cpp's readers in quests from 1.93 to 2.53, and src/port/game/q255_media.cpp's in
// 2.55 and 3.0 Nightly quests (whose tunes up to v4 still go through src/qst.cpp's readtunes).
//
// A writer walks the part as it was loaded, record by record (a tile, a CSet, a sound, a
// tune), mirroring the reader just far enough to know what each record turned into. A record
// that still turns into exactly what ZQuest holds is copied as it came, so what a loader
// dropped or narrowed stays in the file: the low two bits of a 2.55 palette, 24-bit and float
// samples, tile numbers past this engine's last tile, sprites and sounds past its last slot,
// name bytes after the end of the name. A record that no longer does is encoded from ZQuest's
// data in that version's format. With zqw_force_serialize every record is encoded, for the
// round-trip check.
//
// When ZQuest holds something the version has no room for (a renamed weapon sprite in a 2.10
// quest, an 8-bit tile in a 2.10 quest, more tiles than the file can count), the writer says
// what in allegro.log and fails, so the save stops instead of dropping it.
//
// 2.53.0 and 2.53.1 quests are taken too: their sections are in 2.53's own format, and copying
// records keeps the bytes ZQuest's own 2.53 writers would rewrite from memory. A 2.53.1 quest
// is still saved by save_quest as always (zq_files.cpp), so this changes nothing there.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zc_alleg.h"
#include "zdefs.h"
#include "zsys.h"
#include "colors.h"
#include "tiles.h"
#include "sfx.h"
#include "q30_audio.h"
#include "zq_q255_store.h"
#include "zq_writers.h"

extern miscQdata            misc;
extern byte                 *colordata;
extern char                 palnames[MAXLEVELS][17];
extern tiledata             *newtilebuf;
extern wpndata              *wpnsbuf;
extern char                 *weapon_string[WPNCNT];
extern const char           *old_weapon_string[wLast];
extern SAMPLE               customsfxdata[WAV_COUNT];
extern unsigned char        customsfxflag[WAV_COUNT>>3];
extern char                 *sfx_string[WAV_COUNT];
extern const char           *old_sfx_string[Z35];
extern DATAFILE             *sfxdata;
extern byte                 midi_flags[MIDIFLAGS_SIZE];
extern zctune               *customtunes;
extern byte                 quest_rules[QUESTRULES_SIZE];

namespace
{

// ---- limits of the 2.55 formats (q255_media.cpp) -------------------------------------

const long MAX_TILES_255 = 214500;
const int MAX_SPRITES_255 = 8192;
const int MAX_TUNES_255 = 512;
const int V5_LEVEL_CSETS = 17;
const int V5_SPRITE_START = pdFULL + 17 * 512;
const int V5_TOTAL_CSETS = pdFULL + 17 * 512 + 30;
const byte SAMPLE_NONE = 0;
const byte SAMPLE_WAV = 1;
const byte SAMPLE_OGG = 2;
const int CSET_BYTES = 48;

// ---- which loader reads the part --------------------------------------------------------

enum Family { FAM_NONE, FAM_OLD, FAM_255 };

Family family_of(const ZqPartContext &c)
{
    if(c.zelda_version == 0x255)
        return FAM_255;

    if(c.zelda_version > 0x192 && c.zelda_version < 0x255)
        return FAM_OLD;     // sectioned, read by src/qst.cpp

    return FAM_NONE;
}

// ---- results ------------------------------------------------------------------------------

enum { ZQWM_CANT_HOLD = 1, ZQWM_BAD_PART = 2, ZQWM_WRITE = 3 };

int cant_hold(const char *section, const char *what, long index)
{
    char buf[300];

    if(index >= 0)
        snprintf(buf, sizeof(buf), "%s: %s (%ld) can't be kept in this quest's version", section, what, index);
    else
        snprintf(buf, sizeof(buf), "%s: %s can't be kept in this quest's version", section, what);

    al_trace("Saving the %s; the save stops.\n", buf);
    zqw_last_error = buf;       // the reason the save shows the user (zq_writers.h)
    return ZQWM_CANT_HOLD;
}

int bad_part(const char *section)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "%s: the section as it was loaded doesn't read the way its loader reads it", section);
    al_trace("Saving the %s.\n", buf);
    zqw_last_error = buf;
    return ZQWM_BAD_PART;
}

// ZQW_MEDIA_TRACE=file: what the writers do, appended to that file (allegro.log is shared by
// every ZQuest started in the same folder).
bool trace_on()
{
    const char *path = getenv("ZQW_MEDIA_TRACE");
    return path && *path;
}

void trace(const char *fmt, ...)
{
    if(!trace_on())
        return;

    FILE *f = fopen(getenv("ZQW_MEDIA_TRACE"), "a");

    if(!f)
        return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

// ---- the part as it was loaded, read through a window ----------------------------------
// A tile section can be hundreds of MB, too much to hold twice in a 32-bit ZQuest.

const size_t WINDOW = 1 << 20;

class Src
{
public:
    explicit Src(const ZqQuestPart &p) : pos(0), failed(false), part_(p), win_at_(0) {}

    long long size() const { return part_.length; }
    long long left() const { return pos <= part_.length ? part_.length - pos : 0; }
    bool has(long long n) const { return !failed && n >= 0 && n <= left(); }

    // n bytes from 'at', without moving
    bool peek(long long at, void *dst, size_t n)
    {
        if(at < 0 || at + (long long)n > part_.length)
            return false;

        if(!n)
            return true;

        if(n > WINDOW)
            return zq_part_read(part_, at, dst, n);

        if(win_.empty() || at < win_at_ || at + (long long)n > win_at_ + (long long)win_.size())
        {
            long long rest = part_.length - at;
            size_t len = rest < (long long)WINDOW ? (size_t)rest : WINDOW;
            win_.resize(len);

            if(!zq_part_read(part_, at, &win_[0], len))
            {
                win_.clear();
                return false;
            }

            win_at_ = at;
        }

        memcpy(dst, &win_[(size_t)(at - win_at_)], n);
        return true;
    }

    void read(void *dst, size_t n)
    {
        if(!has((long long)n) || !peek(pos, dst, n))
        {
            failed = true;
            memset(dst, 0, n);
            return;
        }

        pos += (long long)n;
    }

    void skip(long long n)
    {
        if(!has(n))
            failed = true;
        else
            pos += n;
    }

    byte getc()
    {
        byte b;
        read(&b, 1);
        return b;
    }

    word igetw()
    {
        byte b[2];
        read(b, 2);
        return (word)(b[0] | (b[1] << 8));
    }

    dword igetl()
    {
        byte b[4];
        read(b, 4);
        return (dword)b[0] | ((dword)b[1] << 8) | ((dword)b[2] << 16) | ((dword)b[3] << 24);
    }

    word mgetw()
    {
        byte b[2];
        read(b, 2);
        return (word)((b[0] << 8) | b[1]);
    }

    dword mgetl()
    {
        byte b[4];
        read(b, 4);
        return ((dword)b[0] << 24) | ((dword)b[1] << 16) | ((dword)b[2] << 8) | (dword)b[3];
    }

    // n bytes of text, as the readers' getstr takes them
    std::string getstr(size_t n)
    {
        std::string out;

        if(!has((long long)n))
        {
            failed = true;
            return out;
        }

        if(n)
        {
            out.resize(n);
            read(&out[0], n);
        }

        return out;
    }

    // n bytes at 'at' compared with data
    bool equals(long long at, const void *data, size_t n)
    {
        const byte *d = (const byte *)data;
        byte buf[4096];

        while(n > 0)
        {
            size_t k = n < sizeof(buf) ? n : sizeof(buf);

            if(!peek(at, buf, k) || memcmp(buf, d, k) != 0)
                return false;

            at += (long long)k;
            d += k;
            n -= k;
        }

        return true;
    }

    long long pos;
    bool failed;

private:
    const ZqQuestPart &part_;
    std::vector<byte> win_;
    long long win_at_;
};

// ---- what a writer writes: to the file, or (with no file) only counted --------------------

class Out
{
public:
    explicit Out(PACKFILE *f) : n(0), ok(true), f_(f) {}

    void put(const void *p, size_t len)
    {
        n += (long long)len;

        if(!f_ || !ok || !len)
            return;

        if(buf_.size() + len > WINDOW)
            flush();

        if(len >= WINDOW)
        {
            if(pack_fwrite(p, (long)len, f_) != (long)len)
                ok = false;

            return;
        }

        const byte *b = (const byte *)p;
        buf_.insert(buf_.end(), b, b + len);
    }

    void putc(int c)
    {
        byte b = (byte)c;
        put(&b, 1);
    }

    void iputw(long v)
    {
        byte b[2] = { (byte)v, (byte)(v >> 8) };
        put(b, 2);
    }

    void iputl(dword v)
    {
        byte b[4] = { (byte)v, (byte)(v >> 8), (byte)(v >> 16), (byte)(v >> 24) };
        put(b, 4);
    }

    void mputw(long v)
    {
        byte b[2] = { (byte)(v >> 8), (byte)v };
        put(b, 2);
    }

    void mputl(dword v)
    {
        byte b[4] = { (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v };
        put(b, 4);
    }

    // n bytes of the part as it was loaded, from 'at'
    void copy(Src &s, long long at, long long len)
    {
        if(!f_)
        {
            n += len;
            return;
        }

        byte buf[65536];

        while(len > 0)
        {
            size_t k = len < (long long)sizeof(buf) ? (size_t)len : sizeof(buf);

            if(!s.peek(at, buf, k))
            {
                ok = false;
                n += len;
                return;
            }

            put(buf, k);
            at += (long long)k;
            len -= (long long)k;
        }
    }

    bool flush()
    {
        if(f_ && ok && !buf_.empty())
        {
            if(pack_fwrite(&buf_[0], (long)buf_.size(), f_) != (long)buf_.size())
                ok = false;
        }

        buf_.clear();
        return ok;
    }

    bool counting() const { return f_ == NULL; }

    long long n;
    bool ok;

private:
    PACKFILE *f_;
    std::vector<byte> buf_;
};

// ---- a section writer ---------------------------------------------------------------------
// body() writes everything after the size, reading the loaded part from s (at the start of
// the body). It runs twice: once to count the bytes for the size, then for real; the first
// run catches anything the version can't hold before a byte is written.

bool edit_test();

class Section
{
public:
    Section(const ZqPartContext &c, Family f)
        : ctx(c), v(c.version), cv(c.cversion), fam(f), zv(c.zelda_version), build(c.build),
          force(zqw_force_serialize && !edit_test()) {}
    virtual ~Section() {}

    virtual int body(Out &o, Src &s) = 0;

    int write(PACKFILE *f)
    {
        const ZqQuestPart &p = *ctx.part;

        if(p.length < 8)
            return ZQW_UNSUPPORTED;

        Src head(p);
        head.skip(4);
        dword orig_size = head.igetl();

        if(head.failed)
            return ZQWM_BAD_PART;

        Out count(NULL);
        Src s1(p);
        s1.skip(8);
        int rc = body(count, s1);

        if(rc)
            return rc;

        // A part is exactly what its reader read, so the walk must end where the part does.
        if(s1.failed || s1.left() != 0)
            return bad_part(name());

        if(count.n > 0x7FFFFFFFLL)
            return cant_hold(name(), "a section of more than 2 GB", -1);

        // the loaders' own size field when the length didn't change (src/qst.cpp's readers
        // don't look at it, and a few old files have one that is off)
        dword size = count.n == p.length - 8 ? orig_size : (dword)count.n;

        Out out(f);
        out.mputl(p.id);
        out.iputw(v);
        out.iputw(cv);
        out.iputl(size);
        Src s2(p);
        s2.skip(8);
        rc = body(out, s2);

        if(rc)
            return rc;

        if(!out.flush() || s2.failed || out.n != 12 + count.n)
            return ZQWM_WRITE;

        return ZQW_OK;
    }

    virtual const char *name() const = 0;

protected:
    const ZqPartContext &ctx;
    word v, cv;
    Family fam;
    word zv;
    byte build;
    bool force;
};

// ---- small helpers ------------------------------------------------------------------------

// q255_media.cpp's narrow_tile: past this engine's last tile, 2.55 tile numbers become 0.
long narrow_tile(dword id)
{
    return id >= (dword)NEWMAXTILES ? 0 : (long)id;
}

dword le32(const byte *b)
{
    return (dword)b[0] | ((dword)b[1] << 8) | ((dword)b[2] << 16) | ((dword)b[3] << 24);
}

// The C string in n bytes that may not end in a NUL.
std::string c_string(const char *p, size_t n)
{
    size_t len = 0;

    while(len < n && p[len])
        ++len;

    return std::string(p, len);
}

std::string default_weapon_name(int i)
{
    if(i < wLast)
        return old_weapon_string[i];

    char buf[16];
    sprintf(buf, "zz%03d", i);
    return buf;
}

std::string default_sfx_name(int i)
{
    if(i < Z35)
        return old_sfx_string[i-1];

    char buf[16];
    sprintf(buf, "s%03d", i);
    return buf;
}

// ============================================================================================
// MCLR: misc colours
// ============================================================================================

class ColorsWriter : public Section
{
public:
    ColorsWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "misc colours"; }
    int body(Out &o, Src &s);
};

// The 20 bytes in front of the tiles, in the order the readers take them.
void colors_lead(const zcolors &z, byte *b)
{
    b[0] = z.text;
    b[1] = z.caption;
    b[2] = z.overw_bg;
    b[3] = z.dngn_bg;
    b[4] = z.dngn_fg;
    b[5] = z.cave_fg;
    b[6] = z.bs_dk;
    b[7] = z.bs_goal;
    b[8] = z.compass_lt;
    b[9] = z.compass_dk;
    b[10] = z.subscr_bg;
    b[11] = z.triframe_color;
    b[12] = z.link_dot;
    b[13] = z.bmap_bg;
    b[14] = z.bmap_fg;
    b[15] = z.triforce_cset;
    b[16] = z.triframe_cset;
    b[17] = z.overworld_map_cset;
    b[18] = z.dungeon_map_cset;
    b[19] = z.blueframe_cset;
}

int ColorsWriter::body(Out &o, Src &s)
{
    // src/qst.cpp: 20 bytes, six 16-bit tiles, two bytes, and from v2 the message text colour.
    // 2.55 v3 adds the tiles again in 32 bits (the real ones); v4 drops the 16-bit ones.
    bool newer = fam == FAM_255;
    bool words = !newer || v < 4;
    bool longs = newer && v >= 3;
    bool has_msgtext = newer || v >= 2;

    byte lead[20], tail[3] = { 0, 0, 1 };
    word w[6] = { 0, 0, 0, 0, 0, 0 };
    dword l[6] = { 0, 0, 0, 0, 0, 0 };
    s.read(lead, 20);

    for(int i = 0; words && i < 6; ++i)
        w[i] = s.igetw();

    tail[0] = s.getc();
    tail[1] = s.getc();

    if(has_msgtext)
        tail[2] = s.getc();

    for(int i = 0; longs && i < 6; ++i)
        l[i] = s.igetl();

    if(s.failed)
        return bad_part(name());

    const zcolors &z = misc.colors;
    byte mlead[20];
    colors_lead(z, mlead);
    long mt[6] = { z.triforce_tile, z.triframe_tile, z.overworld_map_tile, z.dungeon_map_tile, z.blueframe_tile, z.HCpieces_tile };
    byte mtail[3] = { z.HCpieces_cset, z.subscr_shadow, z.msgtext };

    bool same = memcmp(lead, mlead, 20) == 0 && memcmp(tail, mtail, 3) == 0;

    for(int i = 0; i < 6; ++i)
        same = same && (longs ? narrow_tile(l[i]) : (long)w[i]) == mt[i];

    if(same && !force)
    {
        o.copy(s, 8, s.pos - 8);
        return 0;
    }

    if(!has_msgtext && mtail[2] != 1)
        return cant_hold(name(), "a message text colour (sections before v2 always use colour 1)", mtail[2]);

    for(int i = 0; i < 6; ++i)
        if(mt[i] < 0 || (!longs && mt[i] > 0xFFFF))
            return cant_hold(name(), "a map tile past 65535", mt[i]);

    o.put(mlead, 20);

    // v3's 16-bit tiles are leftovers: kept as they were while the real tile stays
    for(int i = 0; words && i < 6; ++i)
        o.iputw(longs ? ((!force && narrow_tile(l[i]) == mt[i]) ? w[i] : (word)mt[i]) : mt[i]);

    o.putc(mtail[0]);
    o.putc(mtail[1]);

    if(has_msgtext)
        o.putc(mtail[2]);

    for(int i = 0; longs && i < 6; ++i)
        o.iputl((!force && narrow_tile(l[i]) == mt[i]) ? l[i] : (dword)mt[i]);

    return 0;
}

int zqw_colors(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && (ctx.version < 2 || ctx.version > 4)))
        return ZQW_UNSUPPORTED;

    ColorsWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// ICON: game icons
// ============================================================================================

class IconsWriter : public Section
{
public:
    IconsWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "game icons"; }
    int body(Out &o, Src &s);
};

int IconsWriter::body(Out &o, Src &s)
{
    bool longs = fam == FAM_255 && v >= 10;
    dword orig[4];

    for(int i = 0; i < 4; ++i)
        orig[i] = longs ? s.igetl() : s.igetw();

    if(s.failed)
        return bad_part(name());

    bool same = true;

    for(int i = 0; i < 4; ++i)
        same = same && (longs ? narrow_tile(orig[i]) : (long)orig[i]) == (long)misc.icons[i];

    if(same && !force)
    {
        o.copy(s, 8, s.pos - 8);
        return 0;
    }

    for(int i = 0; i < 4; ++i)
    {
        long t = misc.icons[i];

        if(t < 0 || (!longs && t > 0xFFFF))
            return cant_hold(name(), "an icon tile past 65535", t);
    }

    for(int i = 0; i < 4; ++i)
    {
        long t = misc.icons[i];

        if(longs)
            o.iputl((!force && narrow_tile(orig[i]) == t) ? orig[i] : (dword)t);
        else
            o.iputw(t);
    }

    return 0;
}

int zqw_icons(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && (ctx.version < 1 || ctx.version > 10)))
        return ZQW_UNSUPPORTED;

    IconsWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// CSET: palettes, palette names and palette cycles
// ============================================================================================

class CsetWriter : public Section
{
public:
    CsetWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "palettes"; }
    int body(Out &o, Src &s);

private:
    int home(int j) const;
};

// Where CSet j of the file lives in colordata, or -1 for none.
int CsetWriter::home(int j) const
{
    if(fam == FAM_OLD)
    {
        // Before v4 the 30 sprite CSets came right after the levels; readcolordata moves them.
        if(v < 4 && j >= newpoSPRITE)
            return newerpoSPRITE + (j - newpoSPRITE);

        return j;
    }

    if(v < 5)
        return j;

    // v5's layout (q255_media.cpp's v5_cset_home): 17 CSets per level, 2.53 keeps 13
    if(j < pdFULL)
        return j;

    if(j >= V5_SPRITE_START)
        return newerpoSPRITE + (j - V5_SPRITE_START);

    int level = (j - pdFULL) / V5_LEVEL_CSETS;
    int cset = (j - pdFULL) % V5_LEVEL_CSETS;
    return cset >= pdLEVEL ? -1 : pdFULL + level * pdLEVEL + cset;
}

int CsetWriter::body(Out &o, Src &s)
{
    int total = newerpdTOTAL, names = MAXLEVELS;
    bool bits8 = false;

    if(fam == FAM_OLD)
    {
        total = v >= 4 ? newerpdTOTAL : newpdTOTAL;
        names = v < 3 ? OLDMAXLEVELS : 512;
    }
    else if(v >= 5)
    {
        total = V5_TOTAL_CSETS;
        bits8 = v >= 6;     // 8 bits per value; this engine holds 6 (the reader drops the low two)
    }

    // Before v4 there is no room between the levels and the sprite CSets.
    if(fam == FAM_OLD && v < 4)
        for(int i = newpoSPRITE * CSET_BYTES; i < newerpoSPRITE * CSET_BYTES; ++i)
            if(colordata[i])
                return cant_hold(name(), "a CSet between the level and sprite CSets", i / CSET_BYTES);

    for(int n = names; n < MAXLEVELS; ++n)
        if(palnames[n][0])
            return cant_hold(name(), "a palette name past the 256th", n);

    byte orig[CSET_BYTES], enc[CSET_BYTES];

    for(int j = 0; j < total; ++j)
    {
        s.read(orig, CSET_BYTES);

        if(s.failed)
            return bad_part(name());

        int h = home(j);

        if(h < 0)       // 2.55's four extra CSets per level: nowhere in 2.53, kept as they were
        {
            o.put(orig, CSET_BYTES);
            continue;
        }

        const byte *m = colordata + h * CSET_BYTES;

        if(!bits8)
        {
            o.put(m, CSET_BYTES);
            continue;
        }

        for(int k = 0; k < CSET_BYTES; ++k)
        {
            if(!force && (orig[k] >> 2) == m[k])
                enc[k] = orig[k];
            else if(m[k] > 63)
                return cant_hold(name(), "a colour value past 63", h);
            else
                enc[k] = (byte)((m[k] << 2) | (m[k] >> 4));     // Allegro's _rgb_scale_6
        }

        o.put(enc, CSET_BYTES);
    }

    for(int n = 0; n < names; ++n)
    {
        s.skip(PALNAMESIZE);
        o.put(palnames[n], PALNAMESIZE);
    }

    word cycles = s.igetw();

    if(s.failed || cycles > 256)
        return bad_part(name());

    s.skip(9LL * cycles);

    int needed = 0;

    for(int i = 0; i < 256; ++i)
        for(int k = 0; k < 3; ++k)
            if(misc.cycles[i][k].first || misc.cycles[i][k].count || misc.cycles[i][k].speed)
                needed = i + 1;

    int count = needed > cycles ? needed : cycles;
    o.iputw(count);

    for(int i = 0; i < count; ++i)
    {
        for(int k = 0; k < 3; ++k)
            o.putc(misc.cycles[i][k].first);

        for(int k = 0; k < 3; ++k)
            o.putc(misc.cycles[i][k].count);

        for(int k = 0; k < 3; ++k)
            o.putc(misc.cycles[i][k].speed);
    }

    return 0;
}

int zqw_csets(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && (ctx.version < 4 || ctx.version > 6)))
        return ZQW_UNSUPPORTED;

    CsetWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// TILE: tiles
// ============================================================================================

class TileWriter : public Section
{
public:
    TileWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "tiles"; }
    int body(Out &o, Src &s);

private:
    int encode(Out &o, long i, bool has_format, bool blank_opt, bool swap);
};

// What readtiles and q255_read_tiles leave in a tile the file doesn't fill.
bool is_reset_tile(const tiledata &t)
{
    if(t.format != tf4Bit)
        return false;

    if(!t.data)
        return true;

    for(int i = 0; i < 128; ++i)
        if(t.data[i])
            return false;

    return true;
}

int TileWriter::encode(Out &o, long i, bool has_format, bool blank_opt, bool swap)
{
    const tiledata &t = newtilebuf[i];
    byte format = t.format;

    if(fam == FAM_255)
    {
        if(format == tfInvalid)
            return cant_hold(name(), "a tile with no format", i);

        if(blank_opt && is_reset_tile(t))
        {
            o.putc(tfInvalid);      // from v3, a blank tile is its format byte alone
            return 0;
        }
    }
    else if(!has_format && format != tf4Bit)
        return cant_hold(name(), "a tile that isn't 4-bit (this version's tiles are all 4-bit)", i);

    if(has_format)
        o.putc(format);

    size_t len = has_format ? (size_t)tilesize(format) : 128;
    byte buf[1024];

    if(t.data)
        memcpy(buf, t.data, len);
    else
        memset(buf, 0, len);

    // 2.10 and early 2.11 quests have the Goriya tiles 130-133 the other way round:
    // readtiles swaps their first 128 bytes, so they are swapped back here.
    if(swap && i >= 130 && i <= 133)
    {
        const tiledata &partner = newtilebuf[i < 132 ? i + 2 : i - 2];

        if(partner.data)
            memcpy(buf, partner.data, 128);
        else
            memset(buf, 0, 128);
    }

    o.put(buf, len);
    return 0;
}

int TileWriter::body(Out &o, Src &s)
{
    bool newer = fam == FAM_255;
    bool wide = newer && build >= 41;   // q255_read_tiles: the build alone decides
    dword field = wide ? s.igetl() : s.igetw();

    if(s.failed)
        return bad_part(name());

    long orig_count = wide ? (long)field : (long)(word)field;

    if(orig_count < 0)
        orig_count = 0;

    if(newer && orig_count > MAX_TILES_255)
        return bad_part(name());

    bool has_format = newer || zv > 0x211 || (zv == 0x211 && build > 4);
    bool blank_opt = newer && v > 2;
    bool swap = !newer && (zv < 0x211 || (zv == 0x211 && build < 7)) && !get_bit(quest_rules, qr_NEWENEMYTILES);

    long needed = 0;

    for(long t = NEWMAXTILES; t > 0; --t)
        if(!is_reset_tile(newtilebuf[t-1]))
        {
            needed = t;
            break;
        }

    long limit = wide ? MAX_TILES_255 : 65535;

    if(needed > limit)
        return cant_hold(name(), "a tile past the last one this version's file can count", needed - 1);

    long count = needed > orig_count ? needed : orig_count;

    if(count == orig_count)
        wide ? o.iputl(field) : o.iputw(field);
    else
        wide ? o.iputl((dword)count) : o.iputw(count);

    long copied = 0;

    for(long i = 0; i < count; ++i)
    {
        if(i < orig_count)
        {
            long long at = s.pos;
            byte format = has_format ? s.getc() : (byte)tf4Bit;
            size_t len = (blank_opt && format == tfInvalid) ? 0 : (size_t)tilesize(format);
            s.skip((long long)len);

            if(s.failed)
                return bad_part(name());

            // 2.55's loader blanks a tile of format 0 and skips its bytes, so a record is
            // compared by what it turned into; the older ones keep every byte they read.
            if(newer && !force)
            {
                const tiledata &t = newtilebuf[i];
                bool same = format == tfInvalid ? is_reset_tile(t)
                            : (t.format == format && t.data && s.equals(at + 1, t.data, len));

                if(same)
                {
                    o.copy(s, at, s.pos - at);
                    ++copied;
                    continue;
                }
            }
        }

        int rc = encode(o, i, has_format, blank_opt, swap);

        if(rc)
            return rc;
    }

    if(trace_on() && !o.counting())
        trace("zqw tiles: %ld in the file, %ld written, %ld copied\n", orig_count, count, copied);

    return 0;
}

int zqw_tiles(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && (ctx.version < 1 || ctx.version > 4)))
        return ZQW_UNSUPPORTED;

    TileWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// WPN: weapon sprites
// ============================================================================================

struct Sprite
{
    long tile;
    byte misc, csets, frames, speed, type;
    word script;
};

Sprite sprite_of(const wpndata &w)
{
    Sprite s;
    s.tile = w.tile;
    s.misc = w.misc;
    s.csets = w.csets;
    s.frames = w.frames;
    s.speed = w.speed;
    s.type = w.type;
    s.script = w.script;
    return s;
}

// readweapons doesn't set the script (2.53 has none: it keeps whatever was on the stack)
bool same_sprite(const Sprite &a, const Sprite &b, bool with_script)
{
    return a.tile == b.tile && a.misc == b.misc && a.csets == b.csets && a.frames == b.frames && a.speed == b.speed &&
           a.type == b.type && (!with_script || a.script == b.script);
}

// readweapons leaves the sprites past the file's count as ZQuest held them before the load
// (2.10 quests hold 68 or 69), so only a copy taken after the load can tell whether one was
// changed since: the file has no room for such a change.
std::vector<Sprite> g_sprites_loaded;
std::vector<std::string> g_sprite_names_loaded;

void note_sprites()
{
    g_sprites_loaded.resize(MAXWPNS);
    g_sprite_names_loaded.resize(WPNCNT);

    for(int i = 0; i < MAXWPNS; ++i)
        g_sprites_loaded[i] = sprite_of(wpnsbuf[i]);

    for(int i = 0; i < WPNCNT; ++i)
        g_sprite_names_loaded[i] = c_string(weapon_string[i], 64);
}

ZqLoadHookRegistration hook_sprites(note_sprites);

class WeaponsWriter : public Section
{
public:
    WeaponsWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "weapon sprites"; }
    int body(Out &o, Src &s) { return fam == FAM_255 ? body_255(o, s) : body_old(o, s); }

private:
    int body_old(Out &o, Src &s);
    int body_255(Out &o, Src &s);
};

int WeaponsWriter::body_old(Out &o, Src &s)
{
    // readweapons: the count, from v3 64-byte names, then 7 bytes per sprite
    word count = s.igetw();

    if(s.failed || count > MAXWPNS)
        return bad_part(name());

    bool names_in_file = v > 2;
    long long names_at = s.pos;

    if(names_in_file)
        s.skip(64LL * count);

    long long recs_at = s.pos;
    s.skip(7LL * count);

    if(s.failed)
        return bad_part(name());

    if(trace_on() && count < WPNCNT && !o.counting())
        trace("zqw weapons: only %d sprites in the file\n", (int)count);

    for(int i = count; i < MAXWPNS && !g_sprites_loaded.empty(); ++i)
        if(!same_sprite(g_sprites_loaded[i], sprite_of(wpnsbuf[i]), false) ||
           (names_in_file && i < WPNCNT && g_sprite_names_loaded[i] != c_string(weapon_string[i], 64)))
            return cant_hold(name(), "a sprite past the last one this version's file holds", i);

    // what readweapons made of the file, its fix-ups included
    std::vector<Sprite> want(MAXWPNS);

    for(int i = 0; i < MAXWPNS; ++i)
    {
        byte r[7];

        if(i >= count)
        {
            want[i] = sprite_of(wpnsbuf[i]);    // not in the file: what ZQuest held before
            continue;
        }

        if(!s.peek(recs_at + 7LL * i, r, 7))
            return bad_part(name());

        Sprite &w = want[i];
        w.tile = r[0] | (r[1] << 8);
        w.misc = r[2];
        w.csets = r[3];
        w.frames = r[4];
        w.speed = r[5];
        w.type = r[6];
        w.script = 0;

        if(v < 6)
            w.misc = (byte)(i == ewFIRETRAIL ? (w.misc | WF_BEHIND) : (w.misc & ~WF_BEHIND));
    }

    // a sprite whose record is ignored: it copies another
    int copies_of[MAXWPNS];

    for(int i = 0; i < MAXWPNS; ++i)
        copies_of[i] = -1;

    if(v < 2)
        copies_of[wSBOOM] = wBOOM;

    if(zv < 0x210)
    {
        copies_of[wLSHEAD] = wHSHEAD;
        copies_of[wLSCHAIN_H] = wHSCHAIN_H;
        copies_of[wLSHANDLE] = wHSHANDLE;
        copies_of[wLSCHAIN_V] = wHSCHAIN_V;
    }

    for(int i = 0; i < MAXWPNS; ++i)
        if(copies_of[i] >= 0)
            want[i] = want[copies_of[i]];

    if(v < 5)
    {
        want[iwQuarterHearts].tile = 1;
        want[iwQuarterHearts].csets = 1;
    }

    // names: none before v3; v3 and v4 reset a few after reading them
    std::vector<int> fixed_name(WPNCNT, 0);

    if(v < 4)
        fixed_name[iwHover] = fixed_name[wFIREMAGIC] = 1;

    if(v < 5)
        fixed_name[iwQuarterHearts] = 1;

    for(int i = 0; i < WPNCNT; ++i)
        if((!names_in_file || fixed_name[i]) && default_weapon_name(i) != weapon_string[i])
            return cant_hold(name(), names_in_file ? "the name of a sprite this version names itself" : "a sprite name (this version has none)", i);

    o.iputw(count);

    for(int i = 0; names_in_file && i < count; ++i)
    {
        char orig[64];

        if(!s.peek(names_at + 64LL * i, orig, 64))
            return bad_part(name());

        if(fixed_name[i] || (!force && c_string(orig, 64) == c_string(weapon_string[i], 64)))
            o.put(orig, 64);
        else
            o.put(weapon_string[i], 64);
    }

    for(int i = 0; i < count; ++i)
    {
        byte r[7];

        if(!s.peek(recs_at + 7LL * i, r, 7))
            return bad_part(name());

        Sprite m = sprite_of(wpnsbuf[i]);

        if(!force && same_sprite(want[i], m, false))
        {
            o.put(r, 7);
            continue;
        }

        if(copies_of[i] >= 0)
        {
            if(!same_sprite(m, sprite_of(wpnsbuf[copies_of[i]]), false))
                return cant_hold(name(), "a sprite this version copies from another one", i);

            o.put(r, 7);
            continue;
        }

        word tile = (word)m.tile;
        byte csets = m.csets;

        if(v < 5 && i == iwQuarterHearts)
        {
            if(m.tile != 1 || m.csets != 1)
                return cant_hold(name(), "the quarter hearts sprite's tile or CSet (this version sets them itself)", i);

            tile = (word)(r[0] | (r[1] << 8));
            csets = r[3];
        }
        else if(m.tile < 0 || m.tile > 0xFFFF)
            return cant_hold(name(), "a sprite tile past 65535", m.tile);

        byte misc_byte = m.misc;

        if(v < 6)
        {
            if((m.misc & WF_BEHIND) != (i == ewFIRETRAIL ? WF_BEHIND : 0))
                return cant_hold(name(), "a sprite's 'behind' setting (this version decides it itself)", i);

            misc_byte = (byte)((m.misc & ~WF_BEHIND) | (r[2] & WF_BEHIND));
        }

        o.iputw(tile);
        o.putc(misc_byte);
        o.putc(csets);
        o.putc(m.frames);
        o.putc(m.speed);
        o.putc(m.type);
    }

    return 0;
}

int WeaponsWriter::body_255(Out &o, Src &s)
{
    // q255_read_weapons, v6-9
    word count = s.igetw();

    if(s.failed || count > MAX_SPRITES_255)
        return bad_part(name());

    long long names_at = s.pos;

    if(v < 9)
        s.skip(64LL * count);

    // each record as it is in the file, and what the reader made of it
    std::vector<long long> rec_at(count), rec_len(count);
    std::vector<long long> name_at(count, -1);
    std::vector<Sprite> want(count);
    std::vector<word> old_tile(count, 0);
    std::vector<dword> long_tile(count, 0);
    std::vector<std::string> want_name(count);

    for(int i = 0; i < count; ++i)
    {
        rec_at[i] = s.pos;
        Sprite &w = want[i];
        memset(&w, 0, sizeof(w));

        if(v >= 9)
        {
            name_at[i] = s.pos;
            byte n = s.getc();
            std::string nm = s.getstr(n);
            want_name[i] = c_string(nm.c_str(), 63);    // store_name keeps 63 characters
        }
        else
        {
            char nm[64];

            if(!s.peek(names_at + 64LL * i, nm, 64))
                return bad_part(name());

            nm[63] = 0;
            want_name[i] = std::string(nm, 64);         // all 64 bytes, the 64th a NUL
        }

        if(v < 8)
        {
            old_tile[i] = s.igetw();

            if(v < 7)
                w.tile = old_tile[i];
        }

        w.misc = s.getc();
        w.csets = s.getc();
        w.frames = s.getc();
        w.speed = s.getc();
        w.type = s.getc();

        if(v >= 7)
        {
            w.script = s.igetw();
            long_tile[i] = s.igetl();
            w.tile = narrow_tile(long_tile[i]);
        }

        if(s.failed)
            return bad_part(name());

        rec_len[i] = s.pos - rec_at[i];
    }

    // The reader empties the sprites past the count; one ZQuest filled since goes in too.
    int needed = 0;

    for(int i = 0; i < MAXWPNS; ++i)
    {
        const wpndata &m = wpnsbuf[i];

        if(m.tile || m.misc || m.csets || m.frames || m.speed || m.type || m.script ||
           (i < WPNCNT && default_weapon_name(i) != weapon_string[i]))
            needed = i + 1;
    }

    int total = needed > count ? needed : count;

    if(v < 7)
        for(int i = 0; i < MAXWPNS && i < total; ++i)
            if(wpnsbuf[i].script)
                return cant_hold(name(), "a sprite script (sections before v7 have none)", i);

    for(int i = 0; i < MAXWPNS && i < total; ++i)
        if(wpnsbuf[i].tile < 0 || (v < 7 && wpnsbuf[i].tile > 0xFFFF))
            return cant_hold(name(), "a sprite tile past 65535", wpnsbuf[i].tile);

    o.iputw(total);

    // v6-8: the names first
    for(int i = 0; v < 9 && i < total; ++i)
    {
        if(i < count && (i >= WPNCNT || (!force && memcmp(want_name[i].data(), weapon_string[i], 64) == 0)))
        {
            o.copy(s, names_at + 64LL * i, 64);
            continue;
        }

        char nm[64];
        memcpy(nm, weapon_string[i], 64);
        nm[63] = 0;
        o.put(nm, 64);
    }

    for(int i = 0; i < total; ++i)
    {
        bool have = i < count;

        if(have && i >= MAXWPNS)        // past this engine's last sprite: kept as it was
        {
            o.copy(s, rec_at[i], rec_len[i]);
            continue;
        }

        Sprite m = sprite_of(wpnsbuf[i]);
        bool name_same = have && (v < 9 ? true : (i >= WPNCNT || want_name[i] == c_string(weapon_string[i], 64)));

        if(have && !force && name_same && same_sprite(want[i], m, true))
        {
            o.copy(s, rec_at[i], rec_len[i]);
            continue;
        }

        if(v >= 9)
        {
            if(have && !force && name_same)
            {
                byte n = 0;
                s.peek(name_at[i], &n, 1);
                o.copy(s, name_at[i], 1 + n);
            }
            else
            {
                std::string nm = c_string(weapon_string[i], 63);
                o.putc((int)nm.size());
                o.put(nm.data(), nm.size());
            }
        }

        if(v < 8)
            o.iputw(v < 7 ? m.tile : (have ? old_tile[i] : (word)m.tile));  // v7's is a leftover

        o.putc(m.misc);
        o.putc(m.csets);
        o.putc(m.frames);
        o.putc(m.speed);
        o.putc(m.type);

        if(v >= 7)
        {
            o.iputw(m.script);
            o.iputl((have && !force && narrow_tile(long_tile[i]) == m.tile) ? long_tile[i] : (dword)m.tile);
        }
    }

    return 0;
}

int zqw_weapons(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && (ctx.version < 6 || ctx.version > 9)))
        return ZQW_UNSUPPORTED;

    WeaponsWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// SFX: sound effects
// ============================================================================================

size_t sample_bytes(const SAMPLE &s)
{
    return (size_t)(s.bits == 8 ? 1 : 2) * (s.stereo == 0 ? 1 : 2) * s.len;
}

// An empty slot, as q255_media.cpp's clear_sfx_slot leaves it.
bool is_cleared_sample(const SAMPLE &s)
{
    return !s.data && s.bits == 8 && s.stereo == 0 && s.freq == 22050 && s.priority == 128 && s.len == 0 &&
           s.loop_start == 0 && s.loop_end == 0 && s.param == 0;
}

bool same_sample_fields(const SAMPLE &a, const SAMPLE &b)
{
    return a.bits == b.bits && a.stereo == b.stereo && a.freq == b.freq && a.priority == b.priority && a.len == b.len &&
           a.loop_start == b.loop_start && a.loop_end == b.loop_end && a.param == b.param;
}

bool same_sample(const SAMPLE &a, const SAMPLE &b)
{
    if(!same_sample_fields(a, b))
        return false;

    size_t n = sample_bytes(a);

    if(!n)
        return true;

    if(!a.data || !b.data)
        return a.data == b.data;

    return memcmp(a.data, b.data, n) == 0;
}

// The eight values and the data of a 2.53-format sample: the data in words, so an odd
// last byte is not in the file (the readers leave it 0).
void put_sample(Out &o, const SAMPLE &smp)
{
    o.iputl((dword)smp.bits);
    o.iputl((dword)smp.stereo);
    o.iputl((dword)smp.freq);
    o.iputl((dword)smp.priority);
    o.iputl(smp.len);
    o.iputl(smp.loop_start);
    o.iputl(smp.loop_end);
    o.iputl(smp.param);
    size_t words = sample_bytes(smp) / 2;

    if(smp.data)
        o.put(smp.data, words * 2);
    else
    {
        static const byte zeros[4096] = { 0 };

        for(size_t left = words * 2; left > 0;)
        {
            size_t k = left < sizeof(zeros) ? left : sizeof(zeros);
            o.put(zeros, k);
            left -= k;
        }
    }
}

// Walks a 2.53-format sample record: the eight values, then the data in words.
// v<3 sizes stereo data as mono (readsfx); v<2 holds bytes rather than words.
bool skip_sample(Src &s, word v)
{
    long bits = (long)s.igetl();
    long stereo = (long)s.igetl();
    s.skip(8);
    dword len = s.igetl();
    s.skip(12);

    if(s.failed)
        return false;

    long long bytes = (long long)(bits == 8 ? 1 : 2) * (v < 3 || stereo == 0 ? 1 : 2) * (long long)len;
    s.skip(v < 2 ? bytes : (bytes / 2) * 2);
    return !s.failed;
}

bool sfx_flag(const byte *flags, int i)
{
    return (flags[(i - 1) >> 3] >> ((i - 1) & 7)) & 1;
}

class SfxWriter : public Section
{
public:
    SfxWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "sound effects"; }
    int body(Out &o, Src &s)
    {
        if(fam == FAM_OLD)
            return body_old(o, s);

        return v >= 9 ? body_v9(o, s) : body_255(o, s);
    }

private:
    int body_old(Out &o, Src &s);
    int body_255(Out &o, Src &s);
    int body_v9(Out &o, Src &s);
};

int SfxWriter::body_old(Out &o, Src &s)
{
    // readsfx, v4 on: the flags (16 bytes before v6), from v5 a 36-byte name for each
    // flagged sound, then each flagged sound's sample.
    int wavcount = v < 6 ? 128 : WAV_COUNT;
    int flag_bytes = wavcount >> 3;
    byte oflags[WAV_COUNT>>3];
    memset(oflags, 0, sizeof(oflags));
    s.read(oflags, flag_bytes);
    std::vector<long long> name_at(WAV_COUNT, -1);

    for(int i = 1; v > 4 && i < wavcount; ++i)
        if(sfx_flag(oflags, i))
        {
            name_at[i] = s.pos;
            s.skip(36);
        }

    for(int i = 1; i < wavcount; ++i)
        if(sfx_flag(oflags, i) && !skip_sample(s, v))
            return bad_part(name());

    if(s.failed)
        return bad_part(name());

    for(int b = flag_bytes; b < (WAV_COUNT>>3); ++b)
        if(customsfxflag[b])
            return cant_hold(name(), "a sound past the 127th", b * 8 + 1);

    for(int i = 1; v <= 4 && i < wavcount; ++i)
        if(sfx_flag(customsfxflag, i) && default_sfx_name(i) != c_string(sfx_string[i], 36))
            return cant_hold(name(), "a sound name (this version has none)", i);

    o.put(customsfxflag, flag_bytes);

    for(int i = 1; v > 4 && i < wavcount; ++i)
    {
        if(!sfx_flag(customsfxflag, i))
            continue;

        char orig[36];

        if(!force && name_at[i] >= 0 && s.peek(name_at[i], orig, 36) && c_string(orig, 36) == c_string(sfx_string[i], 36))
            o.put(orig, 36);
        else
            o.put(sfx_string[i], 36);
    }

    // Every value of a sample is in the file as it is held, so it is written from ZQuest's data.
    for(int i = 1; i < wavcount; ++i)
        if(sfx_flag(customsfxflag, i))
            put_sample(o, customsfxdata[i]);

    return 0;
}

int SfxWriter::body_255(Out &o, Src &s)
{
    // q255_read_sfx, v6-8: as 2.53's, except that a sound with no sample gets the original
    // sound back (with its flag set) in the first slots and stays empty in the others.
    byte oflags[WAV_COUNT>>3];
    s.read(oflags, sizeof(oflags));
    std::vector<long long> name_at(WAV_COUNT, -1), smp_at(WAV_COUNT, -1), smp_len(WAV_COUNT, 0);

    for(int i = 1; i < WAV_COUNT; ++i)
        if(sfx_flag(oflags, i))
        {
            name_at[i] = s.pos;
            s.skip(36);
        }

    for(int i = 1; i < WAV_COUNT; ++i)
        if(sfx_flag(oflags, i))
        {
            smp_at[i] = s.pos;

            if(!skip_sample(s, v))
                return bad_part(name());

            smp_len[i] = s.pos - smp_at[i];
        }

    if(s.failed)
        return bad_part(name());

    // which slots still hold what the loader made of the file
    std::vector<int> same(WAV_COUNT, 0);

    for(int i = 1; i < WAV_COUNT && !force; ++i)
    {
        const SAMPLE &m = customsfxdata[i];
        bool flag = sfx_flag(customsfxflag, i);

        if(sfx_flag(oflags, i))
        {
            char nm[36];
            byte vals[32];

            if(!flag || !s.peek(name_at[i], nm, 36) || memcmp(nm, sfx_string[i], 35) != 0 || sfx_string[i][35] ||
               !s.peek(smp_at[i], vals, 32))
                continue;

            SAMPLE f;
            memset(&f, 0, sizeof(f));
            f.bits = (int)le32(vals);
            f.stereo = (int)le32(vals + 4);
            f.freq = (int)le32(vals + 8);
            f.priority = (int)le32(vals + 12);
            f.len = le32(vals + 16);
            f.loop_start = le32(vals + 20);
            f.loop_end = le32(vals + 24);
            f.param = le32(vals + 28);

            if(!same_sample_fields(f, m))
                continue;

            size_t bytes = sample_bytes(m);
            size_t even = (bytes / 2) * 2;

            if(bytes && !m.data)
                continue;

            if(even && !s.equals(smp_at[i] + 32, m.data, even))
                continue;

            if(bytes != even && ((const byte *)m.data)[even] != 0)
                continue;

            same[i] = 1;
        }
        else if(i < Z35)
        {
            const SAMPLE *def = (const SAMPLE *)sfxdata[i].dat;
            same[i] = flag && default_sfx_name(i) == c_string(sfx_string[i], 36) && same_sample(*def, m);
        }
        else
            same[i] = !flag && default_sfx_name(i) == c_string(sfx_string[i], 36) && is_cleared_sample(m);
    }

    // the flags: as the file had them where nothing changed. An original sound with its own
    // name goes in as no sound at all: the loader puts it back whole, where a sample of its
    // own would lose an odd last byte.
    byte flags[WAV_COUNT>>3];
    memcpy(flags, customsfxflag, sizeof(flags));

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(same[i])
            set_bit(flags, i - 1, sfx_flag(oflags, i));
        else if(i < Z35 && sfx_flag(customsfxflag, i) && default_sfx_name(i) == c_string(sfx_string[i], 36) &&
                same_sample(*(const SAMPLE *)sfxdata[i].dat, customsfxdata[i]))
            set_bit(flags, i - 1, 0);
    }

    o.put(flags, sizeof(flags));

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(!sfx_flag(flags, i))
            continue;

        if(same[i])
            o.copy(s, name_at[i], 36);
        else
            o.put(sfx_string[i], 36);
    }

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(!sfx_flag(flags, i))
            continue;

        if(same[i])
            o.copy(s, smp_at[i], smp_len[i]);
        else
            put_sample(o, customsfxdata[i]);
    }

    return 0;
}

// ---- SFX v9: Allegro 5 samples and OGG files ----------------------------------------------

// al_get_audio_depth_size, as q255_media.cpp's a5_depth_bytes
int a5_depth_bytes(byte depth)
{
    switch(depth & ~8)
    {
    case 0:
        return 1;

    case 1:
        return 2;

    case 2:
    case 3:
        return 4;
    }

    return 0;
}

struct SfxEntry
{
    long long at, len;          // the whole entry
    long long payload_at;       // after the name
    byte type;
    std::string name;
    byte depth, conf;
    dword freq, slen;           // WAV: frequency and length in frames; OGG: as the file has them
    long long data_at;
    dword data_len;             // OGG: the buffer's size
};

// What q255_media.cpp's store_a5_sample makes of a WAV entry, compared with a sound slot.
bool a5_matches(Src &s, const SfxEntry &e, const SAMPLE &m)
{
    int channels = (e.conf >> 4) + (e.conf & 0xF);
    int base = e.depth & ~8;
    bool is_unsigned = (e.depth & 8) != 0;
    int in_size = a5_depth_bytes(e.depth);
    int out_size = base == 0 ? 1 : 2;
    int out_channels = channels > 2 ? 2 : channels;

    if(m.bits != out_size * 8 || m.stereo != (out_channels == 2 ? 1 : 0) || m.freq != (int)e.freq || m.priority != 128 ||
       m.len != e.slen || m.loop_start != 0 || m.loop_end != e.slen || m.param != 0 || !m.data)
        return false;

    size_t frame = (size_t)channels * in_size;
    std::vector<byte> in;
    const byte *have = (const byte *)m.data;
    const dword chunk = 4096;

    for(dword first = 0; first < e.slen; first += chunk)
    {
        dword frames = e.slen - first < chunk ? e.slen - first : chunk;
        in.resize((size_t)frames * frame);

        if(!s.peek(e.data_at + (long long)first * frame, &in[0], in.size()))
            return false;

        for(dword i = 0; i < frames; ++i)
        {
            for(int c = 0; c < out_channels; ++c)
            {
                const byte *p = &in[((size_t)i * channels + c) * in_size];
                size_t at = ((size_t)(first + i) * out_channels + c);

                if(base == 0)
                {
                    if(have[at] != (is_unsigned ? p[0] : (byte)(p[0] ^ 0x80)))
                        return false;

                    continue;
                }

                word value;

                if(base == 1)
                    value = (word)(p[0] | (p[1] << 8));
                else if(base == 2)
                    value = (word)(p[1] | (p[2] << 8));
                else
                {
                    float f;
                    memcpy(&f, p, 4);

                    if(f < -1.0f)
                        f = -1.0f;
                    else if(f > 1.0f)
                        f = 1.0f;

                    value = (word)(short)(f * 32767.0f);
                }

                if(!is_unsigned || base == 3)
                    value ^= 0x8000;

                if(have[at * 2] != (byte)value || have[at * 2 + 1] != (byte)(value >> 8))
                    return false;
            }
        }
    }

    return true;
}

// The sound slot behind a v9 id (q30_sfx_slot), or NULL.
const SAMPLE *v9_slot(int id)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return NULL;

    return id < WAV_COUNT ? &customsfxdata[id] : &zq255.high_sfx[id - WAV_COUNT];
}

const std::vector<byte> *v9_ogg(int id)
{
    std::map<int, std::vector<byte> >::const_iterator it = zq255.ogg_sfx.find(id);
    return it == zq255.ogg_sfx.end() ? NULL : &it->second;
}

// What ZQuest holds for a v9 id: a sample (a flagged slot, or any filled one past 255),
// an OGG file, or nothing.
byte v9_kind(int id)
{
    const SAMPLE *slot = v9_slot(id);

    if(slot && (id < WAV_COUNT ? sfx_flag(customsfxflag, id) : slot->data != NULL))
        return SAMPLE_WAV;

    if(v9_ogg(id))
        return SAMPLE_OGG;

    return SAMPLE_NONE;
}

bool v9_name_unchanged(const SfxEntry &e, int id)
{
    if(id >= WAV_COUNT)
        return true;    // this engine keeps no names past 255

    // store_name: what's before the first NUL, 35 characters at most, the rest NULs
    std::string want = c_string(e.name.c_str(), 35);

    if(want.empty())
        return default_sfx_name(id) == c_string(sfx_string[id], 36);

    char buf[36];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, want.data(), want.size());
    return memcmp(buf, sfx_string[id], 36) == 0;
}

int SfxWriter::body_v9(Out &o, Src &s)
{
    word count = s.igetw();

    if(s.failed)
        return bad_part(name());

    std::vector<SfxEntry> entries(count);

    for(int q = 0; q < count; ++q)
    {
        SfxEntry &e = entries[q];
        e.at = s.pos;
        e.type = s.getc();
        byte n = s.getc();
        e.name = s.getstr(n);
        e.payload_at = s.pos;
        e.depth = e.conf = 0;
        e.freq = e.slen = e.data_len = 0;
        e.data_at = 0;

        if(e.type == SAMPLE_WAV)
        {
            e.depth = s.getc();
            e.conf = s.getc();
            e.freq = s.igetl();
            e.slen = s.igetl();
            int channels = (e.conf >> 4) + (e.conf & 0xF);
            long long frame = (long long)channels * a5_depth_bytes(e.depth);

            if(s.failed || !frame || (long long)e.slen > s.left() / frame)
                return bad_part(name());

            e.data_at = s.pos;
            s.skip(frame * e.slen);
        }
        else if(e.type == SAMPLE_OGG)
        {
            e.data_len = s.igetl();
            e.freq = s.igetl();
            e.slen = s.igetl();
            e.data_at = s.pos;
            s.skip(e.data_len);
        }

        if(s.failed)
            return bad_part(name());

        e.len = s.pos - e.at;
    }

    // what each id holds now, and whether that is what the loader made of the file
    int needed = 0;

    for(int id = 1; id < Q30_MAX_SFX; ++id)
    {
        const SAMPLE *slot = v9_slot(id);
        bool filled = v9_kind(id) != SAMPLE_NONE || !is_cleared_sample(*slot) ||
                      (id < WAV_COUNT && (sfx_flag(customsfxflag, id) || default_sfx_name(id) != c_string(sfx_string[id], 36)));

        if(filled)
            needed = id;
    }

    int total = needed > count ? needed : count;

    if(total > 0xFFFF)
        return cant_hold(name(), "more sounds than the count can say", total);

    o.iputw(total);

    for(int q = 0; q < total; ++q)
    {
        int id = q + 1;
        bool have = q < count;

        if(have && id >= Q30_MAX_SFX)       // past the last slot anything keeps: as it was
        {
            o.copy(s, entries[q].at, entries[q].len);
            continue;
        }

        const SAMPLE &slot = *v9_slot(id);
        byte kind = v9_kind(id);
        const std::vector<byte> *ogg = v9_ogg(id);
        bool name_same = have && v9_name_unchanged(entries[q], id);

        // the slot as the loader left it from this entry
        bool payload_same = false;

        if(have && !force)
        {
            const SfxEntry &e = entries[q];
            bool flag_ok = id >= WAV_COUNT || sfx_flag(customsfxflag, id) == (e.type == SAMPLE_WAV && e.slen > 0);

            if(e.type == SAMPLE_WAV && e.slen > 0)
                payload_same = flag_ok && kind == SAMPLE_WAV && !ogg && a5_matches(s, e, slot);
            else if(e.type == SAMPLE_OGG)
                payload_same = flag_ok && is_cleared_sample(slot) && ogg && ogg->size() == e.data_len &&
                               (e.data_len == 0 || s.equals(e.data_at, &(*ogg)[0], e.data_len));
            else
                payload_same = flag_ok && is_cleared_sample(slot) && !ogg;
        }

        if(payload_same && name_same)
        {
            o.copy(s, entries[q].at, entries[q].len);
            continue;
        }

        if(kind == SAMPLE_WAV && !payload_same)
        {
            // v9 keeps no priority, loop points or parameter: the loader gives every sample
            // 128, 0 to its end, and 0. A sample loaded into ZQuest has those too, give or
            // take the byte ZQuest cuts off an odd-sized one and Allegro's parameter of -1.
            if(slot.bits != 8 && slot.bits != 16)
                return cant_hold(name(), "a sample that isn't 8 or 16 bits", id);

            if(slot.priority != 128 || slot.loop_start != 0 || (slot.loop_end != slot.len && slot.loop_end != slot.len + 1) ||
               (slot.param != 0 && slot.param != 0xFFFFFFFFUL))
                return cant_hold(name(), "a sample's priority or loop points", id);
        }

        o.putc(have && payload_same ? entries[q].type : kind);

        if(have && ((name_same && !force) || id >= WAV_COUNT))
            o.copy(s, entries[q].at + 1, entries[q].payload_at - entries[q].at - 1);
        else
        {
            // no names past 255 here
            std::string nm = id < WAV_COUNT ? c_string(sfx_string[id], 35) : std::string();

            if(id < WAV_COUNT && nm == default_sfx_name(id))
                nm.clear();     // the loader names it the same way

            o.putc((int)nm.size());
            o.put(nm.data(), nm.size());
        }

        if(have && payload_same)
        {
            o.copy(s, entries[q].payload_at, entries[q].at + entries[q].len - entries[q].payload_at);
            continue;
        }

        if(kind == SAMPLE_WAV)
        {
            // signed, as 2.55 holds samples (q255_media.cpp turns them back to unsigned)
            int channels = slot.stereo ? 2 : 1;
            o.putc(slot.bits == 8 ? 0 : 1);
            o.putc(channels << 4);
            o.iputl((dword)slot.freq);
            o.iputl(slot.len);
            size_t values = (size_t)slot.len * channels;
            const byte *d = (const byte *)slot.data;
            std::vector<byte> buf;
            buf.reserve(4096);

            for(size_t k = 0; k < values; ++k)
            {
                if(slot.bits == 8)
                    buf.push_back(d ? (byte)(d[k] ^ 0x80) : 0x80);
                else
                {
                    word w = d ? (word)(d[k * 2] | (d[k * 2 + 1] << 8)) : 0x8000;
                    w ^= 0x8000;
                    buf.push_back((byte)w);
                    buf.push_back((byte)(w >> 8));
                }

                if(buf.size() >= 4096)
                {
                    o.put(&buf[0], buf.size());
                    buf.clear();
                }
            }

            if(!buf.empty())
                o.put(&buf[0], buf.size());
        }
        else if(kind == SAMPLE_OGG)
        {
            // The loader keeps only the OGG file; its rate and length are the file's own.
            bool was_ogg = have && entries[q].type == SAMPLE_OGG;
            o.iputl((dword)ogg->size());
            o.iputl(was_ogg ? entries[q].freq : 0);
            o.iputl(was_ogg ? entries[q].slen : 0);

            if(!ogg->empty())
                o.put(&(*ogg)[0], ogg->size());
        }
    }

    return 0;
}

int zqw_sfx(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_OLD && ctx.version < 4) || (fam == FAM_255 && (ctx.version < 6 || ctx.version > 9)))
        return ZQW_UNSUPPORTED;

    SfxWriter w(ctx, fam);
    return w.write(f);
}

// ============================================================================================
// MIDI: tunes
// ============================================================================================

// read_midi and q255_media.cpp's read_tune_midi: the divisions, then 32 tracks, each its
// length and bytes, big-endian.
bool skip_midi(Src &s)
{
    s.skip(2);

    for(int c = 0; c < MIDI_TRACKS && !s.failed; ++c)
    {
        long len = (long)s.mgetl();

        if(len > 0)
            s.skip(len);
    }

    return !s.failed;
}

bool midi_matches(Src &s, long long at, const MIDI *m)
{
    if(!m)
        return false;

    byte b[4];

    if(!s.peek(at, b, 2) || m->divisions != (short)((b[0] << 8) | b[1]))
        return false;

    at += 2;

    for(int c = 0; c < MIDI_TRACKS; ++c)
    {
        if(!s.peek(at, b, 4))
            return false;

        long len = (long)(((dword)b[0] << 24) | ((dword)b[1] << 16) | ((dword)b[2] << 8) | (dword)b[3]);
        at += 4;

        if(m->track[c].len != len)
            return false;

        if(len > 0)
        {
            if(!m->track[c].data || !s.equals(at, m->track[c].data, (size_t)len))
                return false;

            at += len;
        }
    }

    return true;
}

void put_midi(Out &o, const MIDI *m)
{
    o.mputw(m->divisions);

    for(int c = 0; c < MIDI_TRACKS; ++c)
    {
        o.mputl((dword)m->track[c].len);

        if(m->track[c].len > 0)
            o.put(m->track[c].data, (size_t)m->track[c].len);
    }
}

class MidiWriter : public Section
{
public:
    MidiWriter(const ZqPartContext &c, Family f) : Section(c, f) {}
    const char *name() const { return "tunes"; }
    int body(Out &o, Src &s) { return (fam == FAM_255 && v >= 5) ? body_v5(o, s) : body_old(o, s); }

private:
    int body_old(Out &o, Src &s);
    int body_v5(Out &o, Src &s);
    bool old_matches(Src &s, long long at, const zctune &t);
};

// Whether an old-format tune record still reads as tune t (readtunes).
bool MidiWriter::old_matches(Src &s, long long at, const zctune &t)
{
    int title_len = v < 4 ? 20 : 36;
    char raw[37];
    memset(raw, 0, sizeof(raw));

    if(!s.peek(at, raw, title_len))
        return false;

    // copyfrom's strncpy: up to the first NUL, the rest NULs
    char want[36];
    memset(want, 0, sizeof(want));
    strncpy(want, raw, 36);

    if(memcmp(want, t.title, 36) != 0)
        return false;

    at += title_len;
    byte b[16];

    if(!s.peek(at, b, 16))
        return false;

    long start = (long)(b[0] | (b[1] << 8) | (b[2] << 16) | ((dword)b[3] << 24));
    long loop_start = (long)(b[4] | (b[5] << 8) | (b[6] << 16) | ((dword)b[7] << 24));
    long loop_end = (long)(b[8] | (b[9] << 8) | (b[10] << 16) | ((dword)b[11] << 24));
    short loop = (short)(b[12] | (b[13] << 8));
    short volume = (short)(b[14] | (b[15] << 8));
    at += 16;

    if(t.start != start || t.loop_start != loop_start || t.loop_end != loop_end || t.loop != loop || t.volume != volume)
        return false;

    byte flags = 0, format = MFORMAT_MIDI;

    if(v >= 3 && !s.peek(at++, &flags, 1))
        return false;

    if(v >= 2 && !s.peek(at++, &format, 1))
        return false;

    if(t.flags != flags || t.format != format)
        return false;

    return midi_matches(s, at, (const MIDI *)t.data);
}

int MidiWriter::body_old(Out &o, Src &s)
{
    // readtunes, sectioned: the flags, then each flagged tune: its title (20 bytes before v4),
    // start, loop start and end, loop, volume, from v3 its flags, from v2 its format, its MIDI.
    byte oflags[MIDIFLAGS_SIZE];
    s.read(oflags, MIDIFLAGS_SIZE);
    int title_len = v < 4 ? 20 : 36;
    std::vector<long long> at(MAXCUSTOMTUNES, -1), len(MAXCUSTOMTUNES, 0);

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
    {
        if(!get_bit(oflags, i))
            continue;

        at[i] = s.pos;
        s.skip(title_len + 16 + (v >= 3 ? 1 : 0) + (v >= 2 ? 1 : 0));

        if(!skip_midi(s))
            return bad_part(name());

        len[i] = s.pos - at[i];
    }

    if(s.failed)
        return bad_part(name());

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
    {
        if(!get_bit(midi_flags, i))
            continue;

        const zctune &t = customtunes[i];

        if(v < 4 && strnlen(t.title, 36) > 20)
            return cant_hold(name(), "a tune title longer than 20 characters", i);

        if(v < 3 && t.flags)
            return cant_hold(name(), "a tune's flags (sections before v3 have none)", i);

        if(v < 2 && t.format != MFORMAT_MIDI)
            return cant_hold(name(), "a tune that isn't a MIDI", i);

        if(!t.data)
            return cant_hold(name(), "a tune with no music", i);
    }

    o.put(midi_flags, MIDIFLAGS_SIZE);

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
    {
        if(!get_bit(midi_flags, i))
            continue;

        const zctune &t = customtunes[i];

        if(!force && at[i] >= 0 && old_matches(s, at[i], t))
        {
            o.copy(s, at[i], len[i]);
            continue;
        }

        char title[36];
        memset(title, 0, sizeof(title));
        memcpy(title, t.title, strnlen(t.title, 36));
        o.put(title, title_len);
        o.iputl((dword)t.start);
        o.iputl((dword)t.loop_start);
        o.iputl((dword)t.loop_end);
        o.iputw(t.loop);
        o.iputw(t.volume);

        if(v >= 3)
            o.putc(t.flags);

        if(v >= 2)
            o.putc(t.format);

        put_midi(o, (const MIDI *)t.data);
    }

    return 0;
}

// ---- MIDI v5 (q255_read_midis) ------------------------------------------------------------

// The tune ZQuest holds for index i, or NULL: customtunes below 252, the store above.
const zctune *v5_tune(int i)
{
    if(i < MAXCUSTOMTUNES)
        return get_bit(midi_flags, i) ? &customtunes[i] : NULL;

    size_t at = (size_t)(i - MAXCUSTOMTUNES);

    if(at < zq255.high_tunes.size() && zq255.high_tunes[at].data)
        return &zq255.high_tunes[at];

    return NULL;
}

int MidiWriter::body_v5(Out &o, Src &s)
{
    // a bit string of which tunes are there, their count, then each one there: its title
    // (16-bit length), start, loop start and end, loop, volume, flags, format, MIDI
    long long bits_at = s.pos;
    std::vector<byte> obits;
    word size = s.igetw();
    obits.assign(size, 0);

    if(size)
    {
        byte pairs = s.getc();
        word n = s.igetw();

        for(dword i = 0; i < n && !s.failed; ++i)
        {
            dword at = pairs ? (dword)s.igetw() : i;
            byte value = s.getc();

            if(at >= obits.size())
                obits.resize(at + 1, 0);

            obits[at] = value;
        }
    }

    long long bits_len = s.pos - bits_at;
    word count = s.igetw();

    if(s.failed || count > MAX_TUNES_255)
        return bad_part(name());

    std::vector<long long> at(count, -1), len(count, 0), body_at(count, 0);
    std::vector<std::string> titles(count);

    for(int i = 0; i < count; ++i)
    {
        if(!((size_t)i / 8 < obits.size() && (obits[i / 8] >> (i % 8)) & 1))
            continue;

        at[i] = s.pos;
        word n = s.igetw();
        titles[i] = s.getstr(n);
        body_at[i] = s.pos;
        s.skip(16 + 2);

        if(!skip_midi(s))
            return bad_part(name());

        len[i] = s.pos - at[i];
    }

    if(s.failed)
        return bad_part(name());

    int needed = 0;

    for(int i = 0; i < MAX_TUNES_255; ++i)
        if(v5_tune(i))
            needed = i + 1;

    int total = needed > count ? needed : count;

    for(int i = 0; i < total; ++i)
    {
        const zctune *t = v5_tune(i);

        if(t && t->format != MFORMAT_MIDI)
            return cant_hold(name(), "a tune that isn't a MIDI", i);

        if(t && !t->data)
            return cant_hold(name(), "a tune with no music", i);
    }

    bool bits_same = total == count;

    for(int i = 0; i < count && bits_same; ++i)
        bits_same = (at[i] >= 0) == (v5_tune(i) != NULL);

    if(bits_same && !force)
        o.copy(s, bits_at, bits_len);
    else
    {
        std::vector<byte> bytes((size_t)(needed + 7) / 8, 0);

        for(int i = 0; i < needed; ++i)
            if(v5_tune(i))
                bytes[i / 8] |= (byte)(1 << (i % 8));

        // read_bitstr's form for values in order: the size, a 0, the count, the values
        o.iputw((long)bytes.size());

        if(!bytes.empty())
        {
            o.putc(0);
            o.iputw((long)bytes.size());
            o.put(&bytes[0], bytes.size());
        }
    }

    o.iputw(total);

    for(int i = 0; i < total; ++i)
    {
        const zctune *t = v5_tune(i);

        if(!t)
            continue;

        bool have = i < count && at[i] >= 0;
        bool title_same = false;

        if(have)
        {
            // store_name: before the first NUL, 35 characters at most, the rest NULs
            char want[36];
            memset(want, 0, sizeof(want));
            std::string c = c_string(titles[i].c_str(), 35);
            memcpy(want, c.data(), c.size());
            title_same = memcmp(want, t->title, 36) == 0;
        }

        if(have && !force && title_same)
        {
            byte b[18];
            bool same = s.peek(body_at[i], b, 18);
            same = same && t->start == (long)(b[0] | (b[1] << 8) | (b[2] << 16) | ((dword)b[3] << 24));
            same = same && t->loop_start == (long)(b[4] | (b[5] << 8) | (b[6] << 16) | ((dword)b[7] << 24));
            same = same && t->loop_end == (long)(b[8] | (b[9] << 8) | (b[10] << 16) | ((dword)b[11] << 24));
            same = same && t->loop == (short)(b[12] | (b[13] << 8)) && t->volume == (short)(b[14] | (b[15] << 8));
            same = same && t->flags == b[16] && t->format == b[17];
            same = same && midi_matches(s, body_at[i] + 18, (const MIDI *)t->data);

            if(same)
            {
                o.copy(s, at[i], len[i]);
                continue;
            }
        }

        if(have && !force && title_same)
            o.copy(s, at[i], body_at[i] - at[i]);
        else
        {
            size_t n = strnlen(t->title, 36);
            o.iputw((long)n);
            o.put(t->title, n);
        }

        o.iputl((dword)t->start);
        o.iputl((dword)t->loop_start);
        o.iputl((dword)t->loop_end);
        o.iputw(t->loop);
        o.iputw(t->volume);
        o.putc(t->flags);
        o.putc(t->format);
        put_midi(o, (const MIDI *)t->data);
    }

    return 0;
}

int zqw_midis(PACKFILE *f, const ZqPartContext &ctx)
{
    Family fam = family_of(ctx);

    if(fam == FAM_NONE || (fam == FAM_255 && ctx.version > 5))
        return ZQW_UNSUPPORTED;

    MidiWriter w(ctx, fam);
    return w.write(f);
}

// ---- ZQW_MEDIA_TRACE: what ZQuest holds, hashed, after every load and save -----------------
// For checking these writers where the 2.53 fingerprints can't: tiles past 65520, and what
// only the 2.55 store holds (sounds and tunes past 255, OGG files, sprite scripts).

struct Hash
{
    unsigned long long h;

    Hash() : h(14695981039346656037ULL) {}

    void add(const void *p, size_t n)
    {
        const byte *b = (const byte *)p;

        for(size_t i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }

    void add_long(long v) { add(&v, sizeof(v)); }
    void add_str(const char *s, size_t n) { std::string c = c_string(s, n); add(c.data(), c.size()); add_long((long)c.size()); }

    void add_sample(const SAMPLE &s)
    {
        add_long(s.bits);
        add_long(s.stereo);
        add_long(s.freq);
        add_long(s.priority);
        add_long((long)s.len);
        add_long((long)s.loop_start);
        add_long((long)s.loop_end);
        add_long((long)s.param);

        if(s.data)
            add(s.data, sample_bytes(s));
    }

    void add_tune(const zctune &t)
    {
        add_str(t.title, 36);
        add_long(t.start);
        add_long(t.loop_start);
        add_long(t.loop_end);
        add_long(t.loop);
        add_long(t.volume);
        add_long(t.flags);
        add_long(t.format);
        const MIDI *m = (const MIDI *)t.data;

        if(!m)
            return;

        add_long(m->divisions);

        for(int c = 0; c < MIDI_TRACKS; ++c)
        {
            add_long(m->track[c].len);

            if(m->track[c].len > 0 && m->track[c].data)
                add(m->track[c].data, (size_t)m->track[c].len);
        }
    }
};

void trace_hashes()
{
    if(!trace_on())
        return;

    Hash colors, icons, csets, tiles, weapons, sfx, midis;
    byte lead[20];
    colors_lead(misc.colors, lead);
    colors.add(lead, 20);
    long ct[9] = { misc.colors.triforce_tile, misc.colors.triframe_tile, misc.colors.overworld_map_tile, misc.colors.dungeon_map_tile,
                   misc.colors.blueframe_tile, misc.colors.HCpieces_tile, misc.colors.HCpieces_cset, misc.colors.subscr_shadow, misc.colors.msgtext };
    colors.add(ct, sizeof(ct));

    for(int i = 0; i < 4; ++i)
        icons.add_long(misc.icons[i]);

    csets.add(colordata, newerpsTOTAL);
    csets.add(palnames, sizeof(palnames));

    for(int i = 0; i < 256; ++i)
        for(int k = 0; k < 3; ++k)
        {
            csets.add_long(misc.cycles[i][k].first);
            csets.add_long(misc.cycles[i][k].count);
            csets.add_long(misc.cycles[i][k].speed);
        }

    for(int t = 0; t < NEWMAXTILES; ++t)
    {
        tiles.add_long(newtilebuf[t].format);

        if(newtilebuf[t].data)
            tiles.add(newtilebuf[t].data, tilesize(newtilebuf[t].format));
    }

    for(int i = 0; i < MAXWPNS; ++i)
    {
        Sprite s = sprite_of(wpnsbuf[i]);
        long f[7] = { s.tile, s.misc, s.csets, s.frames, s.speed, s.type, s.script };
        weapons.add(f, sizeof(f));
        weapons.add_str(weapon_string[i], 64);
    }

    sfx.add(customsfxflag, sizeof(customsfxflag));

    for(int i = 1; i < Q30_MAX_SFX; ++i)
    {
        sfx.add_sample(*v9_slot(i));

        if(i < WAV_COUNT)
            sfx.add_str(sfx_string[i], 36);

        const std::vector<byte> *ogg = v9_ogg(i);

        if(ogg && !ogg->empty())
            sfx.add(&(*ogg)[0], ogg->size());
    }

    midis.add(midi_flags, MIDIFLAGS_SIZE);

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
        if(get_bit(midi_flags, i))
            midis.add_tune(customtunes[i]);

    for(size_t i = 0; i < zq255.high_tunes.size(); ++i)
        if(zq255.high_tunes[i].data)
        {
            midis.add_long((long)i);
            midis.add_tune(zq255.high_tunes[i]);
        }

    trace("zqw hashes: MCLR %016llx ICON %016llx CSET %016llx TILE %016llx WPN %016llx SFX %016llx MIDI %016llx\n",
             colors.h, icons.h, csets.h, tiles.h, weapons.h, sfx.h, midis.h);
}

// ZQW_MEDIA_EDIT=1 (for testing): right after the first load, changes one record of each of
// these sections the way an edit would, and the writers then ignore zqw_force_serialize, so
// -qst-roundtrip-check saves the changed records through the encoders and the unchanged ones
// as copies, and the reload must give back the changed data.
bool edit_test()
{
    const char *e = getenv("ZQW_MEDIA_EDIT");
    return e && *e;
}

void edit_once()
{
    static bool done = false;

    if(done || !edit_test())
        return;

    done = true;
    misc.colors.text ^= 1;
    misc.icons[0] = misc.icons[0] < 60000 ? misc.icons[0] + 1 : 0;
    colordata[3 * CSET_BYTES + 5] = (byte)((colordata[3 * CSET_BYTES + 5] + 1) & 63);
    misc.cycles[0][0].speed ^= 1;
    newtilebuf[20].data[0] ^= 0x11;
    newtilebuf[131].data[3] ^= 0x22;    // one of the Goriya tiles a 2.10 quest has the other way round
    wpnsbuf[3].frames ^= 1;

    for(int i = 1; i < WAV_COUNT; ++i)
        if(sfx_flag(customsfxflag, i) && customsfxdata[i].data && customsfxdata[i].len > 4)
        {
            ((byte *)customsfxdata[i].data)[1] ^= 0x10;
            break;
        }

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
        if(get_bit(midi_flags, i) && customtunes[i].data)
        {
            customtunes[i].volume ^= 1;
            break;
        }

    // ZQW_MEDIA_EDIT=2 renames a sprite too, which a section before v3 has no room for
    if(!strcmp(getenv("ZQW_MEDIA_EDIT"), "2"))
        strcpy(weapon_string[5], "Renamed sprite");

    trace("zqw edit test: changed one record of each section\n");
}

ZqLoadHookRegistration hook_edit(edit_once);
ZqLoadHookRegistration hook_trace(trace_hashes);

ZqWriterRegistration reg_colors(ID_COLORS, zqw_colors, "zqw_media: misc colours");
ZqWriterRegistration reg_icons(ID_ICONS, zqw_icons, "zqw_media: game icons");
ZqWriterRegistration reg_csets(ID_CSETS, zqw_csets, "zqw_media: palettes");
ZqWriterRegistration reg_tiles(ID_TILES, zqw_tiles, "zqw_media: tiles");
ZqWriterRegistration reg_weapons(ID_WEAPONS, zqw_weapons, "zqw_media: weapon sprites");
ZqWriterRegistration reg_sfx(ID_SFX, zqw_sfx, "zqw_media: sound effects");
ZqWriterRegistration reg_midis(ID_MIDIS, zqw_midis, "zqw_media: tunes");

} // namespace
