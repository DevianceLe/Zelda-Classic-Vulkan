// zscript255: the compile result exchange of upstream 2.55.17's zscrdata.h (what its
// "zscript" program hands to ZQuest), without the editor side.
#ifndef ZS255_SCRDATA_H
#define ZS255_SCRDATA_H

#include "base/zdefs.h"
#include "parser/Compiler.h"
#include <map>
#include <string>

#define ZC_CONSOLE_INFO_CODE -9998
#define ZC_CONSOLE_ERROR_CODE -9997
#define ZC_CONSOLE_WARN_CODE -9996
#define ZC_CONSOLE_DB_CODE -9995
#define ZC_CONSOLE_TERM_CODE -9994
#define ZC_CONSOLE_IDLE_CODE -9993

// Writes the compiled scripts as upstream's write_compile_data writes its "tmp2" file
// on 64-bit Windows: every count and length a uint64, script type ids int32, labels
// int32, little-endian whatever the platform:
//   u64 type count, then per script (by name): u64 name length, name, i32 type id
//   u64 script count, then per script (by name): u64 name length, name, metadata
//     (as upstream's write_meta), u8 format, u64 line count, then per line:
//     u64 length, ZASM text, i32 label (-1 none)
// Type ids are upstream's ScriptTypeID: 1 global, 2 ffc, 3 item, 4 npc, 5 eweapon,
// 6 lweapon, 7 player, 8 screen, 9 dmap, 10 itemsprite, 11 untyped, 12 combo,
// 13 subscreen, 14 generic. Names starting with '~' are hidden (~Init).
// Returns false if the file can't be written.
bool zs255_write_compile_data(std::string const& path,
	std::map<std::string, ZScript::ParserScriptType>& stypes,
	std::map<std::string, ZScript::disassembled_script_data>& scripts);

std::string get_script_name(ScriptType type);

#endif
