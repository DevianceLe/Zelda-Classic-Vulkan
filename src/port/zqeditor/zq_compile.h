// Compiling a 2.55 or 3.0 Nightly quest's ZScript in ZQuest (zq_compile.cpp, the dialog in
// zq_compile_dialog.cpp). 2.53 and older quests keep 2.53's own compiler and dialogs.
//
// The quest's own generation's compiler does the work: zscript255 (2.55.17's) for a quest made
// in 2.55, zscript30 (3.0's, upstream main at ef321f8) for one made in a 3.0 Nightly, never the
// other way round. Each runs as a separate program, as upstream's ZQuest runs its "zscript",
// over upstream's pipe protocol, and hands back every compiled script exactly as that
// generation's ZQuest stores it in a quest (its -assembled file). Slots are then assigned as
// upstream's do_slots does (Quick Assign, Smart Assign, or by hand), and the scripts section
// (FFSC) is written in the generation's own format: 2.55.17's version 22 for a 2.55 quest,
// 3.0's version 31 (with its ZASM metadata version 7) for a 3.0 quest, whatever version the
// quest had before, as upstream's ZQuest writes it after a compile.

#ifndef ZQ_COMPILE_H
#define ZQ_COMPILE_H

#include <map>
#include <string>
#include <vector>

#include "zdefs.h"

// The script slot types of 2.55 and 3.0, in the order the scripts section keeps their tables
// of slot names.
enum ZqSlotType
{
    ZQS_FFC, ZQS_GLOBAL, ZQS_ITEM, ZQS_NPC, ZQS_LWEAPON, ZQS_EWEAPON, ZQS_HERO, ZQS_DMAP,
    ZQS_SCREEN, ZQS_ITEMSPRITE, ZQS_COMBO, ZQS_GENERIC, ZQS_SUBSCREEN, ZQS_COUNT
};

struct ZqSlotTypeInfo
{
    const char *name;       // "FFC", "Global"...
    int table;              // slots in the section's table of this type (512, 8, 256...)
    int slots;              // slots the user assigns: name table ids 0 to slots - 1
    int offset;             // the table slot of name id 0 (1, except Global: 0)
};
const ZqSlotTypeInfo &zq_slot_type(int t);

// One message from the compiler: code -9998 info, -9997 error, -9996 warning, -9995 debug.
struct ZqCompileMessage
{
    int code;
    std::string text;
};

// A compiled script, as the compiler hands it back.
struct ZqCompiledScript
{
    std::string name;
    int type;                       // ZqSlotType, -1 if no slot takes it (untyped)
    std::vector<byte> record;       // the slot's bytes, as the generation's writer writes them
};

// 3.0: how a script's instance variables moved since the quest's last compile (upstream's
// InstVarRemap), for the scripts where they did.
struct ZqVarRemap
{
    std::map<word, byte> old_kind;                      // every old variable: id -> kind
    std::map<word, word> old_to_new;                    // old id -> new id, for variables both compiles have
    std::map<word, byte> new_kind;                      // old id -> the kind of the variable it became
    std::vector<std::pair<int, word> > initd_moves;     // run() parameter (InitD index) -> the new variable of its name
};

// What a compile gave: every script by name, and for 3.0 the shared command stream and the
// debug data that go with them.
struct ZqCompileResult
{
    bool is30;
    int code;                                   // the compiler's result: 0 compiled
    std::vector<ZqCompileMessage> messages;
    std::map<std::string, ZqCompiledScript> scripts;
    std::vector<std::string> names[ZQS_COUNT];  // per slot type, the scripts it can take, in name order
    std::vector<byte> zasm;                     // 3.0: the stream (count, then the commands)
    std::vector<byte> debug;                    // 3.0: the encoded debug data
    std::map<std::string, ZqVarRemap> remaps;   // 3.0: scripts whose instance variables moved
    int ms;                                     // how long it took
    std::string error;                          // why it couldn't run, if it couldn't

    ZqCompileResult() : is30(false), code(-9999), ms(0) {}
};

// True when the open quest is a 2.55 or 3.0 quest whose scripts this compiles (its scripts
// section was read).
bool zq_compile_available();
bool zq_compile_is30();                     // the quest's scripts are 3.0's (zscript30 compiles them)
int zq_compile_last_version();              // the "last compiled using ZScript" version (MISC), -1 if none
std::string zq_compile_compiler_path();     // the compiler program for the open quest

// The current slot names (what the section's name tables will hold).
std::map<int, std::string> &zq_slot_names(int t);

// Runs the compiler on 'source' (blocking; call it off the UI thread). 'cancel' set to true
// stops it. Messages are added to out.messages as they come (under zq_compile_lock()).
void zq_compile_run(const std::string &source, ZqCompileResult &out, volatile bool *cancel);
void zq_compile_lock();
void zq_compile_unlock();

// Assignment modes, as upstream's: 1 Quick Assign (every named slot gets its script again),
// 2 Smart Assign (also global/hero slots by their slot name, and every other unassigned
// script into the first free slot of its type).
void zq_slots_auto(const ZqCompileResult &r, int mode, std::map<int, std::string> names[ZQS_COUNT]);

// Whether a slot's name is a script the compile gave (false: upstream's "missing" slot,
// which loses its code but keeps its name).
bool zq_slot_found(const ZqCompileResult &r, int t, const std::string &name);

// Puts the compiled scripts into the slots by these names and keeps them for the save: the
// scripts section is rebuilt and read back into ZQuest's tables. False (why says why) changes
// nothing.
bool zq_compile_assign(const ZqCompileResult &r, std::map<int, std::string> names[ZQS_COUNT], std::string &why);

// For the scripts section's writer (zqw_scripts.cpp): when a compile was assigned since the
// load (or the last save), the whole section as it is to be written now (its id first, with
// the current script buffer and slot names). False when nothing was compiled.
bool zq_compile_section(std::vector<byte> &out, std::string &why);
bool zq_compile_pending();

// The compile dialog (Quest > Compile ZScript for 2.55/3.0 quests).
void zq_compile_dialog();

#endif
