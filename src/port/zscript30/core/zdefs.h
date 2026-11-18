#ifndef CORE_ZDEFS_H_
#define CORE_ZDEFS_H_

// zscript30: the part of upstream 3.0's src/core/zdefs.h that the ZScript compiler
// uses, copied unchanged (ZQuest Classic 3.0, GPL). Upstream's full header also pulls
// in Allegro and the game's data types; the compiler needs none of them, so this
// excerpt keeps only: the section versions, the include path limit, the ZASM
// metadata (zasm_meta), and the compiled script types (ffscript, zasm_literals,
// zasm_script, script_data). The headers it includes are those of upstream's that have
// no Allegro in them (its zalleg/, render, fonts and config headers are left out).

#include "base/general.h"
#include "base/lazy_default.h"
#include <cstdint>

#include <cstdio>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <cstring>
#include <memory>
#include <optional>
#include "components/zasm/pc.h"
#include <set>
#include <assert.h>
#include <algorithm>
#include <array>
#include <bit>

#include "base/headers.h"
#include "base/about.h"
#include "base/random.h"
#include "base/util.h"
#include "base/zapp.h"
#include "base/check.h"
#include "base/containers.h"
// Upstream's zdefs.h brings these operators (|, &, ~, |=, &= on enums) to every
// file that includes it; the compiler's code relies on them.
#include "core/flags.h"
#include <fmt/format.h>

//Version number of the different section types
#define V_HEADER           9
#define V_RULES           18
#define V_STRINGS         13
#define V_MISC            23
#define V_TILES            4 //3 added blank tile optimization and 4-bit packing; 4 used by .ztileset
#define V_TILES_ZTILESET_MIN 4 // .ztileset files before this used a different per-tile encoding
#define V_COMBOS          73
#define V_CSETS            6 //palette data
#define V_MAPS            41
#define V_DMAPS           28
#define V_DOORS            1
#define V_ITEMS           71
#define V_WEAPONS          9
#define V_COLORS           4 //Misc Colours
#define V_ICONS            10 //Game Icons
#define V_GRAPHICSPACK     1
#define V_INITDATA        52
#define V_GUYS            60
#define V_GUYS_ZNPC_MIN   57 // .znpc files before this used a different field ordering
#define V_MIDIS            5
#define V_CHEATS           1
#define V_SAVEGAME        54
#define V_COMBOALIASES     5
#define V_HEROSPRITES      16
#define V_SUBSCREEN        21
#define V_ITEMDROPSETS     2
#define V_FFSCRIPT         31
#define V_SFX              9
#define V_FAVORITES        4
#define V_ZINFO            5
#define V_ADVMUSIC         1

// not 'real' sections, just separate version numbers
#define V_COMPATRULE       122

#define V_COMPILESETTING   1

#define V_WEAP_DATA        5
#define V_SCRIPT_CONFIG    1

//= V_SHOPS is under V_MISC

// Loose Object Version Metadata
// If the version is 0, it is ther original.
// 'Higher Versions' use negative numbers, so -1 is newer than 0, -2 is newer than -1.
// Sorry for the ugly hack, but it maintains full compatibility through
// Packfile magic. -Z
#define V_ZCOMBO -1
#define V_ZMOD -1
#define V_ZTHEME -1
#define V_ZASM -1
#define V_ZTILE -1
#define V_ZTILESET -1
#define V_ZDMAP -1
#define V_ZALIAS -1
#define V_ZNPC -1
#define V_ZITEM -1
#define V_ZWPNSPR -1


#define MAX_INCLUDE_PATH_CHARS 65535

//Build date info
extern const int BUILDTM_YEAR;
extern const int BUILDTM_MONTH;
extern const int BUILDTM_DAY;
extern const int BUILDTM_HOUR;
extern const int BUILDTM_MINUTE;
extern const int BUILDTM_SECOND;

// The version of the ZASM engine a script was compiled for
// NOT the same as V_FFSCRIPT, which is the version of the packfile format
// where the scripts are serialized
#define ZASM_VERSION        3

const char* ScriptTypeToString(ScriptType type);

#define ZMETA_AUTOGEN		0x01
#define ZMETA_DISASSEMBLED	0x02
#define ZMETA_IMPORTED		0x04

#define SCRIPT_FORMAT_DEFAULT		0
#define SCRIPT_FORMAT_INVALID		1
#define SCRIPT_FORMAT_DISASSEMBLED	2
#define SCRIPT_FORMAT_ZASM			3

#define METADATA_V			7
#define V_COMPILER_FIRST	BUILDTM_YEAR
#define V_COMPILER_SECOND	BUILDTM_MONTH
#define V_COMPILER_THIRD	BUILDTM_DAY
#define V_COMPILER_FOURTH	BUILDTM_HOUR
#define ZMETA_NULL_TYPE		1
#define NUM_ZMETA_ATTRIBUTES       NUM_COMBO_ATTRIBUTES
struct zasm_meta
{
	word zasm_v;
	word meta_v;
	word ffscript_v;
	ScriptType script_type;
	std::string run_idens[8];
	byte run_types[8];
	byte flags;
	word compiler_v1, compiler_v2, compiler_v3, compiler_v4;
	std::string script_name, author;
	std::string script_info, script_setup;
	std::string attributes[NUM_ZMETA_ATTRIBUTES];
	std::string usrflags[16];
	std::string attributes_help[NUM_ZMETA_ATTRIBUTES];
	std::string usrflags_help[16];
	std::string initd_label[8];
	std::string initd_help[8];
	int8_t initd_type[8];
	
	void setFlag(byte flag)
	{
		switch(flag)
		{
			case ZMETA_DISASSEMBLED:
				flags &= ~ZMETA_IMPORTED;
				flags |= ZMETA_DISASSEMBLED;
				break;
			case ZMETA_IMPORTED:
				flags &= ~ZMETA_DISASSEMBLED;
				flags |= ZMETA_IMPORTED;
				break;
			default:
				flags |= flag;
		}
	}
	bool valid() const
	{
		return zasm_v >= 2 && meta_v >= 1 && ffscript_v >= 16;
	}
	void zero()
	{
		zasm_v = 0;
		meta_v = 0;
		ffscript_v = 0;
		script_type = ScriptType::None;
		flags = 0;
		compiler_v1 = 0;
		compiler_v2 = 0;
		compiler_v3 = 0;
		compiler_v4 = 0;
		for(int32_t q = 0; q < NUM_ZMETA_ATTRIBUTES; ++q)
		{
			attributes[q].clear();
			attributes_help[q].clear();
			if(q > 15) continue;
			usrflags[q].clear();
			usrflags_help[q].clear();
			if(q > 7) continue;
			initd_label[q].clear();
			initd_help[q].clear();
			initd_type[q] = -1;
			run_idens[q].clear();
			run_types[q] = ZMETA_NULL_TYPE;
		}
		script_name.clear();
		author.clear();
		script_info.clear();
		script_setup.clear();
	}
	void autogen(bool clears = true)
	{
		if(clears) zero();
		zasm_v = ZASM_VERSION;
		meta_v = METADATA_V;
		ffscript_v = V_FFSCRIPT;
		flags = ZMETA_AUTOGEN;
		compiler_v1 = V_COMPILER_FIRST;
		compiler_v2 = V_COMPILER_SECOND;
		compiler_v3 = V_COMPILER_THIRD;
		compiler_v4 = V_COMPILER_FOURTH;
	}
	zasm_meta()
	{
		zero();
	}
	~zasm_meta()
	{
		
	}
	bool operator==(zasm_meta const& other) const = default;
	bool parse_meta(const char *buffer);
	std::string get_meta() const;
};
ScriptType get_script_type(std::string const& name);
std::string get_script_name(ScriptType type);

#define NUM_ZS_ARGS 3
// One ZASM instruction. A string or array it carries (its literal) is held by its zasm_script.
struct ffscript
{
	word command;
	// 1 + the index of this instruction's literal in its script's zasm_literals, or 0 if it has
	// none.
	uint16_t literal;
	int32_t arg1, arg2, arg3;

	ffscript() : command(0xFFFF), literal(0), arg1(0), arg2(0), arg3(0) {}
	ffscript(word command, int32_t arg1 = 0, int32_t arg2 = 0, int32_t arg3 = 0)
		: command(command), literal(0), arg1(arg1), arg2(arg2), arg3(arg3)
	{}
};
// Quests have many millions of instructions.
static_assert(sizeof(ffscript) == 16);

// A string or array of values an instruction carries, like the string a WRITEPODSTRING writes or
// the table of a GOTOTABLE. Which one a command uses is its arr_type (see script_command), but the
// qst format allows both on any instruction.
//
// Not having a string (or array) is different from having an empty one: WRITEPODSTRING does nothing
// without a string, but with "" it writes a terminator. ZASM text keeps the difference (nothing is
// printed for a missing one), the qst format does not (both save as length 0 and load as missing).
struct zasm_literal
{
	std::optional<std::string> str;
	std::optional<std::vector<int32_t>> vec;

	// True if there is neither a string nor an array.
	bool unset() const
	{
		return !str && !vec;
	}
	bool operator==(const zasm_literal&) const = default;
};

// The literals of a script's instructions, which refer to them by index (ffscript::literal).
//
// Not thread safe, except that literals may be read while another thread adds one, as long as
// that doesn't grow the capacity (see reserve).
class zasm_literals
{
public:
	// ffscript::literal is 16 bits, and 0 means no literal.
	static constexpr size_t max_size = UINT16_MAX;

	// Returns the value of ffscript::literal for an instruction with this literal, or 0 if there
	// are already max_size literals.
	uint16_t add(zasm_literal literal)
	{
		if (literals.size() >= max_size)
			return 0;
		literals.push_back(std::move(literal));
		return (uint16_t)literals.size();
	}

	// Sets the literal of `op`, unless it is unset (then `op` gets none). Returns false if there
	// are already max_size literals.
	bool set(ffscript& op, zasm_literal literal)
	{
		op.literal = 0;
		if (literal.unset())
			return true;
		op.literal = add(std::move(literal));
		return op.literal != 0;
	}

	const zasm_literal* get(const ffscript& op) const
	{
		return op.literal ? &literals[op.literal - 1] : nullptr;
	}
	const std::string* str(const ffscript& op) const
	{
		auto literal = get(op);
		return literal && literal->str ? &*literal->str : nullptr;
	}
	const std::vector<int32_t>* vec(const ffscript& op) const
	{
		auto literal = get(op);
		return literal && literal->vec ? &*literal->vec : nullptr;
	}

	size_t size() const
	{
		return literals.size();
	}
	size_t capacity() const
	{
		return literals.capacity();
	}
	void reserve(size_t size)
	{
		literals.reserve(size);
	}

private:
	std::vector<zasm_literal> literals;
};

struct script_id {
	auto operator<=>(const script_id&) const = default;

	ScriptType type;
	int index;
};

typedef uint16_t zasm_script_id;
struct script_data;

// In 3.0+ there is exactly one zasm script shared by all scripts.
// Prior, each script has its own chunk of zasm.
struct zasm_script
{
	zasm_script() = default;
	zasm_script(zasm_script_id id, std::string name, std::vector<ffscript>&& zasm, zasm_literals&& literals = {}) : id(id), optimized(false), name(name), size(zasm.size()), zasm(std::exchange(zasm, {})), literals(std::exchange(literals, {})), script_datas() {}
	zasm_script(std::vector<ffscript>&& zasm, zasm_literals&& literals = {}) : id(0), optimized(false), name(""), size(zasm.size()), zasm(std::exchange(zasm, {})), literals(std::exchange(literals, {})), script_datas() {}

	zasm_script_id id;
	bool optimized;
	std::string name;
	size_t size;
	std::vector<ffscript> zasm;
	zasm_literals literals;
	std::vector<script_data*> script_datas;

	// One past the highest SCRIPT_INST_VARS register any instruction in this
	// zasm references. Computed lazily; -1 until then.
	// See zasm_script_num_instance_variables.
	int32_t num_instance_variables = -1;

	// TODO: remove the necessity of this terminal command being here.
	bool valid() const
	{
		return !zasm.empty() && zasm[0].command != 0xFFFF;
	}
};

// The pc of the first jump or call to a pc that is not in the script, if there is one.
//
// The compiler never emits this, but some old quests have them: the 2.55 alphas of 2020 wrote a
// jump's target as a decimal in "Export ZASM", which "Import ZASM" then read as -1. Such a script
// is refused on load, so the actual script runtime can assume all jumps are valid. An example of a
// published quest with bad jumps is https://www.purezc.net/index.php?page=quests&id=787.
std::optional<pc_t> zasm_find_invalid_jump(const std::vector<ffscript>& zasm, const zasm_literals& literals);

// The default (zeroed) metadata of an unused script slot.
extern const zasm_meta empty_zasm_meta;

struct script_data
{
	// The zasm instructions used by this script.
	// In quests before 3.0, each script had its own chunk of zasm.
	// Since 3.0 all scripts share the same chunk.
	std::shared_ptr<::zasm_script> zasm_script = nullptr;
	// Most of the thousands of script slots are unused, so metadata is only allocated when set.
	lazy_default<zasm_meta, &empty_zasm_meta> meta;
	script_id id;
	// Start of script within `zasm_script`.
	uint32_t pc;
	// Exclusive.
	uint32_t end_pc;
	
	// Initializer values for instance vars
	bounded_map<word, int32_t> script_d_init {MAX_SCRIPT_INST_VARIABLES, 0};
	bounded_map<word, exported_variable> script_d_exports {MAX_SCRIPT_INST_VARIABLES, {}};

	script_data(ScriptType type, int index) : meta(), id({type, index}), pc(0), end_pc(0) {}

	std::string name() const
	{
		if (meta.get().script_name.empty())
			return fmt::format("{}-{}", ScriptTypeToString(id.type), id.index);
		else
			return fmt::format("{}-{}-{}", ScriptTypeToString(id.type), id.index, meta.get().script_name);
	}
	
	bool valid() const
	{
		return end_pc && zasm_script && zasm_script->valid();
	}
	
	void disable()
	{
		zasm_script = nullptr;
		pc = 0;
		end_pc = 0;
	}
	
	void clear()
	{
		zasm_script = nullptr;
		pc = end_pc = 0;
		meta.reset();
		script_d_init.clear();
		script_d_exports.clear();
	}
};


#endif
