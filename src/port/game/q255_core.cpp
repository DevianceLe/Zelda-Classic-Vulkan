// The 2.55 quest loader's section loop. See q255.h.

#include "precompiled.h" //always first

#include <map>
#include <stdio.h>
#include <string.h>

#include "q255.h"
#include "q30.h"
#include "zsys.h"

extern const char *skip_text[];
extern dmap *DMaps;
extern std::vector<mapscr> TheMaps;
extern guydata *guysbuf;
int q30_combo_type(int id);
long q30_item_family(int item);
void q30_slope_audit();
extern MsgStr *MsgStrings;
extern word msg_count;
extern byte quest_rules[QUESTRULES_SIZE];

// The rule section's compat version: every older value switches some old behaviours back on.
dword q255_compat_rule_version = 0;
extern ffscript *ffscripts[512];
extern zinitdata zinit;
extern newcombo *combobuf;
extern ffscript *zasm30_stream;
extern ffscript *globalscripts[];
extern std::string zScript;
extern std::map<int, std::pair<std::string, std::string> > ffcmap;

// The hooks src/qst.cpp calls for a quest made by 2.55 (NULL in ZQuest).
extern int (*q255_header_hook)(PACKFILE *f, zquestheader *header, word s_version, int size, byte *map_count, byte *midi_flags);
extern int (*q255_sections_hook)(PACKFILE *f, zquestheader *header, miscQdata *misc, zctune *tunes, bool keepall, byte *skip_flags);
extern void (*qst_section_hook)(PACKFILE *f, dword id, bool start);

bool q255_keep_for_editor = false;  // see q255.h; the port's ZQuest sets it

PACKFILE *open_quest_file(int *open_error, const char *filename, char *deletefilename, bool compressed, bool encrypted, bool show_progress);
int readheader(PACKFILE *f, zquestheader *Header, bool keepdata);
int readitemdropsets(PACKFILE *f, int version, word build, bool keepdata); // qst.h's declaration doesn't match

namespace
{

struct TileUse
{
    const char *kind;
    long past_limit, past_16bit;
    unsigned long highest;
};

std::vector<TileUse> g_tile_use;

void report_tile_use()
{
    for(size_t i = 0; i < g_tile_use.size(); ++i)
    {
        const TileUse &t = g_tile_use[i];
        al_trace("2.55 tile references, %s: highest %lu, %ld at 65520 or higher, %ld above 65535\n", t.kind, t.highest, t.past_limit, t.past_16bit);
    }

    g_tile_use.clear();
}

struct ReportLine
{
    dword id;
    int version, size, used;
    const char *status;
};

std::vector<ReportLine> g_report;

void note(dword id, int version, int size, int used, const char *status)
{
    ReportLine r;
    r.id = id;
    r.version = version;
    r.size = size;
    r.used = used;
    r.status = status;
    g_report.push_back(r);
    al_trace("2.55 section %c%c%c%c version %d: %s (%d of %d bytes)\n", (char)(id >> 24), (char)(id >> 16), (char)(id >> 8), (char)id,
             version, status, used, size);
}

// ---- a section as a PACKFILE, for the sections whose format hasn't changed ----

struct MemFile
{
    const byte *data;
    size_t size, pos;
    int ungot;
};

int mem_fclose(void *) { return 0; }
int mem_getc(void *u)
{
    MemFile *m = (MemFile *)u;
    if(m->ungot >= 0)
    {
        int c = m->ungot;
        m->ungot = -1;
        return c;
    }
    return m->pos < m->size ? m->data[m->pos++] : EOF;
}
int mem_ungetc(int c, void *u)
{
    ((MemFile *)u)->ungot = c;
    return c;
}
// The packfile callbacks use the system's own long (see zcport/lp64.h).
#pragma push_macro("long")
#undef long

long mem_fread(void *p, long n, void *u)
{
    MemFile *m = (MemFile *)u;
    unsigned char *out = (unsigned char *)p;
    long done = 0;
    while(done < n)
    {
        int c = mem_getc(u);
        if(c == EOF)
            break;
        out[done++] = (unsigned char)c;
    }
    (void)m;
    return done;
}
int mem_putc(int, void *) { return EOF; }
long mem_fwrite(AL_CONST void *, long, void *) { return 0; }

#pragma pop_macro("long")
int mem_fseek(void *u, int offset)
{
    MemFile *m = (MemFile *)u;
    if(m->pos + offset > m->size)
        return -1;
    m->pos += offset;
    return 0;
}
int mem_feof(void *u)
{
    MemFile *m = (MemFile *)u;
    return m->ungot < 0 && m->pos >= m->size;
}
int mem_ferror(void *) { return 0; }

PACKFILE_VTABLE mem_vtable = { mem_fclose, mem_getc, mem_ungetc, mem_fread, mem_putc, mem_fwrite, mem_fseek, mem_feof, mem_ferror };

// Runs one of this engine's own readers on a section whose format matches it.
// They expect the section's version, cversion and size first.
int with_own_reader(dword id, word version, word cversion, const std::vector<byte> &body, zquestheader *header, zctune *tunes, bool keep)
{
    std::vector<byte> whole(8 + body.size());
    whole[0] = (byte)version;
    whole[1] = (byte)(version >> 8);
    whole[2] = (byte)cversion;
    whole[3] = (byte)(cversion >> 8);
    dword n = (dword)body.size();
    whole[4] = (byte)n;
    whole[5] = (byte)(n >> 8);
    whole[6] = (byte)(n >> 16);
    whole[7] = (byte)(n >> 24);
    if(!body.empty())
        memcpy(&whole[8], &body[0], body.size());

    MemFile m;
    m.data = &whole[0];
    m.size = whole.size();
    m.pos = 0;
    m.ungot = -1;
    PACKFILE *f = pack_fopen_vtable(&mem_vtable, &m);
    if(!f)
        return qe_nomem;

    int ret = qe_invalid;
    switch(id)
    {
    case ID_DOORS:        ret = readdoorcombosets(f, header, keep); break;
    case ID_MIDIS:        ret = readtunes(f, header, tunes, keep); break;
    case ID_CHEATS:       ret = readcheatcodes(f, header, keep); break;
    case ID_ITEMDROPSETS: ret = readitemdropsets(f, (int)header->zelda_version, (word)header->build, keep); break;
    case ID_FAVORITES:    ret = readfavorites(f, header->zelda_version, header->build, keep); break;
    }
    pack_fclose(f);
    return ret;
}

// What to do with each section; `own` is the newest version our own reader takes.
typedef int (*Reader)(Q255Section &);

struct SectionKind
{
    dword id;
    Reader reader;
    int own;        // >=0: our own reader handles versions up to this one
    int skip_flag;  // index into skip_flags, -1: always kept
};

const SectionKind kinds[] =
{
    { ID_RULES,        q255_read_rules,        -1, skip_rules },
    { ID_STRINGS,      q255_read_strings,      -1, skip_strings },
    { ID_MISC,         q255_read_misc,         -1, skip_misc },
    { ID_TILES,        q255_read_tiles,        -1, skip_tiles },
    { ID_COMBOS,       q255_read_combos,       -1, skip_combos },
    { ID_COMBOALIASES, q255_read_comboaliases, -1, skip_comboaliases },
    { ID_CSETS,        q255_read_csets,        -1, skip_csets },
    { ID_MAPS,         q255_read_maps,         -1, skip_maps },
    { ID_DMAPS,        q255_read_dmaps,        -1, skip_dmaps },
    { ID_DOORS,        NULL,                    V_DOORS, skip_doors },
    { ID_ITEMS,        q255_read_items,        -1, skip_items },
    { ID_WEAPONS,      q255_read_weapons,      -1, skip_weapons },
    { ID_COLORS,       q255_read_colors,       -1, skip_colors },
    { ID_ICONS,        q255_read_icons,        -1, skip_icons },
    { ID_INITDATA,     q255_read_initdata,     -1, skip_initdata },
    { ID_GUYS,         q255_read_guys,         -1, skip_guys },
    { ID_MIDIS,        q255_read_midis,         V_MIDIS, skip_midis },
    { ID_CHEATS,       NULL,                    V_CHEATS, skip_cheats },
    { ID_LINKSPRITES,  q255_read_linksprites,  -1, skip_linksprites },
    { ID_SUBSCREEN,    q255_read_subscreens,   -1, skip_subscreens },
    { ID_ITEMDROPSETS, NULL,                    V_ITEMDROPSETS, skip_itemdropsets },
    { ID_FAVORITES,    NULL,                    V_FAVORITES, skip_favorites },
    { ID_FFSCRIPT,     q255_read_ffscript,     -1, skip_ffscript },
    { ID_SFX,          q255_read_sfx,          -1, skip_sfx }
};

const dword ID_ZINFO_255 = ZC_ID('Z','I','N','F'); // editor names: nothing the player uses
const dword ID_ADVMUSIC_30 = ZC_ID('A','M','U','S'); // 3.0's music objects: nothing here plays them yet

// Reads every section after the header. With keepall false nothing is stored.
int read_sections(PACKFILE *f, zquestheader *header, miscQdata *misc, zctune *tunes, bool keepall, byte *skip_flags)
{
    g_report.clear();
    g_tile_use.clear();
    q255_compat_rule_version = 0;   // an old rules section has none, and must not see the last quest's
    dword id;
    int result = 0;

    while(p_mgetl(&id, f, true))
    {
        // A packed file can't always tell it has ended until a read goes past the end, so the
        // id read after the last section may be that failed read. Upstream's loader stops on
        // the end of the file after reading an id, the same as here; without this, a valid
        // file whose unpacked length falls that way failed to open.
        if(pack_feof(f))
            break;

        word version = 0, cversion = 0;
        dword size = 0;

        // ZQuest keeps each section's original bytes (see qst_section_hook in qst.cpp)
        struct HookScope
        {
            PACKFILE *f;
            dword id;
            HookScope(PACKFILE *pf, dword i) : f(pf), id(i) { if(qst_section_hook) qst_section_hook(f, id, true); }
            ~HookScope() { if(qst_section_hook) qst_section_hook(f, id, false); }
        } hook_scope(f, id);

        if(!p_igetw(&version, f, true) || !p_igetw(&cversion, f, true))
            return qe_invalid;

        // two sections grew a field between the versions and the size
        if(id == ID_RULES && version > 16)
        {
            dword extra;
            if(!p_igetl(&extra, f, true))
                return qe_invalid;

            q255_compat_rule_version = extra;
        }
        else if(id == ID_FFSCRIPT && version >= 18)
        {
            word extra;
            if(!p_igetw(&extra, f, true))
                return qe_invalid;
        }

        if(!p_igetl(&size, f, true))
            return qe_invalid;

        std::vector<byte> body(size);
        if(size && !pfread(&body[0], size, f, true))
            return qe_invalid;

        const SectionKind *kind = NULL;
        for(size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
            if(kinds[i].id == id)
                kind = &kinds[i];

        if(id == ID_ADVMUSIC_30)
        {
            Q255Section music;
            music.data = size ? &body[0] : NULL;
            music.size = size;
            music.version = version;
            music.cversion = cversion;
            music.keep = keepall;
            int ret = q30_read_advmusic(music);
            if(ret || music.failed || music.pos != size)
            {
                note(id, version, (int)size, (int)music.pos, "FAILED");
                result = ret ? ret : qe_invalid;
            }
            else
                note(id, version, (int)size, (int)size, "ok");
            continue;
        }

        if(!kind)
        {
            note(id, version, (int)size, (int)size, id == ID_ZINFO_255 ? "skipped (editor names only)" : (id == ID_ADVMUSIC_30 ? "skipped (3.0 music objects)" : "UNKNOWN, skipped"));
            continue;
        }

        bool keep = keepall && !(skip_flags && kind->skip_flag >= 0 && get_bit(skip_flags, kind->skip_flag));

        // Editor-only data, and the player's arrays are 2.53's size: newer quests can list more
        // favorites than they hold, and readfavorites doesn't check.
        if(id == ID_FAVORITES)
        {
            note(id, version, (int)size, (int)size, "skipped (editor favorites only)");
            continue;
        }

        if(kind->own >= 0 && version > kind->own && !kind->reader)
        {
            note(id, version, (int)size, 0, "NEWER than our reader");
            result = qe_version;
            continue;
        }

        if(kind->own >= 0 && version <= kind->own)
        {
            int ret = with_own_reader(id, version, cversion, body, header, tunes, keep);
            note(id, version, (int)size, (int)size, ret ? "FAILED in our reader" : "ok (our reader)");
            if(ret)
                result = ret;
            continue;
        }

        Q255Section s;
        s.data = size ? &body[0] : NULL;
        s.size = size;
        s.version = version;
        s.cversion = cversion;
        s.keep = keep;
        s.header = header;
        s.misc = misc;
        s.tunes = tunes;

        int ret = kind->reader(s);

        if(ret == Q255_NOT_HANDLED)
        {
            note(id, version, (int)size, 0, "NOT HANDLED yet");
            result = qe_version;
        }
        else if(ret || s.failed)
        {
            note(id, version, (int)size, (int)s.pos, "FAILED");
            result = ret ? ret : qe_invalid;
        }
        else if(s.pos != size)
        {
            note(id, version, (int)size, (int)s.pos, "WRONG SIZE");
            result = qe_invalid;
        }
        else
            note(id, version, (int)size, (int)s.pos, "ok");
    }

    return result;
}

// ZC_Q255_DUMP="dmap,screen": logs what a screen holds, and any string containing ZC_Q255_DUMP_TEXT.
void dump_loaded_data()
{
    const char *spec = getenv("ZC_Q255_DUMP");
    if(!spec)
        return;

    int dm = 0, scr = 0;
    sscanf(spec, "%d,%d", &dm, &scr);
    const dmap &d = DMaps[dm];
    int index = d.map * MAPSCRS + scr + d.xoff;
    al_trace("dump: dmap %d map %d xoff %d flags %lx -> screen index %d\n", dm, d.map, d.xoff, (unsigned long)d.flags, index);

    if(index >= 0 && index < (int)TheMaps.size())
    {
        const mapscr &m = TheMaps[index];
        al_trace("dump: str %d room %d timedwarptics %d valid %x flags %x %x %x %x %x %x %x %x %x %x\n", m.str, m.room, m.timedwarptics, m.valid,
                 m.flags, m.flags2, m.flags3, m.flags4, m.flags5, m.flags6, m.flags7, m.flags8, m.flags9, m.flags10);
        for(int i = 0; i < 4; ++i)
            al_trace("dump: warp %d tile dmap %d scr %d type %d | side dmap %d scr %d type %d\n", i, m.tilewarpdmap[i], m.tilewarpscr[i], m.tilewarptype[i],
                     m.sidewarpdmap[i], m.sidewarpscr[i], m.sidewarptype[i]);
        al_trace("dump: sidewarpindex %d\n", m.sidewarpindex);
        for(int i = 0; i < 32; ++i)
            if(m.ffscript[i] || m.ffdata[i])
                al_trace("dump: ffc %d data %d script %d flags %lx x %ld y %ld vx %ld vy %ld ax %ld ay %ld delay %d link %d combo type %d\n", i, m.ffdata[i], m.ffscript[i], (unsigned long)m.ffflags[i],
                         (long)m.ffx[i], (long)m.ffy[i], (long)m.ffxdelta[i], (long)m.ffydelta[i], (long)m.ffxdelta2[i], (long)m.ffydelta2[i], m.ffdelay[i], m.fflink[i], combobuf[m.ffdata[i]].type);

        {
            std::map<int, int> kinds;
            for(int i = 0; i < 176; ++i)
                ++kinds[combobuf[m.data[i]].type];
            for(std::map<int, int>::const_iterator it = kinds.begin(); it != kinds.end(); ++it)
                al_trace("dump: combo type %d on %d squares\n", it->first, it->second);
        }

        int s = m.str;
        for(int guard = 0; s > 0 && guard < 40; ++guard)
        {
            const MsgStr &t = MsgStrings[s];
            al_trace("dump: string %d next %d flags %x w %d h %d len %d: %.24s\n", s, t.nextstring, t.stringflags, t.w, t.h, (int)strlen(t.s), t.s);
            s = t.nextstring;
        }
    }

    const char *text = getenv("ZC_Q255_DUMP_TEXT");
    for(int i = 0; text && i < msg_count; ++i)
    {
        if(!strstr(MsgStrings[i].s, text))
            continue;
        const MsgStr &t = MsgStrings[i];
        char hex[3 * MSGSIZE + 8];
        hex[0] = 0;
        for(int j = 0; t.s[j] && j < MSGSIZE; ++j)
            sprintf(hex + strlen(hex), "%02x ", (unsigned char)t.s[j]);
        al_trace("dump: string %d next %d flags %x len %d: %s\n  %s\n", i, t.nextstring, t.stringflags, (int)strlen(t.s), t.s, hex);
    }

    const char *strs = getenv("ZC_Q255_DUMP_STRS");
    if(strs)
    {
        int lo = 0, hi = 0;
        sscanf(strs, "%d,%d", &lo, &hi);
        al_trace("dump: start dmap %d, zinit msg speed %d\n", zinit.start_dmap, zinit.msg_speed);
        for(int i = 0; i < (int)TheMaps.size(); ++i)
        {
            const mapscr &m = TheMaps[i];
            if(!(m.valid & mVALID) || m.str < lo || m.str > hi)
                continue;
            char ffcs[120];
            ffcs[0] = 0;
            for(int k = 0; k < 32; ++k)
                if(m.ffscript[k] && strlen(ffcs) < 100)
                    sprintf(ffcs + strlen(ffcs), " %d", m.ffscript[k]);
            al_trace("strscr map %d scr %d: str %d timed %d flags3 %x side0 dmap %d scr %d type %d | ffc%s\n", i / MAPSCRS, i % MAPSCRS, m.str, m.timedwarptics, m.flags3,
                     m.sidewarpdmap[0], m.sidewarpscr[0], m.sidewarptype[0], ffcs);
        }
    }

    const char *chain = getenv("ZC_Q255_DUMP_CHAIN");
    for(int i = 0; chain && i < atoi(chain); ++i)
    {
        int at = d.map * MAPSCRS + i + d.xoff;
        if(at < 0 || at >= (int)TheMaps.size())
            break;
        const mapscr &m = TheMaps[at];
        char ffcs[120];
        ffcs[0] = 0;
        for(int k = 0; k < 32; ++k)
            if(m.ffscript[k] && strlen(ffcs) < 100)
                sprintf(ffcs + strlen(ffcs), " %d", m.ffscript[k]);
        al_trace("chain scr %d: valid %x str %d timed %d flags3 %x flags4 %x flags5 %x side0 dmap %d scr %d type %d | side1 dmap %d scr %d | ffc%s\n",
                 i, m.valid, m.str, m.timedwarptics, m.flags3, m.flags4, m.flags5, m.sidewarpdmap[0], m.sidewarpscr[0], m.sidewarptype[0],
                 m.sidewarpdmap[1], m.sidewarpscr[1], ffcs);
    }

    const char *source_path = getenv("ZC_Q255_DUMP_SOURCE");
    if(source_path)
    {
        FILE *out = fopen(source_path, "wb");
        if(out)
        {
            fwrite(zScript.data(), 1, zScript.size(), out);
            for(std::map<int, std::pair<std::string, std::string> >::const_iterator it = ffcmap.begin(); it != ffcmap.end(); ++it)
                fprintf(out, "\n//ffc slot %d: %s\n", it->first, it->second.second.c_str());
            fclose(out);
        }
    }

    // ZC_Q255_DUMP_USE: what the quest's screens actually place, to rank what is worth porting
    if(getenv("ZC_Q255_DUMP_USE"))
    {
        q30_slope_audit();
        std::map<long, int> combos, ffcs, enemies, items;

        for(size_t n = 0; n < TheMaps.size(); ++n)
        {
            const mapscr &m = TheMaps[n];

            if(!(m.valid & mVALID))
                continue;

            for(size_t p = 0; p < m.data.size(); ++p)
                ++combos[q30_combo_type(m.data[p])];

            for(int i = 0; i < 32; ++i)
                if(m.ffdata[i])
                    ++ffcs[q30_combo_type(m.ffdata[i])];

            for(int i = 0; i < 10; ++i)
                if(m.enemy[i])
                    ++enemies[guysbuf[m.enemy[i]].family];

            if(m.hasitem)
                ++items[q30_item_family(m.item)];
        }

        for(std::map<long, int>::iterator it = combos.begin(); it != combos.end(); ++it)
            al_trace("use combo %ld %d\n", it->first, it->second);

        for(std::map<long, int>::iterator it = ffcs.begin(); it != ffcs.end(); ++it)
            al_trace("use ffc %ld %d\n", it->first, it->second);

        for(std::map<long, int>::iterator it = enemies.begin(); it != enemies.end(); ++it)
            al_trace("use enemy %ld %d\n", it->first, it->second);

        for(std::map<long, int>::iterator it = items.begin(); it != items.end(); ++it)
            al_trace("use item %ld %d\n", it->first, it->second);

        // the starting items' families, and the DMaps that make Link a bunny without the
        // Moon Pearl (upstream's dmfBUNNYIFNOPEARL) or continue at a mirror (dmfMIRRORCONTINUE)
        for(int i = 0; i < MAXITEMS; ++i)
            if(zinit.items[i])
                al_trace("use startitem %ld\n", q30_item_family(i));

        for(int i = 0; i < MAXDMAPS; ++i)
        {
            if(DMaps[i].flags & 0x100000)
                al_trace("use dmapflag bunny %d\n", i);
            if(DMaps[i].flags & 0x200000)
                al_trace("use dmapflag mirrorcontinue %d\n", i);
            if(q30_dmap_mirror(i) >= 0)
                al_trace("use dmapmirror %d -> %d\n", i, q30_dmap_mirror(i));
        }

        for(int i = 0; i < MAXITEMS; ++i)   // the Moon Pearl, bug net and Magic Mirror items
        {
            const long fam = q30_item_family(i);
            if(fam == 269 || fam == 272 || fam == 273)
                al_trace("use itemid %d family %ld level %d flags 0x%x misc1 %ld misc2 %ld wpn %d\n", i, fam, (int)itemsbuf[i].fam_type,
                         (unsigned)itemsbuf[i].flags, (long)itemsbuf[i].misc1, (long)itemsbuf[i].misc2, (int)itemsbuf[i].wpn);
        }
    }

    if(getenv("ZC_Q255_DUMP_SUBS"))
    {
        for(int n = 0; n < MAXCUSTOMSUBSCREENS; ++n)
        {
            if(!custom_subscreen[n].objects[0].type)
                continue;

            al_trace("sub %d type %d name %s\n", n, custom_subscreen[n].ss_type, custom_subscreen[n].name);

            for(int m = 0; m < MAXSUBSCREENITEMS && custom_subscreen[n].objects[m].type; ++m)
            {
                const subscreen_object &o = custom_subscreen[n].objects[m];
                al_trace("  obj %d type %d pos %d x %d y %d w %d h %d c1 %d/%d c2 %d/%d c3 %d/%d d1 %d d2 %d d8 %d\n", m, o.type, o.pos, o.x, o.y, o.w, o.h,
                         o.colortype1, o.color1, o.colortype2, o.color2, o.colortype3, o.color3, (int)o.d1, (int)o.d2, (int)o.d8);
            }
        }
    }

    const char *global_slot = getenv("ZC_Q255_DUMP_GLOBAL");
    if(global_slot && atoi(global_slot) >= 0 && atoi(global_slot) < 4 && globalscripts[atoi(global_slot)])
    {
        const ffscript *code = globalscripts[atoi(global_slot)];
        for(int i = 0; code[i].command != 0xFFFF && i < 20000; ++i)
            al_trace("zasm %d %d %ld %ld\n", i, code[i].command, code[i].arg1, code[i].arg2);
    }

    const char *ffc = getenv("ZC_Q255_DUMP_FFC");
    if(ffc && atoi(ffc) >= 0 && atoi(ffc) < 512 && ffscripts[atoi(ffc)])
    {
        const ffscript *code = ffscripts[atoi(ffc)];
        if(code[0].command == 0xFFFE && zasm30_stream) // 3.0: a stub into the shared stream
        {
            al_trace("zasm starts at %ld of the stream\n", code[0].arg1);
            code = zasm30_stream + code[0].arg1;
        }
        for(int i = 0; code[i].command != 0xFFFF && i < 4000; ++i)
            al_trace("zasm %d %d %ld %ld\n", i, code[i].command, code[i].arg1, code[i].arg2);
    }

    al_trace("dump: rules MSGDISAPPEAR %d MSGFREEZE %d\n", get_bit(quest_rules, qr_MSGDISAPPEAR) ? 1 : 0, get_bit(quest_rules, qr_MSGFREEZE) ? 1 : 0);
}

int header_hook(PACKFILE *f, zquestheader *header, word s_version, int size, byte *map_count, byte *midi_flags)
{
    if(size < 0)
        return qe_invalid;
    std::vector<byte> body((size_t)size);
    if(size && !pfread(&body[0], size, f, true))
        return qe_invalid;

    Q255Section s;
    s.data = size ? &body[0] : NULL;
    s.size = (size_t)size;
    s.version = s_version;
    s.keep = true;
    s.header = header;

    int ret = q255_read_header(s, map_count, midi_flags);
    if(ret == Q255_NOT_HANDLED)
        return qe_version;
    if(ret || s.failed)
        return ret ? ret : qe_invalid;
    if(s.pos != s.size)
    {
        al_trace("2.55 header: used %d of %d bytes\n", (int)s.pos, size);
        return qe_invalid;
    }
    return 0;
}

int read_sections_and_dump(PACKFILE *f, zquestheader *header, miscQdata *misc, zctune *tunes, bool keepall, byte *skip_flags)
{
    int ret = read_sections(f, header, misc, tunes, keepall, skip_flags);
    report_tile_use();
    if(ret == 0 && keepall)
        dump_loaded_data();
    return ret;
}

// Both programs build this file. ZQuest's readers fill its own store
// (src/port/zqeditor/q255_store_zq.cpp), and it doesn't save these quests yet.
struct RegisterHooks
{
    RegisterHooks()
    {
        q255_header_hook = header_hook;
        q255_sections_hook = read_sections_and_dump;
    }
} register_hooks;

} // namespace

void q255_note_tile(const char *kind, unsigned long tile)
{
    TileUse *use = NULL;

    for(size_t i = 0; i < g_tile_use.size(); ++i)
        if(!strcmp(g_tile_use[i].kind, kind))
            use = &g_tile_use[i];

    if(!use)
    {
        TileUse fresh = { kind, 0, 0, 0 };
        g_tile_use.push_back(fresh);
        use = &g_tile_use.back();
    }

    if(tile > use->highest)
        use->highest = tile;

    if(tile >= 65520)
        ++use->past_limit;

    if(tile > 65535)
        ++use->past_16bit;
}

int q255_check_quest(const char *filename, const char *report)
{
    int open_error = 0;
    char deletefilename[1024];
    deletefilename[0] = 0;
    PACKFILE *f = open_quest_file(&open_error, filename, deletefilename, true, true, false);
    if(!f)
        return open_error ? open_error : qe_invalid;

    zquestheader header;
    memset(&header, 0, sizeof(header));
    int ret = readheader(f, &header, true); // the readers need the header filled in; the check exits afterwards
    int sections = -1;
    if(ret == 0)
    {
        sections = read_sections(f, &header, NULL, NULL, false, NULL);
        report_tile_use();
    }
    pack_fclose(f);
    if(deletefilename[0])
        delete_file(deletefilename);

    FILE *out = fopen(report, "w");
    if(out)
    {
        fprintf(out, "header: %d  sections: %d  version 0x%x build %d\n", ret, sections, header.zelda_version, header.build);
        for(size_t i = 0; i < g_report.size(); ++i)
        {
            const ReportLine &r = g_report[i];
            fprintf(out, "%c%c%c%c  v%-3d %10d bytes  used %10d  %s\n", (char)(r.id >> 24), (char)(r.id >> 16), (char)(r.id >> 8), (char)r.id,
                    r.version, r.size, r.used, r.status);
        }
        fclose(out);
    }
    return ret ? ret : (sections > 0 ? sections : 0);
}
