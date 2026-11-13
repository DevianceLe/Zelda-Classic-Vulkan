// A record of a 2.55 or 3.0 Nightly quest section as a list of named fields ("tape"), for
// ZQuest's editors of those quests. Every byte of the record belongs to a field, in file
// order, so encoding an unedited tape gives back the original bytes exactly, and the fields
// are exactly what the quest's own section version holds: nothing newer, nothing converted.
//
// A schema (zq_tape.cpp) walks a record the way the section's reader does, naming each field
// and the tab it belongs to; the record editor (zq_record_editor.cpp) shows the tape; the
// section writers (zqw_items.cpp, zqw_guys.cpp, zqw_maps.cpp) write an edited record's bytes
// and check that the reader takes them back as ZQuest holds them.
//
// A schema can also run over a tape instead of bytes ("rebuild"): every field takes its value
// from the tape by key, or the schema's default for it. That lays a record out again after an
// edit that changes which fields follow (a flag that adds fields, a screen group switched on).

#ifndef ZQ_TAPE_H
#define ZQ_TAPE_H

#include <map>
#include <string>
#include <vector>

#include "zdefs.h"

enum TapeKind
{
    TK_INT,         // a number
    TK_HIDDEN,      // a number the editor doesn't show (counts and switches that shape what follows)
    TK_FLAGS,       // bits, shown as checkboxes
    TK_ENUM,        // a number from a list of names (see TapeList)
    TK_BLOB,        // raw bytes the editor doesn't show
    TK_TEXT,        // a fixed-size text field (bytes = its size)
    TK_CSTR,        // a byte count, then that many characters
    TK_WSTR,        // a word count, then that many characters
    TK_LSTR,        // a dword count, then that many characters
    TK_BITSTR,      // upstream's bitstring (bytes = as stored), edited as bits
    TK_BITFIELD,    // fixed-size bytes edited as bits
    TK_BYTES,       // fixed-size bytes edited one number each
    TK_NLIST        // a count (index: its bytes), then that many numbers (width: each one's bytes)
};

// Name lists for TK_ENUM and the bit names of TK_FLAGS.
enum TapeList
{
    TL_NONE,
    TL_ITEMCLASS,   // upstream's item classes (zinfo)
    TL_COUNTER,     // counters, -1 for none
    TL_SFX,         // the quest's sounds
    TL_SPRITE,      // the quest's weapon sprites
    TL_ITEM,        // the quest's items
    TL_ENEMYCLASS,  // upstream's enemy classes
    TL_ANIM,        // enemy animation styles
    TL_EWEAPON,     // enemy weapons
    TL_DEFENSE,     // defense outcomes
    TL_ITEMFLAGS,   // the item's 32 flags (labels depend on its class)
    TL_ITEMMISC,    // an item attribute (label depends on its class)
    TL_GUYFLAGS,    // enemy flags
    TL_GUYFLAGS2,   // enemy flags 2
    TL_GUYBFLAGS,   // enemy behaviour flags (labels depend on its class)
    TL_MOVEFLAGS,   // movement flags
    TL_WEAPFLAGS,   // weapon data flags
    TL_PICKUPFLAGS, // item pickup flags
    TL_DIR,         // a direction
    TL_DMAPFLAGS,   // DMap flags
    TL_ITEMBITS,    // one bit per item (a DMap's disabled items)
    TL_LAYERBITS,   // one bit per layer, 0-6
    TL_INITFLAGS,   // init data flags (3.0's bitstring)
    TL_INITMISC,    // init data misc bits (2.53's)
    TL_LITEMBITS,   // a level's items: 1 McGuffin, 2 map, 4 compass, 8 boss killed, 16 boss key
    TL_LEVELVALUES, // one number per level (keys)
    TL_BOTTLES,     // one bottle type per slot
    TL_BOTTLETYPE,  // a bottle type (0: none)
    TL_BOTTLEFLAGS, // a bottle type's flags
    TL_SUBFLAGS,    // a subscreen's flags
    TL_WIDGFLAGS,   // a subscreen widget's general flags
    TL_SUBPOSFLAGS, // where a widget shows (subscreen up, down, scrolling)
    TL_SUBTRANSFLAGS,   // a subscreen transition's flags
    TL_INTBTNS,     // buttons: A, B, L, R, Ex1-Ex4
    TL_WIDGSPEC,    // a widget's own flags (index: its type)
    TL_SCRSTATES,   // a screen's state bits (doors, item, secrets...)
    TL_WFLAGS,      // a weapon's flags (upstream's WFLAG_*)
    TL_SIZEFLAGS,   // size and offset overrides
    TL_PSTRFLAGS,   // an item's pickup string flags
    TL_FFCFLAGS,    // a freeform combo's flags
    TL_ROOMFLAGS    // a screen's room flags
};

struct TapeField
{
    std::string key;        // unique within the record ("tile", "misc1", "ffc3.x"...)
    std::string group;      // the editor tab
    std::string label;
    std::string help;
    int width;              // numbers: 1, 2 or 4 bytes; TK_TEXT, TK_BLOB, TK_BITFIELD: the size
    bool sign;
    int kind;               // TapeKind
    int list;               // TapeList
    int index;              // TL_ITEMMISC and friends: which one
    bool extra;             // 2.53's structures don't hold it (the screen editor shows only these)
    unsigned gate;          // the screen "has" bits the field needs (0: none)
    int64_t value;        // numbers
    std::vector<byte> bytes;    // the other kinds (characters, raw bytes)

    TapeField() : width(0), sign(false), kind(TK_INT), list(TL_NONE), index(0), extra(false), gate(0), value(0) {}
};

struct Tape
{
    std::vector<TapeField> f;

    void encode(std::vector<byte> &out) const;
    TapeField *find(const char *key);
    const TapeField *find(const char *key) const;
    int64_t get(const char *key, int64_t dflt = 0) const;   // a number, or dflt
};

// Fields of a tape by key, for tapes too big to search one field at a time (a whole string or
// subscreen section). The tape must not change while the index is used.
struct TapeIndex
{
    explicit TapeIndex(const Tape &t);
    const TapeField *find(const std::string &key) const;
    int64_t get(const std::string &key, int64_t dflt = 0) const;

private:
    std::map<std::string, const TapeField *> m;
};

// Reads a record into a tape (or rebuilds one from another tape). A failed read (past the end)
// sets bad.
struct TapeParser
{
    const byte *d;
    size_t n, pos;
    bool bad;
    Tape *t;
    const Tape *src;        // rebuilding: values come from here by key
    const TapeIndex *src_index;     // rebuilding a big tape: src's fields by key (NULL: searched)
    std::string grp;
    bool extra_mode;
    unsigned gate_bits;
    bool have_def;
    int64_t def_value;

    TapeParser(const byte *data, size_t size, size_t at, Tape *out)
        : d(data), n(size), pos(at), bad(false), t(out), src(NULL), src_index(NULL), extra_mode(false), gate_bits(0),
          have_def(false), def_value(0) {}
    TapeParser(const Tape *source, Tape *out, const TapeIndex *index = NULL)
        : d(NULL), n(0), pos(0), bad(false), t(out), src(source), src_index(index), extra_mode(false), gate_bits(0),
          have_def(false), def_value(0) {}

    void group(const char *g) { grp = g; }
    void extra(bool on) { extra_mode = on; }    // what follows: fields 2.53 doesn't hold
    void gate(unsigned bits) { gate_bits = bits; }
    TapeParser &def(int64_t v) { have_def = true; def_value = v; return *this; }   // the next field's default when rebuilding

    int64_t num(const char *key, const char *label, int width, bool sign, int kind = TK_INT, int list = TL_NONE,
                  int index = 0, const char *help = NULL);
    // shorthands: unsigned and signed numbers
    int64_t u(const char *key, const char *label, int width, int list = TL_NONE, int kind = -1, int index = 0)
    {
        return num(key, label, width, false, kind >= 0 ? kind : (list ? TK_ENUM : TK_INT), list, index);
    }
    int64_t s(const char *key, const char *label, int width, int list = TL_NONE, int kind = -1, int index = 0)
    {
        return num(key, label, width, true, kind >= 0 ? kind : (list ? TK_ENUM : TK_INT), list, index);
    }
    int64_t flags(const char *key, const char *label, int width, int list)
    {
        return num(key, label, width, false, TK_FLAGS, list);
    }
    int64_t hidden(const char *key, int width) { return num(key, key, width, false, TK_HIDDEN); }
    void blob(const char *key, const char *label, size_t len);
    void text(const char *key, const char *label, size_t len);
    void cstr(const char *key, const char *label);
    void wstr(const char *key, const char *label);
    void lstr(const char *key, const char *label);
    void bitstr(const char *key, const char *label, int list);
    void bitfield(const char *key, const char *label, size_t len, int list);
    void bytes(const char *key, const char *label, size_t len, int list);
    // a count of count_width bytes, then that many unsigned numbers of elem_width bytes
    void nlist(const char *key, const char *label, int count_width, int elem_width, int list);
    // A container kept as its bytes: walk reads past it (with rd and rskip) when reading;
    // rebuilding takes the source's bytes, or empty_len zeros.
    void raw(const char *key, const char *label, void (*walk)(TapeParser &), size_t empty_len);
    uint64_t rd(int width);   // reading only: a number, no field
    void rskip(size_t k);
    size_t left() const { return src ? 0 : (pos <= n ? n - pos : 0); }
    // upstream's script_config: script, run args (a counted array), instance values
    void script_config(const char *key, const char *label);
    // upstream's weapon_data, with its own format version
    void weapon_data(const char *key, const char *label);

    // rebuilding: the source's field of that key (through src_index when there is one)
    const TapeField *src_find(const char *key) const
    {
        return src_index ? src_index->find(key) : src->find(key);
    }

private:
    void counted(const char *key, const char *label, int len_bytes, int kind);
    TapeField *take_raw(const char *key, const char *label, int kind, size_t len);
};

// A bitstring's bits (bit i of the result is bit i of the string), and the bytes upstream
// writes for them.
std::vector<byte> zq_bitstr_bits(const std::vector<byte> &stored);
std::vector<byte> zq_bitstr_store(const std::vector<byte> &bits, unsigned size = 65535);
// the size a stored bitstring says it has (what to write it back with)
unsigned zq_bitstr_size(const std::vector<byte> &stored);

// The schemas: one record of the section in version v, from pos. False if it ran past the end.
bool zq_tape_item(TapeParser &p, int v);
bool zq_tape_guy(TapeParser &p, int v);
bool zq_tape_dmap(TapeParser &p, int v);
bool zq_tape_screen(TapeParser &p, int v);  // a screen of the MAP section (versions 18-41)
bool zq_tape_init(TapeParser &p, int v);    // the whole INIT section (versions 18-52)
bool zq_tape_misc(TapeParser &p, int v);    // the whole MISC section (versions 7-23), zq_tape_misc.cpp
bool zq_tape_strings(TapeParser &p, int v); // the whole STR section (versions 5-13), zq_tape_strings.cpp
bool zq_tape_subscreens(TapeParser &p, int v);  // the whole SUBS section (versions 8-21), zq_tape_subscreens.cpp

#endif
