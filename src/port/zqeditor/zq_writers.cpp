// See zq_writers.h.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_writers.h"

#ifndef INTERNAL_VERSION
#define INTERNAL_VERSION 0xA721     // zquest.h; what ZQuest's own save mixes into the key
#endif

// ---- the registry ---------------------------------------------------------------------

namespace
{

struct Entry
{
    dword id;
    ZqPartWriter writer;
    const char *name;
    int priority;
};

std::vector<Entry> &registry()
{
    static std::vector<Entry> r;    // built on first use, whatever order files start in
    return r;
}

// Discards what is written: to ask a writer whether it takes a part without keeping it.
int dc_fclose(void *) { return 0; }
int dc_getc(void *) { return EOF; }
int dc_ungetc(int, void *) { return EOF; }
long dc_fread(void *, long, void *) { return 0; }
int dc_putc(int c, void *) { return c & 0xFF; }
long dc_fwrite(const void *, long n, void *) { return n; }
int dc_fseek(void *, int) { return -1; }
int dc_feof(void *) { return 1; }
int dc_ferror(void *) { return 0; }
const PACKFILE_VTABLE g_discard_vtable = { dc_fclose, dc_getc, dc_ungetc, dc_fread, dc_putc, dc_fwrite, dc_fseek, dc_feof, dc_ferror };

} // namespace

namespace { std::vector<dword> &every_save_ids(); }
bool zqw_force_serialize = false;
std::string zqw_last_error;

namespace
{
std::vector<void (*)()> &load_hooks()
{
    static std::vector<void (*)()> h;
    return h;
}
}

ZqLoadHookRegistration::ZqLoadHookRegistration(void (*fn)())
{
    load_hooks().push_back(fn);
}

void zq_writers_note_loaded()
{
    for(size_t i = 0; i < load_hooks().size(); ++i)
        load_hooks()[i]();
}

ZqEverySave::ZqEverySave(dword id)
{
    every_save_ids().push_back(id);
}

ZqWriterRegistration::ZqWriterRegistration(dword id, ZqPartWriter writer, const char *name, int priority)
{
    Entry e;
    e.id = id;
    e.writer = writer;
    e.name = name;
    e.priority = priority;
    std::vector<Entry> &r = registry();
    size_t at = 0;

    while(at < r.size() && r[at].priority >= priority)  // kept in priority order, ties by arrival
        ++at;

    r.insert(r.begin() + at, e);
}

bool zq_part_versions(const ZqQuestPart &p, word *version, word *cversion)
{
    byte b[4];

    if(zq_part_is_raw(p.id) || !zq_part_read(p, 0, b, 4))
    {
        *version = *cversion = 0;
        return false;
    }

    *version = (word)(b[0] | (b[1] << 8));
    *cversion = (word)(b[2] | (b[3] << 8));
    return true;
}

namespace
{

ZqPartContext context_for(const ZqQuestPart &p)
{
    ZqPartContext ctx;
    ctx.part = &p;
    ctx.zelda_version = zq_quest_file.zelda_version;
    ctx.build = zq_quest_file.build;
    zq_part_versions(p, &ctx.version, &ctx.cversion);
    return ctx;
}

// The writer that takes this part, or NULL. Asks each registered one in turn with a file
// that throws the bytes away; fake_pack_writing passes are the writers' own business.
const Entry *writer_for(const ZqQuestPart &p)
{
    ZqPartContext ctx = context_for(p);
    std::vector<Entry> &r = registry();

    for(size_t i = 0; i < r.size(); ++i)
    {
        if(r[i].id != p.id)
            continue;

        PACKFILE *f = zq_open_sink(&g_discard_vtable, NULL);

        if(!f)
            return NULL;

        int rc = r[i].writer(f, ctx);
        pack_fclose(f);

        if(rc != ZQW_UNSUPPORTED)
            return &r[i];
    }

    return NULL;
}

} // namespace

int zq_write_part(PACKFILE *f, const ZqQuestPart &p)
{
    ZqPartContext ctx = context_for(p);
    std::vector<Entry> &r = registry();

    for(size_t i = 0; i < r.size(); ++i)
    {
        if(r[i].id != p.id)
            continue;

        int rc = r[i].writer(f, ctx);

        if(rc != ZQW_UNSUPPORTED)
            return rc;
    }

    return ZQW_UNSUPPORTED;
}

std::string zq_part_name(const ZqQuestPart &p)
{
    if(p.id == ID_HEADER)
        return "the header";
    if(p.id == 0)
        return "the quest data (pre-1.93 format)";
    if(p.id == ZQ_PART_TAIL)
        return "the end of the file";

    char tag[5] = "????";

    for(int k = 0; k < 4; ++k)
    {
        char c = (char)(p.id >> (24 - 8 * k));
        tag[k] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }

    word v, cv;
    zq_part_versions(p, &v, &cv);
    char buf[48];
    sprintf(buf, "%s v%d", tag, v);
    return buf;
}

// ---- ZQuest's own 2.53 writers, for parts at 2.53's own section version ---------------

namespace
{

struct Own253
{
    dword id;
    word version, cversion;
};

// 2.53's section versions (V_* and CV_* in zdefs.h)
const Own253 g_own253[] =
{
    { ID_RULES, V_RULES, CV_RULES }, { ID_STRINGS, V_STRINGS, CV_STRINGS }, { ID_MISC, V_MISC, CV_MISC },
    { ID_TILES, V_TILES, CV_TILES }, { ID_COMBOS, V_COMBOS, CV_COMBOS }, { ID_CSETS, V_CSETS, CV_CSETS },
    { ID_MAPS, V_MAPS, CV_MAPS }, { ID_DMAPS, V_DMAPS, CV_DMAPS }, { ID_DOORS, V_DOORS, CV_DOORS },
    { ID_ITEMS, V_ITEMS, CV_ITEMS }, { ID_WEAPONS, V_WEAPONS, CV_WEAPONS }, { ID_COLORS, V_COLORS, CV_COLORS },
    { ID_ICONS, V_ICONS, CV_ICONS }, { ID_INITDATA, V_INITDATA, CV_INITDATA }, { ID_GUYS, V_GUYS, CV_GUYS },
    { ID_MIDIS, V_MIDIS, CV_MIDIS }, { ID_CHEATS, V_CHEATS, CV_CHEATS }, { ID_COMBOALIASES, V_COMBOALIASES, CV_COMBOALIASES },
    { ID_LINKSPRITES, V_LINKSPRITES, CV_LINKSPRITES }, { ID_SUBSCREEN, V_SUBSCREEN, CV_SUBSCREEN },
    { ID_ITEMDROPSETS, V_ITEMDROPSETS, CV_ITEMDROPSETS }, { ID_FFSCRIPT, V_FFSCRIPT, CV_FFSCRIPT },
    { ID_SFX, V_SFX, CV_SFX }, { ID_FAVORITES, V_FAVORITES, CV_FAVORITES }
};

int write_own253(PACKFILE *f, const ZqPartContext &ctx)
{
    // Only where the quest's header is 2.53.0/2.53.1 (builds 31-33), or a 2.55 quest whose part
    // is at 2.53's version (2.55 reads those with 2.53's own readers). The same section version
    // in an older quest is read differently, keyed on the header: in a 2.10 quest the 2.53 tile
    // writer's section broke the sections after it on the round trip.
    bool is253 = ctx.zelda_version == 0x250 && ctx.build >= 31 && ctx.build <= 33;

    if(!is253 && ctx.zelda_version != 0x255)
        return ZQW_UNSUPPORTED;

    for(size_t i = 0; i < sizeof(g_own253) / sizeof(g_own253[0]); ++i)
    {
        const Own253 &o = g_own253[i];

        if(o.id != ctx.part->id)
            continue;

        if(ctx.version != o.version || ctx.cversion != o.cversion)
            return ZQW_UNSUPPORTED;

        return zq_write_253_section(f, o.id) ? ZQW_OK : ZQW_UNSUPPORTED;
    }

    return ZQW_UNSUPPORTED;
}

struct RegisterOwn253
{
    RegisterOwn253()
    {
        for(size_t i = 0; i < sizeof(g_own253) / sizeof(g_own253[0]); ++i)
            ZqWriterRegistration(g_own253[i].id, write_own253, "ZQuest's 2.53 writer, at 2.53's own version", 0);
    }
} g_register_own253;

} // namespace

// ---- saving -------------------------------------------------------------------------------

namespace
{

// Puts 'from' in place of 'to'. The old file is set aside first and comes back if the
// rename fails, so 'to' is never left missing.
bool replace_file(const char *from, const char *to)
{
    std::string bak = std::string(to) + ".zqsave.bak";
    remove(bak.c_str());
    FILE *t = fopen(to, "rb");
    bool had = t != NULL;

    if(t)
        fclose(t);

    if(had && rename(to, bak.c_str()) != 0)
        return false;

    if(rename(from, to) != 0)
    {
        if(had)
            rename(bak.c_str(), to);
        return false;
    }

    if(had)
        remove(bak.c_str());

    return true;
}

bool copy_part(PACKFILE *f, const ZqQuestPart &p)
{
    if(!zq_part_is_raw(p.id) && pack_mputl((long)p.id, f) == EOF)
        return false;

    std::vector<byte> chunk(1 << 20);

    for(long long at = 0; at < p.length; at += (long long)chunk.size())
    {
        size_t n = (size_t)(p.length - at < (long long)chunk.size() ? p.length - at : (long long)chunk.size());

        if(!zq_part_read(p, at, &chunk[0], n) || pack_fwrite(&chunk[0], (long)n, f) != (long)n)
            return false;
    }

    return true;
}

} // namespace

namespace
{

// A pre-1.93 quest's header holds what its body is read with (rules, counts, flags), so when
// either changed both are written.
bool flat_pair_changed()
{
    if(zq_quest_file.zelda_version >= 0x193)
        return false;

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];

        if((p.id == ID_HEADER || p.id == 0) && zq_part_changed(p))
            return true;
    }

    return false;
}

std::vector<dword> &every_save_ids()
{
    static std::vector<dword> ids;
    return ids;
}

bool every_save(dword id)
{
    for(size_t i = 0; i < every_save_ids().size(); ++i)
        if(every_save_ids()[i] == id)
            return true;

    return false;
}

bool needs_writing(const ZqQuestPart &p, bool flat_changed)
{
    if(zq_part_changed(p) || (flat_changed && (p.id == ID_HEADER || p.id == 0)))
        return true;

    // Every save, but only where the section's own writer takes the part: ZQuest's 2.53
    // fallback would just rewrite an unchanged section its own way.
    if(!every_save(p.id))
        return false;

    const Entry *e = writer_for(p);
    return e != NULL && e->priority > 0;
}

} // namespace

void zq_own_format_blockers(std::vector<std::string> &names)
{
    names.clear();
    zq_absent_changed(names);       // changed data the version's file has no section for
    bool flat_changed = flat_pair_changed();

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];

        if(!needs_writing(p, flat_changed))
            continue;

        if(!writer_for(p))
        {
            names.push_back(zq_part_name(p));
            continue;
        }

        // the writer takes this version: does it take this data? (a dry run, thrown away)
        PACKFILE *f = zq_open_sink(&g_discard_vtable, NULL);

        if(!f)
            continue;

        zqw_last_error.clear();
        int rc = zq_write_part(f, p);
        pack_fclose(f);

        if(rc != ZQW_OK)
            names.push_back(zq_part_name(p) + (zqw_last_error.empty() ? std::string("") : " (" + zqw_last_error + ")"));
    }
}

int zq_save_own_format(const char *filename, bool encode, std::string &why)
{
    return zq_save_own_format_ex(filename, encode, false, why);
}

int zq_save_own_format_ex(const char *filename, bool encode, bool all_parts, std::string &why)
{
    if(zq_quest_file.parts.empty())
    {
        why = "ZQuest has no copy of this quest's file to start from.";
        return 1;
    }

    if(zq_quest_file.inexact)
    {
        why = "Parts of this quest's file were damaged when it was opened.";
        return 1;
    }

    std::vector<std::string> blockers;

    if(!all_parts)
        zq_own_format_blockers(blockers);

    if(!blockers.empty())
    {
        why = "These changed parts can't be written in this version yet: ";

        for(size_t i = 0; i < blockers.size(); ++i)
            why += (i ? ", " : "") + blockers[i];

        return 2;
    }

    // Everything goes to a temporary file beside the target first, which replaces the target
    // only once the whole save has worked: a save that fails part way leaves the quest's file
    // as it was. Encoded quests are first written decoded (tmpname), then encoded into it.
    bool encoded = encode && zq_quest_file.enc_method >= 0;
    char tmpname[64];
    std::string out_tmp = std::string(filename) + ".zqsave.tmp";
    const char *target = out_tmp.c_str();
    remove(out_tmp.c_str());

    if(encoded)
    {
        temp_name(tmpname);
        target = tmpname;
    }

    PACKFILE *f = pack_fopen_password(target, zq_quest_file.compressed ? F_WRITE_PACKED : F_WRITE,
                                      zq_quest_file.enc_method >= 0 ? datapwd : "");

    if(!f)
    {
        why = "The file couldn't be written.";
        return 3;
    }

    int ret = 0;

    bool flat_changed = !all_parts && flat_pair_changed();

    for(size_t i = 0; i < zq_quest_file.parts.size() && !ret; ++i)
    {
        ZqQuestPart &p = zq_quest_file.parts[i];
        int rc = 0;

        // A changed part is written into the kept copy first, so the copy matches the file
        // from now on, then goes out like any other part.
        bool rewrite = all_parts ? writer_for(p) != NULL : needs_writing(p, flat_changed);
        zqw_last_error.clear();

        if(rewrite && !zq_part_replace(p, zq_write_part, &rc))
        {
            why = "Writing " + zq_part_name(p) + " failed" + (zqw_last_error.empty() ? std::string(".") : ": " + zqw_last_error + ".");
            ret = 5;
        }
        else if(!copy_part(f, p))
        {
            why = "Writing " + zq_part_name(p) + " failed.";
            ret = 4;
        }
    }

    pack_fclose(f);

    if(!ret && encoded)
    {
        int e = encode_file_007(tmpname, out_tmp.c_str(), ((INTERNAL_VERSION + rand()) & 0xffff) + 0x413F0000, ENC_STR, zq_quest_file.enc_method);

        if(e)
        {
            why = "Encoding the file failed.";
            ret = 6;
        }
    }

    if(encoded)
        delete_file(tmpname);

    if(!ret && !replace_file(out_tmp.c_str(), filename))
    {
        why = "The saved file couldn't replace the old one.";
        ret = 7;
    }

    remove(out_tmp.c_str());

    // Only a save that worked is the starting point for the next one. After a failed save the
    // parts the user changed must still count as changed, or the next save would copy their
    // old bytes over the edits.
    if(!ret)
        zq_sections_note_loaded();

    return ret;
}

// ---- the rewrite check ----------------------------------------------------------------

namespace
{

// A write-only file comparing what is written with a part's original bytes (id first).
struct CompareSink
{
    const ZqQuestPart *part;
    long long pos;              // bytes written so far
    long long diff;             // first difference, -1 for none yet
    std::vector<byte> buf;      // the original bytes from buf_at
    long long buf_at;
};

int expected_byte(CompareSink *s, long long at)
{
    long long idlen = zq_part_is_raw(s->part->id) ? 0 : 4;

    if(at < idlen)
        return (int)((s->part->id >> (24 - 8 * at)) & 0xFF);

    long long off = at - idlen;

    if(off >= s->part->length)
        return -1;

    if(off < s->buf_at || off >= s->buf_at + (long long)s->buf.size())
    {
        size_t n = (size_t)(s->part->length - off < 65536 ? s->part->length - off : 65536);
        s->buf.resize(n);

        if(!zq_part_read(*s->part, off, &s->buf[0], n))
            return -1;

        s->buf_at = off;
    }

    return s->buf[(size_t)(off - s->buf_at)];
}

void cs_add(CompareSink *s, byte b)
{
    if(s->diff < 0 && expected_byte(s, s->pos) != b)
        s->diff = s->pos;

    ++s->pos;
}

int cs_fclose(void *) { return 0; }
int cs_getc(void *) { return EOF; }
int cs_ungetc(int, void *) { return EOF; }
long cs_fread(void *, long, void *) { return 0; }
int cs_feof(void *) { return 1; }
int cs_ferror(void *) { return 0; }
int cs_fseek(void *, int) { return -1; }

int cs_putc(int c, void *user)
{
    cs_add((CompareSink *)user, (byte)c);
    return c & 0xFF;
}

long cs_fwrite(const void *p, long n, void *user)
{
    const byte *b = (const byte *)p;

    for(long i = 0; i < n; ++i)
        cs_add((CompareSink *)user, b[i]);

    return n;
}

const PACKFILE_VTABLE g_compare_vtable = { cs_fclose, cs_getc, cs_ungetc, cs_fread, cs_putc, cs_fwrite, cs_fseek, cs_feof, cs_ferror };

} // namespace

int zq_rewrite_check(const char *filename, const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    int written = 0, identical = 0, unsupported = 0;
    fprintf(out, "quest version 0x%x build %d, encoding %d%s\n", zq_quest_file.zelda_version, zq_quest_file.build,
            zq_quest_file.enc_method, zq_quest_file.compressed ? ", compressed" : "");

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        const Entry *e = writer_for(p);

        if(!e)
        {
            ++unsupported;
            fprintf(out, "NO WRITER  %s\n", zq_part_name(p).c_str());
            continue;
        }

        CompareSink sink;
        sink.part = &p;
        sink.pos = 0;
        sink.diff = -1;
        sink.buf_at = 0;
        PACKFILE *f = zq_open_sink(&g_compare_vtable, &sink);
        int rc = f ? zq_write_part(f, p) : 1;

        if(f)
            pack_fclose(f);

        long long expect = (zq_part_is_raw(p.id) ? 0 : 4) + p.length;
        bool same = rc == ZQW_OK && sink.diff < 0 && sink.pos == expect;
        ++written;

        if(same)
        {
            ++identical;
            fprintf(out, "IDENTICAL  %s  (%s)\n", zq_part_name(p).c_str(), e->name);
        }
        else
            fprintf(out, "DIFFERENT  %s  (%s): wrote %lld of %lld bytes, first difference at %lld, result %d\n",
                    zq_part_name(p).c_str(), e->name, sink.pos, expect, sink.diff, rc);
    }

    fprintf(out, "parts %d, written %d, identical %d, no writer %d\n", (int)zq_quest_file.parts.size(), written, identical, unsupported);
    fclose(out);
    return written == identical ? 0 : 1;
}

// ---- the round-trip check -------------------------------------------------------------

int load_quest(const char *filename, bool compressed, bool encrypted);   // zq_class.cpp

int zq_roundtrip_check(const char *filename, const char *report)
{
    FILE *out = fopen(report, "w");

    if(!out)
        return 1;

    // what each part holds now, and which parts the writers will encode
    std::vector<dword> ids;
    std::vector<unsigned long long> before;
    std::vector<int> state;     // 0 compared, 1 its fingerprint failed, 2 nothing to compare
    std::vector<std::string> written;

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        ids.push_back(p.id);
        before.push_back(p.fingerprint);
        state.push_back(!p.has_fingerprint ? 2 : (p.fingerprint_failed ? 1 : 0));

        if(writer_for(p))
            written.push_back(zq_part_name(p));
    }

    char tmpname[64];
    temp_name(tmpname);
    std::string rt = std::string("rt_") + tmpname;
    std::string why;
    zqw_force_serialize = true;
    int r = zq_save_own_format_ex(rt.c_str(), true, true, why);
    zqw_force_serialize = false;
    fprintf(out, "quest version 0x%x build %d: saved with %d parts encoded by writers: %s\n", zq_quest_file.zelda_version,
            zq_quest_file.build, (int)written.size(), r ? why.c_str() : "ok");

    for(size_t i = 0; i < written.size(); ++i)
        fprintf(out, "  encoded %s\n", written[i].c_str());

    int same = 0, different = 0;

    if(r == 0)
    {
        int lr = load_quest(rt.c_str(), true, true);

        if(lr != 0)
        {
            fprintf(out, "RELOAD FAILED: %d\n", lr);
            different = 1;
        }
        else
        {
            for(size_t i = 0; i < ids.size(); ++i)
            {
                const ZqQuestPart *now = NULL;

                for(size_t k = 0; k < zq_quest_file.parts.size() && !now; ++k)
                    if(zq_quest_file.parts[k].id == ids[i])
                        now = &zq_quest_file.parts[k];

                if(state[i] == 2)
                    continue;

                ZqQuestPart probe;
                probe.id = ids[i];
                bool ok = now && state[i] == 0 && now->has_fingerprint && !now->fingerprint_failed && now->fingerprint == before[i];

                if(ok)
                    ++same;
                else
                    ++different;

                fprintf(out, "%s  %s%s\n", ok ? "SAME     " : "DIFFERENT", now ? zq_part_name(*now).c_str() : zq_part_name(probe).c_str(),
                        state[i] == 1 ? " (its fingerprint failed at the first load)" : (now ? "" : " (missing after the reload)"));
            }
        }
    }
    else
        different = 1;

    fprintf(out, "same %d, different %d\n", same, different);
    fclose(out);
    delete_file(rt.c_str());
    return different ? 1 : 0;
}
