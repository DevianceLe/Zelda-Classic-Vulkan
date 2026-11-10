// The 2.55 quest loader, for the player and the port's ZQuest. Quests made by 2.55 have newer section
// formats than this engine's own readers in src/qst.cpp, which stay as they are
// so 2.53 quests load and save exactly as before.
//
// Every section is handed over as a block of memory with its declared size. A
// reader walks it with the get functions below and fills this engine's data
// where a field has a home; fields 2.53 has no home for are read and dropped
// (see the comment at the top of each q255_*.cpp). The caller checks that the
// reader used exactly the declared size, so a wrong reader cannot go unnoticed
// and cannot damage the sections after it.
//
// The readers are written to compile as C++98, like the rest of the game.

#ifndef ZC_Q255_H
#define ZC_Q255_H

#include <string>
#include <vector>

#include "zc_alleg.h"
#include "zdefs.h"
#include "qst.h"

// A reader returns this when it doesn't handle the section yet.
const int Q255_NOT_HANDLED = -1000;

struct Q255Section
{
    const byte *data;
    size_t size;
    size_t pos;
    bool failed;            // a get ran past the end: stop and return qe_invalid

    word version;           // the section's format version
    word cversion;
    bool keep;              // false: only walk the bytes, change nothing

    zquestheader *header;   // the quest's header
    miscQdata *misc;
    zctune *tunes;

    Q255Section() : data(NULL), size(0), pos(0), failed(false), version(0), cversion(0), keep(false),
                    header(NULL), misc(NULL), tunes(NULL) {}

    // Written this way round so a huge length can't overflow past the check.
    bool has(size_t n) const { return !failed && pos <= size && n <= size - pos; }
    size_t left() const { return size - pos; }

    // little-endian, as the file stores them (p_getc, p_igetw, p_igetl)
    byte getc()
    {
        if(!has(1)) { failed = true; return 0; }
        return data[pos++];
    }
    word igetw()
    {
        if(!has(2)) { failed = true; return 0; }
        word v = (word)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return v;
    }
    dword igetl()
    {
        if(!has(4)) { failed = true; return 0; }
        dword v = (dword)data[pos] | ((dword)data[pos + 1] << 8) | ((dword)data[pos + 2] << 16) | ((dword)data[pos + 3] << 24);
        pos += 4;
        return v;
    }
    // signed helpers
    short igetw_s() { return (short)igetw(); }
    long igetl_s() { return (long)(int)igetl(); }
    // big-endian (p_mgetl)
    dword mgetl()
    {
        if(!has(4)) { failed = true; return 0; }
        dword v = ((dword)data[pos] << 24) | ((dword)data[pos + 1] << 16) | ((dword)data[pos + 2] << 8) | (dword)data[pos + 3];
        pos += 4;
        return v;
    }
    // n bytes into dst (NULL: skip them)
    void read(void *dst, size_t n)
    {
        if(!has(n)) { failed = true; return; }
        if(dst)
            memcpy(dst, data + pos, n);
        pos += n;
    }
    void skip(size_t n) { read(NULL, n); }

    // Text with a byte, word or dword length in front (p_getcstr, p_getwstr, p_getlstr).
    std::string getstr(size_t n)
    {
        std::string out;
        if(!has(n)) { failed = true; return out; }
        if(n)
            out.assign((const char *)(data + pos), n);
        pos += n;
        return out;
    }
    std::string getcstr() { byte n = getc(); return getstr(n); }
    std::string getwstr() { word n = igetw(); return getstr(n); }
    std::string getlstr() { dword n = igetl(); return getstr(n); }
};

// ---- readers: one per section -------------------------------------------------
// Each returns 0, qe_invalid (or another qe_ code), or Q255_NOT_HANDLED.

// q255_basic.cpp
int q255_read_header(Q255Section &s, byte *map_count, byte *midi_flags);    // from just after the program version
int q255_read_rules(Q255Section &s);
int q255_read_strings(Q255Section &s);
int q255_read_misc(Q255Section &s);

// q255_media.cpp
int q255_read_colors(Q255Section &s);
int q255_read_icons(Q255Section &s);
int q255_read_csets(Q255Section &s);
int q255_read_tiles(Q255Section &s);
int q255_read_weapons(Q255Section &s);
int q255_read_sfx(Q255Section &s);
int q255_read_midis(Q255Section &s);

// q255_combos.cpp
int q255_read_combos(Q255Section &s);
int q255_read_comboaliases(Q255Section &s);

// q255_items.cpp
int q255_read_items(Q255Section &s);

// q255_guys.cpp
int q255_read_guys(Q255Section &s);

// q255_maps.cpp
int q255_read_maps(Q255Section &s);
int q255_read_dmaps(Q255Section &s);

// q255_init.cpp
int q255_read_initdata(Q255Section &s);
int q255_read_linksprites(Q255Section &s);
int q255_read_subscreens(Q255Section &s);

// q255_scripts.cpp
int q255_read_ffscript(Q255Section &s);

// Records a tile number the quest refers to, for the report at the end of the load.
void q255_note_tile(const char *kind, unsigned long tile);

// True only in the port's ZQuest, which saves these quests back in their own format: readers
// may then keep what they would otherwise drop, for ZQuest's section writers
// (src/port/zqeditor/zqw_*.cpp), through hooks those writer files set. The player leaves it
// false, so nothing a reader keeps can change how a quest plays.
extern bool q255_keep_for_editor;

// -q255-check in.qst report.txt: runs the readers over a quest without keeping
// anything and writes how each section went.
int q255_check_quest(const char *filename, const char *report);

#endif
