// zscript255: upstream ZQuest Classic 2.55.17's ZScript compiler as its own program,
// with no Allegro. Port of upstream's src/parser/parser.cpp (the "zscript" program
// 2.55.17's ZQuest runs to compile a quest's script buffer); the compiler itself is
// upstream's src/parser, in parser/ here, kept as upstream wrote it.
//
// Command line, as upstream's zscript (2.55.17's ZQuest passes
//   -input <buffer file> -console <file> -qr <rules hex> -linked -include <paths>):
//   -input <file>        the ZScript source to compile (required)
//   -linked              talk to the parent process over stdin/stdout as upstream does
//                        (see "Linked mode" below)
//   -unlinked            print to stdout instead; one of -linked/-unlinked is required
//   -console <file>      also write every message to this file
//   -qr <hex>            the quest rules, QUESTRULES_NEW_SIZE (100) bytes as 200 hex
//                        digits; the compile settings among them (qr_PARSER_*) change
//                        the code. Without it, upstream's default rules.
//   -include <a;b;...>   include paths (default: includepaths.txt in the working
//                        directory, else "include/;headers/;scripts/;")
//   -zasm <file>         write the compiled scripts as ZASM text (-append adds to it)
//   -ignore_cassert, -delay_cassert, -force_ignore <files...>   as upstream
//   -delay, -noclose     accepted; -delay waits for a debugger as upstream
// zscript255's own switches:
//   -out <file>          write the compile result (script types, names, metadata,
//                        ZASM lines and labels) in the layout 2.55.17's ZQuest reads
//                        from its "tmp2" file (64-bit layout, see write_compile_data);
//                        in linked mode the default is "tmp2", as upstream. An old
//                        file is removed first; one that can't be written is an
//                        error message (the exit code stays the compile's).
//   -assembled <file>    also write every compiled script as the script record 2.55.17's
//                        ZQuest stores when it puts the script in a slot (assembled from
//                        the ZASM as its ffasm.cpp does; layout in zs255_assemble.h).
//   -config <file>       the user compiler config to read ([Compiler] NO_ERROR_HALT,
//                        HEADER_GUARD, WARN_DEPRECATED, APPROX_EQUAL_MARGIN); default
//                        zscript.cfg in the working directory, as upstream.
//   -messages <file>     write every message as "<code> <text>" on one line: code
//                        -9998 info, -9997 error, -9996 warning, -9995 debug; in the
//                        text, backslash, newline and CR are written \\ \n \r.
//   -chdir <dir>         change the working directory first. Include paths, the
//                        compiler's base_config/zscript.cfg, zscript.cfg and the file
//                        names in messages are relative to it, as upstream's are to
//                        ZQuest's folder; zscript255's headers and config are shipped
//                        in <exe folder>/zscript255_files for this. The files named on
//                        the command line (-input, -console, -out and its "tmp2"
//                        default, -zasm, -assembled, -messages, -config) stay relative
//                        to the folder the program was started in.
//   -build-stamp Y.M.D.H the four "compiler version" numbers stored in every script's
//                        metadata. Upstream stores its build time; zscript255 stores
//                        its own build time unless this is given.
//
// Linked mode (upstream's protocol, binary, native byte order): the program writes an
// int32 0 when it starts; for each message an int32 code (as above), after which it
// reads an int32 back; while working an int32 -9993 then a uint32 of seconds (idle),
// also answered by an int32; at the end an int32 -9994 and the int32 result. The
// message text is in the -console file. On success the compile result is written
// first (-out, default "tmp2" in the folder the program was started in).
//
// Exit code: 0 when the scripts compiled, otherwise the first error's number, or -1.

#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>
#include "base/util.h"
#include "base/qrs.h"
#include "base/zsys.h"
#include "parser/ZScript.h"
#include "parser/config.h"
#include "parser/parserDefs.h"
#include "parser/Compiler.h"
#include "zs255_scrdata.h"
#include "zs255_assemble.h"
#include "zs255_result.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

using namespace std::chrono_literals;
using std::string;

std::vector<std::string> ZQincludePaths;
static std::string console_path;
static std::string messages_path;

extern uint32_t zscript_failcode;
extern bool zscript_error_out;

bool zc_cfg_defaulted = false;

const int BUILDTM_YEAR = (
    __DATE__[7] == '?' ? 1900
    : (((__DATE__[7] - '0') * 1000 )
    + (__DATE__[8] - '0') * 100
    + (__DATE__[9] - '0') * 10
    + __DATE__[10] - '0'));

const int BUILDTM_MONTH = (
    __DATE__ [2] == '?' ? 1
    : __DATE__ [2] == 'n' ? (__DATE__ [1] == 'a' ? 1 : 6)
    : __DATE__ [2] == 'b' ? 2
    : __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 3 : 4)
    : __DATE__ [2] == 'y' ? 5
    : __DATE__ [2] == 'l' ? 7
    : __DATE__ [2] == 'g' ? 8
    : __DATE__ [2] == 'p' ? 9
    : __DATE__ [2] == 't' ? 10
    : __DATE__ [2] == 'v' ? 11
    : 12);

const int BUILDTM_DAY = (
    __DATE__[4] == '?' ? 1
    : ((__DATE__[4] == ' ' ? 0 :
    ((__DATE__[4] - '0') * 10)) + __DATE__[5] - '0'));

const int BUILDTM_HOUR = (
	(__TIME__[0]-'0')*10 +
	(__TIME__[1]-'0'));

const int BUILDTM_MINUTE = (
	(__TIME__[3]-'0')*10 +
	(__TIME__[4]-'0'));

const int BUILDTM_SECOND = (
	(__TIME__[6]-'0')*10 +
	(__TIME__[7]-'0'));

// The "compiler version" stored in script metadata (V_COMPILER_FIRST.. in zdefs.h).
word zs255_compiler_v[4] = {word(BUILDTM_YEAR), word(BUILDTM_MONTH), word(BUILDTM_DAY), word(BUILDTM_HOUR)};

// Upstream's Allegro trace log; nothing here reads one.
void al_trace(const char *, ...)
{
}

void Z_message(char const*, ...)
{
}

const char* ScriptTypeToString(ScriptType type)
{
	static const char script_types[][19] =
	{
		"none", "global", "ffc", "screendata", "hero", "item", "lweapon", "npc", "subscreen",
		"eweapon", "dmapdata", "itemsprite", "dmapdata (AS)", "dmapdata (PS)", "combodata", "dmapdata (MAP)",
		"generic", "generic (FRZ)", "subscreen (engine)"
	};
	return script_types[(int)type];
}

// Upstream's base/zsys.cpp: the switch's index in argv, matched without case.
int32_t used_switch(int32_t argc,char *argv[],const char *s)
{
	// assumes a switch won't be in argv[0]
	for(int32_t i=1; i<argc; i++)
	{
		char const* a = argv[i];
		char const* b = s;
		while(*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
		{
			++a;
			++b;
		}
		if(!*a && !*b)
			return i;
	}
	return 0;
}

bool zparser_errored_out()
{
	return zscript_error_out;
}
void zparser_error_out()
{
	zscript_error_out = true;
}

// Linked mode: upstream's child_process_handler, over this process's stdin/stdout.
static bool linked = true;
struct LinkedPipe
{
	LinkedPipe()
	{
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
#endif
	}
	void write(void const* buf, size_t bytes)
	{
		fwrite(buf, 1, bytes, stdout);
		fflush(stdout);
	}
	void read(void* buf, size_t bytes)
	{
		if(fread(buf, 1, bytes, stdin) != bytes)
			memset(buf, 0, bytes);
	}
};
static LinkedPipe* ConsoleWrite = nullptr;

static void write_message_line(char const* str, int32_t code)
{
	if(messages_path.empty())
		return;
	FILE* f = fopen(messages_path.c_str(), "ab");
	if(!f)
		return;
	fprintf(f, "%d ", code);
	for(char const* p = str; *p; ++p)
	{
		switch(*p)
		{
			case '\\': fputs("\\\\", f); break;
			case '\n': fputs("\\n", f); break;
			case '\r': fputs("\\r", f); break;
			default: fputc(*p, f); break;
		}
	}
	fputc('\n', f);
	fclose(f);
}

void _console_print(char const* str, int32_t code)
{
	write_message_line(str, code);
	if(console_path.size())
	{
		FILE *console=fopen(console_path.c_str(), "a");
		if(console)
		{
			if(ConsoleWrite)
				fprintf(console, "%s", str);
			else
				fprintf(console, "%s\n", str);
			fclose(console);
		}
	}
	if(ConsoleWrite)
	{
		ConsoleWrite->write(&code, sizeof(int32_t));
		ConsoleWrite->read(&code, sizeof(int32_t));
	}
	else printf("%s\n", str);
}

// Upstream formats its messages with Microsoft's _vsnprintf on Windows. Unlike the
// standard vsnprintf it writes no terminating 0 when the text is exactly count
// characters long, and returns -1 when the text is longer than that. zscript255 gives
// the same result on every system, so a message reads the same whatever the system
// (as the source files do, zs255_files.h): this is _vsnprintf, written with vsnprintf.
static int32_t zs255_win_vsnprintf(char* buf, size_t count, const char* format, va_list args)
{
#ifdef _WIN32
	return _vsnprintf(buf, count, format, args);
#else
	va_list measure;
	va_copy(measure, args);
	int32_t len = vsnprintf(nullptr, 0, format, measure);
	va_end(measure);
	if(len < 0)
		return len;
	if(size_t(len) < count)
		return vsnprintf(buf, count, format, args);
	// too long: _vsnprintf stores the first count characters with no 0 after them
	std::vector<char> full(size_t(len) + 1);
	vsnprintf(full.data(), full.size(), format, args);
	memcpy(buf, full.data(), count);
	return size_t(len) == count ? len : -1;
#endif
}

// The zconsole_* functions format into 1024 bytes, as upstream's do: a message of more
// than 1023 characters comes out empty, as it does from 2.55.17 on Windows.
#define ZS255_CONSOLE_FORMAT(code) \
	int32_t ret; \
	char tmp[1024]; \
	va_list argList; \
	va_start(argList, format); \
	ret = zs255_win_vsnprintf(tmp,sizeof(tmp)-1,format,argList); \
	tmp[vbound(ret,0,1023)]=0; \
	va_end(argList); \
	_console_print(tmp, code)

void zconsole_db(const char *format,...)
{
	ZS255_CONSOLE_FORMAT(ZC_CONSOLE_DB_CODE);
}
void zconsole_db(std::string const& str)
{
	_console_print(str.c_str(), ZC_CONSOLE_DB_CODE);
}
void zconsole_warn(const char *format,...)
{
	ZS255_CONSOLE_FORMAT(ZC_CONSOLE_WARN_CODE);
}
void zconsole_warn(std::string const& str)
{
	_console_print(str.c_str(), ZC_CONSOLE_WARN_CODE);
}
void zconsole_error(const char *format,...)
{
	ZS255_CONSOLE_FORMAT(ZC_CONSOLE_ERROR_CODE);
}
void zconsole_error(std::string const& str)
{
	_console_print(str.c_str(), ZC_CONSOLE_ERROR_CODE);
}
void zconsole_info(const char *format,...)
{
	ZS255_CONSOLE_FORMAT(ZC_CONSOLE_INFO_CODE);
}
void zconsole_info(std::string const& str)
{
	_console_print(str.c_str(), ZC_CONSOLE_INFO_CODE);
}
void zconsole_idle(dword seconds)
{
	if(ConsoleWrite)
	{
		int32_t code = ZC_CONSOLE_IDLE_CODE;
		ConsoleWrite->write(&code, sizeof(int32_t));
		ConsoleWrite->write(&seconds, sizeof(dword));
		ConsoleWrite->read(&code, sizeof(int32_t));
	}
}

// Upstream copies the source to a temporary file (std::tmpnam) and compiles that, so
// messages name the copy's path; zscript255 does the same, so they read the same.
// shown_path is the -input as given, script_path the file it names (see -chdir).
std::unique_ptr<ZScript::ScriptsData> compile(std::string script_path, std::string const& shown_path)
{
	if(linked)
		zconsole_info("Compiling the editor script buffer...");
	else zconsole_info("Compiling '%s'", shown_path.c_str());

	if (!std::filesystem::exists(script_path))
	{
		zconsole_error("%s", "Cannot open specified file!");
		zscript_failcode = -404;
		return NULL;
	}

	// copy to tmp file
	char tmpfilename[L_tmpnam];
	if(!std::tmpnam(tmpfilename))
	{
		zconsole_error("%s", "Unable to create a temporary file!");
		zscript_failcode = -404;
		return NULL;
	}

	std::error_code ec;
	std::filesystem::copy_file(script_path, tmpfilename, ec);
	if (ec)
	{
		zconsole_error("%s", "Unable to create a temporary file!");
		zscript_failcode = -404;
		return NULL;
	}

	std::unique_ptr<ZScript::ScriptsData> res(ZScript::compile(tmpfilename));
	std::filesystem::remove(tmpfilename, ec);
	return res;
}

static std::vector<std::string> split(const std::string &s, char delim)
{
	std::vector<std::string> result;
	std::stringstream ss (s);
	std::string item;

	while (getline (ss, item, delim)) {
		result.push_back (item);
	}

	return result;
}

void updateIncludePaths()
{
	std::string includePathString;
	FILE* f = fopen("includepaths.txt", "r");
	if(f)
	{
		int32_t c;
		while((c = fgetc(f)) != EOF && includePathString.size() < MAX_INCLUDE_PATH_CHARS-1)
			includePathString += char(c);
		fclose(f);
	}
	else includePathString = "include/;headers/;scripts/;";

	ZQincludePaths = split(includePathString, ';');
}

bool delay_asserts = false, ignore_asserts = false;
std::vector<std::filesystem::path> force_ignores;

int main(int argc, char **argv)
{
	linked = true;
	if (!used_switch(argc, argv, "-linked"))
	{
		if(used_switch(argc, argv, "-unlinked"))
		{
			linked = false;
		}
		else
		{
			fprintf(stderr, "zscript255: upstream ZQuest Classic 2.55.17's ZScript compiler.\n"
				"Needs -linked or -unlinked, and -input <file>; see zscript255_main.cpp.\n");
			return 1;
		}
	}
	// Files named on the command line are relative to where the program was started,
	// whatever -chdir does.
	std::error_code start_ec;
	std::filesystem::path const start_dir = std::filesystem::current_path(start_ec);
	auto from_start = [&start_dir](std::string const& path) -> std::string
	{
		if(path.empty() || start_dir.empty())
			return path;
		std::filesystem::path p(path);
		return p.is_relative() ? (start_dir / p).string() : path;
	};
	if(int32_t index = used_switch(argc, argv, "-chdir"))
	{
		if(index + 1 >= argc)
		{
			fprintf(stderr, "zscript255: -chdir needs a folder\n");
			return 1;
		}
		std::error_code ec;
		std::filesystem::current_path(argv[index + 1], ec);
		if(ec)
		{
			fprintf(stderr, "zscript255: can't change to '%s'\n", argv[index + 1]);
			return 1;
		}
	}
	if(int32_t index = used_switch(argc, argv, "-build-stamp"))
	{
		int v[4] = {0, 0, 0, 0};
		if(index + 1 >= argc || sscanf(argv[index + 1], "%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3]) != 4)
		{
			fprintf(stderr, "zscript255: -build-stamp needs Y.M.D.H\n");
			return 1;
		}
		for(int q = 0; q < 4; ++q)
			zs255_compiler_v[q] = word(v[q]);
	}
	if(used_switch(argc, argv, "-ignore_cassert"))
		delay_asserts = ignore_asserts = true;
	else if(used_switch(argc, argv, "-delay_cassert"))
		delay_asserts = true;

	if(auto index = used_switch(argc, argv, "-force_ignore"))
	{
		for(int q = index+1; q < argc; ++q)
		{
			if(argv[q][0] == '-') break;
			force_ignores.push_back(std::filesystem::path(argv[q]).lexically_normal());
		}
	}

	int32_t console_path_index = used_switch(argc, argv, "-console");
	if(console_path_index && console_path_index + 1 < argc)
		console_path = from_start(argv[console_path_index + 1]);
	else console_path = "";

	int32_t messages_index = used_switch(argc, argv, "-messages");
	if(messages_index && messages_index + 1 < argc)
	{
		messages_path = from_start(argv[messages_index + 1]);
		if(FILE* f = fopen(messages_path.c_str(), "wb"))
			fclose(f);
	}
	if (linked && !console_path_index)
	{
		zconsole_error("%s", "Error: missing required flag: -console");
		return 1;
	}

	int32_t zasm_out_index = used_switch(argc, argv, "-zasm");
	bool zasm_out_append = used_switch(argc, argv, "-append");
	string zasm_out = "";
	if(zasm_out_index && zasm_out_index + 1 < argc)
		zasm_out = from_start(argv[zasm_out_index + 1]);

	int32_t data_out_index = used_switch(argc, argv, "-out");
	string data_out = linked ? from_start("tmp2") : "";
	if(data_out_index && data_out_index + 1 < argc)
		data_out = from_start(argv[data_out_index + 1]);
	if(!data_out.empty())
	{
		// zscript255: an old result file must not pass for this compile's (upstream
		// leaves the last one in place)
		std::error_code ec;
		std::filesystem::remove(data_out, ec);
	}

	int32_t assembled_index = used_switch(argc, argv, "-assembled");
	string assembled_out = "";
	if(assembled_index && assembled_index + 1 < argc)
		assembled_out = from_start(argv[assembled_index + 1]);
	if(!assembled_out.empty())
	{
		// an old file must not pass for this compile's result
		std::error_code ec;
		std::filesystem::remove(assembled_out, ec);
	}

	std::unique_ptr<LinkedPipe> cph(linked ? new LinkedPipe() : nullptr);
	ConsoleWrite = cph.get();

	if (!zscript_load_base_config("base_config/zscript.cfg"))
	{
		zconsole_error("%s", "Error: failed to load base config");
		return 1;
	}

	{
		int32_t config_index = used_switch(argc, argv, "-config");
		zscript_load_user_config(config_index && config_index + 1 < argc ? from_start(argv[config_index + 1]) : "zscript.cfg");
	}

	int32_t script_path_index = used_switch(argc, argv, "-input");
	if (!script_path_index || script_path_index + 1 >= argc)
	{
		zconsole_error("%s", "Error: missing required flag: -input");
		return 1;
	}

	if(console_path.size())
	{
		FILE *console=fopen(console_path.c_str(), "w");
		if(console)
			fclose(console);
	}

	int32_t qr_hex_index = used_switch(argc, argv, "-qr");
	{
		std::string qr_hex = (qr_hex_index && qr_hex_index + 1 < argc) ?
			argv[qr_hex_index + 1] :
			// TODO: set to defaults in a better way.
			"B343AFAF01C281A00DA58A4211A608DFDF080001162A0410FC5306FE2A274100381B02044031300000065824000000000000D0030000000000000000000000000000000000000000000000000000000034866C3140320000000000000000000000000000";
		if (qr_hex.size() != QUESTRULES_NEW_SIZE * 2)
		{
			zconsole_error("Error: -qr hex string must be of length %d", QUESTRULES_NEW_SIZE * 2);
			return 1;
		}

		for (int i = 0; i < QUESTRULES_NEW_SIZE; i++)
		{
			char ch0 = qr_hex[2 * i];
			char ch1 = qr_hex[2 * i + 1];
			uint8_t nib0 = (ch0 & 0xF) + (ch0 >> 6) | ((ch0 >> 3) & 0x8);
			uint8_t nib1 = (ch1 & 0xF) + (ch1 >> 6) | ((ch1 >> 3) & 0x8);
			quest_rules[i] = (nib0 << 4) | nib1;
		}
		unpack_qrs();
	}

	std::string script_path = argv[script_path_index + 1];
	int32_t syncthing = 0;

	if(linked)
	{
		cph->write(&syncthing, sizeof(int32_t));
	}

	int32_t include_paths_index = used_switch(argc, argv, "-include");
	if (include_paths_index && include_paths_index + 1 < argc)
	{
		std::string include_paths = argv[include_paths_index+1];
		ZQincludePaths = split(include_paths, ';');
	}
	else
	{
		updateIncludePaths();
	}

	if(used_switch(argc, argv, "-delay"))
	{
		zconsole_info("%s","Pausing for debugger...");
		while(true)
		{
			zconsole_idle();
			std::this_thread::sleep_for(1s);
		}
	}
	std::unique_ptr<ZScript::ScriptsData> result(compile(from_start(script_path), script_path));
	if(!result)
		zconsole_info("%s", "Failure!");
	int32_t res = (result ? 0 : (zscript_failcode ? zscript_failcode : -1));

	if(!res && !data_out.empty())
	{
		if(!zs255_write_compile_data(data_out, result->scriptTypes, result->theScripts))
		{
			// zscript255: upstream says nothing here (its editor then reads no result);
			// a half-written file is removed, as for -assembled
			zconsole_error("Can't write '%s'", data_out.c_str());
			std::error_code ec;
			std::filesystem::remove(data_out, ec);
		}
	}
	if(!res && !assembled_out.empty())
	{
		std::string err;
		bool written = zs255_write_assembled(assembled_out, result->scriptTypes, result->theScripts, err);
		// read back with the reader ZQuest uses (zs255_result.h)
		std::vector<zs255_result::AssembledScript> back;
		if(written && (!zs255_result::read_assembled(assembled_out, back) || back.size() != result->theScripts.size()))
		{
			written = false;
			err = "The assembled scripts file '" + assembled_out + "' doesn't read back";
		}
		if(!written)
		{
			// Upstream's editor shows this as an alert after a good compile; the
			// compile itself stands.
			zconsole_error("%s", err.c_str());
			std::error_code ec;
			std::filesystem::remove(assembled_out, ec);
		}
	}

	if(linked)
	{
		int32_t errorcode = ZC_CONSOLE_TERM_CODE;
		cph->write(&errorcode, sizeof(int32_t));
		cph->write(&res, sizeof(int32_t));
	}
	else
	{
		if(res)
		{
			if(res == -1)
				zconsole_warn("Compile finished with exit code '-1' (compiled with warnings)");
			else zconsole_error("Compile finished with exit code '%d' (compiled with errors)", res);
		}
		else zconsole_info("Compile finished with exit code '0' (success)");
	}
	if(!zasm_out.empty() && result)
	{
		FILE *outfile = fopen(zasm_out.c_str(), zasm_out_append ? "a" : "w");
		if(outfile)
		{
			for(auto& p : result->theScripts)
			{
				ZScript::disassembled_script_data const& data = p.second;
				data.write(outfile,false,true);
			}
			fclose(outfile);
		}
	}

	return res;
}
