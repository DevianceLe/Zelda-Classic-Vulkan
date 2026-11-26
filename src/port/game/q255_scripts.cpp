// FFSC (scripts) section of a 2.55 or 3.0 quest, section versions 6 to 31.
//
// From v27 (3.0) every script is a start and end position in one combined
// stream of commands. See ZASM30_ENTER in src/ffscript.cpp for how it runs.
// From v21 commands can carry a text or number literal; from v24 a third
// argument, which only commands that do nothing here make use of.
//
// In order: ffc, item, guy, weapon, screen, global and link scripts, then by
// section version lweapon, eweapon, dmap, item sprite, combo and generic scripts,
// then the ZScript source and the slot-name tables. Every script is a command
// count, a zasm_meta from v16, and the commands up to a 0xFFFF terminator.
//
// Read and dropped, because 2.53 has no field or slot for them:
//   zasm_meta of every script (v16+): zasm_v, meta_v, ffscript_v, script type
//   zasm_meta: run identifiers and types, flags, compiler version numbers
//   zasm_meta: script name, author, script info and setup text
//   zasm_meta: attribute labels and help, usrflag labels and help
//   zasm_meta: InitD labels, help and types
//   global slots 4 to 7: onLaunch, onContGame, F6, onSave (slots 4 to 6 from v14, 7 from v17)
//   link slots 3 and 4 (v11+)
//   lweapon scripts (v10+), eweapon and dmap scripts (v9+)
//   item sprite scripts (v12+), combo scripts (v15+), generic scripts (v20)
//   name tables for npc, lweapon, eweapon, hero, dmap, screen scripts (v9+)
//   name tables for item sprite (v12+), combo (v15+), generic (v20) scripts
//   rules upstream sets on load, 2.53 has neither: qr_SPRITE_JUMP_IS_TRUNCATED
//     (v < 12 on a 2.55 quest), qr_FLUCTUATING_ENEMY_JUMP (v < 19)
//
// Stored as they are, including opcodes 2.53 doesn't have: its interpreter logs
// those and carries on with the next instruction.

#include "precompiled.h" //always first

#include "q255.h"

#include <map>
#include <new>
#include <string>
#include <string.h>
#include <utility>
#include <vector>

#include "ffscript.h" // last: its register names are macros
#include "q30_scripts.h"

ffscript *q30_global_scripts[Q30_GLOBAL_SLOTS];
ffscript *q30_hero_scripts[Q30_HERO_SLOTS];
ffscript *q30_lwpn_scripts[Q30_LWPN_SLOTS];
ffscript *q30_ewpn_scripts[Q30_EWPN_SLOTS];
ffscript *q30_dmap_scripts[Q30_DMAP_SLOTS];
ffscript *q30_itemsprite_scripts[Q30_ITEMSPRITE_SLOTS];
ffscript *q30_combo_scripts[Q30_COMBO_SLOTS];
ffscript *q30_generic_scripts[Q30_GENERIC_SLOTS];
ffscript *q30_subscreen_scripts[Q30_SUBSCREEN_SLOTS];
std::map<std::string, int> q30_script_names[q30_names_count];

static void free_table(ffscript **t, int n)
{
    for(int i = 0; i < n; ++i)
    {
        delete [] t[i];
        t[i] = NULL;
    }
}

// How many commands each script read here holds, its end marker included: the engine ends a
// script whose position leaves them (a jump or return a compiler never makes, or a damaged quest),
// where it used to run the memory past the script. zasm30_stream_len is the v27+ stream's.
long zasm30_stream_len = 0;
static std::map<const ffscript *, long> g_script_lengths;

long q30_script_length(const ffscript *code)
{
    std::map<const ffscript *, long>::const_iterator it = g_script_lengths.find(code);
    return it == g_script_lengths.end() ? 0 : it->second;
}

void q30_script_tables_reset()
{
    free_table(q30_global_scripts, Q30_GLOBAL_SLOTS);
    free_table(q30_hero_scripts, Q30_HERO_SLOTS);
    free_table(q30_lwpn_scripts, Q30_LWPN_SLOTS);
    free_table(q30_ewpn_scripts, Q30_EWPN_SLOTS);
    free_table(q30_dmap_scripts, Q30_DMAP_SLOTS);
    free_table(q30_itemsprite_scripts, Q30_ITEMSPRITE_SLOTS);
    free_table(q30_combo_scripts, Q30_COMBO_SLOTS);
    free_table(q30_generic_scripts, Q30_GENERIC_SLOTS);
    free_table(q30_subscreen_scripts, Q30_SUBSCREEN_SLOTS);

    for(int i = 0; i < q30_names_count; ++i)
        q30_script_names[i].clear();
}

extern long zasm30_ffscript_version;
// upstream's ZScriptVersion: the newest ffscript_v the quest's scripts carry in their meta (a 0
// there counts as 16), not the section's version; it decides gc, FFC refs as sprite ids and
// arrays as objects (ZScriptVersion::gc / ffcRefIsSpriteId / gc_arrays). 0 for a 2.53 quest.
long zasm30_zscript_version = 0;
static word g_meta_ffscript_v = 0;     // the last meta read
static long g_zscript_max = -1;        // the section's newest so far (-1: none)

// A script that was read and kept: its meta's ffscript_v counts (upstream's read_scripts).
static void note_zscript_version()
{
    const long v = g_meta_ffscript_v ? (long)g_meta_ffscript_v : 16;

    if(v > g_zscript_max)
        g_zscript_max = v;
}
extern ffscript *ffscripts[512];
extern ffscript *itemscripts[256];
extern ffscript *guyscripts[256];
extern ffscript *wpnscripts[256];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern ffscript *linkscripts[3];
extern ffscript *screenscripts[256];

extern std::string zScript;
extern std::map<int, std::pair<std::string, std::string> > ffcmap;
extern std::map<int, std::pair<std::string, std::string> > globalmap;
extern std::map<int, std::pair<std::string, std::string> > itemmap;

void setZScriptVersion(int);

struct Zasm30Literal
{
    std::string str;
    std::vector<long> vec;
};

extern ffscript *zasm30_stream;
extern std::map<const ffscript *, Zasm30Literal> zasm30_literals;
extern std::vector<std::map<int, long> > zasm30_inst_inits;
void zasm30_reset();

static const word ZASM30_ENTER_COMMAND = 0xFFFE;

// ZC_Q30_ZASM_DUMP=path: every command and script position, for looking at what a quest uses.
static FILE *g_zasm_dump = NULL;

// Upstream's limits; anything beyond them it refuses as corrupt.
static const long MAX_COMMANDS = 10000000;
static const long MAX_STREAM_COMMANDS = 25000000;
static const long MAX_SOURCE_BYTES = 10 * 1024 * 1024;
static const long MAX_BINDING_BYTES = 1024;
static const word MAX_META_ATTRIBUTES = 24;

// The newest meta layout upstream knows (METADATA_V).
static const word NEWEST_META = 7;

enum
{
    kind_ffc, kind_item, kind_guy, kind_weapon, kind_screen, kind_global, kind_link,
    kind_lweapon, kind_eweapon, kind_dmap, kind_itemsprite, kind_combo, kind_generic,
    kind_count
};

static const char *const kind_name[kind_count] =
{
    "ffc", "item", "guy", "weapon", "screen", "global", "link",
    "lweapon", "eweapon", "dmap", "item sprite", "combo", "generic"
};

// What the trace at the end of the section reports.
static long g_instructions;
static long g_unknown_opcodes;
static long g_unknown_opcodes_kept;
static long g_opcode_uses[2048];
static long g_slots[kind_count];
static long g_dropped[kind_count];
static long g_dropped_with_code[kind_count];

static void reset_counters()
{
    g_instructions = 0;
    g_unknown_opcodes = 0;
    g_unknown_opcodes_kept = 0;
    memset(g_opcode_uses, 0, sizeof(g_opcode_uses));
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_dropped, 0, sizeof(g_dropped));
    memset(g_dropped_with_code, 0, sizeof(g_dropped_with_code));
}

static void trace_counters()
{
    al_trace("2.55 scripts: %ld instructions, %ld use an opcode >= %d (2.53's NUMCOMMANDS), %ld of those in slots 2.53 has\n",
             g_instructions, g_unknown_opcodes, (int)NUMCOMMANDS, g_unknown_opcodes_kept);

    for(int op = 0; op < 2048; ++op)
    {
        if(g_opcode_uses[op])
            al_trace("2.55 scripts: opcode %d used %ld times\n", op, g_opcode_uses[op]);
    }

    for(int i = 0; i < kind_count; ++i)
    {
        if(g_slots[i])
            al_trace("2.55 scripts: %s: %ld slots, %ld dropped, %ld of those with code\n", kind_name[i],
                     g_slots[i], g_dropped[i], g_dropped_with_code[i]);
    }
}

static void skip_cstr(Q255Section &s)
{
    byte n = s.getc();
    s.skip(n);
}

static void skip_wstr(Q255Section &s)
{
    word n = s.igetw();
    s.skip(n);
}

static void skip_label_and_help(Q255Section &s, int count)
{
    for(int i = 0; i < count && !s.failed; ++i)
    {
        skip_cstr(s);
        skip_wstr(s);
    }
}

// ZQuest's 3.0 builds (q255_ffscript_meta_spans): where each meta's meta_v word and its layout 7
// fields (script info and setup) are, when set
static std::vector<size_t> *g_meta_v_at = NULL;
static std::vector<std::pair<size_t, size_t> > *g_meta7_spans = NULL;
static std::vector<std::pair<size_t, size_t> > *g_meta6_attrs = NULL;  // layout 6's attribute label list
static std::vector<size_t> *g_arg3_at = NULL;   // each command's third argument (before version 27)

static int skip_meta(Q255Section &s, word meta)
{
    if(g_meta_v_at)
        g_meta_v_at->push_back(s.pos + 2);

    s.skip(2 + 2);         // zasm_v, meta_v
    g_meta_ffscript_v = s.igetw();
    s.skip(1);             // script type

    for(int i = 0; i < 8 && !s.failed; ++i) // run identifiers
    {
        if(meta < 3)
            s.skip(33);
        else
            skip_cstr(s);
    }

    s.skip(8 + 1 + 4 * 2); // run types, flags, compiler version

    if(meta == 2)
        s.skip(33 + 33); // name, author
    else if(meta > 2)
    {
        skip_cstr(s); // name
        skip_cstr(s); // author

        if(meta >= 7)
        {
            const size_t at = s.pos;
            skip_wstr(s); // script info
            skip_wstr(s); // script setup

            if(g_meta7_spans)
                g_meta7_spans->push_back(std::make_pair(at, s.pos));
        }

        if(meta < 6)
        {
            skip_label_and_help(s, meta < 5 ? 4 : 10);
            skip_label_and_help(s, 8);
            skip_label_and_help(s, 8);
        }
        else
        {
            const size_t at = s.pos;
            word count = s.igetw();
            if(count > MAX_META_ATTRIBUTES)
                return qe_invalid;
            skip_label_and_help(s, count);

            if(g_meta6_attrs)
                g_meta6_attrs->push_back(std::make_pair(at, s.pos));
        }

        skip_label_and_help(s, 16); // usrflags
    }

    if(meta > 3)
    {
        skip_label_and_help(s, 8); // InitD labels and help
        s.skip(8);                 // InitD types
    }

    return s.failed ? qe_invalid : 0;
}

// A jump outside the script (upstream 3.0's zasm_find_invalid_jump: GOTO, GOTOTRUE, GOTOFALSE,
// GOTOLESS, GOTOMORE, GOTOCMP, CALLFUNC, LOOP to a pc outside the commands and their terminator)
// disables the whole script, as upstream's read_old_ffscript does ("the script would have ended at
// the jump anyway, with an error"): it never runs, like an empty slot (the player only).
static void fix_script_jumps(ffscript *script, long used, int index)
{
    long len = used + 1;

    for(long j = 0; j < used; ++j)
    {
        switch(script[j].command)
        {
        case GOTO:
        case GOTOTRUE:
        case GOTOFALSE:
        case GOTOLESS:
        case GOTOMORE:
        case LOOP:
        case 1105:  // CALLFUNC
        case 1108:  // GOTOCMP
            if(script[j].arg1 < 0 || script[j].arg1 >= len)
            {
#ifdef ZC_PLAYER_BUILD
                al_trace("2.55 script %d: jump at command %ld targets %ld, outside the script (length %ld); the script is disabled\n",
                         index, j, script[j].arg1, len);
                script[0].command = 0xFFFF;
                return;
#else
                script[j].arg1 = len - 1;   // (ZQuest keeps the script; its writers save what was read)
#endif
            }
            break;

        default:
            break;
        }
    }
}

static long g_dump_pc = 0;

static void skip_or_keep_literal(Q255Section &s, const ffscript *where, bool keep)
{
    Zasm30Literal lit;
    dword sz = s.igetl();

    if(sz)
        lit.str = s.getstr(sz);

    dword count = s.igetl();

    if(s.failed || count > s.left() / 4)
    {
        s.failed = true;
        return;
    }

    for(dword i = 0; i < count; ++i)
    {
        long v = s.igetl_s();
        if(keep)
            lit.vec.push_back(v);
    }

    if(keep && where && (sz || count))
    {
        zasm30_literals[where] = lit;

        if(g_zasm_dump && !lit.vec.empty())
        {
            fprintf(g_zasm_dump, "vec");
            for(size_t i = 0; i < lit.vec.size(); ++i)
                fprintf(g_zasm_dump, " %ld", lit.vec[i]);
            fprintf(g_zasm_dump, "\n");
        }
    }
}

// How many commands the shared stream holds, with the end command 3.0 adds when the last one
// isn't one (432d9699e): where a script may start and end.
static dword g_stream_size = 0;

// v27+: every command of every script, in one list.
static int read_stream(Q255Section &s)
{
    long count = s.igetl_s();
    word last_command = 0xFFFF;
    g_stream_size = 0;

    if(s.failed || count < 0 || count > MAX_STREAM_COMMANDS)
        return qe_invalid;

    ffscript *code = NULL;

    if(s.keep)
    {
        zasm30_stream_len = 0;
        zasm30_reset();
        code = new (std::nothrow) ffscript[count + 1];
        if(!code)
            return qe_nomem;
        zasm30_stream = code;
    }

    for(long j = 0; j < count; ++j)
    {
        word command = s.igetw();
        long arg1 = s.igetl_s(), arg2 = s.igetl_s();
        long arg3 = s.igetl_s();

        if(s.failed)
            return qe_invalid;

        last_command = command;
        ++g_instructions;

        if(command >= NUMCOMMANDS)
        {
            if(command < 2048)
                ++g_opcode_uses[command];
            ++g_unknown_opcodes;
        }

        if(code)
        {
            code[j].command = command;
            code[j].arg1 = arg1;
            code[j].arg2 = arg2;
            code[j].ptr = NULL;
        }

        if(g_zasm_dump && s.keep)
            fprintf(g_zasm_dump, "zasm %ld %d %ld %ld\n", j, (int)command, arg1, arg2);

        skip_or_keep_literal(s, code ? &code[j] : NULL, s.keep);

        // STACKWRITEATVV_IF needs its third argument (the condition), LOAD_INTERNAL_ARRAY_REF its
        // ref register
        if(code && (command == 1115 || command == 1152))
            zasm30_literals[&code[j]].vec.assign(1, arg3);
    }

    if(code)
    {
        code[count].command = 0xFFFF;
        code[count].arg1 = code[count].arg2 = 0;
        code[count].ptr = NULL;
        zasm30_stream_len = count + 1;

        // ZC_Q30_ZASM_POKE="pc:command:arg1:arg2[:v,v,...];..." replaces commands of the stream as
        // it is read, with a table of values if given (diagnostics only: hand-written ZASM that no
        // compiler makes, to see what the engine does with it).
        if(const char *poke = getenv("ZC_Q30_ZASM_POKE"))
        {
            for(const char *c = poke; *c; )
            {
                long pc = -1, cmd = 0, a1 = 0, a2 = 0;
                int used = 0;

                if(sscanf(c, "%ld:%ld:%ld:%ld%n", &pc, &cmd, &a1, &a2, &used) == 4 && pc >= 0 && pc < count)
                {
                    code[pc].command = (word)cmd;
                    code[pc].arg1 = a1;
                    code[pc].arg2 = a2;
                    zasm30_literals.erase(&code[pc]);
                    c += used;

                    if(*c == ':')
                    {
                        std::vector<long> &vec = zasm30_literals[&code[pc]].vec;

                        do
                        {
                            ++c;
                            vec.push_back(atol(c));
                            while(*c && *c != ',' && *c != ';') ++c;
                        }
                        while(*c == ',');
                    }

                    al_trace("ZC_Q30_ZASM_POKE: command %ld is now %ld %ld %ld\n", pc, cmd, a1, a2);
                }

                while(*c && *c != ';') ++c;
                if(*c) ++c;
            }
        }
    }

    g_stream_size = (dword)count + ((count > 0 && last_command != 0xFFFF) ? 1 : 0);
    return s.failed ? qe_invalid : 0;
}

// The initial values of a script's instance variables (a bounded map with word
// keys), then the descriptions the editor shows for exported variables.
static int read_instance_tables(Q255Section &s, std::map<int, long> &inits)
{
    word size = s.igetw();

    if(size)
    {
        byte pairs = s.getc();
        word count = s.igetw();

        for(word i = 0; i < count && !s.failed; ++i)
        {
            int key = pairs ? s.igetw() : i;
            inits[key] = s.igetl_s();
        }
    }

    size = s.igetw();

    if(size)
    {
        byte pairs = s.getc();
        word count = s.igetw();

        for(word i = 0; i < count && !s.failed; ++i)
        {
            if(pairs)
                s.skip(2);

            s.getcstr();    // name
            s.getlstr();    // help text
            s.skip(1);      // button type

            if(s.version >= 31)
            {
                s.skip(2 + 1); // engine type, custom type
                word names = s.igetw();
                for(word n = 0; n < names && !s.failed; ++n)
                {
                    s.skip(4);
                    s.getlstr();
                }
            }

            s.skip(4 + 4);  // min, max
        }
    }

    return s.failed ? qe_invalid : 0;
}

// One slot of a v27+ quest: whether it is used, then where its code is.
static int read_stream_script(Q255Section &s, word meta, ffscript **slot, int kind)
{
    byte exists = s.getc();
    ++g_slots[kind];

    if(!slot)
        ++g_dropped[kind];

    std::map<int, long> inits;
    long start = -1;

    if(exists)
    {
        int ret = skip_meta(s, meta);
        if(ret)
            return ret;

        if(s.keep && slot)
            q255_note_offset("scriptpc", kind, s.pos);

        start = s.igetl_s();
        long end = s.igetl_s(); // end position

        // As upstream (065bc478f): where a script starts and ends comes from the file, and
        // running it reads from there. One outside the quest's commands is switched off, with
        // its warning; one with no end isn't a script to upstream at all (script_data::valid).
        const dword pc = (dword)start, end_pc = (dword)end;

        if(!end_pc || pc >= g_stream_size || pc > end_pc || end_pc > g_stream_size)
        {
            if(s.keep && !s.failed && end_pc)
                al_trace("Warning: found script outside of the quest's ZASM (pc %u to %u), disabling script: kind %d\n", (unsigned)pc, (unsigned)end_pc, kind);

            start = -1;
        }

        if(g_zasm_dump && s.keep)
            fprintf(g_zasm_dump, "script %d %ld %ld\n", kind, start, end);

        if(s.version >= 30 && read_instance_tables(s, inits))
            return qe_invalid;

        if(slot && start >= 0)
            note_zscript_version();

        if(!slot)
            ++g_dropped_with_code[kind];
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep && slot)
    {
        ffscript *stub = new (std::nothrow) ffscript[2];
        if(!stub)
            return qe_nomem;

        stub[0].command = (exists && start >= 0) ? ZASM30_ENTER_COMMAND : (word)0xFFFF;
        stub[0].arg1 = start;
        stub[0].arg2 = -1;
        stub[0].ptr = NULL;
        stub[1].command = 0xFFFF;
        stub[1].arg1 = stub[1].arg2 = 0;
        stub[1].ptr = NULL;

        if(!inits.empty())
        {
            stub[0].arg2 = (long)zasm30_inst_inits.size();
            zasm30_inst_inits.push_back(inits);
        }

        if(*slot != NULL)
            delete [] *slot;
        *slot = stub;
    }

    return 0;
}

// `slot` NULL: 2.53 has nowhere to put this script.
static int read_one_script(Q255Section &s, word meta, ffscript **slot, int kind, int index)
{
    if(s.version >= 27)
        return read_stream_script(s, meta, slot, kind);

    long num_commands = s.igetl_s();

    if(s.failed || num_commands < 0 || num_commands > MAX_COMMANDS)
        return qe_invalid;

    if(s.version >= 16)
    {
        int ret = skip_meta(s, meta);
        if(ret)
            return ret;

        if(slot && num_commands > 0)
            note_zscript_version();
    }

    // Walk it first: the count may claim more than is there, so the real length
    // decides the allocation, and a bad script leaves the old one alone.
    size_t start = s.pos;
    long used = 0;

    for(long j = 0; j < num_commands; ++j)
    {
        word command = s.igetw();

        if(s.failed)
            return qe_invalid;
        if(command == 0xFFFF)
            break;

        s.skip(4 + 4); // arg1, arg2

        if(s.version >= 24)
        {
            if(g_arg3_at)
                g_arg3_at->push_back(s.pos);

            s.skip(4); // arg3
        }

        if(s.version >= 21)
            skip_or_keep_literal(s, NULL, false);

        if(s.failed)
            return qe_invalid;

        ++used;
        ++g_instructions;

        if(command >= NUMCOMMANDS)
        {
            if(command < 2048)
                ++g_opcode_uses[command];

            ++g_unknown_opcodes;
            if(slot)
                ++g_unknown_opcodes_kept;
        }
    }

    ++g_slots[kind];
    if(!slot)
    {
        ++g_dropped[kind];
        if(used)
            ++g_dropped_with_code[kind];
    }

    if(s.keep && slot)
    {
        size_t end = s.pos;
        if(g_zasm_dump)
            fprintf(g_zasm_dump, "script %d %ld %ld\n", kind, g_dump_pc, g_dump_pc + used);

        ffscript *script = new (std::nothrow) ffscript[used + 1];

        if(!script)
            return qe_nomem;

        s.pos = start;
        for(long j = 0; j < used; ++j)
        {
            script[j].command = s.igetw();
            script[j].arg1 = s.igetl_s();
            script[j].arg2 = s.igetl_s();
            script[j].ptr = NULL;

            long arg3 = 0;

            if(s.version >= 24)
                arg3 = s.igetl_s();

            if(g_zasm_dump)
                fprintf(g_zasm_dump, "zasm %ld %d %ld %ld\n", g_dump_pc++, (int)script[j].command, script[j].arg1, script[j].arg2);

            if(s.version >= 21)
                skip_or_keep_literal(s, &script[j], true);

            if(script[j].command == 1115 || script[j].command == 1152)
                zasm30_literals[&script[j]].vec.assign(1, arg3);
        }
        s.pos = end;

        script[used].command = 0xFFFF;
        script[used].arg1 = 0;
        script[used].arg2 = 0;
        script[used].ptr = NULL;

        if(*slot != NULL)
        {
            g_script_lengths.erase(*slot);
            delete [] *slot;
        }
        *slot = script;
        g_script_lengths[script] = used + 1;

        fix_script_jumps(script, used, index);
    }

    return 0;
}

// `count` scripts from the file; the first `limit` of them have a slot in `slots`, the rest
// (up to `limit2`) a slot in `more`, at the script's own index.
static int read_scripts(Q255Section &s, word meta, int count, ffscript **slots, int limit, int kind, ffscript **more = NULL, int limit2 = 0)
{
    for(int i = 0; i < count; ++i)
    {
        ffscript **slot = NULL;

        if(slots && i < limit)
            slot = &slots[i];
        else if(more && i < limit2)
            slot = &more[i];

        int ret = read_one_script(s, meta, slot, kind, i);
        if(ret)
            return ret;
    }
    return 0;
}

// Text as the old readers store it: everything up to the first NUL.
static std::string take_text(Q255Section &s, long n)
{
    std::string text;

    if(!s.has((size_t)n))
    {
        s.failed = true;
        return text;
    }

    if(n > 0)
        text.assign((const char *)(s.data + s.pos), (size_t)n);
    s.pos += (size_t)n;

    size_t nul = text.find('\0');
    if(nul != std::string::npos)
        text.resize(nul);

    return text;
}

enum { bindings_dropped, bindings_ffc, bindings_global, bindings_item, bindings_names };

static int read_bindings(Q255Section &s, int table, int names_table = -1)
{
    word count = s.igetw();

    for(int i = 0; i < count; ++i)
    {
        word id = s.igetw();
        long length = s.igetl_s();

        if(s.failed || length < 0 || length > MAX_BINDING_BYTES)
            return qe_invalid;

        if(!s.keep || (table == bindings_dropped && names_table < 0))
        {
            s.skip((size_t)length);
            if(s.failed)
                return qe_invalid;
            continue;
        }

        std::string name = take_text(s, length);
        if(s.failed)
            return qe_invalid;

        if(names_table >= 0 && !q30_script_names[names_table].count(name))
            q30_script_names[names_table][name] = id;

        switch(table)
        {
        case bindings_ffc:
            if(id < NUMSCRIPTFFC - 1)
                ffcmap[id].second = name;
            break;

        case bindings_global:
            if(id < NUMSCRIPTGLOBAL)
            {
                // an old "~Continue" would wreak havoc, as in the 2.53 reader
                if(name == "~Continue")
                {
                    globalmap[id].second = "";
                    if(globalscripts[GLOBAL_SCRIPT_CONTINUE] != NULL)
                        globalscripts[GLOBAL_SCRIPT_CONTINUE][0].command = 0xFFFF;
                }
                else
                    globalmap[id].second = name;
            }
            break;

        case bindings_item:
            if(id < NUMSCRIPTITEM - 1)
                itemmap[id].second = name;
            break;
        }
    }

    return s.failed ? qe_invalid : 0;
}

static int read_section(Q255Section &s, word meta)
{
    const word v = s.version;
    int ret;

    if(s.keep)      // the scripts read last time are about to be replaced
    {
        g_script_lengths.clear();
        zasm30_stream_len = 0;
        g_zscript_max = -1;
    }

    if(v >= 27 && (ret = read_stream(s)) != 0)
        return ret;

    if((ret = read_scripts(s, meta, 512, ffscripts, NUMSCRIPTFFC, kind_ffc)) != 0) return ret;
    if((ret = read_scripts(s, meta, 256, itemscripts, NUMSCRIPTITEM, kind_item)) != 0) return ret;
    if((ret = read_scripts(s, meta, 256, guyscripts, NUMSCRIPTGUYS, kind_guy)) != 0) return ret;
    if((ret = read_scripts(s, meta, 256, wpnscripts, NUMSCRIPTWEAPONS, kind_weapon)) != 0) return ret;
    if((ret = read_scripts(s, meta, 256, screenscripts, NUMSCRIPTSCREEN, kind_screen)) != 0) return ret;

    int global_count = v > 16 ? 8 : (v > 13 ? 7 : 4);
    if(s.keep)
    {
        q30_script_tables_reset();
        zasm30_ffscript_version = v;
    }

    // Slots 0..3 are 2.53's; the newer ones go in the tables here.
    if((ret = read_scripts(s, meta, global_count, globalscripts, NUMSCRIPTGLOBAL, kind_global, q30_global_scripts, Q30_GLOBAL_SLOTS)) != 0) return ret;

    if((ret = read_scripts(s, meta, v > 10 ? 5 : 3, q30_hero_scripts, Q30_HERO_SLOTS, kind_link)) != 0) return ret;

    if(v == 9)
    {
        if((ret = read_scripts(s, meta, 256, q30_ewpn_scripts, Q30_EWPN_SLOTS, kind_eweapon)) != 0) return ret;
        if((ret = read_scripts(s, meta, 256, q30_dmap_scripts, Q30_DMAP_SLOTS, kind_dmap)) != 0) return ret;
    }

    if(v >= 10)
    {
        if((ret = read_scripts(s, meta, 256, q30_lwpn_scripts, Q30_LWPN_SLOTS, kind_lweapon)) != 0) return ret;
        if((ret = read_scripts(s, meta, 256, q30_ewpn_scripts, Q30_EWPN_SLOTS, kind_eweapon)) != 0) return ret;
        if((ret = read_scripts(s, meta, 256, q30_dmap_scripts, Q30_DMAP_SLOTS, kind_dmap)) != 0) return ret;
    }

    if(v >= 12 && (ret = read_scripts(s, meta, 256, q30_itemsprite_scripts, Q30_ITEMSPRITE_SLOTS, kind_itemsprite)) != 0) return ret;
    if(v >= 15 && (ret = read_scripts(s, meta, 512, q30_combo_scripts, Q30_COMBO_SLOTS, kind_combo)) != 0) return ret;

    if(v > 19)
    {
        word count = s.igetw();
        if((ret = read_scripts(s, meta, count, q30_generic_scripts, Q30_GENERIC_SLOTS, kind_generic)) != 0) return ret;
    }

    if(v > 21)
    {
        word count = s.igetw(); // subscreen scripts
        if((ret = read_scripts(s, meta, count, q30_subscreen_scripts, Q30_SUBSCREEN_SLOTS, kind_generic)) != 0) return ret;
    }

    if(s.keep)
    {
        zasm30_zscript_version = (v >= 16 && g_zscript_max >= 0) ? g_zscript_max : v;
        al_trace("2.55 scripts: section version %d, ZScript version %ld\n", (int)v, zasm30_zscript_version);
    }

    long source_bytes = s.igetl_s();

    if(s.failed || source_bytes < 0 || source_bytes > MAX_SOURCE_BYTES)
        return qe_invalid;

    if(s.keep)
        zScript = take_text(s, source_bytes);
    else
        s.skip((size_t)source_bytes);

    if((ret = read_bindings(s, bindings_ffc, q30_names_ffc)) != 0) return ret;
    if((ret = read_bindings(s, bindings_global, q30_names_global)) != 0) return ret;
    if((ret = read_bindings(s, bindings_item, q30_names_item)) != 0) return ret;

    if(v > 8)
    {
        static const int names[6] = { q30_names_npc, q30_names_lweapon, q30_names_eweapon, q30_names_hero, q30_names_dmap, q30_names_screen };

        for(int i = 0; i < 6; ++i)
            if((ret = read_bindings(s, bindings_dropped, names[i])) != 0) return ret;
    }

    if(v > 11 && (ret = read_bindings(s, bindings_dropped, q30_names_itemsprite)) != 0) return ret;
    if(v >= 15 && (ret = read_bindings(s, bindings_dropped, q30_names_combo)) != 0) return ret;
    if(v > 19 && (ret = read_bindings(s, bindings_dropped, q30_names_generic)) != 0) return ret;
    if(v > 21 && (ret = read_bindings(s, bindings_dropped, q30_names_subscreen)) != 0) return ret;

    if(v >= 29)
    {
        dword debug_bytes = s.igetl(); // the debugger's line tables
        s.skip(debug_bytes);
    }

    return s.failed ? qe_invalid : 0;
}

// From v18 the meta layout has a version word of its own. It sits before the
// section size, so readers aren't given it; the layout that fits the section
// exactly is the right one, since the wrong ones run off the end almost at once.
static int find_meta_version(const Q255Section &s)
{
    for(word meta = 0; meta <= NEWEST_META; ++meta)
    {
        Q255Section trial = s;
        trial.keep = false;

        if(read_section(trial, meta) == 0 && trial.pos == trial.size)
            return meta;
    }

    return -1;
}

// For ZQuest (zq_builds_30.cpp): the scripts section's data of version v and metadata layout 7,
// walked as the loader reads it: where each script's meta_v word is, the spans its layout 7
// fields take, (attrs) the spans of its attribute label lists (layout 6 on) and (arg3_at) where
// each command's third argument is (versions 24 to 26). False when it doesn't read to its end.
bool q255_ffscript_meta_spans(const byte *data, size_t size, word v, std::vector<size_t> &meta_v_at,
                              std::vector<std::pair<size_t, size_t> > &spans, std::vector<std::pair<size_t, size_t> > *attrs,
                              std::vector<size_t> *arg3_at)
{
    if(v < 18 || v > 31)
        return false;

    Q255Section s;
    s.data = data;
    s.size = size;
    s.pos = 0;
    s.version = v;
    s.keep = false;
    meta_v_at.clear();
    spans.clear();
    g_meta_v_at = &meta_v_at;
    g_meta7_spans = &spans;
    g_meta6_attrs = attrs;
    g_arg3_at = arg3_at;

    if(attrs)
        attrs->clear();

    if(arg3_at)
        arg3_at->clear();

    const int ret = read_section(s, 7);
    g_meta_v_at = NULL;
    g_meta7_spans = NULL;
    g_meta6_attrs = NULL;
    g_arg3_at = NULL;
    return ret == 0 && !s.failed && s.pos == s.size;
}

int q255_read_ffscript(Q255Section &s)
{
    if(s.version < 6 || s.version > 31)
        return Q255_NOT_HANDLED;

    word meta = 0;

    if(s.version >= 18)
    {
        int found = find_meta_version(s);

        if(found < 0)
        {
            al_trace("2.55 scripts: no meta layout fits the %d byte section\n", (int)s.size);
            return qe_invalid;
        }

        meta = (word)found;
    }

    if(s.keep)
    {
        setZScriptVersion(s.version);
        const char *path = getenv("ZC_Q30_ZASM_DUMP");
        g_zasm_dump = path ? fopen(path, "w") : NULL;
        g_dump_pc = 0;
    }

    reset_counters();
    int ret = read_section(s, meta);
    trace_counters();

    if(g_zasm_dump)
    {
        fclose(g_zasm_dump);
        g_zasm_dump = NULL;
    }

    return ret;
}
