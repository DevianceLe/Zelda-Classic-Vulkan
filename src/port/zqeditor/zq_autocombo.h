// Combo pools and autocombos of 2.55.17 and 3.0 Nightly quests, for ZQuest (zq_cpool.cpp).
//
// Ported from upstream ZQuest Classic (GPL): core/cpool.h and core/autocombo.h. The quest's
// combo alias section (CMBA) stores them from version 4 (pools) and 5 (autocombos), after its
// aliases. ZQuest keeps them as upstream's editor holds them after loading the quest (the
// lists below), the CMBA writer writes them as upstream's editor writes them, and the map
// editor draws with them (the Pool and Autocombo drawing modes, zquest.cpp).
//
// Written for the port (no jwin or Allegro code).

#ifndef ZQ_AUTOCOMBO_H
#define ZQ_AUTOCOMBO_H

#include <stdint.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "zdefs.h"

#ifndef MAXCOMBOPOOLS
#define MAXCOMBOPOOLS 8192
#endif
#ifndef MAXAUTOCOMBOS
#define MAXAUTOCOMBOS 8192
#endif

// ---- combo pools (core/cpool.h) ---------------------------------------------------------

// One combo of a pool: placed with the pool's other combos, as often as its weight says.
// cset -1 keeps the cset the editor draws with.
struct cpool_entry
{
    int32_t cid;
    int8_t cset;
    word quant;

    cpool_entry() : cid(-1), cset(-1), quant(0) {}
    cpool_entry(int32_t data, int8_t cs, word q) : cid(data), cset(cs), quant(q) {}
    void clear() { *this = cpool_entry(); }
    bool valid() const { return quant > 0 && unsigned(cid) < MAXCOMBOS; }
};

struct combo_pool
{
    std::vector<cpool_entry> combos;

    combo_pool() : totalweight(0) {}
    combo_pool &operator=(const combo_pool &other);
    void push(int32_t cid, int8_t cs, word q = 1);  // adds weight to an entry, or a new entry
    void add(int32_t cid, int8_t cs, word q = 1);   // adds a new entry
    void swap(size_t ind1, size_t ind2);
    void erase(size_t ind);
    void trim();                                    // merges duplicates, drops weight-0 entries
    const cpool_entry *get_ind(size_t index) const;
    const cpool_entry *get_w(size_t weight_index) const;
    const cpool_entry *get_w_wrap(size_t weight_index) const;
    const cpool_entry *pick() const;                // a random entry, by weight
    bool get_ind(int32_t &cid, int8_t &cs, size_t index) const;
    bool get_w(int32_t &cid, int8_t &cs, size_t weight_index) const;
    bool get_w_wrap(int32_t &cid, int8_t &cs, size_t weight_index) const;
    bool pick(int32_t &cid, int8_t &cs) const;
    void clear()
    {
        combos.clear();
        totalweight = 0;
    }
    void recalc();
    size_t getTotalWeight() const { return totalweight; }
    bool valid() const { return totalweight > 0; }

private:
    size_t totalweight;     // not saved: the sum of the weights
    cpool_entry *get(int32_t cid, int8_t cs);
};

// ---- autocombos (core/autocombo.h) ------------------------------------------------------

enum { ACT_NORMAL, ACT_CPOOL };
enum
{
    AUTOCOMBO_NONE, AUTOCOMBO_Z1, AUTOCOMBO_BASIC, AUTOCOMBO_FENCE, AUTOCOMBO_Z4, AUTOCOMBO_RELATIONAL,
    AUTOCOMBO_DGNCARVE, AUTOCOMBO_DOR, AUTOCOMBO_TILING, AUTOCOMBO_REPLACE, AUTOCOMBO_DENSEFOREST,
    AUTOCOMBO_EXTEND
};

enum
{
    ACF_VALID = 0x1,
    ACF_CROSSSCREENS = 0x2,
    ACF_FLIP = 0x4,
    ACF_LEGACY = 0x4,       // used by Relational and Dungeon Carving, neither of which use flip
    ACF_VERTICAL = 0x4,     // used by Extend
    ACF_CONNECTEDGE = 0x8,  // used by Basic, Flat Mountain, Relational, DoR, and Dense Forest
    ACF_CONNECTSOLID = 0x10, // used by Basic, Relational, and Dense Forest
    ACF_NOCREATE = 0x20,    // used by Extend
    ACF_UNIQUECORNER = 0x20, // used by Dungeon Carving
    ACF_DOGROUP = 0x40      // used by Extend
};

enum
{
    ACIR_ILLEGAL_TYPE = 0x1,
    ACIR_DUPLICATE_COMBO = 0x2,
    ACIR_MISSING_COMBO = 0x4,
    ACIR_ERASE_IN_SET = 0x8
};

struct autocombo_entry
{
    int32_t cid;
    byte ctype;             // unused by upstream so far (meant for combo pools)
    int16_t offset;         // the slot
    int16_t engrave_offset; // the picture shown beside the slot (zq_engravings.cpp)

    autocombo_entry() : cid(-1), ctype(0), offset(-1), engrave_offset(0) {}
    autocombo_entry(int32_t data, byte ct, int32_t of, int32_t eo) : cid(data), ctype(ct), offset((int16_t)of),
        engrave_offset((int16_t)eo) {}
    void clear() { *this = autocombo_entry(); }
    bool valid() const { return offset > -1 && unsigned(cid) < MAXCOMBOS; }

    static int16_t base_engrave_offset(byte type);
};

struct combo_auto
{
    std::vector<autocombo_entry> combos;
    byte flags;

    combo_auto() : flags(0), cid_display(0), cid_erase(0), type(AUTOCOMBO_NONE), arg(0), invalid_reasons(0),
        offsets(0, 0) {}
    combo_auto &operator=(const combo_auto &other);
    void addEntry(int32_t cid, byte ct, int32_t of, int32_t eo);
    void removeEntry();
    void clear(bool clear_all = false)
    {
        if(clear_all)
        {
            type = AUTOCOMBO_NONE;
            cid_display = 0;
            flags = 0;
            cid_erase = 0;
            arg = 0;    // (upstream leaves arg as it was; a fresh upstream ZQuest has 0 there)
        }

        combos.clear();
    }
    bool valid() const { return (flags & ACF_VALID) != 0; }
    void updateValid();
    std::string getInvalidReason();
    bool hasTemplate();
    bool canErase();

    bool containsCombo(int32_t cid, bool requirevalid = true) const;
    bool isIgnoredCombo(int32_t cid) const;

    byte getFlags() const { return flags; }
    void setFlags(byte newflags) { flags = newflags; }
    int32_t getType() const { return type; }
    void setType(int32_t newtype) { type = (byte)newtype; }
    byte getArg() const { return arg; }
    void setArg(byte newarg) { arg = newarg; }
    std::pair<byte, byte> getOffsets() { return offsets; }
    void setOffsets(byte xo, byte yo)
    {
        if(xo < 255)
            offsets.first = xo;
        if(yo < 255)
            offsets.second = yo;
    }

    int32_t getDisplay() const;
    int32_t getIconDisplay() const { return cid_display; }
    void setDisplay(int32_t newdisplay) { cid_display = newdisplay; }
    int32_t getEraseCombo() const { return cid_erase; }
    void setEraseCombo(int32_t newerase) { cid_erase = newerase; }

    static int32_t legacy_offsets(byte type, int16_t offset);
    static int32_t convert_offsets(byte type, int16_t offset);
    static bool ignore_fill(byte type, int32_t slot);

    std::map<int32_t, byte> getMapping();

    int32_t cid_display;
    int32_t cid_erase;

private:
    byte type;
    byte arg;
    byte invalid_reasons;
    std::pair<byte, byte> offsets;  // not saved: the tiling and dense forest patterns' origin
};

// The open quest's pools and autocombos, as upstream's editor holds them after loading it
// (empty for a quest older than 2.55 or whose CMBA section has none).
extern combo_pool combo_pools[MAXCOMBOPOOLS];
extern combo_auto combo_autos[MAXAUTOCOMBOS];

// ---- the store (zq_cpool.cpp) -----------------------------------------------------------

// What the open quest's own format can hold: its CMBA section's version holds pools from 4 and
// autocombos from 5, its MAP section's version scrolling region ids from 31. (A quest without
// such a section: 0.) A quest older than 2.55 holds none of them.
int zq_cpool_cmba_version();
int zq_region_map_version();
bool zq_cpool_quest_holds_pools();
bool zq_cpool_quest_holds_autos();
bool zq_region_quest_holds_regions();

// The pools and autocombos in upstream's own encoding: a pool count and the pools up to the
// last valid one, then (with_autos) an autocombo count and the autocombos up to the last valid
// one, as upstream's writecomboaliases writes them after its aliases.
void zq_cpool_encode(std::vector<byte> &out, bool with_autos);

// Reads that encoding (from a CMBA section of version 'version', from 'at' in 'data') into
// the lists, as upstream's readcomboaliases does. False if it doesn't read.
bool zq_cpool_decode(const std::vector<byte> &data, size_t at, int version, combo_pool *pools,
                     combo_auto *autos, std::string &why);

// The CMBA writer (zqw_combos.cpp): what follows the aliases in a section of 'version', given
// the part's own bytes from there (orig_tail). Unchanged pools and autocombos keep their bytes;
// changed ones are written as upstream writes them. False when the version can't hold what the
// quest has now (why says so).
bool zq_cpool_section_tail(int version, const std::vector<byte> &orig_tail, std::vector<byte> &out,
                           std::string &why);

// A map's scrolling region ids, 64 bytes (two screens a byte, even columns in the high nibble),
// as the region editor left them. False if the store has none for that map (all 0).
bool zq_region_ids(int map, byte out[64]);
void zq_region_set_ids(int map, const byte ids[64]);

// Upstream's region check (core/mapscr.cpp get_all_region_descriptions): every region id a
// rectangle, not touching another of the same id. ids: one per screen, 128 (8 rows of 16).
struct ZqRegionDesc
{
    int id, screen, w, h;
};
bool zq_regions_valid(const byte ids[128], std::vector<ZqRegionDesc> *out);

// Random weighted pick from a pool for the map editor (combo -1 if the pool is empty).
bool zq_cpool_pick(int pool, int &cid, int &cs);

#endif
