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
