// zscript255: the parts of upstream 2.55.17's base/zdefs.h that its ZScript compiler uses.
//
// Upstream's zdefs.h pulls in the whole engine (Allegro, game data, rendering). The
// compiler only needs the script metadata (zasm_meta), the script format numbers and a
// few constants, copied here as upstream defines them. Everything else the parser
// includes through this header is the C++ standard library.
#ifndef _ZDEFS_H_
#define _ZDEFS_H_

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include <math.h>
#include <cstring>
#include <set>
#include <list>
#include <assert.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "base/headers.h"
#include "base/qrs.h"
#include "base/util.h"
#include "zconfig.h"

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

// Version of the packfile format scripts are written in (2.55.17: 22).
#define V_FFSCRIPT         22

void al_trace(const char *format, ...);

//Build date info (zscript255_main.cpp)
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

#define METADATA_V			5
// Upstream stamps compiled scripts with its build time. zscript255 uses its own build
// time the same way, unless -build-stamp gives the four numbers (zscript255_main.cpp).
extern word zs255_compiler_v[4];
#define V_COMPILER_FIRST	zs255_compiler_v[0]
#define V_COMPILER_SECOND	zs255_compiler_v[1]
#define V_COMPILER_THIRD	zs255_compiler_v[2]
#define V_COMPILER_FOURTH	zs255_compiler_v[3]
#define ZMETA_NULL_TYPE		1
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
	std::string script_name;
	std::string author;
	std::string attributes[10];
	std::string attribytes[8];
	std::string attrishorts[8];
	std::string usrflags[16];
	std::string attributes_help[10];
	std::string attribytes_help[8];
	std::string attrishorts_help[8];
	std::string usrflags_help[16];
	std::string initd[8];
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
		for(int32_t q = 0; q < 16; ++q)
		{
			usrflags[q].clear();
			usrflags_help[q].clear();
			if(q > 9) continue;
			attributes[q].clear();
			attributes_help[q].clear();
			if(q > 7) continue;
			initd[q].clear();
			initd_help[q].clear();
			initd_type[q] = -1;
			run_idens[q].clear();
			run_types[q] = ZMETA_NULL_TYPE;
			attribytes[q].clear();
			attribytes_help[q].clear();
			attrishorts[q].clear();
			attrishorts_help[q].clear();
		}
		script_name.clear();
		author.clear();
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
	zasm_meta& operator=(zasm_meta const& other)
	{
		zasm_v = other.zasm_v;
		meta_v = other.meta_v;
		ffscript_v = other.ffscript_v;
		script_type = other.script_type;
		for(auto q = 0; q < 16; ++q)
		{
			usrflags[q] = other.usrflags[q];
			usrflags_help[q] = other.usrflags_help[q];
			if(q > 9) continue;
			attributes[q] = other.attributes[q];
			attributes_help[q] = other.attributes_help[q];
			if(q > 7) continue;
			initd[q] = other.initd[q];
			initd_help[q] = other.initd_help[q];
			initd_type[q] = other.initd_type[q];
			run_idens[q] = other.run_idens[q];
			run_types[q] = other.run_types[q];
			attribytes[q] = other.attribytes[q];
			attribytes_help[q] = other.attribytes_help[q];
			attrishorts[q] = other.attrishorts[q];
			attrishorts_help[q] = other.attrishorts_help[q];
			if(q > 3) continue;
		}
		flags = other.flags;
		compiler_v1 = other.compiler_v1;
		compiler_v2 = other.compiler_v2;
		compiler_v3 = other.compiler_v3;
		compiler_v4 = other.compiler_v4;
		script_name = other.script_name;
		author = other.author;
		return *this;
	}
	// The metadata as text, as the ZASM listing starts each script (zs255_scrdata.cpp).
	std::string get_meta() const;
	// Reads one "#NAME = value" line of that text back, as 2.55.17's ZQuest does when it
	// assembles a compiled script into a slot (zs255_scrdata.cpp).
	bool parse_meta(const char *buffer);
};
ScriptType get_script_type(std::string const& name);

// Number types for script InitD (upstream zdefs.h).
enum {nswapDEC, nswapHEX, nswapLDEC, nswapLHEX, nswapBOOL, nswapMAX};

#endif
