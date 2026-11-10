// ZQuest's writers for the header, the quest rules, the strings and the misc data of sectioned
// quests, in the version each quest was made in (see zq_writers.h): 1.93 to 2.53 as
// src/qst.cpp reads them, 2.55 and 3.0 Nightly as src/port/game/q255_basic.cpp does.
//
// Each writer walks the part as it was loaded, record by record, the way its reader does. A
// record ZQuest still holds as it was loaded is copied byte for byte. A changed record is
// encoded in the part's own format, undoing what the loader did to it; fields the format has
// and ZQuest hasn't (a 2.55 string's portrait, say) come from the record as it was. When
// ZQuest holds something the format can't (a 73rd character in a 2.10 string, a rule the
// loader sets for that version whatever the file says), the writer refuses with
// ZQW_CANT_HOLD and leaves the reason in zqw_basic_refusal(), rather than drop it.
//
// The rules are the one section kept as stored, never as converted: the loader turns
// compatibility rules on and off by version, and the old meaning of some stored bits drives
// how later sections are read. So the rules are written as they were saved with only the
// user's own changes applied on top, and each change goes to whichever stored bit the loader
// turns into it.
//
// ZQW_BASIC_STRICT=1 (for testing the encoders): where a narrowed field still reads back as
// what ZQuest holds, it is encoded from ZQuest's data too, instead of copied as it was.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "md5.h"
#include "sfx.h"
#include "zq_writers.h"

extern zquestheader header;
extern miscQdata misc;
extern MsgStr *MsgStrings;
extern byte quest_rules[QUESTRULES_SIZE];
extern byte extra_rules[EXTRARULES_SIZE];
extern byte emulation_patches[emuLAST];
extern byte quest_ewpn_offset_rule_saved;

void get_questpwd(char *encrypted_pwd, short pwdkey, char *pwd);     // qst.cpp

// q255_basic.cpp, for this file only
bool q255_editor_message_text(char *out, const char *raw, size_t n, bool ascii, bool *dropped);
int q255_editor_command_args(int code, bool *runs_here);

namespace
{

enum { ZQW_CANT_HOLD = 1, ZQW_BAD_PART = 2, ZQW_WRITE_FAILED = 3 };

std::string g_refusal;

int refuse(const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_refusal = buf;
    zqw_last_error = buf;       // the reason the save shows the user (zq_writers.h)
    al_trace("Saving in the quest's own version: %s\n", buf);
    return ZQW_CANT_HOLD;
}

bool strict()
{
    static int s = -1;

    if(s < 0)
    {
        const char *e = getenv("ZQW_BASIC_STRICT");
        s = (e && *e && *e != '0') ? 1 : 0;
    }

    return s == 1 && zqw_force_serialize;
}

// ---- bytes in and out -------------------------------------------------------------------

word rd16(const std::vector<byte> &b, size_t at)
{
    return (word)(b[at] | (b[at + 1] << 8));
}

dword rd32(const std::vector<byte> &b, size_t at)
{
    return (dword)b[at] | ((dword)b[at + 1] << 8) | ((dword)b[at + 2] << 16) | ((dword)b[at + 3] << 24);
}

// Walks a part's bytes as a reader does; bad is set when it runs past the end.
struct In
{
    const std::vector<byte> &b;
    size_t pos;
    bool bad;

    In(const std::vector<byte> &v, size_t at) : b(v), pos(at), bad(false) {}

    bool has(size_t n) const { return !bad && pos <= b.size() && n <= b.size() - pos; }
    byte c() { if(!has(1)) { bad = true; return 0; } return b[pos++]; }
    word w() { if(!has(2)) { bad = true; return 0; } word v = rd16(b, pos); pos += 2; return v; }
    dword l() { if(!has(4)) { bad = true; return 0; } dword v = rd32(b, pos); pos += 4; return v; }
    void skip(size_t n) { if(!has(n)) { bad = true; return; } pos += n; }
};

struct Out
{
    std::vector<byte> b;

    void c(int v) { b.push_back((byte)v); }
    void w(int v) { c(v); c(v >> 8); }
    void l(dword v) { c((int)v); c((int)(v >> 8)); c((int)(v >> 16)); c((int)(v >> 24)); }
    void put(const void *p, size_t n) { const byte *q = (const byte *)p; b.insert(b.end(), q, q + n); }
    void copy(const std::vector<byte> &src, size_t at, size_t n) { b.insert(b.end(), src.begin() + at, src.begin() + at + n); }
    void zeros(size_t n) { b.insert(b.end(), n, (byte)0); }
    void set32(size_t at, dword v) { b[at] = (byte)v; b[at + 1] = (byte)(v >> 8); b[at + 2] = (byte)(v >> 16); b[at + 3] = (byte)(v >> 24); }
};

int emit(PACKFILE *f, dword id, const std::vector<byte> &body)
{
    if(!zq_part_is_raw(id) && pack_mputl((long)id, f) == EOF)
        return ZQW_WRITE_FAILED;

    if(!body.empty() && pack_fwrite(&body[0], (long)body.size(), f) != (long)body.size())
        return ZQW_WRITE_FAILED;

    return ZQW_OK;
}

// A section's size field: what was there, or what it is now if it was right before.
// The 1.93-2.53 readers mostly ignore it; the 2.55 loader reads exactly that many bytes.
dword new_size(dword old_size, size_t old_body, size_t new_body)
{
    if(old_size == (dword)old_body)
        return (dword)new_body;

    return old_size + (dword)new_body - (dword)old_body;
}

bool part_bytes(const ZqQuestPart &p, std::vector<byte> &b)
{
    b.resize((size_t)p.length);
    return b.empty() || zq_part_read(p, 0, &b[0], b.size());
}

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];

    return NULL;
}

// A section's version, -1 when the quest has none.
int part_version(dword id)
{
    const ZqQuestPart *p = find_part(id);
    word v, cv;

    if(!p || !zq_part_versions(*p, &v, &cv))
        return -1;

    return v;
}

bool is_255()
{
    return zq_quest_file.zelda_version == 0x255;
}

bool is_own_253()
{
    return zq_quest_file.zelda_version == 0x250 && zq_quest_file.build >= 31;
}

// ---- what was loaded ------------------------------------------------------------------

struct Loaded
{
    zquestheader hdr;
    byte rules[QUESTRULES_SIZE];
    std::vector<byte> strings;      // msg_count MsgStrs, as bytes (MsgStr's operator= isn't const)
    word string_count;
    miscQdata misc;
};

Loaded g_loaded;

void note_loaded()
{
    memcpy(&g_loaded.hdr, &header, sizeof(header));
    memcpy(g_loaded.rules, quest_rules, QUESTRULES_SIZE);
    g_loaded.string_count = msg_count;
    g_loaded.strings.resize((size_t)msg_count * sizeof(MsgStr));

    if(msg_count)
        memcpy(&g_loaded.strings[0], MsgStrings, (size_t)msg_count * sizeof(MsgStr));

    memcpy(&g_loaded.misc, &misc, sizeof(misc));
}

ZqLoadHookRegistration g_load_hook(note_loaded);

const MsgStr *loaded_string(int i)
{
    if(i < 0 || i >= g_loaded.string_count)
        return NULL;

    return (const MsgStr *)&g_loaded.strings[(size_t)i * sizeof(MsgStr)];
}

bool same_string(const MsgStr &a, const MsgStr &b)
{
    return !memcmp(a.s, b.s, sizeof(a.s)) && a.nextstring == b.nextstring && a.tile == b.tile && a.cset == b.cset &&
           a.trans == b.trans && a.font == b.font && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
           a.sfx == b.sfx && a.listpos == b.listpos && a.vspace == b.vspace && a.hspace == b.hspace &&
           a.stringflags == b.stringflags;
}

// The text as C strings: what follows the terminator isn't part of it.
bool same_text(const char *a, const char *b)
{
    return !strncmp(a, b, MSGSIZE + 1);
}

size_t text_length(const char *s)
{
    size_t n = 0;

    while(n < MSGSIZE + 1 && s[n])
        ++n;

    return n;
}

// ---- the header -----------------------------------------------------------------------

// Where things are in a sectioned header part: the file's id string, then a section.
const size_t H_SECTION = 31, H_VERSION = 35, H_SIZE = 39, H_ZVERSION = 43, H_BUILD = 45;

bool header_changed()
{
    const zquestheader &a = header, &o = g_loaded.hdr;
    return a.zelda_version != o.zelda_version || a.build != o.build || memcmp(a.pwd_hash, o.pwd_hash, 16) ||
           a.internal != o.internal || a.quest_number != o.quest_number || memcmp(a.version, o.version, sizeof(a.version)) ||
           memcmp(a.minver, o.minver, sizeof(a.minver)) || memcmp(a.title, o.title, sizeof(a.title)) ||
           memcmp(a.author, o.author, sizeof(a.author)) || a.use_keyfile != o.use_keyfile ||
           memcmp(a.data_flags, o.data_flags, sizeof(a.data_flags)) || memcmp(a.templatepath, o.templatepath, sizeof(a.templatepath));
}

// The header keeps two of its data flags; the rest of those bytes are padding. The two say
// how other sections are laid out: whether the tiles section holds tiles (readtiles reads
// none without it), and whether the cheats section holds the codes. They go back as the
// file has them, since those sections are written to match the file as it was. ZQuest's
// cheats flag is the cheats section's own on/off switch, which readcheatcodes puts there
// and the cheats section carries, unless the quest has no cheats section.
int check_data_flags()
{
    for(int i = 0; i < ZQ_MAXDATA; ++i)
        if(i != ZQ_TILES && i != ZQ_CHEATS2 && header.data_flags[i] != g_loaded.hdr.data_flags[i])
            return refuse("The header has no room for data flag %d.", i);

    if(header.data_flags[ZQ_TILES] != g_loaded.hdr.data_flags[ZQ_TILES])
        return refuse("Whether the quest has its own tiles can't be changed in this quest's version.");

    return ZQW_OK;
}

void write_data_flags(Out &out, const std::vector<byte> &b, size_t at)
{
    out.copy(b, at, 5);         // the tiles flag, padding

    if(find_part(ID_CHEATS))
        out.copy(b, at + 5, 1);
    else
        out.c(header.data_flags[ZQ_CHEATS2]);

    out.copy(b, at + 6, 14);
}

int check_version_unchanged(const std::vector<byte> &b)
{
    if((word)header.zelda_version != rd16(b, H_ZVERSION) || header.build != b[H_BUILD])
        return refuse("The header's version (%x, build %d) no longer matches the file (%x, build %d).",
                      (word)header.zelda_version, header.build, rd16(b, H_ZVERSION), b[H_BUILD]);

    return ZQW_OK;
}

// get_questpwd the other way round: the 30 bytes and the key a header before version 3 keeps.
bool encrypt_password(const char *plain, short key, byte *out)
{
    size_t n = strlen(plain);

    if(n > 28)      // get_questpwd clears the 30th byte before decoding, so 28 and a terminator
        return false;

    char p[30];
    memset(p, 0, sizeof(p));
    memcpy(p, plain, n);

    for(int i = 0; i < 30; ++i)
    {
        out[i] = (byte)(char)(p[i] + key);
        int t = key >> 15;
        key = (short)((key << 1) + t);
    }

    return true;
}

int write_password_old(Out &out, const std::vector<byte> &b, size_t at)
{
    if(!memcmp(header.pwd_hash, g_loaded.hdr.pwd_hash, 16))
    {
        out.copy(b, at, 32);
        return ZQW_OK;
    }

    // Headers before version 3 keep the password itself, so only one typed since the load
    if(!header.dirty_password)
        return refuse("The password changed, but ZQuest doesn't have it to write into this header.");

    short key = (short)rd16(b, at + 30);
    byte enc[30];

    if(!header.password[0])
        key = 0;            // no key: get_questpwd gives an empty password
    else if(!key)
        key = 0x2A5D;

    if(!encrypt_password(header.password, key, enc))
        return refuse("This quest's header holds passwords of up to 28 characters.");

    // read back as readheader does
    char dec[30];
    get_questpwd((char *)enc, key, dec);
    dec[29] = 0;
    cvs_MD5Context ctx;
    unsigned char md5[16];
    cvs_MD5Init(&ctx);
    cvs_MD5Update(&ctx, (const unsigned char *)dec, (unsigned)strlen(dec));
    cvs_MD5Final(md5, &ctx);

    if(memcmp(md5, header.pwd_hash, 16))
        return refuse("The new password doesn't read back as the one set.");

    out.put(enc, 30);
    out.w(key);
    return ZQW_OK;
}

// readheader's 1.93 to 2.53 branch
int write_header_old(PACKFILE *f, const std::vector<byte> &b, word hv)
{
    const size_t pwd = hv < 3 ? 32 : 16;
    const size_t tpl = hv == 1 ? 280 : sizeof(header.templatepath);
    const size_t at_internal = H_BUILD + 1 + pwd;
    const size_t at_flags = at_internal + 2 + 1 + 9 + 9 + 65 + 65 + 1;
    const size_t at_template = at_flags + 20;

    if(b.size() != at_template + tpl + 1)
        return ZQW_BAD_PART;

    if(!zqw_force_serialize && !header_changed())
        return emit(f, ID_HEADER, b);

    int r = check_version_unchanged(b);

    if(!r)
        r = check_data_flags();

    if(r)
        return r;

    for(size_t i = tpl; i < sizeof(header.templatepath); ++i)
        if(header.templatepath[i] != g_loaded.hdr.templatepath[i])
            return refuse("This header holds a template path of up to 279 characters.");

    Out out;
    out.copy(b, 0, H_BUILD + 1);        // id string, section, version, size, program version, build

    if(hv < 3)
    {
        r = write_password_old(out, b, H_BUILD + 1);

        if(r)
            return r;
    }
    else
        out.put(header.pwd_hash, 16);

    out.w(header.internal);
    out.c(header.quest_number);
    out.put(header.version, sizeof(header.version));
    out.put(header.minver, sizeof(header.minver));
    out.put(header.title, sizeof(header.title));
    out.put(header.author, sizeof(header.author));
    out.c(header.use_keyfile);
    write_data_flags(out, b, at_flags);
    out.put(header.templatepath, tpl);
    out.copy(b, at_template + tpl, 1);  // the map count, which readmaps sets again
    return emit(f, ID_HEADER, out.b);
}

// A text field the 2.55 reader reads n bytes of from a field of file_n bytes, ending it at n-1.
int write_narrowed_text(Out &out, const std::vector<byte> &b, size_t at, size_t file_n, const char *now, size_t n, const char *what)
{
    std::vector<char> was(b.begin() + at, b.begin() + at + n);
    was[n - 1] = 0;

    if(!strict() && !memcmp(&was[0], now, n))
    {
        out.copy(b, at, file_n);
        return ZQW_OK;
    }

    if(now[n - 1])
        return refuse("The header's %s is longer than it can be.", what);

    out.put(now, n);
    out.zeros(file_n - n);
    return ZQW_OK;
}

// q255_read_header
int write_header_255(PACKFILE *f, const std::vector<byte> &b, word hv)
{
    if(hv < 3 || hv > 9)
        return ZQW_UNSUPPORTED;

    dword size = rd32(b, H_SIZE);
    const size_t vlen = hv >= 8 ? 16 : 9;
    const size_t at_hash = H_BUILD + 1;
    const size_t at_version = at_hash + 16 + 2 + 1;
    const size_t at_title = at_version + 2 * vlen;
    const size_t at_flags = at_title + 65 + 65 + 1;
    const size_t at_template = at_flags + 20;
    const size_t at_rest = at_template + sizeof(header.templatepath);

    if(size < 2 || H_ZVERSION + (size_t)size != b.size() || at_rest + 1 > b.size())
        return ZQW_BAD_PART;

    if(!zqw_force_serialize && !header_changed())
        return emit(f, ID_HEADER, b);

    int r = check_version_unchanged(b);

    if(!r)
        r = check_data_flags();

    if(r)
        return r;

    Out out;
    out.copy(b, 0, H_BUILD + 1);
    out.put(header.pwd_hash, 16);
    out.w(header.internal);
    out.c(header.quest_number);

    if((r = write_narrowed_text(out, b, at_version, vlen, header.version, sizeof(header.version), "version")) != 0 ||
       (r = write_narrowed_text(out, b, at_version + vlen, vlen, header.minver, sizeof(header.minver), "minimum version")) != 0 ||
       (r = write_narrowed_text(out, b, at_title, 65, header.title, sizeof(header.title), "title")) != 0 ||
       (r = write_narrowed_text(out, b, at_title + 65, 65, header.author, sizeof(header.author), "author")) != 0)
        return r;

    out.c(header.use_keyfile);
    write_data_flags(out, b, at_flags);
    out.put(header.templatepath, sizeof(header.templatepath));
    out.copy(b, at_rest, b.size() - at_rest);   // map count, and the build details ZQuest has no place for
    return emit(f, ID_HEADER, out.b);
}

int zqw_header(PACKFILE *f, const ZqPartContext &ctx)
{
    std::vector<byte> b;

    if(!part_bytes(*ctx.part, b))
        return ZQW_BAD_PART;

    // The header of a quest older than 1.93 isn't a section: another writer's. readheader
    // compares the id string as text; the byte after it is whatever the saving program had.
    if(b.size() < H_BUILD + 1 || strncmp((const char *)&b[0], QH_NEWIDSTR, sizeof(header.id_str)))
        return ZQW_UNSUPPORTED;

    word hv = rd16(b, H_VERSION);

    if(rd16(b, H_ZVERSION) == 0x255)
        return write_header_255(f, b, hv);

    return write_header_old(f, b, hv);
}

// ---- the rules ------------------------------------------------------------------------

// A section of memory as a file to read, for running readrules on stored bits.
struct MemIn
{
    const byte *data;
    size_t size, pos;
    int ungot;
};

int mi_fclose(void *) { return 0; }

int mi_getc(void *u)
{
    MemIn *m = (MemIn *)u;

    if(m->ungot >= 0)
    {
        int c = m->ungot;
        m->ungot = -1;
        return c;
    }

    return m->pos < m->size ? m->data[m->pos++] : EOF;
}

int mi_ungetc(int c, void *u)
{
    ((MemIn *)u)->ungot = c;
    return c;
}

long mi_fread(void *p, long n, void *u)
{
    byte *out = (byte *)p;
    long done = 0;

    while(done < n)
    {
        int c = mi_getc(u);

        if(c == EOF)
            break;

        out[done++] = (byte)c;
    }

    return done;
}

int mi_putc(int, void *) { return EOF; }
long mi_fwrite(const void *, long, void *) { return 0; }

int mi_fseek(void *u, int offset)
{
    MemIn *m = (MemIn *)u;

    if(offset < 0 || m->pos + offset > m->size)
        return -1;

    m->pos += offset;
    return 0;
}

int mi_feof(void *u)
{
    MemIn *m = (MemIn *)u;
    return m->ungot < 0 && m->pos >= m->size;
}

int mi_ferror(void *) { return 0; }

const PACKFILE_VTABLE g_mem_in_vtable = { mi_fclose, mi_getc, mi_ungetc, mi_fread, mi_putc, mi_fwrite, mi_fseek, mi_feof, mi_ferror };

// What readrules makes of stored rules in this quest. readrules works on the globals, so
// they are put back afterwards; its own copy of the rules as stored (deprecated_rules, which
// only the readers of a load use) is left as the last call had it.
void read_rules_old(const byte *stored, word v, word cv, byte *out)
{
    byte keep_rules[QUESTRULES_SIZE], keep_extra[EXTRARULES_SIZE], keep_emu[emuLAST];
    byte keep_ewpn = quest_ewpn_offset_rule_saved;
    memcpy(keep_rules, quest_rules, sizeof(keep_rules));
    memcpy(keep_extra, extra_rules, sizeof(keep_extra));
    memcpy(keep_emu, emulation_patches, sizeof(keep_emu));

    byte buf[8 + QUESTRULES_SIZE];
    buf[0] = (byte)v;
    buf[1] = (byte)(v >> 8);
    buf[2] = (byte)cv;
    buf[3] = (byte)(cv >> 8);
    buf[4] = QUESTRULES_SIZE;
    buf[5] = buf[6] = buf[7] = 0;
    memcpy(buf + 8, stored, QUESTRULES_SIZE);

    MemIn m;
    m.data = buf;
    m.size = sizeof(buf);
    m.pos = 0;
    m.ungot = -1;

    zquestheader h;
    memset(&h, 0, sizeof(h));
    h.zelda_version = zq_quest_file.zelda_version;
    h.build = zq_quest_file.build;
    memset(out, 0, QUESTRULES_SIZE);
    PACKFILE *pf = pack_fopen_vtable(&g_mem_in_vtable, &m);

    if(pf)
    {
        readrules(pf, &h, false);
        pack_fclose(pf);
        memcpy(out, quest_rules, QUESTRULES_SIZE);
    }

    memcpy(quest_rules, keep_rules, sizeof(keep_rules));
    memcpy(extra_rules, keep_extra, sizeof(keep_extra));
    memcpy(emulation_patches, keep_emu, sizeof(keep_emu));
    quest_ewpn_offset_rule_saved = keep_ewpn;
}

bool bit_of(const byte *bits, int n)
{
    return (bits[n >> 3] >> (n & 7)) & 1;
}

void flip_bit(byte *bits, int n)
{
    bits[n >> 3] ^= (byte)(1 << (n & 7));
}

const int g_deprecated_rules[] =
{
    qr_CONTFULL_DEP, qr_NOCLOCKS_DEP, qr_TEMPCLOCKS_DEP, qr_BRKBLSHLDS_DEP, qr_LINKEDCOMBOS, qr_MAGICWAND_DEP,
    qr_MAGICCANDLE_DEP, qr_MAGICBOOTS_DEP, qr_NOROPE2FLASH_DEP, qr_NOBUBBLEFLASH_DEP, qr_GHINI2BLINK_DEP,
    qr_PHANTOMGHINI2_DEP, qr_NOPOTIONCOMBINE_DEP, qr_TRANSLUCENTNAYRUSLOVEROCKET_DEP, qr_FLICKERINGNAYRUSLOVEROCKET_DEP,
    qr_SLOWENEMYANIM_DEP, qr_ALLOW10RUPEEDROPS_DEP, qr_REDPOTIONONCE_DEP, qr_OLDTRIBBLES_DEP, qr_REFLECTROCKS,
    qr_HEARTPIECEINCR_DEP, qr_EATSMALLSHIELD_DEP, qr_FIREMAGICSPRITE_DEP, qr_SLOWBOMBFUSES_DEP, qr_LONGBOMBBOOM_DEP
};

// Stored bits that readitems turns into item settings for items sections before version 18
const int g_item_rules[] = { 27, 28, 29, 30, 32, 49, 50, 51, 75, 76, 102, 113, 116, qr_TEMPCLOCKS_DEP, qr_NOBOMBPALFLASH + 1, qr_NOBOMBPALFLASH + 2 };

// Rules the load sets or clears whatever this quest's file says, or whose stored bit the
// readers of other sections also turn into their own data: a change to one of them would
// come back otherwise, or would change those sections, when the quest is opened again.
const char *loader_owned_rule(int bit)
{
    if(bit == qr_CONTFULL_DEP)
        return "the load turns it into init data";

    if(is_255())
        return NULL;

    int init = part_version(ID_INITDATA), dmaps = part_version(ID_DMAPS);
    int items = part_version(ID_ITEMS), guys = part_version(ID_GUYS), sfx = part_version(ID_SFX);
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;

    if(bit == qr_BRANGPICKUP + 1 && init >= 0 && init < 16)
        return "this quest's init data reader clears it";

    if(bit == qr_GOTOLESSNOTEQUAL && sfx >= 0 && sfx < 2)
        return "this quest's sound reader sets it";

    if(is_own_253())
        return NULL;

    if((bit == qr_COOLSCROLL && init >= 0 && init <= 6) || (bit == qr_COOLSCROLL + 1 && init >= 0 && init < 16) ||
       (bit == 27 && init >= 15 && init <= 16))
        return "its stored bit also sets this quest's init data";

    if(bit == 14 && dmaps >= 0 && dmaps < 7)
        return "its stored bit also sets this quest's DMaps";

    for(size_t i = 0; i < sizeof(g_item_rules) / sizeof(g_item_rules[0]); ++i)
        if(bit == g_item_rules[i] && items >= 0 && items < 18)
            return "its stored bit also sets this quest's items";

    if(bit == 46 && ((guys >= 0 && guys < 8) || zv < 0x211 || (zv == 0x211 && build < 7)))
        return "its stored bit also sets this quest's enemies";

    for(size_t i = 0; i < sizeof(g_deprecated_rules) / sizeof(g_deprecated_rules[0]); ++i)
        if(bit == g_deprecated_rules[i])
            return "it is an old rule whose stored bit the readers convert";

    return NULL;
}

// readrules, 1.93 to 2.53: version, cversion, size, and the 20 bytes.
int write_rules_old(PACKFILE *f, const std::vector<byte> &b)
{
    if(b.size() != 8 + QUESTRULES_SIZE)
        return ZQW_BAD_PART;

    byte diff[QUESTRULES_SIZE];
    bool any = false;

    for(int i = 0; i < QUESTRULES_SIZE; ++i)
    {
        diff[i] = (byte)(g_loaded.rules[i] ^ quest_rules[i]);
        any = any || diff[i];
    }

    if(!any && !zqw_force_serialize)
        return emit(f, ID_RULES, b);

    word v = rd16(b, 0), cv = rd16(b, 2);
    byte stored[QUESTRULES_SIZE], now[QUESTRULES_SIZE], was[QUESTRULES_SIZE];
    memcpy(stored, &b[8], QUESTRULES_SIZE);
    read_rules_old(stored, v, cv, was);
    memcpy(now, was, sizeof(now));
    int ret = ZQW_OK;

    for(int bit = 0; bit < QUESTRULES_SIZE * 8 && !ret; ++bit)
    {
        if(!bit_of(diff, bit))
            continue;

        const char *why = loader_owned_rule(bit);

        // the load changed this bit after the rules were read
        if(!why && bit_of(was, bit) != bit_of(g_loaded.rules, bit))
            why = "the load sets it from other data";

        if(why)
        {
            ret = refuse("Rule %d can't be changed in this quest's version: %s.", bit, why);
            break;
        }

        // The stored bit the loader makes this rule from: its own bit if that works, else
        // whichever single stored bit moves this rule and nothing else.
        byte want[QUESTRULES_SIZE];
        memcpy(want, now, sizeof(want));
        flip_bit(want, bit);
        bool found = false;

        for(int k = -1; k < QUESTRULES_SIZE * 8 && !found; ++k)
        {
            int cand = k < 0 ? bit : k;

            if(k == bit)
                continue;

            byte trial[QUESTRULES_SIZE], got[QUESTRULES_SIZE];
            memcpy(trial, stored, sizeof(trial));
            flip_bit(trial, cand);
            read_rules_old(trial, v, cv, got);

            if(!memcmp(got, want, sizeof(got)))
            {
                memcpy(stored, trial, sizeof(stored));
                memcpy(now, got, sizeof(now));
                found = true;
            }
        }

        if(!found)
            ret = refuse("Rule %d can't be changed in this quest's version: the loader sets it.", bit);
    }

    if(!ret)
    {
        byte check[QUESTRULES_SIZE];
        read_rules_old(stored, v, cv, check);

        for(int i = 0; i < QUESTRULES_SIZE && !ret; ++i)
            if(check[i] != (byte)(was[i] ^ diff[i]))
                ret = refuse("The rules don't read back as set.");
    }

    // leaves readrules' copy of the stored rules as the load had it
    read_rules_old(&b[8], v, cv, was);

    if(ret)
        return ret;

    Out out;
    out.copy(b, 0, 8);
    out.put(stored, QUESTRULES_SIZE);
    return emit(f, ID_RULES, out.b);
}

// Rule numbers in 2.55's bit field (engine/src/core/qrs.h)
const int Q255_QR_STRING_FRAME_OLD_WIDTH_HEIGHT = 205;

// q255_read_rules: what it makes of the first 20 bytes
void read_rules_255(const byte *stored, word v, dword compat, byte *out)
{
    memcpy(out, stored, QUESTRULES_SIZE);

    if(v > 16 && compat >= 12)
        flip_bit(out, qr_NOGANONINTRO);

    if(zq_quest_file.build < 42 && bit_of(out, qr_BITMAPOFFSETFIX))
        flip_bit(out, qr_BITMAPOFFSETFIX);
}

struct Rules255
{
    word version;
    dword compat;
    size_t body;        // where the rule bits start
    dword size;
};

bool parse_rules_255(const std::vector<byte> &b, Rules255 &r)
{
    In in(b, 0);
    r.version = in.w();
    in.w();
    r.compat = r.version > 16 ? in.l() : 0;
    r.size = in.l();
    r.body = in.pos;
    size_t total = r.version < 15 ? QUESTRULES_SIZE : 100;
    return !in.bad && r.body + r.size == b.size() && r.size >= total;
}

// q255_read_rules: version, cversion, from v17 the compat rule version, size, then the rule
// bits (20 bytes, 100 from v15) and from v18 the compiler settings.
int write_rules_255(PACKFILE *f, const std::vector<byte> &b)
{
    Rules255 r;

    if(!parse_rules_255(b, r))
        return ZQW_BAD_PART;

    if(r.version < 13 || r.version > 18)
        return ZQW_UNSUPPORTED;

    byte diff[QUESTRULES_SIZE];
    bool any = false;

    for(int i = 0; i < QUESTRULES_SIZE; ++i)
    {
        diff[i] = (byte)(g_loaded.rules[i] ^ quest_rules[i]);
        any = any || diff[i];
    }

    if(!any && !zqw_force_serialize)
        return emit(f, ID_RULES, b);

    byte stored[QUESTRULES_SIZE], was[QUESTRULES_SIZE];
    memcpy(stored, &b[r.body], QUESTRULES_SIZE);
    read_rules_255(stored, r.version, r.compat, was);

    for(int bit = 0; bit < QUESTRULES_SIZE * 8; ++bit)
    {
        if(!bit_of(diff, bit))
            continue;

        const char *why = loader_owned_rule(bit);

        if(!why && bit_of(was, bit) != bit_of(g_loaded.rules, bit))
            why = "the load sets it from other data";

        if(!why && bit == qr_BITMAPOFFSETFIX && zq_quest_file.build < 42)
            why = "the loader clears it for quests of this build";

        if(why)
            return refuse("Rule %d can't be changed in this quest's version: %s.", bit, why);

        flip_bit(stored, bit);      // kept or inverted, a stored bit moves its rule
    }

    byte check[QUESTRULES_SIZE];
    read_rules_255(stored, r.version, r.compat, check);

    for(int i = 0; i < QUESTRULES_SIZE; ++i)
        if(check[i] != (byte)(was[i] ^ diff[i]))
            return refuse("The rules don't read back as set.");

    Out out;
    out.copy(b, 0, r.body);
    out.put(stored, QUESTRULES_SIZE);
    out.copy(b, r.body + QUESTRULES_SIZE, b.size() - r.body - QUESTRULES_SIZE);  // newer rules, compiler settings
    return emit(f, ID_RULES, out.b);
}

int zqw_rules(PACKFILE *f, const ZqPartContext &ctx)
{
    std::vector<byte> b;

    if(!part_bytes(*ctx.part, b))
        return ZQW_BAD_PART;

    if(ctx.zelda_version == 0x255)
        return write_rules_255(f, b);

    if(ctx.zelda_version < 0x193)
        return ZQW_UNSUPPORTED;

    return write_rules_old(f, b);
}

// ---- the strings ----------------------------------------------------------------------

int check_tile16(long tile, int i)
{
    if(tile < 0 || tile > 0xFFFF)
        return refuse("String %d's frame tile, %ld, is past what this quest's version holds.", i, tile);

    return ZQW_OK;
}

// readstrings, 1.93 to 2.53
struct StrOld
{
    word version;
    int text;           // bytes of text per string
    size_t start;       // the first string
    size_t record;      // bytes per string
};

size_t old_string_record(word v)
{
    if(v < 2)
        return 73 + 2;

    return 145 + 2 + 2 + 3 + (v < 5 ? 1 : 2 * 4 + 3) + 1 + (v > 3 ? 2 : 0);
}

int write_old_string(Out &out, const std::vector<byte> &b, size_t at, bool have, const MsgStr &m, int i, word v)
{
    if(v < 2)
    {
        for(int j = 72; j <= MSGSIZE; ++j)
            if(m.s[j])
                return refuse("String %d is longer than this quest's version holds (72 characters).", i);

        if(m.tile || m.cset || m.trans || m.font != font_zfont || m.y != 32 || m.sfx != WAV_MSG || m.listpos != i ||
           m.x != 24 || m.w != 24 * 8 || m.h != 3 * 8 || m.hspace || m.vspace || m.stringflags)
            return refuse("String %d has settings this quest's version doesn't hold (only text and the next string).", i);

        out.put(m.s, 72);

        if(have)
            out.copy(b, at + 72, 1);
        else
            out.c(0);

        out.w(m.nextstring);
        return ZQW_OK;
    }

    // v2 lost the last 4 characters to a bug, and the reader clears them; every version ends at 144
    const int keep = v < 3 ? MSGSIZE - 4 : MSGSIZE;

    for(int j = keep; j <= MSGSIZE; ++j)
        if(m.s[j])
            return refuse("String %d is longer than this quest's version holds (%d characters).", i, keep);

    out.put(m.s, keep);

    if(have)
        out.copy(b, at + keep, 145 - keep);
    else
        out.zeros(145 - keep);

    size_t f = at + 145;
    out.w(m.nextstring);
    f += 2;

    int r = check_tile16(m.tile, i);

    if(r)
        return r;

    out.w((int)m.tile);
    out.c(m.cset);

    if(have && (b[f + 3] != 0) == m.trans)
        out.copy(b, f + 3, 1);
    else
        out.c(m.trans ? 1 : 0);

    out.c(m.font);

    if(v < 5)
    {
        if(m.x != 24 || m.w != 24 * 8 || m.h != 3 * 8 || m.hspace || m.vspace || m.stringflags)
            return refuse("String %d has a position, size, spacing or flags this quest's version doesn't hold.", i);

        if(m.y < 0 || m.y > 255)
            return refuse("String %d's y position, %d, is past what this quest's version holds.", i, m.y);

        out.c(m.y);
    }
    else
    {
        out.w(m.x);
        out.w(m.y);
        out.w(m.w);
        out.w(m.h);
        out.c(m.hspace);
        out.c(m.vspace);
        out.c(m.stringflags);
    }

    out.c(m.sfx);

    if(v > 3)
        out.w(m.listpos);
    else if(m.listpos != i)
        return refuse("String %d has a place in the list this quest's version doesn't hold.", i);

    return ZQW_OK;
}

int write_strings_old(PACKFILE *f, const std::vector<byte> &b)
{
    In in(b, 0);
    word v = in.w();
    in.w();
    dword size = in.l();
    word count = in.w();
    size_t rec = old_string_record(v);

    if(in.bad || 10 + (size_t)count * rec != b.size())
        return ZQW_BAD_PART;

    Out out;
    out.copy(b, 0, 8);
    out.w(msg_count);

    for(int i = 0; i < msg_count; ++i)
    {
        bool have = i < count;
        size_t at = 10 + (size_t)i * rec;
        const MsgStr *was = loaded_string(i);

        if(have && was && !zqw_force_serialize && same_string(MsgStrings[i], *was))
        {
            out.copy(b, at, rec);
            continue;
        }

        int r = write_old_string(out, b, at, have, MsgStrings[i], i, v);

        if(r)
            return r;
    }

    out.set32(4, new_size(size, b.size() - 8, out.b.size() - 8));
    return emit(f, ID_STRINGS, out.b);
}

// q255_read_strings, versions 5 to 13
struct Str255
{
    size_t at, end;
    size_t text_at, text_n;     // the text, after its length from v9
    size_t tile_at, trans_at, w_at, flags_at, extra_at, sfx_at, tail_at;
};

bool parse_string_255(In &in, word v, Str255 &s)
{
    s.at = in.pos;
    size_t n = MSGSIZE + 1;

    if(v >= 9)
    {
        dword len = in.l();

        if(len > 8193)
            return false;

        n = len;
    }

    s.text_at = in.pos;
    s.text_n = n;
    in.skip(n);
    in.skip(2);                 // next string
    s.tile_at = in.pos;
    in.skip(v >= 6 ? 4 : 2);
    in.skip(1);                 // cset
    s.trans_at = in.pos;
    in.skip(1 + 1 + 4);         // trans, font, x, y
    s.w_at = in.pos;
    in.skip(4 + 2);             // w, h, hspace, vspace
    s.flags_at = in.pos;
    in.skip(1);
    s.extra_at = in.pos;        // margins, portrait, shadow, draw layer
    in.skip((v >= 7 ? 13 : 0) + (v >= 8 ? 2 : 0) + (v >= 10 ? 1 : 0));
    s.sfx_at = in.pos;
    in.skip(v >= 12 ? 2 : 1);
    in.skip(2);                 // list position
    s.tail_at = in.pos;
    in.skip(v >= 13 ? 29 : 0);  // icons, scroll speeds, sounds
    s.end = in.pos;
    return !in.bad;
}

const long Q255_LAST_FRAME_TILE = NEWMAXTILES - 22;

long tile_255(long tile)
{
    return (tile < 0 || tile > Q255_LAST_FRAME_TILE) ? 0 : tile;
}

word frame_size_255(word file, bool old_frame)
{
    return old_frame ? file : (word)(file > 16 ? file - 16 : 0);
}

// From string version 11 on, the text is plain text with commands such as \2\5\ ; before
// that, the same binary form as 2.53's.
bool text_to_ascii(const char *s, std::string &out)
{
    std::string str(s, text_length(s));
    size_t last = 0;

    for(size_t i = str.size(); i > 0; --i)
        if(str[i - 1] != ' ')
        {
            last = i;
            break;
        }

    size_t cur = 0;

    while(cur < str.size())
    {
        byte c = (byte)str[cur];
        bool runs = false;
        int args = (cur < last && (c < 32 || c > 126)) ? q255_editor_command_args((int)c - 1, &runs) : -1;

        // A command this engine doesn't run was plain text when the quest was read, or the
        // reader would have left it out; it goes back as it is.
        if(args < 0 || !runs)
        {
            if(c == '\\')
                out += "\\\\";
            else if(c == '{')
                out += "\\{";
            else
                out += (char)c;

            ++cur;
            continue;
        }

        char buf[16];
        int code = (int)c - 1;
        std::string cmd;

        if(code == MSGC_NEWLINE && args == 0)
            cmd = "\n";
        else
        {
            sprintf(buf, "\\%d", code);
            cmd = buf;
        }

        for(int a = 0; a < args; ++a)
        {
            if(cur + 1 >= str.size())
                return false;           // cut short: the reader would keep it as it is

            ++cur;
            int value = (byte)str[cur] - 1;

            if(cur + 2 < str.size() && (byte)str[cur + 1] == 255)
            {
                value += 254 * (byte)str[cur + 2];
                cur += 2;
            }

            sprintf(buf, "\\%d", value);
            cmd += buf;
        }

        ++cur;

        if(cmd != "\n")
            cmd += "\\ ";               // the reader takes one space after a command

        out += cmd;
    }

    return true;
}

int write_text_255(Out &out, const std::vector<byte> &b, const Str255 *o, const MsgStr &m, int i, word v)
{
    const bool ascii = v >= 11;

    if(o)
    {
        char was[MSGSIZE + 1];
        bool dropped = false;

        if(!q255_editor_message_text(was, o->text_n ? (const char *)&b[o->text_at] : "", o->text_n, ascii, &dropped))
            return ZQW_BAD_PART;

        // what the reader left out of the text is only in the file
        if(same_text(was, m.s) && (dropped || !strict()))
        {
            out.copy(b, o->text_at - (v >= 9 ? 4 : 0), o->text_n + (v >= 9 ? 4 : 0));
            return ZQW_OK;
        }

        if(dropped)
            return refuse("String %d has text commands or text ZQuest can't show, which a change would lose.", i);
    }

    size_t n = text_length(m.s);

    if(n > MSGSIZE)
        return refuse("String %d has no end.", i);

    std::string text;

    if(ascii)
    {
        if(!text_to_ascii(m.s, text))
            return refuse("String %d ends in the middle of a command.", i);
    }
    else
        text.assign(m.s, n);

    char back[MSGSIZE + 1];

    if(!q255_editor_message_text(back, text.data(), text.size(), ascii, NULL) || !same_text(back, m.s))
        return refuse("String %d's text can't be written in this quest's version as it is.", i);

    if(v >= 9)
    {
        out.l((dword)text.size());
        out.put(text.data(), text.size());
    }
    else
    {
        out.put(text.data(), text.size());
        out.zeros(MSGSIZE + 1 - text.size());
    }

    return ZQW_OK;
}

int write_string_255(Out &out, const std::vector<byte> &b, const Str255 *o, const Str255 *tmpl, const MsgStr &m,
                     int i, word v, bool old_frame)
{
    int r = write_text_255(out, b, o, m, i, v);

    if(r)
        return r;

    out.w(m.nextstring);

    long was_tile = o ? (v >= 6 ? (long)(int)rd32(b, o->tile_at) : (long)rd16(b, o->tile_at)) : -1;

    if(o && !strict() && tile_255(was_tile) == m.tile)
        out.copy(b, o->tile_at, v >= 6 ? 4 : 2);
    else
    {
        if(m.tile < 0 || m.tile > Q255_LAST_FRAME_TILE)
            return refuse("String %d's frame tile, %d, can't be read back.", i, (int)m.tile);

        if(v >= 6)
            out.l((dword)m.tile);
        else
        {
            if((r = check_tile16(m.tile, i)) != 0)
                return r;

            out.w((int)m.tile);
        }
    }

    out.c(m.cset);

    if(o && (b[o->trans_at] != 0) == m.trans)
        out.copy(b, o->trans_at, 1);
    else
        out.c(m.trans ? 1 : 0);

    out.c(m.font);
    out.w(m.x);
    out.w(m.y);

    for(int k = 0; k < 2; ++k)
    {
        word now = k ? m.h : m.w;

        if(o && !strict() && frame_size_255(rd16(b, o->w_at + 2 * k), old_frame) == now)
            out.copy(b, o->w_at + 2 * k, 2);
        else
        {
            long file = old_frame ? (long)now : (long)now + 16;

            if(file > 0xFFFF)
                return refuse("String %d's frame is too big for this quest's version.", i);

            out.w((int)file);
        }
    }

    out.c(m.hspace);
    out.c(m.vspace);

    byte flags = m.stringflags;

    if(v < 13 && (flags & (STRINGFLAG_CENTER | STRINGFLAG_RIGHT)))
        return refuse("String %d is centred or right-aligned, which this quest's version doesn't hold.", i);

    if(o && !strict() && (byte)(b[o->flags_at] & (v < 13 ? ~(STRINGFLAG_CENTER | STRINGFLAG_RIGHT) : 0xFF)) == flags)
        out.copy(b, o->flags_at, 1);
    else
        out.c(flags);

    // margins, portrait, shadow, draw layer: nothing ZQuest has
    size_t extra = o ? o->sfx_at - o->extra_at : 0;

    if(o)
        out.copy(b, o->extra_at, extra);
    else if(tmpl)
        out.copy(b, tmpl->extra_at, tmpl->sfx_at - tmpl->extra_at);
    else
    {
        if(v >= 7)
        {
            const byte margins[4] = { 8, 8, 8, 8 };
            out.put(margins, 4);
            out.zeros(4 + 3);       // portrait tile, cset, x, y
            out.c(1);               // portrait size
            out.c(1);
        }

        if(v >= 8)
            out.zeros(2);

        if(v >= 10)
            out.c(6);               // draw layer
    }

    if(v >= 12)
    {
        if(o && !strict() && (rd16(b, o->sfx_at) > 255 ? 0 : rd16(b, o->sfx_at)) == m.sfx)
            out.copy(b, o->sfx_at, 2);
        else
            out.w(m.sfx);
    }
    else
        out.c(m.sfx);

    out.w(m.listpos);

    if(v >= 13)
    {
        if(o)
            out.copy(b, o->tail_at, 29);
        else if(tmpl)
            out.copy(b, tmpl->tail_at, 29);
        else
            out.zeros(29);
    }

    return ZQW_OK;
}

// Whether the quest's strings are read with 2.53's frame size (see q255_read_rules).
bool old_string_frame_255()
{
    if(zq_quest_file.build < 50)
        return true;

    const ZqQuestPart *p = find_part(ID_RULES);
    std::vector<byte> b;
    Rules255 r;

    if(!p || !part_bytes(*p, b) || !parse_rules_255(b, r))
        return true;

    int bit = Q255_QR_STRING_FRAME_OLD_WIDTH_HEIGHT;
    return r.version >= 15 && bit / 8 < (int)r.size && ((b[r.body + bit / 8] >> (bit % 8)) & 1);
}

int write_strings_255(PACKFILE *f, const std::vector<byte> &b)
{
    In in(b, 0);
    word v = in.w();
    in.w();
    dword size = in.l();

    if(in.bad || 8 + (size_t)size != b.size())
        return ZQW_BAD_PART;

    if(v < 5 || v > 13)
        return ZQW_UNSUPPORTED;

    word count = in.w();
    std::vector<Str255> recs(count);

    for(int i = 0; i < count; ++i)
        if(!parse_string_255(in, v, recs[i]))
            return ZQW_BAD_PART;

    if(in.pos != b.size())
        return ZQW_BAD_PART;

    const bool old_frame = old_string_frame_255();
    const Str255 *tmpl = count ? &recs[0] : NULL;       // string 0 keeps upstream's defaults
    Out out;
    out.copy(b, 0, 8);
    out.w(msg_count);

    for(int i = 0; i < msg_count; ++i)
    {
        const Str255 *o = i < count ? &recs[i] : NULL;
        const MsgStr *was = loaded_string(i);

        if(o && was && !zqw_force_serialize && same_string(MsgStrings[i], *was))
        {
            out.copy(b, o->at, o->end - o->at);
            continue;
        }

        int r = write_string_255(out, b, o, tmpl, MsgStrings[i], i, v, old_frame);

        if(r)
            return r;
    }

    out.set32(4, (dword)(out.b.size() - 8));
    return emit(f, ID_STRINGS, out.b);
}

int zqw_strings(PACKFILE *f, const ZqPartContext &ctx)
{
    std::vector<byte> b;

    if(!part_bytes(*ctx.part, b))
        return ZQW_BAD_PART;

    if(ctx.zelda_version == 0x255)
        return write_strings_255(f, b);

    if(ctx.zelda_version < 0x193)
        return ZQW_UNSUPPORTED;

    return write_strings_old(f, b);
}

// ---- the misc data --------------------------------------------------------------------

// A shop's or an info shop's three slots, as readmisc sorts them: slots whose item (or
// string) is missing move to the end, by three swaps. The sort moves empty slots among
// themselves too, so the slots ZQuest holds aren't always what to write: these find an
// order of them that sorts into what ZQuest holds.
struct Slot
{
    int key;            // hasitem, or the string
    int a, b;           // item and price, or price
};

void sort_slots(Slot *s)
{
    for(int j = 0; j < 2; ++j)
        for(int k = 0; k < 2 - j; ++k)
            if(s[k].key == 0)
            {
                Slot t = s[k];
                s[k] = s[k + 1];
                s[k + 1] = t;
            }
}

bool same_slots(const Slot *a, const Slot *b)
{
    for(int i = 0; i < 3; ++i)
        if(a[i].key != b[i].key || a[i].a != b[i].a || a[i].b != b[i].b)
            return false;

    return true;
}

// out: an order of now's slots that sorts into now. False if there is none.
bool unsort_slots(const Slot *now, Slot *out)
{
    static const int perms[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };

    for(int p = 0; p < 6; ++p)
    {
        Slot t[3];

        for(int i = 0; i < 3; ++i)
            t[i] = now[perms[p][i]];

        Slot s[3] = { t[0], t[1], t[2] };
        sort_slots(s);

        if(same_slots(s, now))
        {
            for(int i = 0; i < 3; ++i)
                out[i] = t[i];

            return true;
        }
    }

    return false;
}

void shop_slots(const shoptype &s, Slot *out)
{
    for(int j = 0; j < 3; ++j)
    {
        out[j].key = s.hasitem[j];
        out[j].a = s.item[j];
        out[j].b = s.price[j];
    }
}

void info_slots(const infotype &s, Slot *out)
{
    for(int j = 0; j < 3; ++j)
    {
        out[j].key = s.str[j];
        out[j].a = s.price[j];
        out[j].b = 0;
    }
}

bool all_zero(const void *p, size_t n)
{
    const byte *b = (const byte *)p;

    for(size_t i = 0; i < n; ++i)
        if(b[i])
            return false;

    return true;
}

// The last shop (info shop) holding anything, + 1: the reader clears those past its count.
int shops_in_use()
{
    for(int i = 255; i >= 0; --i)
        if(!all_zero(&misc.shop[i], sizeof(shoptype)))
            return i + 1;

    return 0;
}

int infos_in_use()
{
    for(int i = 255; i >= 0; --i)
        if(!all_zero(&misc.info[i], sizeof(infotype)))
            return i + 1;

    return 0;
}

// The reader leaves warp rings past its count as they were, which is what was loaded.
int rings_in_use(int limit)
{
    for(int i = limit - 1; i >= 0; --i)
        if(memcmp(&misc.warp[i], &g_loaded.misc.warp[i], sizeof(warpring)))
            return i + 1;

    return 0;
}

int write_shop(Out &out, int i, int name, int item_bytes, bool hasitem_stored)
{
    const shoptype &s = misc.shop[i];
    Slot now[3], order[3];
    shop_slots(s, now);

    if(!name && !all_zero(s.name, sizeof(s.name)))
        return refuse("Shop %d has a name, which this quest's version doesn't hold.", i);

    if(!unsort_slots(now, order))
        return refuse("Shop %d's items can't be written so they read back in this order.", i);

    if(!hasitem_stored)
        for(int j = 0; j < 3; ++j)
            if(order[j].key != (order[j].a != 0 ? 1 : 0))
                return refuse("Shop %d offers item 0, or lists an item it doesn't offer, which this quest's version doesn't hold.", i);

    if(name)
        out.put(s.name, sizeof(s.name));

    for(int j = 0; j < 3; ++j)
    {
        if(item_bytes == 2)
            out.w(order[j].a);
        else
            out.c(order[j].a);
    }

    for(int j = 0; j < 3; ++j)
        out.w(order[j].b);

    if(hasitem_stored)
        for(int j = 0; j < 3; ++j)
            out.c(order[j].key);

    return ZQW_OK;
}

int write_info(Out &out, int i, bool name)
{
    const infotype &s = misc.info[i];
    Slot now[3], order[3];
    info_slots(s, now);

    if(!name && !all_zero(s.name, sizeof(s.name)))
        return refuse("Info shop %d has a name, which this quest's version doesn't hold.", i);

    if(!unsort_slots(now, order))
        return refuse("Info shop %d's strings can't be written so they read back in this order.", i);

    if(name)
        out.put(s.name, sizeof(s.name));

    for(int j = 0; j < 3; ++j)
        out.w(order[j].key);

    for(int j = 0; j < 3; ++j)
        out.w(order[j].a);

    return ZQW_OK;
}

// Reads a written misc section back with readmisc, which changes nothing outside the misc
// data it is given, and checks that it gives what ZQuest holds. Before misc version 3 the
// section also holds the subscreen colours and the save game icons, which the save's own
// check for changed parts doesn't look at; this is where they are checked.
int read_back_misc_old(const std::vector<byte> &body)
{
    static miscQdata back;      // 26 KB, kept off the stack
    memcpy(&back, &misc, sizeof(back));     // what a load starts from: readmisc keeps what it doesn't read

    MemIn m;
    m.data = body.empty() ? NULL : &body[0];
    m.size = body.size();
    m.pos = 0;
    m.ungot = -1;

    zquestheader h;
    memset(&h, 0, sizeof(h));
    h.zelda_version = zq_quest_file.zelda_version;
    h.build = zq_quest_file.build;
    PACKFILE *pf = pack_fopen_vtable(&g_mem_in_vtable, &m);

    if(!pf)
        return ZQW_WRITE_FAILED;

    int r = readmisc(pf, &h, &back, true);
    bool all = mi_feof(&m) != 0;
    pack_fclose(pf);

    if(r || !all)
        return refuse("The misc data doesn't read back.");

    if(memcmp(back.shop, misc.shop, sizeof(misc.shop)) || memcmp(back.info, misc.info, sizeof(misc.info)))
        return refuse("The shops don't read back as they are.");

    if(memcmp(back.warp, misc.warp, sizeof(misc.warp)))
        return refuse("The warp rings don't read back as they are.");

    if(memcmp(back.triforce, misc.triforce, sizeof(misc.triforce)) || back.endstring != misc.endstring)
        return refuse("The triforce pieces or the end string don't read back as they are.");

    if(memcmp(&back.colors, &misc.colors, sizeof(misc.colors)) || memcmp(back.icons, misc.icons, sizeof(misc.icons)))
        return refuse("The misc colours or the save game icons don't read back as they are.");

    return ZQW_OK;
}

// readmisc, 1.93 to 2.53
int write_misc_old(PACKFILE *f, const std::vector<byte> &b)
{
    In in(b, 0);
    word v = in.w();
    in.w();
    dword size = in.l();

    const size_t shop_rec = (v > 6 ? 32 : 0) + 3 + 6 + (v > 3 ? 3 : 0);
    const size_t info_rec = (v > 6 ? 32 : 0) + 6 + 6;
    const int ring_n = 8 + (v > 5 ? 1 : 0);
    const size_t ring_rec = (size_t)ring_n * (v <= 3 ? 1 : 2) + ring_n + 1;
    const size_t wind_rec = (v <= 3 ? 1 : 2) + 1 + (v == 5 ? 1 : 0);

    word shops = in.w();
    size_t shops_at = in.pos;
    in.skip((size_t)shops * shop_rec);
    word infos = in.w();
    size_t infos_at = in.pos;
    in.skip((size_t)infos * info_rec);
    word rings = in.w();
    size_t rings_at = in.pos;
    in.skip((size_t)rings * ring_rec);
    word winds = 0;
    size_t winds_at = in.pos;

    if(v <= 5)
    {
        winds = in.w();
        winds_at = in.pos;
        in.skip((size_t)winds * wind_rec);
    }

    in.skip(8);         // triforce pieces
    size_t colors_at = in.pos;

    if(v < 3)
        in.skip(20 + 12 + 1 + (v > 1 ? 1 : 0) + 8);

    in.skip(2);         // end string

    if(in.bad || in.pos != b.size() || shops > 256 || infos > 256)
        return ZQW_BAD_PART;

    if(!zqw_force_serialize && !memcmp(&misc, &g_loaded.misc, sizeof(misc)))
        return emit(f, ID_MISC, b);

    const bool force = zqw_force_serialize;
    Out out;
    out.copy(b, 0, 8);
    int r = ZQW_OK;

    // shops
    int n = shops_in_use();
    n = n > shops ? n : shops;
    out.w(n);

    for(int i = 0; i < n && !r; ++i)
    {
        if(i < shops && !force && !memcmp(&misc.shop[i], &g_loaded.misc.shop[i], sizeof(shoptype)))
            out.copy(b, shops_at + i * shop_rec, shop_rec);
        else
            r = write_shop(out, i, v > 6, 1, v > 3);
    }

    // info shops
    n = infos_in_use();
    n = n > infos ? n : infos;
    out.w(n);

    for(int i = 0; i < n && !r; ++i)
    {
        if(i < infos && !force && !memcmp(&misc.info[i], &g_loaded.misc.info[i], sizeof(infotype)))
            out.copy(b, infos_at + i * info_rec, info_rec);
        else
            r = write_info(out, i, v > 6);
    }

    if(r)
        return r;

    // warp rings; before misc version 6 they have 8 places, and the 9th ring is the wind warps
    const int ring_limit = v <= 5 ? 8 : 9;
    n = rings_in_use(ring_limit);
    n = n > rings ? n : rings;
    out.w(n);

    for(int i = 0; i < n; ++i)
    {
        bool have = i < rings;

        // past what 2.53 holds (the reader runs on into the palette cycles), or a ring the wind
        // warps replace as they are read
        if(have && (i >= 9 || (v <= 5 && i == 8 && winds > 0)))
        {
            out.copy(b, rings_at + i * ring_rec, ring_rec);
            continue;
        }

        const warpring &w = misc.warp[i];
        const warpring &o = g_loaded.misc.warp[i];

        if(have && !force && !memcmp(&w, &o, sizeof(w)))
        {
            out.copy(b, rings_at + i * ring_rec, ring_rec);
            continue;
        }

        if(memcmp(w.name, o.name, sizeof(w.name)) || (ring_n < 9 && (w.dmap[8] != o.dmap[8] || w.scr[8] != o.scr[8])))
            return refuse("Warp ring %d has a name or a 9th place, which this quest's version doesn't hold.", i);

        for(int j = 0; j < ring_n; ++j)
        {
            if(v <= 3)
            {
                if(w.dmap[j] > 255)
                    return refuse("Warp ring %d goes to DMap %d, past what this quest's version holds.", i, w.dmap[j]);

                out.c(w.dmap[j]);
            }
            else
                out.w(w.dmap[j]);
        }

        out.put(w.scr, ring_n);
        out.c(w.size);
    }

    // wind warps
    if(v <= 5)
    {
        const warpring &w = misc.warp[8];
        const warpring &o = g_loaded.misc.warp[8];
        int last = 0;

        for(int j = 8; j >= 0 && !last; --j)
            if(w.dmap[j] != o.dmap[j] || w.scr[j] != o.scr[j])
                last = j + 1;

        n = last > winds ? last : winds;

        if(memcmp(w.name, o.name, sizeof(w.name)) || (n > 0 ? w.size != 9 : w.size != o.size))
            return refuse("The wind warps have a name or a size, which this quest's version doesn't hold.");

        out.w(n);

        for(int j = 0; j < n; ++j)
        {
            if(j < winds && !force && w.dmap[j] == o.dmap[j] && w.scr[j] == o.scr[j])
            {
                out.copy(b, winds_at + j * wind_rec, wind_rec);
                continue;
            }

            if(v <= 3)
            {
                if(w.dmap[j] > 255)
                    return refuse("A wind warp goes to DMap %d, past what this quest's version holds.", w.dmap[j]);

                out.c(w.dmap[j]);
            }
            else
                out.w(w.dmap[j]);

            out.c(w.scr[j]);

            if(v == 5)
            {
                if(j < winds)
                    out.copy(b, winds_at + j * wind_rec + wind_rec - 1, 1);
                else
                    out.c(0);
            }
        }
    }

    out.put(misc.triforce, 8);

    // before misc version 3 the subscreen colours, tiles and the save game icons are here
    if(v < 3)
    {
        const zcolors &c = misc.colors;
        const zcolors &o = g_loaded.misc.colors;
        bool same = !memcmp(&c, &o, sizeof(c)) && !memcmp(misc.icons, g_loaded.misc.icons, sizeof(misc.icons));

        if(same && !force)
            out.copy(b, colors_at, 20 + 12 + 1 + (v > 1 ? 1 : 0) + 8);
        else
        {
            if(c.msgtext != o.msgtext || memcmp(c.foo, o.foo, sizeof(c.foo)) || memcmp(c.foo2, o.foo2, sizeof(c.foo2)) ||
               (v < 2 && c.subscr_shadow != o.subscr_shadow))
                return refuse("The misc colours have settings this quest's version doesn't hold.");

            const byte head[20] =
            {
                c.text, c.caption, c.overw_bg, c.dngn_bg, c.dngn_fg, c.cave_fg, c.bs_dk, c.bs_goal, c.compass_lt, c.compass_dk,
                c.subscr_bg, c.triframe_color, c.link_dot, c.bmap_bg, c.bmap_fg, c.triforce_cset, c.triframe_cset,
                c.overworld_map_cset, c.dungeon_map_cset, c.blueframe_cset
            };
            const tile_id tiles[10] =
            {
                c.triforce_tile, c.triframe_tile, c.overworld_map_tile, c.dungeon_map_tile, c.blueframe_tile, c.HCpieces_tile,
                misc.icons[0], misc.icons[1], misc.icons[2], misc.icons[3]
            };

            for(int k = 0; k < 10; ++k)
                if(tiles[k] < 0 || tiles[k] > 0xFFFF)
                    return refuse("A subscreen or save game icon tile is past what this quest's version holds.");

            out.put(head, 20);

            for(int k = 0; k < 6; ++k)
                out.w((int)tiles[k]);

            out.c(c.HCpieces_cset);

            if(v > 1)
                out.c(c.subscr_shadow);

            for(int k = 6; k < 10; ++k)
                out.w((int)tiles[k]);
        }
    }

    out.w(misc.endstring);
    out.set32(4, new_size(size, b.size() - 8, out.b.size() - 8));

    if((r = read_back_misc_old(out.b)) != 0)
        return r;

    return emit(f, ID_MISC, out.b);
}

// q255_read_misc, versions 7 to 23
int write_misc_255(PACKFILE *f, const std::vector<byte> &b)
{
    In in(b, 0);
    word v = in.w();
    in.w();
    dword size = in.l();

    if(in.bad || 8 + (size_t)size != b.size())
        return ZQW_BAD_PART;

    if(v < 7 || v > 23)
        return ZQW_UNSUPPORTED;

    const int item_bytes = v >= 20 ? 2 : 1;
    const size_t shop_rec = 32 + 3 * item_bytes + 6 + 3;
    const size_t info_rec = 32 + 12;
    const size_t ring_rec = 18 + 9 + 1;

    word shops = in.w();
    size_t shops_at = in.pos;
    in.skip((size_t)shops * shop_rec);
    word infos = in.w();
    size_t infos_at = in.pos;
    in.skip((size_t)infos * info_rec);
    word rings = in.w();
    size_t rings_at = in.pos;
    in.skip((size_t)rings * ring_rec);
    in.skip(8 + 2);         // triforce pieces, end string
    size_t strs_at = in.pos;

    if(v >= 8)
        in.skip((size_t)shops * 6);     // the shops' item info strings

    size_t rest_at = in.pos;

    if(in.bad)
        return ZQW_BAD_PART;

    if(!zqw_force_serialize && !memcmp(&misc, &g_loaded.misc, sizeof(misc)))
        return emit(f, ID_MISC, b);

    const bool force = zqw_force_serialize;
    Out out;
    out.copy(b, 0, 8);
    int r = ZQW_OK;

    int n = shops_in_use();
    n = n > shops ? n : shops;
    out.w(n);

    for(int i = 0; i < n && !r; ++i)
    {
        size_t at = shops_at + i * shop_rec;

        if(i >= 256 || (i < shops && !force && !memcmp(&misc.shop[i], &g_loaded.misc.shop[i], sizeof(shoptype))))
        {
            out.copy(b, at, shop_rec);
            continue;
        }

        if(i < shops)
        {
            // as the reader has it: an item past 255 is an empty slot here
            In s(b, at + 32);
            shoptype was;
            memcpy(was.name, &b[at], 32);
            bool high = false;
            word items[3];

            for(int j = 0; j < 3; ++j)
                items[j] = item_bytes == 2 ? s.w() : s.c();

            for(int j = 0; j < 3; ++j)
                was.price[j] = s.w();

            for(int j = 0; j < 3; ++j)
                was.hasitem[j] = s.c();

            for(int j = 0; j < 3; ++j)
            {
                if(items[j] > 255)
                {
                    items[j] = 0;
                    was.hasitem[j] = 0;
                    high = true;
                }

                was.item[j] = (byte)items[j];
            }

            if(high)
            {
                Slot ws[3], ns[3];
                shop_slots(was, ws);
                shop_slots(misc.shop[i], ns);
                sort_slots(ws);

                if(same_slots(ws, ns) && !memcmp(was.name, misc.shop[i].name, 32))
                {
                    out.copy(b, at, shop_rec);
                    continue;
                }

                r = refuse("Shop %d sells an item past 255, which ZQuest can't show, so it can't be changed.", i);
                break;
            }
        }

        r = write_shop(out, i, 1, item_bytes, true);
    }

    if(r)
        return r;

    int new_shops = n;

    n = infos_in_use();
    n = n > infos ? n : infos;
    out.w(n);

    for(int i = 0; i < n && !r; ++i)
    {
        if(i >= 256 || (i < infos && !force && !memcmp(&misc.info[i], &g_loaded.misc.info[i], sizeof(infotype))))
            out.copy(b, infos_at + i * info_rec, info_rec);
        else
            r = write_info(out, i, true);
    }

    if(r)
        return r;

    n = rings_in_use(9);
    n = n > rings ? n : rings;
    out.w(n);

    for(int i = 0; i < n; ++i)
    {
        if(i >= 9 || (i < rings && !force && !memcmp(&misc.warp[i], &g_loaded.misc.warp[i], sizeof(warpring))))
        {
            out.copy(b, rings_at + i * ring_rec, ring_rec);
            continue;
        }

        const warpring &w = misc.warp[i];

        if(memcmp(w.name, g_loaded.misc.warp[i].name, sizeof(w.name)))
            return refuse("Warp ring %d has a name, which this quest's version doesn't hold.", i);

        for(int j = 0; j < 9; ++j)
            out.w(w.dmap[j]);

        out.put(w.scr, 9);
        out.c(w.size);
    }

    out.put(misc.triforce, 8);
    out.w(misc.endstring);

    if(v >= 8)
    {
        out.copy(b, strs_at, (size_t)shops * 6);

        if(new_shops > shops)
            out.zeros((size_t)(new_shops - shops) * 6);
    }

    out.copy(b, rest_at, b.size() - rest_at);       // sprites, bottles, sounds, save menus...
    out.set32(4, (dword)(out.b.size() - 8));
    return emit(f, ID_MISC, out.b);
}

int zqw_misc(PACKFILE *f, const ZqPartContext &ctx)
{
    std::vector<byte> b;

    if(!part_bytes(*ctx.part, b))
        return ZQW_BAD_PART;

    if(ctx.zelda_version == 0x255)
        return write_misc_255(f, b);

    if(ctx.zelda_version < 0x193)
        return ZQW_UNSUPPORTED;

    return write_misc_old(f, b);
}

ZqWriterRegistration g_reg_header(ID_HEADER, zqw_header, "header, 1.93 to 3.0");
ZqWriterRegistration g_reg_rules(ID_RULES, zqw_rules, "rules, every version");
ZqWriterRegistration g_reg_strings(ID_STRINGS, zqw_strings, "strings, every version");
ZqWriterRegistration g_reg_misc(ID_MISC, zqw_misc, "misc data, every version");

} // namespace

// Why the last of these writers refused to write a part.
const char *zqw_basic_refusal()
{
    return g_refusal.c_str();
}
