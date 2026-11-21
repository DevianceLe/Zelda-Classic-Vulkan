// ZQuest's store for the combo pools, autocombos and scrolling regions of 2.55.17 and 3.0
// Nightly quests (zq_autocombo.h), and what reads and writes them.
//
// Combo pools and autocombos: the CMBA section holds them after its aliases (pools from version
// 4, autocombos from 5). After every load (and every save in the quest's own version) they are
// read from the kept CMBA part as upstream's readcomboaliases reads them into combo_pools[] and
// combo_autos[]. The pool and autocombo editors (zq_cpool_editor.cpp) and the map editor change
// those lists; their encoding counts in the CMBA fingerprint, so a change sends the section
// through its writer (zqw_combos.cpp), which asks zq_cpool_section_tail for what follows the
// aliases: the original bytes while nothing changed, otherwise upstream's writecomboaliases
// encoding (pools and autocombos up to the last valid one).
//
// Scrolling regions: the MAP reader keeps each map's 64 id bytes (MAP v31+) in
// q255_map_region_ids. The region editor changes them there; they count in the MAP fingerprint,
// and zqw_maps.cpp writes them into each map's head.
//
// The classes are ported from upstream ZQuest Classic (GPL): core/cpool.cpp and
// core/autocombo.cpp. Written for the port otherwise (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zq_autocombo.h"
#include "zq_class.h"
#include "zq_sections.h"
#include "zq_writers.h"

extern std::vector<std::vector<byte> > q255_map_region_ids;    // q255_maps.cpp (MAP v31+)
bool zq255_quest_is_newer();
extern newcombo *combobuf;
extern word map_count;
void zq_fix_drawing_modes();    // zquest.cpp

combo_pool combo_pools[MAXCOMBOPOOLS];
combo_auto combo_autos[MAXAUTOCOMBOS];

// ---- combo_pool (upstream core/cpool.cpp) -----------------------------------------------

combo_pool &combo_pool::operator=(const combo_pool &other)
{
    if(this == &other)
        return *this;

    std::vector<cpool_entry> keep = other.combos;
    clear();

    for(size_t i = 0; i < keep.size(); ++i)
        add(keep[i].cid, keep[i].cset, keep[i].quant);

    recalc();
    return *this;
}

void combo_pool::push(int32_t cid, int8_t cs, word q)
{
    if(!q)
        return;

    totalweight += q;

    for(size_t i = 0; i < combos.size(); ++i)
    {
        if(combos[i].cid == cid && combos[i].cset == cs)
        {
            combos[i].quant += q;
            return;
        }
    }

    combos.push_back(cpool_entry(cid, cs, q));
}

void combo_pool::add(int32_t cid, int8_t cs, word q)
{
    totalweight += q;
    combos.push_back(cpool_entry(cid, cs, q));
}

void combo_pool::swap(size_t ind1, size_t ind2)
{
    if(ind1 >= combos.size() || ind2 >= combos.size())
        return;

    cpool_entry t = combos[ind1];
    combos[ind1] = combos[ind2];
    combos[ind2] = t;
}

void combo_pool::erase(size_t ind)
{
    if(ind >= combos.size())
        return;

    totalweight -= combos[ind].quant;
    combos.erase(combos.begin() + ind);
}

void combo_pool::trim()
{
    for(size_t i = 0; i < combos.size();)
    {
        if(!combos[i].valid())     // weight 0 or a bad combo: removed
        {
            combos.erase(combos.begin() + i);
            continue;
        }

        cpool_entry *canon = get(combos[i].cid, combos[i].cset);

        if(canon != &combos[i])    // a duplicate entry: merged into the first
        {
            canon->quant += combos[i].quant;
            combos.erase(combos.begin() + i);
            continue;
        }

        ++i;
    }

    recalc();
}

const cpool_entry *combo_pool::get_ind(size_t index) const
{
    return index < combos.size() ? &combos[index] : NULL;
}

const cpool_entry *combo_pool::get_w(size_t weight_index) const
{
    if(combos.empty() || totalweight < 1 || weight_index >= totalweight)
        return NULL;

    size_t curweight = 0;

    for(size_t i = 0; i < combos.size(); ++i)
    {
        curweight += combos[i].quant;

        if(weight_index < curweight)
            return &combos[i];
    }

    return NULL;
}

const cpool_entry *combo_pool::get_w_wrap(size_t weight_index) const
{
    if(combos.empty() || totalweight < 1)
        return NULL;

    return get_w(weight_index % totalweight);
}

const cpool_entry *combo_pool::pick() const
{
    if(totalweight < 1)
        return NULL;

    // upstream: zc_rand(totalweight-1), a number from 0 to totalweight-1
    size_t r = ((size_t)rand() * ((size_t)RAND_MAX + 1) + (size_t)rand()) % totalweight;
    return get_w(r);
}

static bool load_entry(const cpool_entry *entry, int32_t &cid, int8_t &cs)
{
    if(!entry)
        return false;

    cid = entry->cid;

    if(entry->cset > -1)
        cs = entry->cset;

    return true;
}

bool combo_pool::get_ind(int32_t &cid, int8_t &cs, size_t index) const { return load_entry(get_ind(index), cid, cs); }
bool combo_pool::get_w(int32_t &cid, int8_t &cs, size_t weight_index) const { return load_entry(get_w(weight_index), cid, cs); }
bool combo_pool::get_w_wrap(int32_t &cid, int8_t &cs, size_t weight_index) const { return load_entry(get_w_wrap(weight_index), cid, cs); }
bool combo_pool::pick(int32_t &cid, int8_t &cs) const { return load_entry(pick(), cid, cs); }

void combo_pool::recalc()
{
    totalweight = 0;

    for(size_t i = 0; i < combos.size(); ++i)
        totalweight += combos[i].quant;
}

cpool_entry *combo_pool::get(int32_t cid, int8_t cs)
{
    for(size_t i = 0; i < combos.size(); ++i)
        if(combos[i].cid == cid && combos[i].cset == cs)
            return &combos[i];

    return NULL;
}

// ---- combo_auto (upstream core/autocombo.cpp) -------------------------------------------

int16_t autocombo_entry::base_engrave_offset(byte type)
{
    switch(type)
    {
    case AUTOCOMBO_Z1: return 0;
    case AUTOCOMBO_BASIC: return 16;
    case AUTOCOMBO_FENCE: return 32;
    case AUTOCOMBO_Z4: return 64;
    case AUTOCOMBO_RELATIONAL: return 96;
    case AUTOCOMBO_DGNCARVE: return 144;
    case AUTOCOMBO_DOR: return 240;
    case AUTOCOMBO_DENSEFOREST: return 320;
    }

    return 0;
}

combo_auto &combo_auto::operator=(const combo_auto &other)
{
    if(this == &other)
        return *this;

    std::vector<autocombo_entry> keep = other.combos;
    clear();
    // (upstream copies the type after the entries, so they get the old type's engravings)
    for(size_t i = 0; i < keep.size(); ++i)
        addEntry(keep[i].cid, keep[i].ctype, keep[i].offset, keep[i].engrave_offset);

    type = other.type;
    cid_display = other.cid_display;
    flags = other.flags;
    cid_erase = other.cid_erase;
    arg = other.arg;
    return *this;
}

void combo_auto::addEntry(int32_t cid, byte ct, int32_t of, int32_t eo)
{
    if(eo < 0)
        eo = of + autocombo_entry::base_engrave_offset(type);

    combos.push_back(autocombo_entry(cid, ct, of, eo));
}

void combo_auto::removeEntry()
{
    if(!combos.empty())
        combos.pop_back();
}

void combo_auto::updateValid()
{
    bool isvalid = true;
    bool canDupe = false;
    bool canCombo0 = false;
    invalid_reasons = 0;

    switch(type)
    {
    case AUTOCOMBO_Z1:
    case AUTOCOMBO_BASIC:
    case AUTOCOMBO_REPLACE:
    case AUTOCOMBO_RELATIONAL:
    case AUTOCOMBO_EXTEND:
        canDupe = true;
        // fall through
    case AUTOCOMBO_FENCE:
    case AUTOCOMBO_Z4:
    case AUTOCOMBO_DGNCARVE:
    case AUTOCOMBO_DOR:
    case AUTOCOMBO_TILING:
    case AUTOCOMBO_DENSEFOREST:
        canCombo0 = true;
        break;

    default:
        invalid_reasons |= ACIR_ILLEGAL_TYPE;
        flags &= ~ACF_VALID;
        return;
    }

    if(type == AUTOCOMBO_TILING)
    {
        flags |= ACF_VALID;
        return;
    }

    for(size_t i = 0; i < combos.size(); ++i)
    {
        const autocombo_entry &c = combos[i];

        if(c.cid == cid_erase)
        {
            invalid_reasons |= ACIR_ERASE_IN_SET;
            flags &= ~ACF_VALID;
            isvalid = false;
        }

        if(!canCombo0 && c.cid == 0)
        {
            invalid_reasons |= ACIR_MISSING_COMBO;
            flags &= ~ACF_VALID;
            isvalid = false;
        }

        for(size_t k = 0; k < combos.size(); ++k)
        {
            const autocombo_entry &c2 = combos[k];

            if(c2.offset != c.offset && c2.cid == c.cid && !canDupe)
            {
                invalid_reasons |= ACIR_DUPLICATE_COMBO;
                flags &= ~ACF_VALID;
                isvalid = false;
                break;
            }
        }
    }

    if(isvalid)
        flags |= ACF_VALID;
}

std::string combo_auto::getInvalidReason()
{
    std::string ret;

    if(invalid_reasons & ACIR_ILLEGAL_TYPE)
        ret += type == AUTOCOMBO_NONE ? "\n-Autocombo is the (None) Type" : "\n-Autocombo is an invalid Type";
    if(invalid_reasons & ACIR_MISSING_COMBO)
        ret += "\n-This type does not allow combo 0 in the set";
    if(invalid_reasons & ACIR_ERASE_IN_SET)
        ret += "\n-The erase combo is shared with one or more combos in the set";
    if(invalid_reasons & ACIR_DUPLICATE_COMBO)
        ret += "\n-This type does not allow two combos with the same ID";

    return ret;
}

bool combo_auto::hasTemplate()
{
    switch(type)
    {
    case AUTOCOMBO_TILING:
        return (arg & 0xF) + 1 <= 4;
    case AUTOCOMBO_NONE:
    case AUTOCOMBO_REPLACE:
        return false;
    default:
        return true;
    }
}

bool combo_auto::canErase()
{
    return type != AUTOCOMBO_NONE && type != AUTOCOMBO_REPLACE;
}

bool combo_auto::containsCombo(int32_t cid, bool requirevalid) const
{
    if(!valid() && requirevalid)
        return false;

    for(size_t i = 0; i < combos.size(); ++i)
        if(combos[i].cid == cid && cid != 0)
            return true;

    return false;
}

// Combos the fill tool goes over (dungeon carving's and complex mountains' insides, Replace's
// "from" combos).
bool combo_auto::isIgnoredCombo(int32_t cid) const
{
    switch(type)
    {
    case AUTOCOMBO_DGNCARVE:
    case AUTOCOMBO_DOR:
    case AUTOCOMBO_REPLACE:
        break;
    default:
        return false;
    }

    for(size_t slot = 0; slot < combos.size(); ++slot)
        if(combos[slot].cid == cid && ignore_fill(type, (int32_t)slot))
            return true;

    return false;
}

int32_t combo_auto::getDisplay() const
{
    if(cid_display)
        return cid_display;

    for(size_t i = 0; i < combos.size(); ++i)
        if(combos[i].cid > 0)
            return combos[i].cid;

    return 0;
}

// 'Auto Generate' with Legacy Ordering: the slot's combo offset in 2.53's relational and dungeon
// carving combo layouts.
int32_t combo_auto::legacy_offsets(byte type, int16_t offset)
{
    static const int8_t relational[47] =
    {
        31, 20, 24, 33, 26, 0, 16, 30, 42, 34, 38, 44, 45, 40, 41, 46, 8, 4, 32, 25, 1, 2, 43, 39,
        12, 6, 23, 19, 9, 3, 29, 37, 21, 22, 27, 17, 36, 35, 28, 18, 7, 11, 5, 10, 14, 13, 15
    };
    static const int8_t dungeon[94] =
    {
        0, 46, 95, 94, 8, 4, 56, 52, 1, 2, 49, 50, 31, 24, 79, 72, 42, 38, 90, 86, 68, 34, 74, 26,
        20, 82, 16, 64, 33, 41, 81, 89, 45, 44, 93, 92, 30, 40, 78, 88, 32, 25, 80, 73, 43, 39, 91,
        87, 23, 19, 71, 67, 29, 37, 77, 85, 12, 6, 60, 54, 9, 3, 57, 51, 7, 11, 55, 59, 14, 13, 62,
        61, 27, 17, 75, 65, 28, 18, 76, 66, 21, 22, 69, 70, 36, 35, 84, 83, 10, 5, 58, 53, 15, 63
    };

    switch(type)
    {
    case AUTOCOMBO_RELATIONAL:
        if(offset >= 0 && offset < 47)
            return relational[offset];
        break;

    case AUTOCOMBO_DGNCARVE:
        if(offset >= 0 && offset < 94)
            return dungeon[offset];
        return 0;
    }

    return 0;
}

// 'Auto Generate': the slot's combo offset from the first combo, in the layout the engravings
// show (rows of 4 combos, with gaps where a row has fewer slots).
int32_t combo_auto::convert_offsets(byte type, int16_t offset)
{
    switch(type)
    {
    case AUTOCOMBO_BASIC:
        return offset;

    case AUTOCOMBO_Z1:
        if(offset <= 2) return offset;
        if(offset <= 5) return offset + 1;
        break;

    case AUTOCOMBO_Z4:
        if(offset <= 2) return offset;
        if(offset <= 5) return offset + 1;
        if(offset <= 8) return offset + 2;
        if(offset <= 11) return offset + 3;
        if(offset <= 14) return offset + 4;
        if(offset <= 17) return offset + 5;
        if(offset <= 19) return offset + 6;
        if(offset <= 21) return offset + 8;
        if(offset <= 29) return offset + 10;
        break;

    case AUTOCOMBO_FENCE:
        if(offset <= 2) return offset;
        if(offset <= 5) return offset + 1;
        if(offset <= 8) return offset + 2;
        if(offset <= 10) return offset + 3;
        if(offset <= 12) return offset + 5;
        if(offset <= 20) return offset + 7;
        break;

    case AUTOCOMBO_DGNCARVE:
        if(offset < 93) return offset;
        if(offset == 93) return offset + 1;
        return offset + 2;

    case AUTOCOMBO_DOR:
        if(offset <= 26) return offset;
        if(offset <= 47) return offset + 1 + (offset - 27) / 3;
        return offset + 8;
    }

    return offset;
}

// Slots the fill tool ignores (see isIgnoredCombo).
bool combo_auto::ignore_fill(byte type, int32_t slot)
{
    switch(type)
    {
    case AUTOCOMBO_DGNCARVE:
        switch(slot)
        {
        case 20: case 22: case 25: case 27: case 93: case 99: case 100: case 103: case 104: case 110:
        case 111: case 112: case 113: case 114: case 117: case 118: case 121: case 122: case 123:
        case 124: case 125:
            return true;
        case 21: case 23: case 24: case 26: case 94: case 95: case 96: case 97: case 98: case 101:
        case 102: case 105: case 106: case 107: case 108: case 109: case 115: case 116: case 119:
        case 120:
            return false;
        }

        if(slot % 4 > 1)
            return true;

        break;

    case AUTOCOMBO_DOR:
        if(slot == 34)
            return true;

        return !(slot >= 24 && slot <= 38);

    case AUTOCOMBO_REPLACE:
        return slot % 2 == 0;
    }

    return false;
}

std::map<int32_t, byte> combo_auto::getMapping()
{
    std::map<int32_t, byte> ret;
    byte b = 0;

    for(size_t i = 0; i < combos.size(); ++i, ++b)
    {
        const autocombo_entry &cmb = combos[i];

        if(cmb.ctype == ACT_NORMAL)
            ret[cmb.cid] = b;
        else if(cmb.ctype == ACT_CPOOL && unsigned(cmb.cid) < MAXCOMBOPOOLS)
            for(size_t k = 0; k < combo_pools[cmb.cid].combos.size(); ++k)
                if(combo_pools[cmb.cid].combos[k].cid > -1)
                    ret[combo_pools[cmb.cid].combos[k].cid] = b;
    }

    return ret;
}

// ---- reading and writing (upstream readcomboaliases / writecomboaliases) ---------------

namespace
{

struct Rd
{
    const std::vector<byte> &d;
    size_t pos;
    bool bad;

    Rd(const std::vector<byte> &v, size_t at) : d(v), pos(at), bad(false) {}
    bool has(size_t k) const { return !bad && pos <= d.size() && k <= d.size() - pos; }
    byte g8()
    {
        if(!has(1)) { bad = true; return 0; }
        return d[pos++];
    }
    word g16() { word lo = g8(); return (word)(lo | (g8() << 8)); }
    dword g32() { dword lo = g16(); return lo | ((dword)g16() << 16); }
};

void p8(std::vector<byte> &o, byte v) { o.push_back(v); }
void p16(std::vector<byte> &o, word v) { p8(o, (byte)v); p8(o, (byte)(v >> 8)); }
void p32(std::vector<byte> &o, dword v) { p16(o, (word)v); p16(o, (word)(v >> 16)); }

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];

    return NULL;
}

int part_version(dword id)
{
    const ZqQuestPart *p = find_part(id);
    word v = 0, cv = 0;

    if(!p || !zq_part_versions(*p, &v, &cv))
        return 0;

    return v;
}

bool read_part(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return p.length == 0 || zq_part_read(p, 0, &out[0], (size_t)p.length);
}

// Where the aliases of a CMBA part (bytes from its version word on) end: 8192 aliases from
// version 3, 2048 before. 0 if it doesn't read.
size_t aliases_end(const std::vector<byte> &part, int version)
{
    Rd in(part, 8);
    const int n = version >= 3 ? 8192 : version >= 2 ? 2048 : 256;

    for(int j = 0; j < n; ++j)
    {
        in.g16();
        in.g8();
        const byte w = in.g8(), h = in.g8(), mask = in.g8();
        int layers = 0;

        for(int q = 0; q < 6; ++q)
            layers += (mask >> q) & 1;

        const size_t cells = (size_t)(w + 1) * (h + 1) * (layers + 1) * 3;

        if(!in.has(cells))
            return 0;

        in.pos += cells;
    }

    return in.bad ? 0 : in.pos;
}

void clear_lists()
{
    for(int i = 0; i < MAXCOMBOPOOLS; ++i)
        combo_pools[i].clear();

    for(int i = 0; i < MAXAUTOCOMBOS; ++i)
        combo_autos[i].clear(true);
}

// After every load and every save in the quest's own version: the lists as the CMBA part now
// holds them (after a save, the part holds what was written), and no region ids for a quest
// whose reader doesn't keep them.
void note_loaded()
{
    clear_lists();
    zq_fix_drawing_modes();     // the Pool and Autocombo modes only where the format holds them

    if(!zq255_quest_is_newer())
    {
        q255_map_region_ids.clear();
        return;
    }

    const ZqQuestPart *p = find_part(ID_COMBOALIASES);
    const int v = part_version(ID_COMBOALIASES);
    std::vector<byte> part;

    if(!p || v < 4 || !read_part(*p, part))
        return;

    const size_t at = aliases_end(part, v);
    std::string why;

    if(!at || !zq_cpool_decode(part, at, v, combo_pools, combo_autos, why))
    {
        al_trace("zq_cpool: the combo pools didn't read: %s\n", why.c_str());
        clear_lists();
        return;
    }

    if(getenv("ZQ_CPOOL_TRACE"))
    {
        int pools = 0, autos = 0;

        for(int i = 0; i < MAXCOMBOPOOLS; ++i)
            pools += combo_pools[i].valid();

        for(int i = 0; i < MAXAUTOCOMBOS; ++i)
            autos += combo_autos[i].valid();

        al_trace("zq_cpool: CMBA v%d: %d valid pools, %d valid autocombos\n", v, pools, autos);
    }
}

ZqLoadHookRegistration g_hook_cpools(note_loaded);

// The pools and autocombos count in the CMBA fingerprint (nothing while there are none, so a
// quest without them keeps 2.53's fingerprint).
void hash_cpools(uint64_t *h)
{
    std::vector<byte> e;
    zq_cpool_encode(e, true);

    if(e.size() > 4)
        zq_hash_add(h, &e[0], e.size());
}

// The region ids count in the MAP fingerprint (nothing for a quest with none).
void hash_regions(uint64_t *h)
{
    for(size_t i = 0; i < q255_map_region_ids.size(); ++i)
    {
        const std::vector<byte> &ids = q255_map_region_ids[i];
        bool any = false;

        for(size_t k = 0; k < ids.size() && !any; ++k)
            any = ids[k] != 0;

        if(!any)
            continue;

        const unsigned m = (unsigned)i;
        zq_hash_add(h, &m, sizeof(m));
        zq_hash_add(h, &ids[0], ids.size());
    }
}

ZqStoreHashRegistration g_hash_cpools(ID_COMBOALIASES, hash_cpools);
ZqStoreHashRegistration g_hash_regions(ID_MAPS, hash_regions);

} // namespace

int zq_cpool_cmba_version()
{
    return zq255_quest_is_newer() ? part_version(ID_COMBOALIASES) : 0;
}

int zq_region_map_version()
{
    return zq255_quest_is_newer() ? part_version(ID_MAPS) : 0;
}

bool zq_cpool_quest_holds_pools() { return zq_cpool_cmba_version() >= 4; }
bool zq_cpool_quest_holds_autos() { return zq_cpool_cmba_version() >= 5; }
bool zq_region_quest_holds_regions() { return zq_region_map_version() >= 31; }

namespace
{

// upstream writecomboaliases, after the aliases
void encode_lists(const combo_pool *pools, const combo_auto *autos, std::vector<byte> &out, bool with_autos)
{
    int num = MAXCOMBOPOOLS - 1;

    while(num >= 0 && !pools[num].valid())
        --num;

    ++num;
    p16(out, (word)num);

    for(int cp = 0; cp < num; ++cp)
    {
        const combo_pool &pool = pools[cp];
        p32(out, (dword)pool.combos.size());

        for(size_t q = 0; q < pool.combos.size(); ++q)
        {
            p32(out, (dword)pool.combos[q].cid);
            p8(out, (byte)pool.combos[q].cset);
            p16(out, pool.combos[q].quant);
        }
    }

    if(!with_autos)
        return;

    num = MAXAUTOCOMBOS - 1;

    while(num >= 0 && !autos[num].valid())
        --num;

    ++num;
    p16(out, (word)num);

    for(int ca = 0; ca < num; ++ca)
    {
        const combo_auto &a = autos[ca];
        p8(out, (byte)a.getType());
        p32(out, (dword)a.getIconDisplay());
        p32(out, (dword)a.getEraseCombo());
        p8(out, a.getFlags());
        p8(out, a.getArg());
        p32(out, (dword)a.combos.size());

        for(size_t q = 0; q < a.combos.size(); ++q)
        {
            p8(out, a.combos[q].ctype);
            p32(out, (dword)a.combos[q].cid);
        }
    }
}

} // namespace

void zq_cpool_encode(std::vector<byte> &out, bool with_autos)
{
    encode_lists(combo_pools, combo_autos, out, with_autos);
}

bool zq_cpool_decode(const std::vector<byte> &data, size_t at, int version, combo_pool *pools, combo_auto *autos,
                     std::string &why)
{
    Rd in(data, at);
    word num_pools = 0;

    if(version >= 4)
        num_pools = in.g16();

    if(num_pools > MAXCOMBOPOOLS)
    {
        why = "more combo pools than upstream holds";
        return false;
    }

    for(word cp = 0; cp < num_pools && !in.bad; ++cp)
    {
        const int32_t n = (int32_t)in.g32();
        pools[cp].clear();

        if(n < 1)
            continue;

        if(!in.has((size_t)n * 7))
        {
            in.bad = true;
            break;
        }

        for(int32_t q = 0; q < n; ++q)
        {
            const int32_t cid = (int32_t)in.g32();
            const int8_t cs = (int8_t)in.g8();
            const word quant = in.g16();
            pools[cp].add(cid, cs, quant);
        }

        pools[cp].recalc();
    }

    word num_autos = 0;

    if(version >= 5 && !in.bad)
        num_autos = in.g16();

    if(num_autos > MAXAUTOCOMBOS)
    {
        why = "more autocombos than upstream holds";
        return false;
    }

    for(word ca = 0; ca < num_autos && !in.bad; ++ca)
    {
        const byte type = in.g8();
        const int32_t display = (int32_t)in.g32();
        const int32_t erase = (int32_t)in.g32();
        const byte flags = in.g8();
        const byte arg = in.g8();
        const int32_t n = (int32_t)in.g32();

        if(n < 1)       // upstream leaves it cleared
            continue;

        if(!in.has((size_t)n * 5))
        {
            in.bad = true;
            break;
        }

        combo_auto t;
        t.setType(type);
        t.setDisplay(display);
        t.setEraseCombo(erase);
        t.setFlags(flags);
        t.setArg(arg);

        for(int32_t q = 0; q < n; ++q)
        {
            const byte ctype = in.g8();
            const int32_t cid = (int32_t)in.g32();
            t.addEntry(cid, ctype, q, -1);
        }

        autos[ca] = t;
    }

    if(in.bad)
    {
        why = "the section ends inside its combo pools or autocombos";
        return false;
    }

    return true;
}

bool zq_cpool_section_tail(int version, const std::vector<byte> &orig_tail, std::vector<byte> &out, std::string &why)
{
    out.clear();

    // What the original bytes hold, read as upstream reads them, and encoded again.
    std::vector<combo_pool> pools(MAXCOMBOPOOLS);
    std::vector<combo_auto> autos(MAXAUTOCOMBOS);
    std::string err;
    std::vector<byte> was, now;
    zq_cpool_encode(now, true);

    if(zq_cpool_decode(orig_tail, 0, version, &pools[0], &autos[0], err))
        encode_lists(&pools[0], &autos[0], was, true);

    // Nothing changed: the bytes as they were (whatever upstream would write instead).
    // ZQ_CPOOL_REENCODE=1 (a test switch) encodes them anyway, to compare with upstream's.
    static const bool reencode = getenv("ZQ_CPOOL_REENCODE") != NULL;

    if(!was.empty() && was == now && !zqw_force_serialize && !reencode)
    {
        out = orig_tail;
        return true;
    }

    const bool has_pools = now.size() > 4 && (now[0] | now[1]) != 0;
    std::vector<byte> pools_only;
    zq_cpool_encode(pools_only, false);
    const bool has_autos = now.size() > pools_only.size() + 2;

    if(version < 4)
    {
        if(has_pools || has_autos)
        {
            why = "its combo alias format (CMBA v" + std::to_string(version) +
                  ") has no room for combo pools or autocombos; Save As 2.55.18 or 3.0 Nightly keeps them";
            return false;
        }

        out = orig_tail;
        return true;
    }

    if(version == 4)
    {
        if(has_autos)
        {
            why = "its combo alias format (CMBA v4) has no room for autocombos; Save As 2.55.18 or 3.0 Nightly keeps them";
            return false;
        }

        out = pools_only;
    }
    else
        out = now;

    // a forced rewrite of unchanged lists (the round-trip checks) keeps bytes upstream would
    // normalise (trailing empty pools, cleared autocombos), as the reader sees the same
    if(was == now && zqw_force_serialize && !reencode)
        out = orig_tail;

    return true;
}

// ---- scrolling regions ------------------------------------------------------------------

bool zq_region_ids(int map, byte out[64])
{
    memset(out, 0, 64);

    if(map < 0 || (size_t)map >= q255_map_region_ids.size() || q255_map_region_ids[map].size() != 64)
        return false;

    memcpy(out, &q255_map_region_ids[map][0], 64);

    for(int k = 0; k < 64; ++k)
        if(out[k])
            return true;

    return false;
}

void zq_region_set_ids(int map, const byte ids[64])
{
    if(map < 0)
        return;

    if((size_t)map >= q255_map_region_ids.size())
        q255_map_region_ids.resize(map + 1);

    q255_map_region_ids[map].assign(ids, ids + 64);
}

namespace
{

int rid(const byte ids[128], int x, int y) { return ids[y * 16 + x]; }

// upstream determine_region_size: the rectangle a screen's region spans
void region_size(const byte ids[128], int screen, int &ox, int &oy, int &ex, int &ey)
{
    const int id = ids[screen];
    ox = screen % 16;
    oy = screen / 16;

    if(id == 0)
    {
        ex = ox;
        ey = oy;
        return;
    }

    while(ox > 0 && rid(ids, ox - 1, oy) == id) --ox;
    while(oy > 0 && rid(ids, ox, oy - 1) == id) --oy;
    ex = ox;
    while(ex < 15 && rid(ids, ex + 1, oy) == id) ++ex;
    ey = oy;
    while(ey < 7 && rid(ids, ox, ey + 1) == id) ++ey;
}

} // namespace

bool zq_regions_valid(const byte ids[128], std::vector<ZqRegionDesc> *out)
{
    bool seen[128];
    memset(seen, 0, sizeof(seen));

    for(int i = 0; i < 128; ++i)
    {
        if(seen[i])
            continue;

        int ox, oy, ex, ey;
        region_size(ids, i, ox, oy, ex, ey);
        const int id = rid(ids, ox, oy);

        if(id == 0)
        {
            seen[i] = true;
            continue;
        }

        if(out)
        {
            ZqRegionDesc d = { id, i, ex - ox + 1, ey - oy + 1 };
            out->push_back(d);
        }

        // everything inside the rectangle is this id and in no other region
        for(int x = ox; x <= ex; ++x)
            for(int y = oy; y <= ey; ++y)
            {
                const int s = y * 16 + x;

                if(seen[s] || ids[s] != id)
                    return false;

                seen[s] = true;
            }

        // and nothing beside it (not diagonally) has the same id
        for(int x = ox - 1; x <= ex + 1; ++x)
            for(int y = oy - 1; y <= ey + 1; ++y)
            {
                if(x < 0 || y < 0 || x >= 16 || y >= 8)
                    continue;

                const int count = (x == ox - 1 || x == ex + 1) + (y == oy - 1 || y == ey + 1);

                if(count == 1 && rid(ids, x, y) == id)
                    return false;
            }
    }

    return true;
}

bool zq_cpool_pick(int pool, int &cid, int &cs)
{
    if(pool < 0 || pool >= MAXCOMBOPOOLS)
        return false;

    int32_t c = cid;
    int8_t s = (int8_t)cs;

    if(!combo_pools[pool].pick(c, s))
        return false;

    cid = c;
    cs = s;
    return true;
}

// 2.53's combo move (zq_tiles.cpp move_combos, which Insert and Remove also use): combos
// [copy, copy + count) went to tile on. The pools' entries and the autocombos' entries, erase
// combos and display combos in that range follow them, as in upstream's combo move (its "Combo
// Pools" and "Auto Combos" lists: a reference inside the moved range gets the move's offset;
// combo 0 never moves). The lists are empty for a 2.53 quest, so nothing changes there.
void zq_cpool_combos_moved(int copy, int tile, int count)
{
    if(!zq255_quest_is_newer() || count <= 0 || copy == tile)
        return;

    const int32_t diff = tile - copy;

    struct Move
    {
        static void one(int32_t &cid, int copy, int count, int32_t diff)
        {
            if(cid != 0 && cid >= copy && cid < copy + count)
                cid += diff;
        }
    };

    for(int i = 0; i < MAXCOMBOPOOLS; ++i)
    {
        combo_pool &p = combo_pools[i];

        for(size_t k = 0; k < p.combos.size(); ++k)
            Move::one(p.combos[k].cid, copy, count, diff);
    }

    for(int i = 0; i < MAXAUTOCOMBOS; ++i)
    {
        combo_auto &a = combo_autos[i];

        for(size_t k = 0; k < a.combos.size(); ++k)
            Move::one(a.combos[k].cid, copy, count, diff);

        Move::one(a.cid_erase, copy, count, diff);
        Move::one(a.cid_display, copy, count, diff);
    }
}

// -qst-dump-cpools in.qst report.txt (zquest.cpp): the open quest's combo pools, autocombos and
// every map's region ids as ZQuest holds them, and the CMBA tail as the writer would write it,
// to compare a save with upstream's re-save of it.
int zq_dump_cpools(const char *report)
{
    FILE *f = fopen(report, "w");

    if(!f)
        return 1;

    fprintf(f, "CMBA version %d, MAP version %d\n", zq_cpool_cmba_version(), zq_region_map_version());

    for(int i = 0; i < MAXCOMBOPOOLS; ++i)
    {
        const combo_pool &p = combo_pools[i];

        if(p.combos.empty())
            continue;

        fprintf(f, "pool %d (weight %d):", i, (int)p.getTotalWeight());

        for(size_t k = 0; k < p.combos.size(); ++k)
            fprintf(f, " %d/%d x%d", (int)p.combos[k].cid, (int)p.combos[k].cset, (int)p.combos[k].quant);

        fprintf(f, "\n");
    }

    for(int i = 0; i < MAXAUTOCOMBOS; ++i)
    {
        combo_auto &a = combo_autos[i];

        if(a.combos.empty() && !a.getType())
            continue;

        fprintf(f, "auto %d: type %d display %d erase %d flags 0x%02X arg %d, %d combos:", i, (int)a.getType(),
                (int)a.getIconDisplay(), (int)a.getEraseCombo(), (int)a.getFlags(), (int)a.getArg(), (int)a.combos.size());

        for(size_t k = 0; k < a.combos.size(); ++k)
            fprintf(f, " %d", (int)a.combos[k].cid);

        // a stored valid flag that upstream's check would now set differently (the editors only
        // store the flag on an edit, so such a flag stays as loaded until then)
        combo_auto chk = a;
        chk.updateValid();

        if(chk.valid() != a.valid())
            fprintf(f, " (stored valid flag differs from the check)");

        fprintf(f, "\n");
    }

    for(size_t m = 0; m < q255_map_region_ids.size(); ++m)
    {
        byte ids[64];

        if(!zq_region_ids((int)m, ids))
            continue;

        fprintf(f, "map %d regions:", (int)m + 1);

        for(int y = 0; y < 8; ++y)
        {
            fprintf(f, " ");

            for(int x = 0; x < 16; ++x)
                fprintf(f, "%X", (x % 2 == 0) ? (ids[y * 8 + x / 2] >> 4) : (ids[y * 8 + x / 2] & 15));
        }

        fprintf(f, "\n");

        // ZQ_DUMP_REGION_UNUSED=1: also the screens not in use that still carry a region id
        // (the region editor clears those only on a map it shows)
        if(getenv("ZQ_DUMP_REGION_UNUSED"))
        {
            bool any = false;

            for(int s = 0; s < 128; ++s)
            {
                const byte d = ids[(s / 16) * 8 + (s % 16) / 2];
                const int id = (s % 2 == 0) ? (d >> 4) : (d & 15);
                mapscr *scr = (int)m < map_count ? Map.AbsoluteScr((int)m, s) : NULL;

                if(id && !(scr && (scr->valid & mVALID)))
                {
                    if(!any)
                        fprintf(f, "map %d unused screens with ids:", (int)m + 1);

                    fprintf(f, " %02X=%d", s, id);
                    any = true;
                }
            }

            if(any)
                fprintf(f, "\n");
        }
    }

    // ZQ_DUMP_SCREEN=map,screen (map from 1, screen in hex): that screen's combos and csets too
    if(const char *ds = getenv("ZQ_DUMP_SCREEN"))
    {
        int m = 0;
        unsigned sc = 0;

        if(sscanf(ds, "%d,%x", &m, &sc) == 2 && m >= 1 && m <= map_count && sc < 136)
            if(mapscr *scr = Map.AbsoluteScr(m - 1, sc))
            {
                fprintf(f, "map %d screen 0x%02X combos (cset):\n", m, sc);

                for(int y = 0; y < 11; ++y)
                {
                    for(int x = 0; x < 16; ++x)
                        fprintf(f, " %5d(%d)", (int)scr->data[y * 16 + x], (int)scr->cset[y * 16 + x]);

                    fprintf(f, "\n");
                }
            }
    }

    std::vector<byte> e;
    zq_cpool_encode(e, true);
    uint64_t h = 1469598103934665603ULL;
    zq_hash_add(&h, e.empty() ? NULL : &e[0], e.size());
    fprintf(f, "encoded pools and autocombos: %d bytes, hash %08X%08X\n", (int)e.size(), (unsigned)(h >> 32), (unsigned)h);
    fclose(f);
    return 0;
}
