// Compiling a 2.55 or 3.0 Nightly quest's ZScript in ZQuest (zq_compile.cpp, the dialog in
// zq_compile_dialog.cpp). 2.53 and older quests keep 2.53's own compiler and dialogs.
//
// The quest's own generation's compiler does the work: zscript255 (2.55.18's) for a quest made
// in 2.55, zscript30 (3.0's, upstream main at ad25219) for one made in a 3.0 Nightly, never the
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
#include <set>
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
// InstVarRemap, components/zasm/inst_var_remap.h), for the scripts where they did.
struct ZqInstVar
{
    word id;
    std::string name, type_name;
    byte kind;                      // upstream's InstVarKind: 0 other, 1 int (value * 10000), 2 long
    ZqInstVar() : id(0), kind(0) {}
};

struct ZqInitDMove
{
    int initd;                      // the run() parameter's InitD index
    byte kind;                      // the parameter's
    word new_id;                    // the variable that takes its values
};

struct ZqVarRemap
{
    std::map<word, word> old_to_new;                // old id -> new id, for variables both compiles have
    std::map<word, ZqInstVar> old_vars, new_vars;   // every variable of each compile, by id
    // a script converted from run() parameters to variables (the new run() has none): the old
    // parameters, their InitD index as the id, and where each one's InitD values are copied
    std::vector<ZqInstVar> old_params;
    std::vector<ZqInitDMove> initd_moves;
};

// One slot whose placed scripts' stored values have to move (upstream's SlotRemap,
// zq/script_slot_remap.h).
struct ZqSlotRemap
{
    int type, slot;                 // ZqSlotType and the table slot
    std::string script_name;
    ZqVarRemap remap;
    std::set<word> new_exports;     // the new script's exported variables: only they can take values
    std::map<word, std::string> old_labels, new_labels;     // export labels, by id
    std::map<word, int> unmatched_value_counts;     // per old variable without a new one: placed scripts with a value
    std::map<int, int> initd_value_counts;          // per old parameter: placed scripts with a value other than 0

    ZqSlotRemap() : type(0), slot(0) {}
    std::vector<ZqInstVar> unmatched_old() const;
    std::vector<ZqInstVar> unmatched_new() const;
    // the user is to be asked: values of a variable that is gone could go to a new one, or
    // InitD values of a converted script to a variable
    bool needs_user_mapping() const;
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

// 3.0, before an assignment: the slots that keep their script but whose instance variables
// moved (upstream's plan_slot_remaps), with how many placed scripts hold values for what has no
// place after the compile. The dialog asks the user about those that need it (upstream's
// InstVarRemapDialog) and changes the plan's remaps by the answers. False (why says why) when
// the quest's placed scripts can't be read.
bool zq_compile_plan_remaps(const ZqCompileResult &r, const std::map<int, std::string> names[ZQS_COUNT],
                            std::vector<ZqSlotRemap> &plan, std::string &why);

// Puts the compiled scripts into the slots by these names and keeps them for the save: the
// scripts section is rebuilt and read back into ZQuest's tables, and for 3.0 the values placed
// scripts hold for exported variables follow their variables (upstream's apply_slot_remaps) by
// 'plan', or by a plan made here (what isn't matched by name is discarded, as upstream does
// without a window). 'log' gets upstream's lines about what was moved. False (why says why)
// changes nothing.
bool zq_compile_assign(const ZqCompileResult &r, std::map<int, std::string> names[ZQS_COUNT], std::string &why,
                       const std::vector<ZqSlotRemap> *plan = NULL, std::string *log = NULL);

// zq_compile_remap.cpp: the placed scripts' stored values. Counts what the plan's remaps have no
// place for; works out every record as the plan leaves it (false: a section couldn't be read or
// laid out again, nothing is changed); puts those records in (after the assignment).
struct ZqRemapPending;
bool zq_remap_count(std::vector<ZqSlotRemap> &plan, std::string &why);
ZqRemapPending *zq_remap_prepare(const std::vector<ZqSlotRemap> &plan, std::string &why);
bool zq_remap_commit(ZqRemapPending *pending, const std::vector<ZqSlotRemap> &plan, std::string &log, std::string &why);
void zq_remap_discard(ZqRemapPending *pending);
// -qst-inst-dump: every placed script's run arguments and stored variable values, one per line.
bool zq_remap_dump(const char *path);
// testing: ZQ_COMPILE_SET_INST's "type,slot,variable,value;..." set on every placed script of
// those slots; how many were set (-1: failed, why says)
int zq_remap_test_set(const char *spec, std::string &why);

// For the scripts section's writer (zqw_scripts.cpp): when a compile was assigned since the
// load (or the last save), the whole section as it is to be written now (its id first, with
// the current script buffer and slot names). False when nothing was compiled.
bool zq_compile_section(std::vector<byte> &out, std::string &why);
bool zq_compile_pending();

// The compile dialog (Quest > Compile ZScript for 2.55/3.0 quests).
void zq_compile_dialog();

#endif
