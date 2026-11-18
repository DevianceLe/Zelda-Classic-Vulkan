// zscript255: the second half of what 2.55.17's ZQuest does with a compile result.
//
// After its "zscript" program compiles the buffer, 2.55.17's ZQuest writes each script it
// puts in a slot back out as ZASM text (disassembled_script_data::write) and assembles that
// text with zq/ffasm.cpp's parse_script_string into a script_data: metadata read back from
// the "#..." lines, then one command per line (command number, two arguments, an optional
// string and an optional {..} list). zq_class.cpp's write_one_ffscript then stores it in
// the quest's script section. This file is a port of those three pieces, so zscript255
// can hand ZQuest the exact script records 2.55.17 stores.
#ifndef ZS255_ASSEMBLE_H
#define ZS255_ASSEMBLE_H

#include "base/zdefs.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ZScript
{
	struct disassembled_script_data;
	class ParserScriptType;
}

namespace zs255_asm
{
	// zdefs.h script_command / script_variable: one row of the assembler's tables
	// (zs255_asm_tables.inc, from zq/ffasm.cpp).
	struct script_command
	{
		char name[64];
		byte args;
		byte arg1_type; //0=reg, 1=val;
		byte arg2_type; //0=reg, 1=val;
		byte arr_type; //0x1 = string, 0x2 = array
	};
	struct script_variable
	{
		char name[64];
		int32_t id;
		word maxcount;
		byte multiple;
	};
	extern script_command command_list[];
	extern script_variable variable_list[];
	extern const int32_t num_commands;

	// zdefs.h ffscript: one command of a script. command 0xFFFF ends the script.
	struct zasm_line
	{
		word command = 0xFFFF;
		int32_t arg1 = 0;
		int32_t arg2 = 0;
		std::unique_ptr<std::string> str;
		std::unique_ptr<std::vector<int32_t>> vec;
		void clear()
		{
			command = 0xFFFF;
			arg1 = arg2 = 0;
			str.reset();
			vec.reset();
		}
	};

	// zdefs.h script_data: the commands (size of them stored, the last one 0xFFFF) and
	// the metadata.
	struct script_record
	{
		std::vector<zasm_line> zasm;
		zasm_meta meta;
		size_t size = 0;
	};

	// ffasm.cpp parse_script_string: assembles ZASM text (metadata lines, then one
	// command per line) into rec. On a line it can't read, returns false with upstream's
	// message in error and rec disabled (its first command 0xFFFF), as ZQuest leaves it.
	bool assemble(std::string const& scriptstr, script_record& rec, std::string& error);

	// zq_class.cpp write_one_ffscript: the bytes 2.55.17 stores for one script slot in
	// its script section (section version V_FFSCRIPT 22, metadata version METADATA_V 5),
	// little-endian:
	//   u32 size (commands including the closing 0xFFFF)
	//   metadata: u16 zasm_v, u16 meta_v, u16 ffscript_v, u8 script type,
	//     8 x cstr run_idens, 8 x u8 run_types, u8 flags, 4 x u16 compiler_v1..4,
	//     cstr script_name, cstr author, then (attribute, help) pairs as
	//     10 x (cstr attributes, wstr attributes_help), 8 x (cstr attribytes, wstr help),
	//     8 x (cstr attrishorts, wstr help), 16 x (cstr usrflags, wstr help),
	//     8 x (cstr initd, wstr initd_help), 8 x i8 initd_type
	//     (cstr = u8 length + bytes, at most 255; wstr = u16 length + bytes)
	//   then per command: u16 command; if 0xFFFF stop; else i32 arg1, i32 arg2,
	//     u32 string length + bytes, u32 list length + i32 values
	// An empty slot is a record of size 1 with zeroed metadata and the single 0xFFFF.
	void write_record(script_record const& rec, std::vector<uint8_t>& out);

	// What ZQuest does for a slot given a compiled script: write it as text, assemble it.
	bool assemble_compiled(ZScript::disassembled_script_data const& data, script_record& rec, std::string& error);
}

// zscript255's -assembled file: every compiled script as 2.55.17's ZQuest would store it in
// a slot. Little-endian:
//   "ZS255ASM", u32 file format 1, u32 V_FFSCRIPT (22), u32 METADATA_V (5),
//   u32 script count, then per script, in name order (as the compile result file):
//     u32 name length, name, i32 script type id (as the compile result file, -1 if the
//     name has no type there), u32 record length, record (zs255_asm::write_record)
// Returns false with a message if a script can't be assembled (a compiler bug) or the
// file can't be written; nothing usable is left in the file then.
bool zs255_write_assembled(std::string const& path,
	std::map<std::string, ZScript::ParserScriptType>& stypes,
	std::map<std::string, ZScript::disassembled_script_data>& scripts,
	std::string& error);

#endif
