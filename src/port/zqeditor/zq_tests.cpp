// Test tools for saving quests in their own version (command line only):
//
// -qst-dump-hashes in.qst report.txt
//     loads the quest and writes one line per part (its data as ZQuest holds it, through the
//     2.53 writers, hashed) and one per map screen. Comparing the reports of an original and a
//     saved copy shows exactly which parts and screens a save changed.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <vector>

#include "zdefs.h"
#include "qst.h"
#include "zq_sections.h"
#include "zq_writers.h"

extern zquestheader header;
extern word map_count;
int writemapscreen(PACKFILE *f, int i, int j);

namespace
{

struct Hash
{
    unsigned long long h;
};

void add(Hash *s, unsigned char b)
{
    s->h ^= b;
    s->h *= 1099511628211ULL;
}

int h_fclose(void *) { return 0; }
int h_getc(void *) { return EOF; }
int h_ungetc(int, void *) { return EOF; }
long h_fread(void *, long, void *) { return 0; }
int h_feof(void *) { return 1; }
int h_ferror(void *) { return 0; }
int h_fseek(void *, int) { return -1; }
int h_putc(int c, void *u) { add((Hash *)u, (unsigned char)c); return c & 0xFF; }
long h_fwrite(const void *p, long n, void *u)
{
    for(long i = 0; i < n; ++i)
        add((Hash *)u, ((const unsigned char *)p)[i]);
    return n;
}

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

    for(int m = 0; m < map_count; ++m)
    {
        for(int s = 0; s < MAPSCRS; ++s)
        {
            Hash h;
            h.h = 14695981039346656037ULL;
            PACKFILE *f = zq_open_sink(&g_vt, &h);

            if(!f)
                continue;

            writemapscreen(f, m, s);
            pack_fclose(f);
            fprintf(out, "screen %3d %3d %016llx\n", m, s, h.h);
        }
    }

    fclose(out);
    return 0;
}
