// Test tools for saving quests in their own version (command line only):
//
// -qst-dump-hashes in.qst report.txt
//     loads the quest and writes one line per part (its data as ZQuest holds it, through the
//     2.53 writers, hashed) and one per map screen. Comparing the reports of an original and a
//     saved copy shows exactly which parts and screens a save changed.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "qst.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_tile_refs.h"
#include "zq_q255.h"

extern zquestheader header;
extern word map_count;
extern word msg_count;
extern MsgStr *MsgStrings;
int writemapscreen(PACKFILE *f, int i, int j);
int writestrings(PACKFILE *f, word version, word build, word start_msgstr, word max_msgstrs);

namespace
{

struct Hash
{
    uint64_t h;
    FILE *bytes;        // also the bytes themselves, when set (ZQ_DUMP_BYTES)
};

void add(Hash *s, unsigned char b)
{
    s->h ^= b;
    s->h *= 1099511628211ULL;
    if(s->bytes)
        fputc(b, s->bytes);
}

int h_fclose(void *) { return 0; }
int h_getc(void *) { return EOF; }
int h_ungetc(int, void *) { return EOF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long h_fread(void *, long, void *) { return 0; }
#pragma pop_macro("long")
int h_feof(void *) { return 1; }
int h_ferror(void *) { return 0; }
int h_fseek(void *, int) { return -1; }
int h_putc(int c, void *u) { add((Hash *)u, (unsigned char)c); return c & 0xFF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long h_fwrite(const void *p, long n, void *u)
{
    for(long i = 0; i < n; ++i)
        add((Hash *)u, ((const unsigned char *)p)[i]);
    return n;
}
#pragma pop_macro("long")

const PACKFILE_VTABLE g_vt = { h_fclose, h_getc, h_ungetc, h_fread, h_putc, h_fwrite, h_fseek, h_feof, h_ferror };

} // namespace

int zq_dump_hashes(const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    char title[sizeof(header.title) + 1];
    memcpy(title, header.title, sizeof(header.title));
    title[sizeof(header.title)] = 0;
    fprintf(out, "quest version 0x%x build %d title [%s]\n", header.zelda_version, header.build, title);

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];

        if(p.has_fingerprint)
            fprintf(out, "part %-16s %016llx%s\n", zq_part_name(p).c_str(), p.fingerprint, p.fingerprint_failed ? " (writer failed)" : "");
    }

    if(getenv("ZQ_DUMP_BYTES"))
    {
        Hash h;
        h.h = 0;
        std::string name = std::string(report) + ".strings.bin";
        h.bytes = fopen(name.c_str(), "wb");
        PACKFILE *f = zq_open_sink(&g_vt, &h);
        if(f)
        {
            writestrings(f, ZELDA_VERSION, VERSION_BUILD, 0, MAXMSGS);
            pack_fclose(f);
        }
        if(h.bytes)
            fclose(h.bytes);
        fprintf(out, "strings: msg_count %d\n", msg_count);
    }

    for(int m = 0; m < map_count; ++m)
    {
        for(int s = 0; s < MAPSCRS; ++s)
        {
            Hash h;
            h.h = 14695981039346656037ULL;
            h.bytes = NULL;
            if(getenv("ZQ_DUMP_BYTES") && m == 0 && s < 4)
            {
                // ZQ_DUMP_BYTES: the first screens' 2.53 bytes, to compare two builds field by field
                std::string name = std::string(report) + ".screen" + std::to_string(s) + ".bin";
                h.bytes = fopen(name.c_str(), "wb");
            }
            PACKFILE *f = zq_open_sink(&g_vt, &h);

            if(!f)
                continue;

            writemapscreen(f, m, s);
            pack_fclose(f);
            if(h.bytes)
                fclose(h.bytes);
            fprintf(out, "screen %3d %3d %016llx\n", m, s, h.h);
        }
    }

    fclose(out);
    return 0;
}

// -qst-compare a.qst b.qst report.txt
//     opens both quests and compares them part by part (header, then each section by id, in the
//     order found): SAME, DIFFERENT (first byte that differs, both lengths), or only in one. For
//     checking a quest saved as a newer version against upstream's own save of the same quest.

int load_quest(const char *filename, bool compressed, bool encrypted);   // zq_class.cpp

namespace
{

struct KeptPart
{
    dword id;
    word version, cversion;
    int64_t at, length;
    bool matched;
};

std::string part_tag(dword id)
{
    if(id == ID_HEADER)
        return "HDR ";
    if(id == 0)
        return "BODY";
    if(id == ZQ_PART_TAIL)
        return "TAIL";

    char t[5];

    for(int k = 0; k < 4; ++k)
    {
        char c = (char)(id >> (24 - 8 * k));
        t[k] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }

    t[4] = 0;
    return t;
}

} // namespace

int zq_compare_quests(const char *a, const char *b, const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    // the first quest's parts, copied out of the kept copy before the second load replaces it
    char ta[2048];
    snprintf(ta, sizeof(ta), "%s.a.bin", report);
    int ra = load_quest(a, true, true);

    if(ra != 0)
    {
        fprintf(out, "LOAD FAILED: %s (%d)\n", a, ra);
        fclose(out);
        return 1;
    }

    fprintf(out, "a: %s  version 0x%x build %d, %d parts\n", a, zq_quest_file.zelda_version, zq_quest_file.build, (int)zq_quest_file.parts.size());
    std::vector<KeptPart> pa;
    FILE *fa = fopen(ta, "wb");
    int64_t pos = 0;
    std::vector<byte> chunk(1 << 20);

    for(size_t i = 0; fa && i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        KeptPart k;
        k.id = p.id;
        zq_part_versions(p, &k.version, &k.cversion);
        k.at = pos;
        k.length = p.length;
        k.matched = false;

        for(int64_t at = 0; at < p.length; at += (int64_t)chunk.size())
        {
            size_t n = (size_t)(p.length - at < (int64_t)chunk.size() ? p.length - at : (int64_t)chunk.size());
            zq_part_read(p, at, &chunk[0], n);
            fwrite(&chunk[0], 1, n, fa);
        }

        pos += p.length;
        pa.push_back(k);
    }

    if(fa)
        fclose(fa);

    int rb = load_quest(b, true, true);

    if(rb != 0)
    {
        fprintf(out, "LOAD FAILED: %s (%d)\n", b, rb);
        fclose(out);
        remove(ta);
        return 1;
    }

    fprintf(out, "b: %s  version 0x%x build %d, %d parts\n", b, zq_quest_file.zelda_version, zq_quest_file.build, (int)zq_quest_file.parts.size());
    fa = fopen(ta, "rb");
    int same = 0, different = 0, only = 0;
    std::vector<byte> cb(1 << 20);

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        word v, cv;
        zq_part_versions(p, &v, &cv);
        KeptPart *k = NULL;

        for(size_t j = 0; j < pa.size() && !k; ++j)
            if(!pa[j].matched && pa[j].id == p.id)
                k = &pa[j];

        if(!k)
        {
            ++only;
            fprintf(out, "ONLY IN B  %s v%d cv%d  %lld bytes\n", part_tag(p.id).c_str(), v, cv, p.length);
            continue;
        }

        k->matched = true;
        int64_t diff = -1;
        int64_t n = k->length < p.length ? k->length : p.length;

        for(int64_t at = 0; at < n && diff < 0 && fa; at += (int64_t)chunk.size())
        {
            size_t m = (size_t)(n - at < (int64_t)chunk.size() ? n - at : (int64_t)chunk.size());
            _fseeki64(fa, k->at + at, SEEK_SET);
            fread(&chunk[0], 1, m, fa);
            zq_part_read(p, at, &cb[0], m);

            for(size_t q = 0; q < m && diff < 0; ++q)
                if(chunk[q] != cb[q])
                    diff = at + (int64_t)q;
        }

        if(diff < 0 && k->length != p.length)
            diff = n;

        if(diff < 0)
        {
            ++same;
            fprintf(out, "SAME       %s v%d cv%d  %lld bytes\n", part_tag(p.id).c_str(), v, cv, p.length);
        }
        else
        {
            ++different;
            fprintf(out, "DIFFERENT  %s a v%d cv%d %lld bytes, b v%d cv%d %lld bytes, first difference at %lld\n",
                    part_tag(p.id).c_str(), k->version, k->cversion, k->length, v, cv, p.length, diff);
        }
    }

    for(size_t j = 0; j < pa.size(); ++j)
        if(!pa[j].matched)
        {
            ++only;
            fprintf(out, "ONLY IN A  %s v%d cv%d  %lld bytes\n", part_tag(pa[j].id).c_str(), pa[j].version, pa[j].cversion, pa[j].length);
        }

    fprintf(out, "same %d, different %d, only in one %d\n", same, different, only);

    if(fa)
        fclose(fa);

    fclose(out);
    remove(ta);
    return (different || only) ? 1 : 0;
}

// -qst-tile-check in.qst report.txt
//     reads the TILE section from the bytes kept of the file (zq_sections.h), on its own rather
//     than through the loader, and compares every tile with ZQuest's tile buffer: the tiles past
//     65,519 above all, which ZQuest held no room for before it had 825 pages. The report gives
//     the counts and a hash of the tiles past 65,519 from each side. 0 when every tile matches.

extern tiledata *newtilebuf;
int tilesize(byte format);

namespace
{

// The kept part, read front to back through a buffer.
struct PartReader
{
    const ZqQuestPart *p;
    int64_t at;
    std::vector<byte> buf;
    size_t pos, len;
    bool failed;

    PartReader(const ZqQuestPart *part) : p(part), at(0), buf(1 << 20), pos(0), len(0), failed(false) {}

    bool fill()
    {
        int64_t left = p->length - at;

        if(left <= 0)
            return false;

        len = (size_t)(left < (int64_t)buf.size() ? left : (int64_t)buf.size());

        if(!zq_part_read(*p, at, &buf[0], len))
            return false;

        at += (int64_t)len;
        pos = 0;
        return true;
    }

    int getc()
    {
        if(pos >= len && !fill())
        {
            failed = true;
            return 0;
        }

        return buf[pos++];
    }

    dword get(int n)
    {
        dword v = 0;

        for(int i = 0; i < n; ++i)
            v |= (dword)getc() << (8 * i);

        return v;
    }

    void read(byte *out, size_t n)
    {
        for(size_t i = 0; i < n; ++i)
            out[i] = (byte)getc();
    }
};

void hash_tile(uint64_t &h, byte format, const byte *data, size_t n)
{
    h ^= format;
    h *= 1099511628211ULL;

    for(size_t i = 0; i < n; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
}

bool all_zero(const byte *p, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        if(p[i])
            return false;

    return true;
}

} // namespace

int zq_tile_check(const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    const ZqQuestPart *tiles = NULL;

    for(size_t i = 0; i < zq_quest_file.parts.size() && !tiles; ++i)
        if(zq_quest_file.parts[i].id == ID_TILES)
            tiles = &zq_quest_file.parts[i];

    if(!tiles)
    {
        fprintf(out, "NO TILE PART: quest version 0x%x build %d\n", header.zelda_version, header.build);
        fclose(out);
        return 0;
    }

    const word zv = zq_quest_file.zelda_version;
    const byte build = zq_quest_file.build;
    const bool newer = zv >= 0x255;
    PartReader r(tiles);
    word version = (word)r.get(2);
    r.get(2);                                   // cversion
    r.get(4);                                   // size
    long count = (newer && build >= 41) ? (long)r.get(4) : (long)r.get(2);
    const bool has_format = newer || zv > 0x211 || (zv == 0x211 && build > 4);
    const bool blank_opt = newer && version > 2;
    const long LIMIT = TILES_PER_PAGE * 252;

    long mismatches = 0, mismatches_past = 0, first_bad = -1, drawn_past = 0;
    std::vector<int> drawn_on_page(TILE_PAGES, 0);
    std::vector<long> first_drawn;
    // ZQ_TILE_DUMP=n: that tile's bytes as the file has them, for comparing two files
    const char *dump_env = getenv("ZQ_TILE_DUMP");
    long dump_tile = dump_env ? atol(dump_env) : -1;
    std::string dump_line;
    uint64_t file_hash = 14695981039346656037ULL, zq_hash = 14695981039346656037ULL;
    byte data[1024], zero[1024];
    memset(zero, 0, sizeof(zero));

    for(long i = 0; i < NEWMAXTILES; ++i)
    {
        // the tile as the file has it, a blank tile from the end of the file's tiles on
        byte format = tf4Bit;
        size_t n = (size_t)tilesize(tf4Bit);
        bool blank = true;

        if(i < count)
        {
            byte f = has_format ? (byte)r.getc() : (byte)tf4Bit;

            // A newer quest's tile of format 0 is blank: from v3 nothing follows it, before
            // that 256 bytes that are skipped. 2.53's reader keeps such a tile as it is.
            if(newer && f == tfInvalid)
            {
                if(!blank_opt)
                    r.read(data, 256);
            }
            else
            {
                size_t len = (size_t)tilesize(f);

                if(len == 0 || len > sizeof(data))
                {
                    r.failed = true;
                    break;
                }

                r.read(data, len);
                format = f;
                n = len;
                blank = false;
            }
        }

        if(r.failed)
            break;

        const byte *want = blank ? zero : data;
        const tiledata &t = newtilebuf[i];

        if(i == dump_tile)
        {
            char hex[8];
            snprintf(hex, sizeof(hex), "f%d ", format);
            dump_line = hex;

            for(size_t k = 0; k < n; ++k)
            {
                snprintf(hex, sizeof(hex), "%02x", want[k]);
                dump_line += hex;
            }
        }
        bool same = t.data && t.format == format && memcmp(t.data, want, n) == 0;

        if(blank && !same)      // a blank tile reads as a reset one, whatever its format byte said
            same = t.data && t.format == tf4Bit && all_zero(t.data, (size_t)tilesize(tf4Bit));

        if(!same)
        {
            ++mismatches;

            if(first_bad < 0)
                first_bad = i;

            if(i >= LIMIT)
                ++mismatches_past;
        }

        if(i >= LIMIT)
        {
            if(!all_zero(want, n))
            {
                ++drawn_past;
                ++drawn_on_page[i / TILES_PER_PAGE];

                if(first_drawn.size() < 8)
                    first_drawn.push_back(i);
            }

            hash_tile(file_hash, format, want, n);

            if(t.data)
                hash_tile(zq_hash, t.format, t.data, (size_t)tilesize(t.format));
        }
    }

    long past = count > LIMIT ? count - LIMIT : 0;
    bool ok = !r.failed && mismatches == 0 && file_hash == zq_hash;
    fprintf(out, "%s: quest version 0x%x build %d, TILE v%d, %ld tiles in the file, %ld of them past 65,519 (%ld drawn on)\n",
            ok ? "SAME" : (r.failed ? "UNREADABLE" : "DIFFERENT"), zv, build, version, count, past, drawn_past);
    fprintf(out, "tiles that differ from ZQuest's: %ld, past 65,519: %ld, first: %ld\n", mismatches, mismatches_past, first_bad);
    fprintf(out, "hash of the tiles past 65,519: file %016llx, ZQuest %016llx\n",
            (unsigned long long)file_hash, (unsigned long long)zq_hash);
    fprintf(out, "pages past 251 with tiles drawn on (page:tiles):");

    for(int pg = 252, shown = 0; pg < TILE_PAGES && shown < 16; ++pg)
        if(drawn_on_page[pg])
        {
            fprintf(out, " %d:%d", pg, drawn_on_page[pg]);
            ++shown;
        }

    fprintf(out, "\nfirst tiles drawn on past 65,519:");

    for(size_t k = 0; k < first_drawn.size(); ++k)
        fprintf(out, " %ld", first_drawn[k]);

    fprintf(out, "\n");

    if(dump_tile >= 0)
        fprintf(out, "tile %ld in the file: %s\n", dump_tile, dump_line.c_str());

    fclose(out);
    return ok ? 0 : 1;
}

// -qst-tile-limit-test in.qst report.txt
//     what keeps a save from losing tile numbers (zq_tile_refs.h), tried on a loaded quest
//     without the window, on a copy of the file beside the report:
//     a 2.53 or older quest: a combo given tile 70,000, then a tile drawn on past 65,519, must each
//     stop the 2.53.1 writer and the save in the quest's own version, and the copy must stay as
//     it was; with them undone the quest fits again.
//     a 2.55 or 3.0 quest: a combo's tile moved by 65,536 must count as a change of the combos
//     part (the 2.53 writers behind the fingerprints keep 16 bits of it), and not once undone.
//     0 when everything behaved.

extern newcombo *combobuf;
int save_unencoded_quest(const char *filename, bool compressed);
void reset_tile(tiledata *buf, int t, int format);

namespace
{

std::string file_digest(const char *path)
{
    FILE *f = fopen(path, "rb");

    if(!f)
        return "missing";

    uint64_t h = 14695981039346656037ULL;
    long n = 0;
    int c;

    while((c = fgetc(f)) != EOF)
    {
        h ^= (unsigned char)c;
        h *= 1099511628211ULL;
        ++n;
    }

    fclose(f);
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld bytes %016llx", n, (unsigned long long)h);
    return buf;
}

bool copy_file_to(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb");

    if(!a)
        return false;

    FILE *b = fopen(to, "wb");

    if(!b)
    {
        fclose(a);
        return false;
    }

    char buf[65536];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), a)) > 0)
        fwrite(buf, 1, n, b);

    fclose(a);
    fclose(b);
    return true;
}

} // namespace

int zq_tile_limit_test(const char *filename, const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    int failures = 0;
    std::string why;

    if(!zq255_quest_is_newer())
    {
        std::string copy = std::string(report) + ".copy.qst";
        copy_file_to(filename, copy.c_str());
        const std::string before = file_digest(copy.c_str());
        bool fits = zq_tiles_fit_253(why);
        std::string kept;
        int past = zq_tile_refs_loaded_past(&kept);
        fprintf(out, "as loaded: %s%s (tile numbers past 65,519 in the file: %d%s%s)\n", fits ? "fits a 2.53 file" : "REFUSED: ",
                fits ? "" : why.c_str(), past, past ? ": " : "", kept.c_str());
        failures += !fits;

        for(int kind = 0; kind < 2; ++kind)
        {
            const long TILE = 70000;
            tile_id old_tile = combobuf[1].tile;

            if(kind == 0)
                combobuf[1].tile = TILE;
            else
            {
                reset_tile(newtilebuf, TILE, tf4Bit);
                newtilebuf[TILE].data[0] = 0x11;
            }

            const char *what = kind == 0 ? "combo 1 given tile 70,000" : "tile 70,000 drawn on";
            why.clear();
            fits = zq_tiles_fit_253(why);
            fprintf(out, "%s: %s%s\n", what, fits ? "FITS (wrong)" : "refused: ", fits ? "" : why.c_str());
            failures += fits;

            std::string qsu = std::string(report) + ".qsu";
            remove(qsu.c_str());
            int r = save_unencoded_quest(qsu.c_str(), false);
            FILE *t = fopen(qsu.c_str(), "rb");
            fprintf(out, "  2.53.1 writer: returned %d, %s\n", r, t ? "WROTE A FILE (wrong)" : "wrote nothing");
            failures += (r == 0) + (t != NULL);

            if(t)
                fclose(t);

            std::string own_why;
            r = zq_save_own_format_ex(copy.c_str(), true, false, own_why);
            fprintf(out, "  save in its own version: returned %d (%s)\n", r, own_why.c_str());
            failures += r != 8;

            if(kind == 0)
                combobuf[1].tile = old_tile;
            else
                reset_tile(newtilebuf, TILE, tf4Bit);
        }

        why.clear();
        fits = zq_tiles_fit_253(why);
        fprintf(out, "undone: %s%s\n", fits ? "fits a 2.53 file" : "REFUSED: ", fits ? "" : why.c_str());
        failures += !fits;
        const std::string after = file_digest(copy.c_str());
        fprintf(out, "the copy of the file: %s (before %s, after %s)\n", before == after ? "unchanged" : "CHANGED", before.c_str(), after.c_str());
        failures += before != after;
        remove(copy.c_str());
    }
    else
    {
        const ZqQuestPart *combos = zq_find_part(ID_COMBOS);
        int i = 1;

        // a combo whose tile can move by 65,536 and stay within the tiles there are
        while(i < MAXCOMBOS && !(combobuf[i].tile > 0 && combobuf[i].tile + 65536 < NEWMAXTILES))
            ++i;

        if(!combos || i >= MAXCOMBOS)
            fprintf(out, "no combos part, or no combo to try (part %s)\n", combos ? "found" : "missing");
        else
        {
            bool at_load = zq_part_changed(*combos);
            tile_id old_tile = combobuf[i].tile;
            combobuf[i].tile = old_tile + 65536;
            bool moved = zq_part_changed(*combos);
            combobuf[i].tile = old_tile;
            bool undone = zq_part_changed(*combos);
            fprintf(out, "combo %d, tile %ld to %ld: combos part changed: as loaded %s, moved %s, undone %s\n",
                    i, (long)old_tile, (long)old_tile + 65536, at_load ? "YES (wrong)" : "no", moved ? "yes" : "NO (wrong)", undone ? "YES (wrong)" : "no");
            failures += at_load + !moved + undone;
        }
    }

    fprintf(out, "%s\n", failures ? "FAILED" : "PASSED");
    fclose(out);
    return failures ? 1 : 0;
}
