// Records as lists of named fields, and the schemas for the 2.55 / 3.0 Nightly item and enemy
// records (see zq_tape.h). The schemas follow q255_items.cpp's read_item and q255_guys.cpp's
// read_guy field for field; where those skip bytes, the schema names them.
//
// Written for the port.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>

#include "zq_tape.h"

// ---- the tape ---------------------------------------------------------------------------

static void put_count(std::vector<byte> &out, size_t n, int len_bytes)
{
    for(int k = 0; k < len_bytes; ++k)
        out.push_back((byte)((uint64_t)n >> (8 * k)));
}

void Tape::encode(std::vector<byte> &out) const
{
    for(size_t i = 0; i < f.size(); ++i)
    {
        const TapeField &x = f[i];

        switch(x.kind)
        {
        case TK_BLOB:
        case TK_TEXT:
        case TK_BITSTR:
        case TK_BITFIELD:
        case TK_BYTES:
            out.insert(out.end(), x.bytes.begin(), x.bytes.end());
            break;

        case TK_NLIST:
            put_count(out, x.width > 0 ? x.bytes.size() / (size_t)x.width : 0, x.index);
            out.insert(out.end(), x.bytes.begin(), x.bytes.end());
            break;

        case TK_CSTR:
        {
            const size_t n = x.bytes.size() < 255 ? x.bytes.size() : 255;
            put_count(out, n, 1);
            out.insert(out.end(), x.bytes.begin(), x.bytes.begin() + n);
            break;
        }

        case TK_WSTR:
        {
            const size_t n = x.bytes.size() < 65535 ? x.bytes.size() : 65535;
            put_count(out, n, 2);
            out.insert(out.end(), x.bytes.begin(), x.bytes.begin() + n);
            break;
        }

        case TK_LSTR:
            put_count(out, x.bytes.size(), 4);
            out.insert(out.end(), x.bytes.begin(), x.bytes.end());
            break;

        default:
            for(int k = 0; k < x.width; ++k)
                out.push_back((byte)((uint64_t)x.value >> (8 * k)));
        }
    }
}

TapeField *Tape::find(const char *key)
{
    for(size_t i = 0; i < f.size(); ++i)
        if(f[i].key == key)
            return &f[i];

    return NULL;
}

const TapeField *Tape::find(const char *key) const
{
    return const_cast<Tape *>(this)->find(key);
}

int64_t Tape::get(const char *key, int64_t dflt) const
{
    const TapeField *x = find(key);
    return x ? x->value : dflt;
}

TapeIndex::TapeIndex(const Tape &t)
{
    for(size_t i = 0; i < t.f.size(); ++i)
        m.insert(std::make_pair(t.f[i].key, &t.f[i]));
}

const TapeField *TapeIndex::find(const std::string &key) const
{
    std::map<std::string, const TapeField *>::const_iterator it = m.find(key);
    return it == m.end() ? NULL : it->second;
}

int64_t TapeIndex::get(const std::string &key, int64_t dflt) const
{
    const TapeField *x = find(key);
    return x ? x->value : dflt;
}

// ---- bitstrings (p_getbitstr / p_putbitstr) ---------------------------------------------

std::vector<byte> zq_bitstr_bits(const std::vector<byte> &b)
{
    std::vector<byte> out;
    size_t p = 0;

    if(b.size() < 2)
        return out;

    const size_t sz = (size_t)(b[0] | (b[1] << 8));
    p = 2;

    if(!sz || p + 3 > b.size())
        return out;

    const byte pairs = b[p];
    const size_t count = (size_t)(b[p + 1] | (b[p + 2] << 8));
    p += 3;

    for(size_t q = 0; q < count; ++q)
    {
        size_t idx = q;

        if(pairs)
        {
            if(p + 2 > b.size())
                break;

            idx = (size_t)(b[p] | (b[p + 1] << 8));
            p += 2;
        }

        if(p >= b.size())
            break;

        const byte val = b[p++];

        if(val && idx < sz)
        {
            if(idx >= out.size())
                out.resize(idx + 1, 0);

            out[idx] = val;
        }
    }

    return out;
}

unsigned zq_bitstr_size(const std::vector<byte> &b)
{
    return b.size() >= 2 ? (unsigned)(b[0] | (b[1] << 8)) : 65535;
}

std::vector<byte> zq_bitstr_store(const std::vector<byte> &bits, unsigned size)
{
    size_t cap = bits.size() < 65535 ? bits.size() : 65535;

    while(cap && !bits[cap - 1])
        --cap;

    size_t set = 0;

    for(size_t q = 0; q < cap; ++q)
        if(bits[q])
            ++set;

    if(size && cap > size)
        cap = size;

    const bool pairs = set * 3 <= cap;
    std::vector<byte> o;
    put_count(o, size ? size : 65535, 2);
    o.push_back(pairs ? 1 : 0);
    put_count(o, pairs ? set : cap, 2);

    for(size_t q = 0; q < cap; ++q)
    {
        if(pairs)
        {
            if(!bits[q])
                continue;

            put_count(o, q, 2);
        }

        o.push_back(bits[q]);
    }

    return o;
}

// ---- reading a record into one ----------------------------------------------------------

int64_t TapeParser::num(const char *key, const char *label, int width, bool sign, int kind, int list, int index,
                          const char *help)
{
    TapeField x;
    x.key = key;
    x.group = grp;
    x.label = label;
    x.help = help ? help : "";
    x.width = width;
    x.sign = sign;
    x.kind = kind;
    x.list = list;
    x.index = index;
    x.extra = extra_mode;
    x.gate = gate_bits;
    const int64_t dflt = have_def ? def_value : 0;
    have_def = false;

    if(src)
    {
        const TapeField *from = src->find(key);
        x.value = from ? from->value : dflt;
        t->f.push_back(x);
        return x.value;
    }

    if(bad || pos > n || (size_t)width > n - pos)
    {
        bad = true;
        return 0;
    }

    uint64_t v = 0;

    for(int k = 0; k < width; ++k)
        v |= (uint64_t)d[pos + k] << (8 * k);

    pos += width;

    if(sign && width < 8 && (v >> (8 * width - 1)) & 1)
        v |= ~0ULL << (8 * width);

    x.value = (int64_t)v;
    t->f.push_back(x);
    return x.value;
}

// A field of raw bytes, len of them from the record (or the source tape's, or zeros).
TapeField *TapeParser::take_raw(const char *key, const char *label, int kind, size_t len)
{
    TapeField x;
    x.key = key;
    x.group = grp;
    x.label = label;
    x.kind = kind;
    x.width = (int)len;
    x.extra = extra_mode;
    x.gate = gate_bits;
    have_def = false;

    if(src)
    {
        const TapeField *from = src->find(key);

        if(from && (from->bytes.size() == len || kind == TK_CSTR || kind == TK_WSTR || kind == TK_LSTR || kind == TK_BITSTR ||
                    kind == TK_NLIST))
            x.bytes = from->bytes;
        else
            x.bytes.assign(len, 0);

        t->f.push_back(x);
        return &t->f.back();
    }

    if(bad || pos > n || len > n - pos)
    {
        bad = true;
        return NULL;
    }

    x.bytes.assign(d + pos, d + pos + len);
    pos += len;
    t->f.push_back(x);
    return &t->f.back();
}

void TapeParser::nlist(const char *key, const char *label, int count_width, int elem_width, int list)
{
    size_t count = 0;

    if(!src)
    {
        if(bad || pos > n || (size_t)count_width > n - pos)
        {
            bad = true;
            return;
        }

        for(int k = 0; k < count_width; ++k)
            count |= (size_t)d[pos + k] << (8 * k);

        pos += count_width;
    }

    if(TapeField *f = take_raw(key, label, TK_NLIST, count * (size_t)elem_width))
    {
        f->width = elem_width;
        f->index = count_width;
        f->list = list;
    }
}

void TapeParser::blob(const char *key, const char *label, size_t len)
{
    take_raw(key, label, TK_BLOB, len);
}

void TapeParser::text(const char *key, const char *label, size_t len)
{
    take_raw(key, label, TK_TEXT, len);
}

void TapeParser::bytes(const char *key, const char *label, size_t len, int list)
{
    if(TapeField *f = take_raw(key, label, TK_BYTES, len))
        f->list = list;
}

uint64_t TapeParser::rd(int width)
{
    if(src || bad || pos > n || (size_t)width > n - pos)
    {
        bad = bad || !src;
        return 0;
    }

    uint64_t v = 0;

    for(int k = 0; k < width; ++k)
        v |= (uint64_t)d[pos + k] << (8 * k);

    pos += width;
    return v;
}

void TapeParser::rskip(size_t k)
{
    if(src || bad || pos > n || k > n - pos)
    {
        bad = bad || !src;
        return;
    }

    pos += k;
}

void TapeParser::raw(const char *key, const char *label, void (*walk)(TapeParser &), size_t empty_len)
{
    if(src)
    {
        const TapeField *from = src->find(key);
        TapeField *f = take_raw(key, label, TK_BLOB, from ? from->bytes.size() : empty_len);
        (void)f;
        return;
    }

    const size_t start = pos;
    walk(*this);

    if(bad)
        return;

    const size_t len = pos - start;
    pos = start;
    take_raw(key, label, TK_BLOB, len);
}

void TapeParser::bitfield(const char *key, const char *label, size_t len, int list)
{
    if(TapeField *f = take_raw(key, label, TK_BITFIELD, len))
        f->list = list;
}

// a count of len_bytes bytes, then that many characters
void TapeParser::counted(const char *key, const char *label, int len_bytes, int kind)
{
    if(src)
    {
        take_raw(key, label, kind, 0);
        return;
    }

    if(bad || pos > n || (size_t)len_bytes > n - pos)
    {
        bad = true;
        return;
    }

    size_t len = 0;

    for(int k = 0; k < len_bytes; ++k)
        len |= (size_t)d[pos + k] << (8 * k);

    pos += len_bytes;

    if(TapeField *f = take_raw(key, label, kind, len))
        f->width = 0;
}

void TapeParser::cstr(const char *key, const char *label) { counted(key, label, 1, TK_CSTR); }
void TapeParser::wstr(const char *key, const char *label) { counted(key, label, 2, TK_WSTR); }
void TapeParser::lstr(const char *key, const char *label) { counted(key, label, 4, TK_LSTR); }

// p_getbitstr: a word size; if it isn't 0, a pairs byte, a word count, then the entries
void TapeParser::bitstr(const char *key, const char *label, int list)
{
    if(src)
    {
        TapeField *f = take_raw(key, label, TK_BITSTR, 0);
        f->list = list;

        if(f->bytes.empty())
            f->bytes.assign(2, 0);  // an empty string: a size of 0

        return;
    }

    if(bad || pos > n || n - pos < 2)
    {
        bad = true;
        return;
    }

    size_t len = 2;
    const size_t sz = (size_t)(d[pos] | (d[pos + 1] << 8));

    if(sz)
    {
        if(n - pos < 5)
        {
            bad = true;
            return;
        }

        const byte pairs = d[pos + 2];
        const size_t count = (size_t)(d[pos + 3] | (d[pos + 4] << 8));
        len = 5 + count * (pairs ? 3 : 1);
    }

    if(TapeField *f = take_raw(key, label, TK_BITSTR, len))
    {
        f->list = list;
        f->width = 0;
    }
}

void TapeParser::script_config(const char *key, const char *label)
{
    char k[96], l[96];
    snprintf(k, sizeof(k), "%s.script", key);
    snprintf(l, sizeof(l), "%s", label);
    u(k, l, 2);
    snprintf(k, sizeof(k), "%s.argc", key);
    const int64_t argc = def(8).hidden(k, 2);

    for(int64_t q = 0; q < argc && !bad; ++q)
    {
        snprintf(k, sizeof(k), "%s.initd%d", key, (int)q);
        snprintf(l, sizeof(l), "%s InitD[%d]", label, (int)q);
        s(k, l, 4);
    }

    snprintf(k, sizeof(k), "%s.instc", key);
    const int64_t inst = hidden(k, 2);
    snprintf(k, sizeof(k), "%s.inst", key);
    blob(k, "instance values", (size_t)inst * 6);
}

// read_weap_data: a weapon's data with its own format version
void TapeParser::weapon_data(const char *key, const char *label)
{
    char k[96], l[128];
#define K(name) (snprintf(k, sizeof(k), "%s.%s", key, name), k)
#define L(name) (snprintf(l, sizeof(l), "%s: %s", label, name), l)
    const int64_t v = def(5).hidden(K("version"), 2);
    flags(K("flags"), L("Flags"), 2, TL_WEAPFLAGS);
    flags(K("moveflags"), L("Movement flags"), 4, TL_MOVEFLAGS);
    flags(K("wflags"), L("Weapon flags"), 2, TL_WFLAGS);

    for(int q = 0; q < 5 && !bad; ++q)
    {
        char nk[32], nl[48];
        snprintf(nk, sizeof(nk), "burnspr%d", q);
        snprintf(nl, sizeof(nl), "Burning sprite %d", q);
        u(K(nk), L(nl), v < 1 ? 1 : 2, TL_SPRITE);
        snprintf(nk, sizeof(nk), "lightrad%d", q);
        snprintf(nl, sizeof(nl), "Light radius %d", q);
        u(K(nk), L(nl), 1);

        if(v >= 3)
        {
            snprintf(nk, sizeof(nk), "lightofs%d", q);
            snprintf(nl, sizeof(nl), "Light offset %d", q);
            s(K(nk), L(nl), 4);
        }
    }

    u(K("glow"), L("Glow shape"), 1);
    flags(K("override"), L("Override flags"), 4, TL_SIZEFLAGS);
    s(K("tilew"), L("Tile width"), 4);
    s(K("tileh"), L("Tile height"), 4);
    s(K("hxsz"), L("Hit width"), 4);
    s(K("hysz"), L("Hit height"), 4);
    s(K("hzsz"), L("Hit Z height"), 4);
    s(K("hxofs"), L("Hit X offset"), 4);
    s(K("hyofs"), L("Hit Y offset"), 4);
    s(K("xofs"), L("Draw X offset"), 4);
    s(K("yofs"), L("Draw Y offset"), 4);
    s(K("step"), L("Step speed"), 4);
    u(K("unblockable"), L("Unblockable (bits)"), 1);
    s(K("timeout"), L("Timeout"), 4);
    u(K("imitate"), L("Imitate weapon"), 1);
    u(K("defdef"), L("Default defense"), 1);
    u(K("liftlvl"), L("Lift level"), 1);
    u(K("lifttime"), L("Lift time"), 1);
    s(K("lifthei"), L("Lift height"), 4);

    if(v < 5)
    {
        u(K("script"), L("Script"), 2);

        for(int q = 0; q < 8; ++q)
        {
            char nk[32], nl[48];
            snprintf(nk, sizeof(nk), "initd%d", q);
            snprintf(nl, sizeof(nl), "InitD[%d]", q);
            s(K(nk), L(nl), 4);
        }
    }
    else
    {
        std::string sk = std::string(key) + ".sc";
        std::string sl = std::string(label) + ": Script";
        script_config(sk.c_str(), sl.c_str());
    }

    s(K("pierce"), L("Pierce count"), 2);

    if(v >= 2)
    {
        s(K("bouncemult"), L("Bounce multiplier"), 4);
        s(K("bounceadd"), L("Bounce added"), 4);
    }

    if(v >= 4)
    {
        s(K("vpsuspend"), L("Viewport suspend range"), 4);
        s(K("vpdespawn"), L("Viewport despawn range"), 4);
    }
#undef K
#undef L
}

// ---- items (q255_items.cpp's read_item, versions 25 to 71) -----------------------------

static const char *const g_misc_keys[10] = { "misc1", "misc2", "misc3", "misc4", "misc5", "misc6", "misc7", "misc8", "misc9", "misc10" };

bool zq_tape_item(TapeParser &p, int v)
{
    char k[32], l[48];

    if(v >= 67)
    {
        p.group("Data");
        p.cstr("name", "Name");
    }

    p.group("Graphics");
    if(v > 35) p.s("tile", "Tile", 4); else p.u("tile", "Tile", 2);
    p.u("misc", "Flash and two-hand flags", 1);
    p.u("csets", "CSet (low) / flash CSet (high)", 1);
    p.u("frames", "Animation frames", 1);
    p.u("speed", "Animation speed", 1);
    p.u("delay", "Initial delay", 1);
    p.s("ltm", "Player tile modifier", 4);

    p.group("Data");
    if(v >= 31) p.s("family", "Class", 4, TL_ITEMCLASS); else p.u("family", "Class", 1, TL_ITEMCLASS);
    p.u("fam_type", "Level", 1);
    if(v >= 31) p.s("power", "Power", 4); else p.u("power", "Power", 1);
    p.flags("flags", "Flags", v >= 41 ? 4 : 2, TL_ITEMFLAGS);

    if(v < 71)
    {
        p.group("Scripts");
        p.u("script", "Action script", 2);
    }

    p.group("Data");
    p.s("count", "Counter", 1, TL_COUNTER);
    p.u("amount", "Increase amount (0x8000: gradual)", 2);

    if(v < 71)
    {
        p.group("Scripts");
        p.u("collect_script", "Pickup script", 2);
    }

    p.group("Data");
    p.s("setmax", "Increase counter max", 2);
    p.u("max", "But not above", 2);
    p.group("Pickup");
    p.u("playsound", "Pickup sound", v >= 66 ? 2 : 1, TL_SFX);

    if(v < 71)
    {
        p.group("Scripts");

        for(int j = 0; j < 8; ++j)
        {
            snprintf(k, sizeof(k), "initd%d", j);
            snprintf(l, sizeof(l), "InitD[%d]", j);
            p.s(k, l, 4);
        }
    }

    if(v < 67)
    {
        p.group("Scripts");
        p.u("inita0", "InitA[0]", 1);
        p.u("inita1", "InitA[1]", 1);
    }

    p.group("Sprites");

    for(int j = 0; j < 10; ++j)
    {
        snprintf(k, sizeof(k), "wpn%d", j);
        snprintf(l, sizeof(l), "Sprites[%d]", j);
        p.u(k, l, v >= 68 ? 2 : 1, TL_SPRITE, TK_ENUM, j);
    }

    p.group("Pickup");
    p.u("pickup_hearts", "Hearts required", 1);
    p.group("Data");
    p.s("misc1", "Attributes[0]", 4, TL_ITEMMISC, TK_INT, 0);
    p.s("misc2", "Attributes[1]", 4, TL_ITEMMISC, TK_INT, 1);
    p.group("Action");

    if(v >= 53)
    {
        p.s("cost0", "Cost", 2);
        p.s("cost1", "Second cost", 2);
    }
    else
        p.u("cost0", "Magic cost", 1);

    p.group("Data");

    for(int j = 2; j < 10; ++j)
    {
        snprintf(l, sizeof(l), "Attributes[%d]", j);
        p.s(g_misc_keys[j], l, 4, TL_ITEMMISC, TK_INT, j);
    }

    p.group("Action");

    if(v >= 66)
    {
        p.u("usesound", "Use sound", 2, TL_SFX, TK_ENUM, 0);
        p.u("usesound2", "Use sound 2", 2, TL_SFX, TK_ENUM, 1);
    }
    else
    {
        p.u("usesound", "Use sound", 1, TL_SFX, TK_ENUM, 0);

        if(v >= 49)
            p.u("usesound2", "Use sound 2", 1, TL_SFX, TK_ENUM, 1);
    }

    if(v >= 26)
    {
        p.group("Weapon");

        if(v < 63)
        {
            p.u("w.imitate", "Imitate weapon", 1);
            p.u("w.defdef", "Default defense", 1);
        }

        p.s("weaprange", "Range", 4);
        p.s("weapduration", "Duration", 4);

        for(int j = 0; j < 10; ++j)
        {
            snprintf(k, sizeof(k), "pattern%d", j);
            snprintf(l, sizeof(l), "Movement pattern[%d]", j);
            p.s(k, l, 4);
        }
    }

    if(v >= 27)
    {
        p.group("Weapon");
        p.s("duplicates", "Duplicates", 4);

        if(v < 63)
        {
            p.group("Scripts");

            for(int j = 0; j < 8; ++j)
            {
                snprintf(k, sizeof(k), "w.initd%d", j);
                snprintf(l, sizeof(l), "Weapon InitD[%d]", j);
                p.s(k, l, 4);
            }
        }

        if(v < 67)
        {
            p.group("Scripts");
            p.u("w.inita0", "Weapon InitA[0]", 1);
            p.u("w.inita1", "Weapon InitA[1]", 1);
        }

        p.group("Graphics");
        p.u("drawlayer", "Draw layer", 1);
        p.s("hxofs", "Hit X offset", 4);
        p.s("hyofs", "Hit Y offset", 4);
        p.s("hxsz", "Hit width", 4);
        p.s("hysz", "Hit height", 4);
        p.s("hzsz", "Hit Z height", 4);
        p.s("xofs", "Draw X offset", 4);
        p.s("yofs", "Draw Y offset", 4);

        if(v < 63)
        {
            p.group("Weapon");
            p.s("w.hxofs", "Hit X offset", 4);
            p.s("w.hyofs", "Hit Y offset", 4);
            p.s("w.hxsz", "Hit width", 4);
            p.s("w.hysz", "Hit height", 4);
            p.s("w.hzsz", "Hit Z height", 4);
            p.s("w.xofs", "Draw X offset", 4);
            p.s("w.yofs", "Draw Y offset", 4);
            p.group("Scripts");
            p.u("w.script", "Weapon script", 2);
        }

        p.group("Sprites");
        p.s("wpnsprite", "Weapon sprite (scripts)", 4);
        p.group("Action");
        p.s("costtimer0", "Cost timer", 4);

        if(v >= 53)
            p.s("costtimer1", "Second cost timer", 4);
    }

    if(v >= 28)
    {
        p.group("Graphics");
        p.flags("override", "Size override flags", 4, TL_SIZEFLAGS);
        p.s("tilew", "Tile width", 4);
        p.s("tileh", "Tile height", 4);
    }

    if(v >= 29 && v < 63)
    {
        p.group("Weapon");
        p.flags("w.override", "Size override flags", 4, TL_SIZEFLAGS);
        p.s("w.tilew", "Tile width", 4);
        p.s("w.tileh", "Tile height", 4);
    }

    p.group("Pickup");

    if(v >= 30)
        p.flags("pickup", "Pickup flags", 4, TL_PICKUPFLAGS);

    if(v >= 32)
        p.u("pstring", "Pickup string", 2);

    if(v >= 33)
        p.flags("psflags", "Pickup string flags", 2, TL_PSTRFLAGS);

    if(v >= 34)
    {
        p.group("Action");
        p.s("costctr0", "Cost counter", 1, TL_COUNTER);

        if(v >= 53)
            p.s("costctr1", "Second cost counter", 1, TL_COUNTER);
    }

    if(v >= 71)
    {
        p.group("Scripts");
        p.script_config("sc.action", "Action script");
        p.script_config("sc.pickup", "Pickup script");
        p.script_config("sc.sprite", "Sprite script");
    }
    else if(v >= 44)
    {
        p.group("Scripts");
        p.blob("labels", "script labels (unused)", 8 * (65 + (v < 63 ? 65 : 0) + 65 + 4));

        if(v < 67)
        {
            p.u("s.inita0", "Sprite InitA[0]", 1);
            p.u("s.inita1", "Sprite InitA[1]", 1);
        }

        p.u("sprite_script", "Sprite script", 2);
    }

    if(v >= 48)
    {
        p.group("Pickup");
        p.u("pickupflag", "How pickup flags apply", 1);
    }

    if(v >= 57)
    {
        p.group("Data");
        p.cstr("display_name", "Display name");
    }

    if(v >= 58 && v < 63)
    {
        p.group("Weapon");

        // each burning sprite, then (from 59) its light radius, as read_single_item_old reads them
        for(int j = 0; j < 5; ++j)
        {
            snprintf(k, sizeof(k), "burnspr%d", j);
            snprintf(l, sizeof(l), "Burning sprite %d", j);
            p.u(k, l, 1, TL_SPRITE);

            if(v >= 59)
            {
                snprintf(k, sizeof(k), "lightrad%d", j);
                snprintf(l, sizeof(l), "Light radius %d", j);
                p.u(k, l, 1);
            }
        }
    }

    if(v >= 60)
    {
        p.group("Pickup");
        p.u("pickup_litems", "Level items given", v >= 65 ? 2 : 1);
        p.s("pickup_litem_level", "For level (-1: current)", 2);
    }

    if(v >= 62)
    {
        p.group("Data");
        p.flags("moveflags", "Movement flags", 4, TL_MOVEFLAGS);

        if(v < 63)
        {
            p.group("Weapon");
            p.flags("w.moveflags", "Movement flags", 4, TL_MOVEFLAGS);
        }
    }

    if(v >= 63)
    {
        p.group("Weapon");
        p.weapon_data("wd", "Weapon");
    }

    if(v >= 64)
    {
        p.group("Action");
        p.s("cooldown", "Cooldown", 4);
    }

    if(v >= 70)
    {
        p.group("Data");
        p.s("vpsuspend", "Viewport suspend range", 4);
        p.s("vpdespawn", "Viewport despawn range", 4);
    }

    return !p.bad;
}

// ---- enemies (q255_guys.cpp's read_guy, versions 24 to 60) ------------------------------

static const char *const g_defense_names[41] =
{
    "Boomerang", "Bomb", "Super Bomb", "Arrow", "Fire", "Wand Melee", "Magic", "Hookshot", "Hammer", "Sword",
    "Sword Beam", "Reflected Beam", "Reflected Magic", "Reflected Fireball", "Reflected Rock", "Stomp Boots", "Byrna Beam",
    "Script (old)", "Unused (2.50)", "Quake Hammer", "Script 1", "Script 2", "Script 3", "Script 4", "Script 5",
    "Script 6", "Script 7", "Script 8", "Script 9", "Script 10", "Ice", "Bait", "Wind", "Sparkle", "Sonic",
    "Whistle", "Switch Hook", "Thrown", "Reflected Arrow", "Reflected Fire", "Reflected Fire 2"
};

bool zq_tape_guy(TapeParser &p, int v)
{
    char k[32], l[64];
    p.group("Flags");
    p.flags("flags", "Flags", 4, TL_GUYFLAGS);
    p.flags("flags2", "Flags 2", 4, TL_GUYFLAGS2);
    p.group("Graphics");
    if(v >= 36) p.u("tile", "Tile", 4); else p.u("tile", "Tile", 2);
    p.u("width", "Tile width", 1);
    p.u("height", "Tile height", 1);
    if(v >= 36) p.u("s_tile", "Special tile", 4); else p.u("s_tile", "Special tile", 2);
    p.u("s_width", "Special tile width", 1);
    p.u("s_height", "Special tile height", 1);
    if(v >= 36) p.u("e_tile", "New tile", 4); else p.u("e_tile", "New tile", 2);
    p.u("e_width", "New tile width", 1);
    p.u("e_height", "New tile height", 1);
    p.group("Data");
    p.s("hp", "HP", 2);
    p.s("family", "Class", 2, TL_ENEMYCLASS);
    p.group("Graphics");
    p.s("cset", "CSet", 2);
    p.s("anim", "Old animation", 2, TL_ANIM);
    p.s("e_anim", "New animation", 2, TL_ANIM);
    p.s("frate", "Old frame rate", 2);
    p.s("e_frate", "New frame rate", 2);
    p.group("Data");
    p.s("dp", "Contact damage", 2);
    p.s("wdp", "Weapon damage", 2);
    p.s("weapon", "Weapon", 2);
    p.s("rate", "Random rate", 2);
    p.s("hrate", "Halt rate", 2);
    p.s("step", "Step speed", 2);
    p.s("homing", "Homing factor", 2);
    p.s("grumble", "Hunger", 2);
    p.s("item_set", "Dropset", 2);
    p.group("Attributes");

    for(int j = 0; j < 10; ++j)
    {
        snprintf(k, sizeof(k), "attr%d", j);
        snprintf(l, sizeof(l), "Misc Attr. %d", j + 1);
        p.s(k, l, 4, TL_NONE, TK_INT, j);
    }

    p.group("Sounds");
    p.s("bgsfx", "Background sound", 2, TL_SFX);
    p.group("Graphics");
    p.s("bosspal", "Boss palette", 2);
    p.s("extend", "Extend (big tiles)", 2);
    p.group("Defenses");

    for(int j = 0; j < 19; ++j)
    {
        snprintf(k, sizeof(k), "def%d", j);
        p.u(k, g_defense_names[j], 1, TL_DEFENSE, TK_ENUM, j);
    }

    p.group("Sounds");
    p.u("hitsfx", "Hit sound", v < 55 ? 1 : 2, TL_SFX);
    p.u("deadsfx", "Death sound", v < 55 ? 1 : 2, TL_SFX);
    p.group("Attributes");
    p.s("attr10", "Misc Attr. 11", 4, TL_NONE, TK_INT, 10);
    p.s("attr11", "Misc Attr. 12", 4, TL_NONE, TK_INT, 11);

    if(v > 24)
    {
        p.group("Defenses");

        for(int j = 19; j < 41; ++j)
        {
            snprintf(k, sizeof(k), "def%d", j);
            p.u(k, g_defense_names[j], 1, TL_DEFENSE, TK_ENUM, j);
        }
    }

    if(v > 25)
    {
        p.group("Size");
        p.s("txsz", "Tile width (x)", 4);
        p.s("tysz", "Tile height (x)", 4);
        p.s("hxsz", "Hit width", 4);
        p.s("hysz", "Hit height", 4);
        p.s("hzsz", "Hit Z height", 4);
    }

    if(v > 26)
    {
        p.group("Size");
        p.s("hxofs", "Hit X offset", 4);
        p.s("hyofs", "Hit Y offset", 4);
        p.s("xofs", "Draw X offset", 4);
        p.s("yofs", "Draw Y offset", 4);
        p.s("zofs", "Draw Z offset", 4);
    }

    if(v > 27)
    {
        p.group("Weapon");
        p.s("wpnsprite", "Weapon sprite", 4, TL_SPRITE);
    }

    if(v > 28)
    {
        p.group("Size");
        p.flags("sizeflags", "Size override flags", 4, TL_SIZEFLAGS);
    }

    if(v >= 30)
    {
        p.group("Graphics");
        p.s("frozentile", "Frozen tile", 4);
        p.s("frozencset", "Frozen CSet", 4);
        p.s("frozenclock", "Frozen clock", 4);

        for(int j = 0; j < 10; ++j)
        {
            snprintf(k, sizeof(k), "frozenmisc%d", j);
            snprintf(l, sizeof(l), "Frozen misc %d", j);
            p.s(k, l, 2);
        }
    }

    if(v >= 34)
    {
        p.group("Sounds");
        p.s("firesfx", "Weapon sound", 2, TL_SFX);
        p.group("Attributes");

        for(int j = 15; j < 32; ++j)
        {
            snprintf(k, sizeof(k), "attr%d", j);
            snprintf(l, sizeof(l), "Misc Attr. %d", j + 1);
            p.s(k, l, 4, TL_NONE, TK_INT, j);
        }

        p.group("Movement");

        for(int j = 0; j < 32; ++j)
        {
            snprintf(k, sizeof(k), "movement%d", j);
            snprintf(l, sizeof(l), "Movement[%d]", j);
            p.s(k, l, 4);
        }

        p.group("Weapon");

        for(int j = 0; j < 32; ++j)
        {
            snprintf(k, sizeof(k), "new_weapon%d", j);
            snprintf(l, sizeof(l), "Weapon data[%d]", j);
            p.s(k, l, 4);
        }

        p.group("Scripts");

        if(v < 59)
        {
            p.u("script", "Enemy script", 2);

            for(int j = 0; j < 8; ++j)
            {
                snprintf(k, sizeof(k), "initd%d", j);
                snprintf(l, sizeof(l), "InitD[%d]", j);
                p.s(k, l, 4);
            }

            p.s("inita0", "InitA[0] (unused)", 4);
            p.s("inita1", "InitA[1] (unused)", 4);
        }
        else
            p.script_config("sc", "Enemy script");
    }

    if(v >= 37)
    {
        p.group("Flags");
        p.flags("editorflags", "Behaviour flags", 4, TL_GUYBFLAGS);
    }

    if(v >= 38)
    {
        p.group("Attributes");
        p.s("attr12", "Misc Attr. 13", 4, TL_NONE, TK_INT, 12);
        p.s("attr13", "Misc Attr. 14", 4, TL_NONE, TK_INT, 13);
        p.s("attr14", "Misc Attr. 15", 4, TL_NONE, TK_INT, 14);
    }

    if(v >= 39)
    {
        p.group("Scripts");

        if(v < 59)
            p.blob("labels", "InitD labels (unused)", 8 * 65);

        if(v < 54)
            p.blob("wlabels", "Weapon InitD labels (unused)", 8 * 65);
    }

    if(v >= 40 && v < 54)
    {
        p.group("Scripts");
        p.u("w.script", "Weapon script", 2);
    }

    if(v >= 41 && v < 54)
    {
        p.group("Scripts");

        for(int j = 0; j < 8; ++j)
        {
            snprintf(k, sizeof(k), "w.initd%d", j);
            snprintf(l, sizeof(l), "Weapon InitD[%d]", j);
            p.s(k, l, 4);
        }
    }

    p.group("Flags");

    if(v >= 47)
        p.flags("moveflags", "Movement flags", 4, TL_MOVEFLAGS);
    else if(v >= 42)
        p.flags("moveflags", "Movement flags", 1, TL_MOVEFLAGS);

    p.group("Graphics");

    if(v > 55)
    {
        p.u("spr_shadow", "Shadow sprite", 2, TL_SPRITE);
        p.u("spr_death", "Death sprite", 2, TL_SPRITE);
        p.u("spr_spawn", "Spawn sprite", 2, TL_SPRITE);
    }
    else if(v > 44)
    {
        p.u("spr_shadow", "Shadow sprite", 1, TL_SPRITE);
        p.u("spr_death", "Death sprite", 1, TL_SPRITE);
        p.u("spr_spawn", "Spawn sprite", 1, TL_SPRITE);
    }

    if(v >= 52 && v < 54)
    {
        p.group("Weapon");
        p.u("w.unblockable", "Unblockable (bits)", 1);
        p.flags("w.moveflags", "Movement flags", 4, TL_MOVEFLAGS);
        p.flags("w.override", "Size override flags", 4, TL_SIZEFLAGS);
        p.s("w.tilew", "Tile width", 4);
        p.s("w.tileh", "Tile height", 4);
        p.s("w.hxsz", "Hit width", 4);
        p.s("w.hysz", "Hit height", 4);
        p.s("w.hzsz", "Hit Z height", 4);
        p.s("w.hxofs", "Hit X offset", 4);
        p.s("w.hyofs", "Hit Y offset", 4);
        p.s("w.xofs", "Draw X offset", 4);
        p.s("w.yofs", "Draw Y offset", 4);
        p.s("w.step", "Step speed", 4);

        for(int j = 0; j < 5; ++j)
        {
            snprintf(k, sizeof(k), "w.burnspr%d", j);
            snprintf(l, sizeof(l), "Burning sprite %d", j);
            p.u(k, l, 2, TL_SPRITE);
        }

        for(int j = 0; j < 5; ++j)
        {
            snprintf(k, sizeof(k), "w.lightrad%d", j);
            snprintf(l, sizeof(l), "Light radius %d", j);
            p.u(k, l, 2);
        }
    }

    if(v >= 53)
    {
        p.group("Sounds");
        p.u("specialsfx", "Special sound", v >= 55 ? 2 : 1, TL_SFX);
    }

    if(v >= 54)
    {
        p.group("Weapon");
        p.weapon_data("wd", "Weapon");
    }

    if(v >= 58)
    {
        p.group("Data");
        p.s("vpsuspend", "Viewport suspend range", 4);
        p.s("vpdespawn", "Viewport despawn range", 4);
    }

    if(v >= 60)
    {
        p.group("Sounds");
        p.u("firesfx2", "Second weapon sound", 2, TL_SFX);
    }

    return !p.bad;
}

// ---- DMaps (q255_maps.cpp's read_dmap, versions 9 to 28) --------------------------------

static const unsigned DMAP_GRAVITY = 0x800000;     // dmfCUSTOM_GRAVITY: the DMap has gravity fields
static const unsigned DMAP_VIEWPORT = 0x1000000;   // dmfCUSTOM_VIEWPORT_FOLLOW: and viewport fields

bool zq_tape_dmap(TapeParser &p, int v)
{
    char k[32], l[64];
    p.group("Data");
    p.u("map", "Map", 1);
    p.u("level", "Level", 2);
    p.s("xoff", "Map X offset", 1);
    p.u("compass", "Compass screen", 1);
    p.u("color", "Palette", 2);

    if(v < 25)
    {
        p.group("Music");
        p.u("midi", "MIDI (0 none, 1 overworld, 2 dungeon, 3 level 9, 4+ custom)", 1);
    }

    p.group("Data");
    p.u("cont", "Continue screen", 1);
    p.u("type", "Type (low bits) and continue flag (128)", 1);
    p.group("Map grid");

    for(int q = 0; q < 8; ++q)
    {
        snprintf(k, sizeof(k), "grid%d", q);
        snprintf(l, sizeof(l), "Row %d (bits: columns)", q);
        p.u(k, l, 1);
    }

    p.group("Data");
    p.text("name", "Name", 21);

    if(v < 20)
        p.text("title", "Title", 21);
    else
        p.wstr("title", "Title");

    p.text("intro", "Intro", 73);
    p.group("Map graphics");
    const int tb = v >= 11 ? 4 : 2;
    p.u("minimap1_tile", "Minimap (no map) tile", tb);
    p.u("minimap1_cset", "Minimap (no map) CSet", 1);
    p.u("minimap2_tile", "Minimap (with map) tile", tb);
    p.u("minimap2_cset", "Minimap (with map) CSet", 1);
    p.u("largemap1_tile", "Large map (no map) tile", tb);
    p.u("largemap1_cset", "Large map (no map) CSet", 1);
    p.u("largemap2_tile", "Large map (with map) tile", tb);
    p.u("largemap2_cset", "Large map (with map) CSet", 1);

    if(v < 25)
    {
        p.group("Music");
        p.text("tmusic", "Music file", 56);
        p.u("tmusictrack", "Music track", 1);
    }

    p.group("Subscreens");
    p.u("active_subscreen", "Active subscreen", 1);
    p.u("passive_subscreen", "Passive subscreen", 1);
    p.group("Disabled items");

    if(v >= 26)
        p.bitstr("disabled", "Disabled items", TL_ITEMBITS);
    else
        p.bitfield("disabled", "Disabled items", 32, TL_ITEMBITS);

    p.group("Flags");
    const int64_t flags = p.flags("flags", "Flags", 4, TL_DMAPFLAGS);

    if(v >= 10)
        p.u("sideview", "Sideview (old)", 1);

    p.group("Scripts");

    if(v >= 27)
    {
        p.script_config("sc.active", "DMap script");
        p.script_config("sc.asub", "Active subscreen script");
        p.script_config("sc.psub", "Passive subscreen script");
        p.script_config("sc.onmap", "Map script");
    }
    else
    {
        if(v >= 12)
        {
            p.u("script", "DMap script", 2);

            for(int q = 0; q < 8; ++q)
            {
                snprintf(k, sizeof(k), "initd%d", q);
                snprintf(l, sizeof(l), "DMap script InitD[%d]", q);
                p.s(k, l, 4);
            }
        }

        if(v >= 13)
            p.blob("labels", "InitD labels (unused)", 8 * 65);

        if(v >= 14)
        {
            p.u("asub_script", "Active subscreen script", 2);
            p.u("psub_script", "Passive subscreen script", 2);

            for(int q = 0; q < 8; ++q)
            {
                snprintf(k, sizeof(k), "sub_initd%d", q);
                snprintf(l, sizeof(l), "Subscreen scripts InitD[%d]", q);
                p.s(k, l, 4);
            }

            p.blob("sub_labels", "subscreen InitD labels (unused)", 8 * 65);
        }

        if(v >= 15)
        {
            p.u("onmap_script", "Map script", 2);

            for(int q = 0; q < 8; ++q)
            {
                snprintf(k, sizeof(k), "onmap_initd%d", q);
                snprintf(l, sizeof(l), "Map script InitD[%d]", q);
                p.s(k, l, 4);
            }

            p.blob("onmap_labels", "map script InitD labels (unused)", 8 * 65);
        }
    }

    p.group("Data");

    if(v >= 16)
        p.def(-1).s("mirror", "Mirror DMap (-1 none)", 2);

    if(v >= 18 && v < 25)
    {
        p.group("Music");
        p.s("loop_start", "Loop start", 4);
        p.s("loop_end", "Loop end", 4);
        p.s("xfade_in", "Crossfade in", 4);
        p.s("xfade_out", "Crossfade out", 4);
    }

    p.group("Subscreens");

    if(v >= 19)
        p.u("overlay_subscreen", "Overlay subscreen", 1);

    if(v >= 20)
    {
        p.group("Data");
        p.s("intro_string", "Intro string", 4);
    }

    if(v == 21)
        p.blob("regions", "region ids (unused)", 64);

    if(v > 22 && (flags & DMAP_GRAVITY))
    {
        p.group("Gravity and viewport");
        p.s("gravity", "Gravity (fixed point)", 4);
        p.s("terminal_v", "Terminal velocity (fixed point)", 4);
    }

    if(v >= 28 && (flags & DMAP_VIEWPORT))
    {
        p.group("Gravity and viewport");
        p.u("vf.deadzone_w", "Viewport dead zone width", 1);
        p.u("vf.deadzone_h", "Viewport dead zone height", 1);
        p.u("vf.lookahead_x", "Viewport lookahead X", 1);
        p.u("vf.lookahead_y", "Viewport lookahead Y", 1);
        p.s("vf.lookahead_speed", "Viewport lookahead speed (fixed point)", 4);
        p.s("vf.recenter_speed", "Viewport recenter speed (fixed point)", 4);
        p.u("vf.recenter_delay", "Viewport recenter delay", 2);
    }

    if(v > 23)
    {
        p.group("Subscreens");
        p.def(-1).s("map_subscreen", "Map subscreen (-1 none)", 2);
        p.group("Data");
        p.u("floor", "Floor", 1);
    }

    if(v >= 25)
    {
        p.group("Music");
        p.u("music", "Music (quest music object)", 2);
    }

    return !p.bad;
}

// ---- screens (q255_maps.cpp's read_screen and read_screen_new, versions 18 to 41) ---------

// the screen's "has" bits (v23+)
enum
{
    SHAS_ROOMDATA = 0x0001, SHAS_ITEM = 0x0002, SHAS_TWARP = 0x0004, SHAS_SWARP = 0x0008, SHAS_WARPRET = 0x0010,
    SHAS_LAYERS = 0x0020, SHAS_MAZE = 0x0040, SHAS_D_S_U = 0x0080, SHAS_FLAGS = 0x0100, SHAS_ENEMY = 0x0200,
    SHAS_CARRY = 0x0400, SHAS_SCRIPT = 0x0800, SHAS_SECRETS = 0x2000, SHAS_COMBOFLAG = 0x4000, SHAS_MISC = 0x8000
};

static void bytes_n(TapeParser &p, const char *key, const char *label, int n, int width = 1, int64_t dflt = 0)
{
    char k[48], l[80];

    for(int q = 0; q < n; ++q)
    {
        snprintf(k, sizeof(k), "%s%d", key, q);
        snprintf(l, sizeof(l), "%s %d", label, q);
        p.def(dflt).u(k, l, width);
    }
}

static void screen_ffc(TapeParser &p, int v, int m, bool masked, bool flat)
{
    char k[48], g[32];
    snprintf(g, sizeof(g), "FFC %d", m + 1);
    p.group(g);
    p.gate(0);
#define F(name) (snprintf(k, sizeof(k), "ffc%d.%s", m, name), k)
    const int64_t combo = p.u(F("combo"), "Combo", 2);

    if(!flat && !masked && combo == 0)
        return;     // an empty slot is only the combo

    p.u(F("cset"), "CSet", 1);
    p.u(F("delay"), "Delay", 2);
    p.s(F("x"), "X (fixed point)", 4);
    p.s(F("y"), "Y (fixed point)", 4);
    p.s(F("vx"), "X speed", 4);
    p.s(F("vy"), "Y speed", 4);
    p.s(F("ax"), "X acceleration", 4);
    p.s(F("ay"), "Y acceleration", 4);
    p.u(F("link"), "Linked to FFC", 1);

    if(flat || v < 24)
    {
        p.u(F("width"), "Width (hit and tiles)", 1);
        p.u(F("height"), "Height (hit and tiles)", 1);
    }
    else
    {
        p.s(F("hit_w"), "Hit width", 4);
        p.s(F("hit_h"), "Hit height", 4);
        p.u(F("tiles_w"), "Tile width", 1);
        p.u(F("tiles_h"), "Tile height", 1);
    }

    p.flags(F("flags"), "Flags", 4, TL_FFCFLAGS);

    if(flat || v < 41)
    {
        p.u(F("script"), "Script", 2);

        for(int q = 0; q < 8; ++q)
        {
            char kk[48], l[24];
            snprintf(kk, sizeof(kk), "ffc%d.initd%d", m, q);
            snprintf(l, sizeof(l), "InitD[%d]", q);
            p.s(kk, l, 4);
        }
    }
    else
    {
        snprintf(k, sizeof(k), "ffc%d.sc", m);
        p.script_config(k, "Script");
    }

    if(flat || v < 33)
    {
        p.u(F("inita0"), "InitA[0]", 1);
        p.u(F("inita1"), "InitA[1]", 1);
    }
    else
    {
        p.extra(true);
        p.u(F("layer"), "Layer", 1);
        p.extra(false);
    }

    if(!flat && v >= 40)
    {
        p.extra(true);
        p.s(F("vpsuspend"), "Viewport suspend range", 4);
        p.s(F("vpdespawn"), "Viewport despawn range", 4);
        p.extra(false);
    }
#undef F
}

static void flags_1_to_10(TapeParser &p, int64_t *flags10, bool carry_between)
{
    for(int q = 1; q <= 10; ++q)
    {
        char k[16], l[16];
        snprintf(k, sizeof(k), q == 1 ? "flags" : "flags%d", q);
        snprintf(l, sizeof(l), "Flags %d", q);

        if(q == 6 && carry_between)
        {
            p.u("noreset", "No reset", 2);
            p.u("nocarry", "No carry", 2);
        }

        const int64_t f = p.u(k, l, 1);

        if(q == 10 && flags10)
            *flags10 = f;
    }
}

// v18-22: every field in a fixed order
static bool screen_flat(TapeParser &p, int v)
{
    p.group("Screen");
    p.u("valid", "Valid", 1);
    p.u("guy", "Guy", 1);
    p.u("str", "String", 2);
    p.u("room", "Room type", 1);
    p.u("item", "Item", 1);
    p.u("hasitem", "Has item", 1);
    bytes_n(p, "tilewarptype", "Tile warp type", 4);
    p.u("door_combo_set", "Door combo set", 2);
    bytes_n(p, "warpreturnx", "Warp return X", 4);
    bytes_n(p, "warpreturny", "Warp return Y", 4);
    p.u("warpreturnc", "Warp return combo", 2);
    p.u("stairx", "Stair X", 1);
    p.u("stairy", "Stair Y", 1);
    p.u("itemx", "Item X", 1);
    p.u("itemy", "Item Y", 1);
    p.u("color", "Palette", 2);
    p.u("enemyflags", "Enemy flags", 1);
    bytes_n(p, "door", "Door", 4);
    bytes_n(p, "tilewarpdmap", "Tile warp DMap", 4, 2);
    bytes_n(p, "tilewarpscr", "Tile warp screen", 4);
    p.u("tilewarpoverlayflags", "Tile warp overlay flags", 1);
    p.u("exitdir", "Exit direction", 1);
    bytes_n(p, "enemy", "Enemy", 10, 2);
    p.u("pattern", "Enemy pattern", 1);
    bytes_n(p, "sidewarptype", "Side warp type", 4);
    p.u("sidewarpoverlayflags", "Side warp overlay flags", 1);
    p.u("warparrivalx", "Warp arrival X", 1);
    p.u("warparrivaly", "Warp arrival Y", 1);
    bytes_n(p, "path", "Maze path", 4);
    bytes_n(p, "sidewarpscr", "Side warp screen", 4);
    bytes_n(p, "sidewarpdmap", "Side warp DMap", 4, 2);
    p.u("sidewarpindex", "Side warp index", 1);
    p.u("undercombo", "Under combo", 2);
    p.u("undercset", "Under CSet", 1);
    p.u("catchall", "Catch-all", 2);
    flags_1_to_10(p, NULL, true);
    p.u("csensitive", "Combo sensitivity", 1);
    p.u("oceansfx", "Ambient sound", 1);
    p.u("bosssfx", "Boss sound", 1);
    p.u("secretsfx", "Secret sound", 1);
    p.u("holdupsfx", "Hold-up sound", 1);
    bytes_n(p, "layermap", "Layer map", 6);
    bytes_n(p, "layerscreen", "Layer screen", 6);
    bytes_n(p, "layeropacity", "Layer opacity", 6);
    p.u("timedwarptics", "Timed warp tics", 2);
    p.u("nextmap", "Next map", 1);
    p.u("nextscr", "Next screen", 1);
    p.blob("secretcombo", "secret combos", 256);
    p.blob("secretcset", "secret csets", 128);
    p.blob("secretflag", "secret flags", 128);
    p.blob("data", "combos", 352);
    p.blob("sflag", "placed flags", 176);
    p.blob("cset", "combo csets", 176);
    p.s("screen_midi", "Screen MIDI", 2);
    p.u("lens_layer", "Lens layer", 1);
    const int64_t numff = p.u("numff", "FFCs in use (bits)", 4);

    for(int m = 0; m < 32 && !p.bad; ++m)
        if((numff >> m) & 1)
            screen_ffc(p, v, m, true, true);

    p.group("Screen");

    if(v >= 19)
        p.blob("unused19", "unused arrays", 100);

    p.extra(true);
    p.group("Script");

    if(v >= 20)
    {
        p.u("script", "Screen script", 2);

        for(int q = 0; q < 8; ++q)
        {
            char k[16], l[24];
            snprintf(k, sizeof(k), "initd%d", q);
            snprintf(l, sizeof(l), "InitD[%d]", q);
            p.s(k, l, 4);
        }
    }

    if(v >= 21)
        p.u("preloadscript", "Run the script before the screen scrolls in", 1);

    if(v >= 22)
    {
        p.group("Layers");
        p.flags("hidelayers", "Hidden layers", 1, TL_LAYERBITS);
        p.flags("hidescriptlayers", "Layers hidden from scripts", 1, TL_LAYERBITS);
    }

    p.extra(false);
    return !p.bad;
}

bool zq_tape_screen(TapeParser &p, int v)
{
    if(v < 23)
        return screen_flat(p, v);

    p.group("Screen");
    const int64_t valid = p.u("valid", "Valid", 1);

    if(!(valid & mVALID))
        return !p.bad;

    const int64_t has = p.hidden("has", 4);
    int64_t flags10 = 0;

    p.gate(SHAS_ROOMDATA);

    if(has & SHAS_ROOMDATA)
    {
        p.group("Room");
        p.u("guy", "Guy", 1);

        if(v > 26)
        {
            p.extra(true);
            p.s("guytile", "Guy tile", 4);
            p.u("guycs", "Guy CSet", 1);
            p.flags("roomflags", "Room flags", 2, TL_ROOMFLAGS);
            p.extra(false);
        }

        p.u("str", "String", 2);
        p.u("room", "Room type", 1);
        p.u("catchall", "Catch-all", 2);
    }

    p.gate(SHAS_ITEM);

    if(has & SHAS_ITEM)
    {
        p.group("Screen");
        p.u("item", "Item", v < 39 ? 1 : 2);
        p.u("hasitem", "Has item", 1);
        p.u("itemx", "Item X", 1);
        p.u("itemy", "Item Y", 1);
    }

    p.gate(SHAS_SWARP | SHAS_TWARP);

    if(has & (SHAS_SWARP | SHAS_TWARP))
    {
        p.group("Warps");
        p.u("warpreturnc", "Warp return combo", 2);
    }

    p.gate(SHAS_TWARP);

    if(has & SHAS_TWARP)
    {
        p.group("Warps");
        bytes_n(p, "tilewarptype", "Tile warp type", 4);
        bytes_n(p, "tilewarpdmap", "Tile warp DMap", 4, 2);
        bytes_n(p, "tilewarpscr", "Tile warp screen", 4);
        p.u("tilewarpoverlayflags", "Tile warp overlay flags", 1);
    }

    p.gate(SHAS_SWARP);

    if(has & SHAS_SWARP)
    {
        p.group("Warps");
        bytes_n(p, "sidewarptype", "Side warp type", 4, 1, wtSCROLL);   // what an absent group reads as
        bytes_n(p, "sidewarpdmap", "Side warp DMap", 4, 2);
        bytes_n(p, "sidewarpscr", "Side warp screen", 4);
        p.u("sidewarpoverlayflags", "Side warp overlay flags", 1);
        p.u("sidewarpindex", "Side warp index", 1);
    }

    p.gate(SHAS_WARPRET);

    if(has & SHAS_WARPRET)
    {
        p.group("Warps");
        bytes_n(p, "warpreturnx", "Warp return X", 4);
        bytes_n(p, "warpreturny", "Warp return Y", 4);
        p.u("warparrivalx", "Warp arrival X", 1);
        p.u("warparrivaly", "Warp arrival Y", 1);
    }

    p.gate(SHAS_LAYERS);

    if(has & SHAS_LAYERS)
    {
        p.group("Layers");
        bytes_n(p, "layermap", "Layer map", 6);
        bytes_n(p, "layerscreen", "Layer screen", 6);
        bytes_n(p, "layeropacity", "Layer opacity", 6, 1, 255);    // clear_screen's, what an absent group reads as
        p.extra(true);
        p.flags("hidelayers", "Hidden layers", 1, TL_LAYERBITS);
        p.flags("hidescriptlayers", "Layers hidden from scripts", 1, TL_LAYERBITS);
        p.extra(false);
    }

    p.gate(SHAS_MAZE);

    if(has & SHAS_MAZE)
    {
        p.group("Maze");
        bytes_n(p, "path", "Maze path", 4);
        p.u("exitdir", "Exit direction", 1);

        if(v >= 32)
        {
            p.extra(true);
            p.u("maze_wipe", "Maze transition wipe", 1);
            p.extra(false);
        }
    }

    p.gate(SHAS_D_S_U);

    if(has & SHAS_D_S_U)
    {
        p.group("Screen");
        p.u("door_combo_set", "Door combo set", 2);
        bytes_n(p, "door", "Door", 4);
        p.u("stairx", "Stair X", 1);
        p.u("stairy", "Stair Y", 1);
        p.u("undercombo", "Under combo", 2);
        p.u("undercset", "Under CSet", 1);
    }

    p.gate(SHAS_FLAGS);

    if(has & SHAS_FLAGS)
    {
        p.group("Screen");
        flags_1_to_10(p, &flags10, false);
        p.u("enemyflags", "Enemy flags", 1);
    }

    p.gate(SHAS_ENEMY);

    if(has & SHAS_ENEMY)
    {
        p.group("Screen");
        bytes_n(p, "enemy", "Enemy", 10, 2);
        p.u("pattern", "Enemy pattern", 1);
    }

    p.gate(SHAS_CARRY);

    if(has & SHAS_CARRY)
    {
        p.group("Carry-over");

        if(v < 34)
        {
            p.u("noreset", "No reset", 2);
            p.u("nocarry", "No carry", 2);
        }
        else
        {
            p.u("noreset", "No reset", 4);
            p.u("nocarry", "No carry", 4);
            p.extra(true);
            p.flags("exstate_reset", "ExStates that don't reset", 4, TL_NONE);
            p.flags("exstate_carry", "ExStates that carry over", 4, TL_NONE);
            p.extra(false);
        }

        p.u("nextmap", "Next map", 1);
        p.u("nextscr", "Next screen", 1);
    }

    p.gate(SHAS_SCRIPT);

    if(has & SHAS_SCRIPT)
    {
        p.group("Script");
        p.extra(true);

        if(v < 41)
        {
            p.u("script", "Screen script", 2);
            p.u("preloadscript", "Run the script before the screen scrolls in", 1);

            for(int q = 0; q < 8; ++q)
            {
                char k[16], l[24];
                snprintf(k, sizeof(k), "initd%d", q);
                snprintf(l, sizeof(l), "InitD[%d]", q);
                p.s(k, l, 4);
            }
        }
        else
        {
            p.script_config("sc", "Screen script");
            p.u("preloadscript", "Run the script before the screen scrolls in", 1);
        }

        p.extra(false);
    }

    p.gate(SHAS_SECRETS);

    if(has & SHAS_SECRETS)
    {
        p.group("Screen");
        p.blob("secretcombo", "secret combos", 256);
        p.blob("secretcset", "secret csets", 128);
        p.blob("secretflag", "secret flags", 128);
    }

    p.gate(SHAS_COMBOFLAG);

    if(has & SHAS_COMBOFLAG)
    {
        p.group("Screen");
        p.blob("data", "combos", 352);
        p.blob("sflag", "placed flags", 176);
        p.blob("cset", "combo csets", 176);
    }

    p.gate(SHAS_MISC);

    if(has & SHAS_MISC)
    {
        p.group("Screen");
        p.u("color", "Palette", 2);
        p.def(1).u("csensitive", "Combo sensitivity", 1);
        const int sw = v < 38 ? 1 : 2;
        p.u("oceansfx", "Ambient sound", sw, TL_SFX);
        p.u("bosssfx", "Boss sound", sw, TL_SFX);
        p.def(27).u("secretsfx", "Secret sound", sw, TL_SFX);
        p.def(20).u("holdupsfx", "Hold-up sound", sw, TL_SFX);
        p.u("timedwarptics", "Timed warp tics", 2);
        p.group("Music and lens");

        if(v < 37)
            p.def(-1).s("screen_midi", "Screen MIDI (-1: the DMap's)", 2);
        else
        {
            p.extra(true);
            p.def(-1).s("music", "Music (-2 silence, -1 the DMap's, or a quest music object)", 4);
            p.extra(false);
        }

        p.u("lens_layer", "Lens layer", 1);

        if(v > 27)
        {
            p.extra(true);
            p.flags("lens_show", "Layers the Lens shows", 1, TL_LAYERBITS);
            p.flags("lens_hide", "Layers the Lens hides", 1, TL_LAYERBITS);
            p.extra(false);
        }
    }

    p.gate(0);
    const bool masked = v < 25;
    int64_t bits = 0;
    int ffcs = 32;

    if(masked)
        bits = p.u("numff", "FFCs in use (bits)", 4);
    else
        ffcs = (int)p.hidden("ffc_count", 2);

    if(ffcs > 128)
    {
        p.bad = true;
        return false;
    }

    for(int m = 0; m < ffcs && !p.bad; ++m)
    {
        if(masked && !((bits >> m) & 1))
            continue;

        screen_ffc(p, v, m, masked, false);
    }

    p.gate(0);

    if(v > 29)
    {
        p.group("Notes");
        p.extra(true);
        p.lstr("notes", "Notes");
        p.extra(false);
    }

    if(v >= 35 && (flags10 & 0x03))
    {
        p.group("Gravity");
        p.extra(true);
        p.s("gravity", "Gravity (fixed point)", 4);
        p.s("terminal_v", "Terminal velocity (fixed point)", 4);
        p.extra(false);
    }

    return !p.bad;
}

// ---- init data (q255_init.cpp's q255_read_initdata, versions 18 to 52) ---------------------

// containers kept as bytes: walkers that read past them as the reader does
static void walk_count(TapeParser &p, size_t count, size_t each)
{
    if(each && count > p.left() / each)
    {
        p.bad = true;
        return;
    }

    p.rskip(count * each);
}

// bounded vec / map with a word size: values of vs bytes, word keys when paired
static void walk_bounded_w(TapeParser &p, size_t vs)
{
    if(!p.rd(2))
        return;

    const uint64_t pairs = p.rd(1);
    walk_count(p, (size_t)p.rd(2), vs + (pairs ? 2 : 0));
}

static void walk_bw2(TapeParser &p) { walk_bounded_w(p, 2); }
static void walk_bw4(TapeParser &p) { walk_bounded_w(p, 4); }

// bounded_map<word, bounded_vec<byte, int>> (generic script InitD)
static void walk_gen_initd(TapeParser &p)
{
    if(!p.rd(2))
        return;

    const uint64_t pairs = p.rd(1);
    const size_t count = (size_t)p.rd(2);

    for(size_t q = 0; q < count && !p.bad; ++q)
    {
        if(pairs)
            p.rskip(2);

        if(!p.rd(1))
            continue;

        const uint64_t vpairs = p.rd(1);
        walk_count(p, (size_t)p.rd(1), vpairs ? 5 : 4);
    }
}

// bounded_map<word, bounded_map<dword, int>> (generic script data)
static void walk_gen_data(TapeParser &p)
{
    if(!p.rd(2))
        return;

    const uint64_t pairs = p.rd(1);
    const size_t count = (size_t)p.rd(2);

    for(size_t q = 0; q < count && !p.bad; ++q)
    {
        if(pairs)
            p.rskip(2);

        if(!p.rd(4))
            continue;

        const uint64_t vpairs = p.rd(1);
        walk_count(p, (size_t)p.rd(4), vpairs ? 8 : 4);
    }
}

// bounded_map<dword, bounded_map<dword, int>> (screen script data)
static void walk_screen_data(TapeParser &p)
{
    if(!p.rd(4))
        return;

    const uint64_t pairs = p.rd(1);
    const size_t count = (size_t)p.rd(4);

    for(size_t q = 0; q < count && !p.bad; ++q)
    {
        if(pairs)
            p.rskip(4);

        if(!p.rd(4))
            continue;

        const uint64_t ipairs = p.rd(1);
        walk_count(p, (size_t)p.rd(4), ipairs ? 8 : 4);
    }
}

// bounded_map<word, std::map<word, int>> (generic script instance values)
static void walk_gen_inst(TapeParser &p)
{
    if(!p.rd(2))
        return;

    const uint64_t pairs = p.rd(1);
    const size_t count = (size_t)p.rd(2);

    for(size_t q = 0; q < count && !p.bad; ++q)
    {
        if(pairs)
            p.rskip(2);

        walk_count(p, (size_t)p.rd(4), 6);
    }
}

static void walk_script_args(TapeParser &p)
{
    const size_t n = (size_t)p.rd(2);
    p.rskip((n < 8 ? n : 8) * 4);
    walk_count(p, (size_t)p.rd(2), 6);
}

static void walk_script_array(TapeParser &p, size_t capacity)
{
    const size_t n = (size_t)p.rd(2);

    for(size_t q = 0; q < n && q < capacity && !p.bad; ++q)
        walk_script_args(p);
}

static void walk_global_scripts(TapeParser &p) { walk_script_array(p, 8); }
static void walk_hero_scripts(TapeParser &p) { walk_script_array(p, 5); }

// before v37: the generic scripts, one record per slot that has data
static void walk_old_generic(TapeParser &p)
{
    const size_t n = (size_t)p.rd(2);

    if(n > 512)
    {
        p.bad = true;
        return;
    }

    for(size_t q = 1; q < n && !p.bad; ++q)
    {
        if(!(p.rd(1) & 2))
            continue;

        p.rskip(2 + 2 + 8 * 4 + 4);
        walk_count(p, (size_t)p.rd(4), 4);
        p.rskip(4);
    }
}

static void walk_old_screen_data(TapeParser &p)
{
    const size_t used = (size_t)p.rd(4);

    for(size_t q = 0; q < used && !p.bad; ++q)
        if(p.rd(4))
            walk_count(p, (size_t)p.rd(4), 4);
}

static void counter_pair(TapeParser &p, int q, const char *what, int width)
{
    char k[32], l[48];
    snprintf(k, sizeof(k), "%s%d", what, q);
    snprintf(l, sizeof(l), "%s counter %d", what[0] == 'm' ? "Max" : "Start", q);
    p.u(k, l, width, TL_NONE, TK_INT, q);
}

static void init_light(TapeParser &p, bool with_colour)
{
    p.group("Lighting");
    p.u("dither_type", "Dither type", 1);
    p.u("dither_arg", "Dither argument", 1);
    p.u("dither_percent", "Dither percent", 1);
    p.u("def_lightrad", "Default light radius", 1);
    p.u("transdark_percent", "Transparent dark percent", 1);

    if(with_colour)
        p.u("darkcol", "Darkness colour", 1);
}

static void init_grid(TapeParser &p, int width)
{
    p.group("Subscreen");
    const char *keys[8] = { "ss_grid_x", "ss_grid_y", "ss_grid_xofs", "ss_grid_yofs", "ss_grid_color", "ss_bbox_1_color",
                            "ss_bbox_2_color", "ss_flags" };
    const char *labels[8] = { "Grid X", "Grid Y", "Grid X offset", "Grid Y offset", "Grid colour", "Selector colour 1",
                              "Selector colour 2", "Subscreen editor flags" };

    for(int q = 0; q < 8; ++q)
        p.s(keys[q], labels[q], width);
}

static bool init_old(TapeParser &p, int v)
{
    p.group("Items");
    p.bytes("items", "Starting items", 256, TL_ITEMBITS);
    p.group("Counters");

    if(v < 29)
    {
        p.u("bombs_old", v >= 19 ? "Bombs (old byte)" : "Bombs", 1);
        p.u("super_bombs_old", v >= 19 ? "Super bombs (old byte)" : "Super bombs", 1);
    }

    p.group("Life and magic");
    p.u("hc", "Heart containers", 1);
    p.u("start_heart", "Starting hearts", 2);
    p.u("cont_heart", "Continue hearts", 2);
    p.u("hcp", "Heart pieces", 1);
    p.u("hcp_per_hc", "Pieces per container", 1);
    p.group("Counters");

    if(v < 29)
        p.u("max_bombs_old", v >= 19 ? "Max bombs (old byte)" : "Max bombs", 1);

    p.u("keys", "Keys", 1);
    p.u("rupies", "Rupees", 2);
    p.group("Levels");
    p.u("triforce", "McGuffins (levels 1-8, bits)", 1, TL_NONE, TK_HIDDEN);
    p.bitfield("map_bits", "Maps", 64, TL_NONE);
    p.bitfield("compass_bits", "Compasses", 64, TL_NONE);
    p.bitfield("bosskey_bits", "Boss keys", 64, TL_NONE);
    p.group("Flags");
    p.bitfield("misc", "Flags", 16, TL_INITMISC);
    p.group("Data");
    p.u("last_map", "Map shown in the editor", 1);
    p.u("last_screen", "Screen shown in the editor", 1);
    p.group("Life and magic");
    p.u("max_magic", "Max magic", 2);
    p.u("magic", "Magic", 2);
    p.group("Counters");
    p.u("bomb_ratio", "Bombs per super bomb (max)", 1);
    p.group("Messages");
    p.u("msg_more_x", "'More' icon X", 1);
    p.u("msg_more_y", "'More' icon Y", 1);
    p.group("Subscreen");
    p.u("subscreen", "Subscreen", 1);
    p.group("Data");
    p.u("start_dmap", "Starting DMap", 2);
    p.group("Hero");
    p.u("animation_style", "Animation style", 1);
    p.group("Counters");

    if(v < 29)
    {
        p.u("arrows_old", v >= 19 ? "Arrows (old byte)" : "Arrows", 1);
        p.u("max_arrows_old", v >= 19 ? "Max arrows (old byte)" : "Max arrows", 1);
    }

    p.group("Levels");
    p.bytes("level_keys", "Level keys", MAXLEVELS, TL_LEVELVALUES);
    init_grid(p, 2);
    p.u("subscreen_style", "Subscreen style", 1);
    p.u("usecustomsfx", "Custom subscreen sounds", 1);
    p.group("Counters");
    p.u("max_rupees", "Max rupees", 2);
    p.u("max_keys", "Max keys", 2);
    p.group("Physics");
    p.u("gravity_old", "Gravity (old)", 1);
    p.u("terminalv", "Terminal velocity", 2);
    p.group("Messages");
    p.u("msg_speed", "Message speed", 1);
    p.group("Data");
    p.u("transition_type", "Transition type", 1);
    p.group("Hero");
    p.u("jump_threshold", "Jump layer threshold", 1);
    p.group("Messages");
    p.u("msg_more_is_offset", "'More' icon Y is an offset", 1);

    if(v >= 19)
    {
        p.group("Counters");
        p.u("bombs", "Bombs", 2);
        p.u("super_bombs", "Super bombs", 2);
        p.u("max_bombs", "Max bombs", 2);
        p.u("max_sbombs", "Max super bombs", 2);
        p.u("arrows", "Arrows", 2);
        p.u("max_arrows", "Max arrows", 2);
    }

    p.group("Hero");

    if(v >= 20)
        p.u("hero_step", "Walk step", 2);

    if(v >= 21)
    {
        p.group("Subscreen");
        p.u("subscr_speed", "Subscreen scroll speed", 2);
    }

    if(v > 21)
    {
        p.group("Life and magic");
        p.u("hp_per_heart", "HP per heart", 1);
        p.u("magic_per_block", "Magic per block", 1);
        p.u("hero_dmg_mult", "Hero damage multiplier", 1);
        p.u("ene_dmg_mult", "Enemy damage multiplier", 1);
    }

    if(v > 22)
    {
        p.group("Counters");

        for(int q = 0; q < 25; ++q)
        {
            char k[24], l[40];
            snprintf(k, sizeof(k), "custom%d", q);
            snprintf(l, sizeof(l), "Custom counter %d", q + 1);
            p.u(k, l, 2);
        }

        for(int q = 0; q < 25; ++q)
        {
            char k[24], l[40];
            snprintf(k, sizeof(k), "mcustom%d", q);
            snprintf(l, sizeof(l), "Max custom counter %d", q + 1);
            p.u(k, l, 2);
        }
    }

    if(v > 23)
        init_light(p, v > 24);

    if(v > 25)
    {
        p.group("Physics");
        p.s("gravity", "Gravity (1/100 pixel)", 4);
        p.s("swimgravity", "Swim gravity", 4);
    }

    if(v > 26)
    {
        p.group("Hero");
        p.u("sideswim_up", "Sideswim up step", 2);
        p.u("sideswim_side", "Sideswim side step", 2);
        p.u("sideswim_down", "Sideswim down step", 2);
    }

    if(v > 27)
        p.s("exit_water_jump", "Exit water jump", 4);

    if(v > 29)
        p.s("bunny_ltm", "Bunny tile modifier", 4);

    if(v > 30)
        p.u("switchhook_style", "Switch hook style", 1);

    if(v > 31)
    {
        p.group("Life and magic");
        p.u("magic_drain", "Magic drain rate", 1);
    }

    if(v > 32)
    {
        p.group("Scripts");
        p.raw("generic", "generic script settings", walk_old_generic, 2);
    }

    if(v > 33)
    {
        p.group("Hero");
        p.u("swim_mult", "Swim speed multiplier", 1);
        p.u("swim_div", "Swim speed divisor", 1);
    }

    if(v > 34)
    {
        p.group("Scripts");
        p.raw("screen_data", "screen script data", walk_old_screen_data, 4);
    }

    if(v > 35)
    {
        p.group("Hero");
        p.s("shove_offset", "Shove offset (fixed point)", 4);
    }

    return !p.bad;
}

bool zq_tape_init(TapeParser &p, int v)
{
    if(v < 37)
        return init_old(p, v);

    p.group("Items");

    if(v >= 47)
        p.bitstr("items", "Starting items", TL_ITEMBITS);
    else
        p.bitfield("items", "Starting items", 32, TL_ITEMBITS);

    p.group("Levels");
    char k[32], l[64];

    if(v >= 42)
    {
        for(int q = 0; q < MAXLEVELS; ++q)
        {
            snprintf(k, sizeof(k), "litem%d", q);
            snprintf(l, sizeof(l), "Level %d items", q);
            p.u(k, l, v >= 44 ? 2 : 1, TL_LITEMBITS, TK_HIDDEN, q);
        }
    }
    else
    {
        for(int q = 0; q < MAXLEVELS / 8; ++q)
        {
            const char *what[4] = { "lmap", "lcompass", "lboss", "lmcg" };

            for(int w = 0; w < 4; ++w)
            {
                snprintf(k, sizeof(k), "%s%d", what[w], q);
                p.u(k, k, 1, TL_NONE, TK_HIDDEN, q);
            }
        }
    }

    p.bitstr("level_keys", "Level keys", TL_LEVELVALUES);
    p.group("Counters");
    const int counters = (int)p.def(7).hidden("num_counters", 1);

    for(int q = 0; q < counters && !p.bad; ++q)
        counter_pair(p, q, "ctr", 2);

    for(int q = 0; q < counters && !p.bad; ++q)
        counter_pair(p, q, "mctr", 2);

    p.u("bomb_ratio", "Bombs per super bomb (max)", 1);
    p.group("Life and magic");
    p.u("hcp", "Heart pieces", 1);
    p.u("hcp_per_hc", "Pieces per container", 1);
    p.u("cont_heart", "Continue HP (or percent)", 2);
    p.def(16).u("hp_per_heart", "HP per heart", 1);
    p.def(32).u("magic_per_block", "Magic per block", 1);
    p.u("hero_dmg_mult", "Hero damage multiplier", 1);
    p.u("ene_dmg_mult", "Enemy damage multiplier", 1);
    init_light(p, true);
    init_grid(p, 4);
    p.group("Flags");
    p.bitstr("flags", "Flags", TL_INITFLAGS);
    p.group("Data");
    p.u("last_map", "Map shown in the editor", 1);
    p.u("last_screen", "Screen shown in the editor", 1);
    p.group("Messages");

    if(v < 49)
    {
        p.u("msg_more_x", "'More' icon X", 1);
        p.u("msg_more_y", "'More' icon Y", 1);
        p.u("msg_more_is_offset", "'More' icon Y is an offset", 1);
    }

    p.u("msg_speed", "Message speed", 1);

    if(v >= 49)
        p.u("msg_advance_delay", "Message advance delay", 1);

    p.group("Physics");
    p.s("gravity", "Gravity (1/100 pixel)", 4);
    p.s("swimgravity", "Swim gravity", 4);
    p.u("terminalv", "Terminal velocity", 2);
    p.group("Hero");
    p.u("swim_speed", "Swim speed", 1);
    p.u("swim_mult", "Swim speed multiplier", 1);
    p.u("swim_div", "Swim speed divisor", 1);
    p.u("sideswim_up", "Sideswim up step", 2);
    p.u("sideswim_side", "Sideswim side step", 2);
    p.u("sideswim_down", "Sideswim down step", 2);
    p.s("exit_water_jump", "Exit water jump", 4);
    p.u("hero_step", "Walk step", 2);
    p.u("animation_style", "Animation style", 1);
    p.u("jump_threshold", "Jump layer threshold", 1);
    p.s("bunny_ltm", "Bunny tile modifier", 4);
    p.group("Data");
    p.u("start_dmap", "Starting DMap", 2);
    p.group("Subscreen");
    p.u("subscr_speed", "Subscreen scroll speed", 2);
    p.group("Hero");
    p.u("switchhook_style", "Switch hook style", 1);
    p.group("Life and magic");
    p.u("magic_drain", "Magic drain rate", 1);
    p.group("Hero");
    p.s("shove_offset", "Shove offset (fixed point)", 4);
    p.group("Scripts");
    p.raw("gen_doscript", "generic scripts that run at start", [](TapeParser &q) { walk_bounded_w(q, 1); }, 2);
    p.raw("gen_exitstate", "generic script exit states", walk_bw2, 2);
    p.raw("gen_reloadstate", "generic script reload states", walk_bw2, 2);
    p.raw("gen_initd", "generic script InitD", walk_gen_initd, 2);
    p.raw("gen_eventstate", "generic script event states", walk_bw4, 2);
    p.raw("gen_data", "generic script data", walk_gen_data, 2);
    p.raw("screen_data", "screen script data", walk_screen_data, 4);

    if(v >= 38)
    {
        p.group("Sprites");
        p.u("flicker_speed", "Sprite flicker speed", 1);
        p.u("flicker_color", "Sprite flicker colour", 1);
        p.u("flicker_transp", "Sprite flicker transparency", 1);
    }

    if(v >= 39)
    {
        p.group("Physics");
        p.s("air_drag", "Air drag (fixed point)", 4);
    }

    // Some z3 builds wrote only the region mapping byte at v40; nothing else follows it there.
    const bool z3_region = v == 40 && (p.src ? p.src->find("region_only") != NULL : p.left() == 1);

    if(z3_region)
    {
        p.group("Data");
        p.u("region_only", "Region mapping", 1);
    }
    else
    {
        if(v >= 40)
        {
            p.group("Lighting");
            p.u("light_wave_rate", "Light wave rate", 2);
            p.u("light_wave_size", "Light wave size", 2);
        }

        if(v >= 41)
        {
            p.group("Data");
            p.u("region_mapping", "Region mapping", 1);
        }
    }

    if(v >= 43)
    {
        p.group("Bottles");
        p.bytes("bottles", "Bottle slots", 256, TL_BOTTLES);
    }

    if(v >= 44)
    {
        p.group("Levels");
        p.raw("lvlswitches", "level switch states", walk_bw4, 2);
    }

    if(v >= 45)
    {
        p.group("Sprites");
        p.u("item_spawn_flicker", "Item spawn flicker", 2);
        p.u("item_timeout_dur", "Item timeout duration", 2);
        p.u("item_timeout_flicker", "Item timeout flicker", 2);
        p.u("item_flicker_speed", "Item flicker speed", 1);
    }

    if(v >= 46)
    {
        p.group("Sprites");
        const char *z[5] = { "Ground", "Layer 3", "Layer 4", "Overhead", "Layer 5" };

        for(int q = 0; q < 5; ++q)
        {
            snprintf(k, sizeof(k), "z_threshold%d", q);
            snprintf(l, sizeof(l), "Z threshold: %s", z[q]);
            p.u(k, l, 2);
        }
    }

    if(v >= 48)
    {
        p.group("Hero");
        p.s("itembox_xofs", "Item box X offset", 1);
        p.s("itembox_yofs", "Item box Y offset", 1);
        p.u("itembox_w", "Item box width", 1);
        p.u("itembox_h", "Item box height", 1);
    }

    if(v >= 50)
    {
        p.group("Scripts");
        p.raw("gen_inst", "generic script instance values", walk_gen_inst, 2);
        p.raw("global_scripts", "global script settings", walk_global_scripts, 2);
        p.raw("hero_scripts", "hero script settings", walk_hero_scripts, 2);
    }

    if(v >= 51)
    {
        p.group("Viewport");
        p.u("vf.deadzone_w", "Dead zone width", 1);
        p.u("vf.deadzone_h", "Dead zone height", 1);
    }

    if(v >= 52)
    {
        p.group("Viewport");
        p.u("vf.lookahead_x", "Lookahead X", 1);
        p.u("vf.lookahead_y", "Lookahead Y", 1);
        p.s("vf.lookahead_speed", "Lookahead speed (fixed point)", 4);
        p.s("vf.recenter_speed", "Recenter speed (fixed point)", 4);
        p.u("vf.recenter_delay", "Recenter delay", 2);
    }

    return !p.bad;
}
