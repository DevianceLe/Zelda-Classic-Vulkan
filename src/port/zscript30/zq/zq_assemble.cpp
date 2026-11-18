// zscript30: "-assembled <file>", the compiled scripts exactly as upstream 3.0's ZQuest stores
// them in a quest, for our ZQuest (src/port/zqeditor/zq_compile.cpp), which has no 3.0 code of
// its own.
//
// Upstream's ZQuest reads the compiler's results ("tmp2"), prints every instruction as ZASM text
// (write_script, without comments or metadata) and reads that text back into instructions and
// literals (zq/ffasm.cpp parse_script_string): that list is the one command stream every script
// slot points into (do_slots). The text step is ported here as it is, so the stream comes out the
// same, quirks included (a jump target is "label - 1", a GOTOTABLE's targets likewise).
//
// The file (all numbers little-endian):
//   "ZS30ASM\0", u32 format (1), u32 section version (31), u32 metadata version (7), u32 zasm
//   version (3)
//   u32 count, then per script name the compiler knows: str name, i32 ParserScriptType id
//   u32 count, then per compiled script: str name, u8 format, u32 size + the slot's record as
//     writeffscript's write_one_ffscript writes it (1, metadata, pc, end pc, the instance
//     variables' initial values, the exported variables); a script that isn't valid() there (see below) is
//     the single byte 0, an empty slot
//   u32 size + the command stream as write_quest_zasm writes it (count, then every command with
//     its third argument, string and array)
//   u32 size + the debug data, encoded (DebugData::encode), which writeffscript puts last
//   u32 count, then per script whose instance variables moved since the quest's last compile
//     (-remap-old <file>: that compile's encoded debug data; upstream's plan_slot_remaps, see
//     write_remaps below): str name, u32 count + (u16 id, u8 kind) per old variable, u32 count +
//     (u16 old id, u16 new id, u8 new kind) per variable both have, u32 count + (u32 InitD index,
//     u16 new id) per run() parameter that became a variable of the same name
// where str is a u32 length and the bytes.
//
// Ported from upstream ZQuest Classic 3.0 (GPL), src/zq/ffasm.cpp and src/zq/zq_class.cpp.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/util.h"
#include "components/zasm/cmp.h"
#include "components/zasm/debug_data.h"
#include "components/zasm/inst_var_remap.h"
#include "components/zasm/table.h"
#include "core/zdefs.h"
#include "parser/Compiler.h"
#include "parser/parserDefs.h"

using std::string;
using std::vector;
using namespace util;

// parser.cpp's (zscrdata.h): the compiled scripts as ZASM text.
void write_script(ZScript::ZasmCompilerResult& zasmCompilerResult, string& dest, bool commented, bool write_meta);

namespace
{

// ---- upstream zq/ffasm.cpp ------------------------------------------------------------------

int32_t ffparse(char const* string)
{
	double negcheck = atof(string);

	//if no decimal point, just atoi
	char const* ptr1=strchr(string, '.');
	if(!ptr1)
	{
		return atoi(string)*10000;
	}

	int32_t ret=0;
	char *tempstring1;
	tempstring1=(char *)malloc(strlen(string)+5);
	strcpy(tempstring1, string);

	for(int32_t i=0; i<4; ++i)
	{
		tempstring1[strlen(string)+i]='0';
	}

	char* ptr=strchr(tempstring1, '.');
	*ptr=0;
	ret=atoi(tempstring1)*10000;

	++ptr;
	char *ptr2=ptr;
	ptr2+=4;
	*ptr2=0;

	if(negcheck<0)
		ret-=atoi(ptr);
	else ret+=atoi(ptr);

	free(tempstring1);
	return ret;
}

bool ffcheck(char const* arg)
{
	for(int32_t i=0; i<0x100; i++)
	{
		if(arg[i]!='\0')
		{
			if(i==0)
			{
				if(arg[i]!='-' && arg[i]!='.' && !(arg[i] >= '0' && arg[i] <='9'))
					return false;
			}
			else
			{
				if(arg[i]!='.' && !(arg[i] >= '0' && arg[i] <='9'))
					return false;
			}
		}
		else
		{
			i=0x100;
		}
	}

	return true;
}

// Upstream keeps this for the whole editor session; every run of this program is one compile.
std::map<std::string, int32_t> labels;

#define SUBBUFSZ         0x200
#define ERR_INSTRUCTION  0
#define ERR_PARAM1       1
#define ERR_PARAM2       2
#define ERR_PARAM3       3
#define ERR_STR          4
#define ERR_VEC          5
#define ERR_LITERALS     6
const char* errstrbuf[] =
{
	"invalid instruction!",
	"parameter 1 invalid!",
	"parameter 2 invalid!",
	"parameter 3 invalid!",
	"string parameter invalid!",
	"vector parameter invalid!",
	"too many string and array literals!",
};

int32_t set_argument(char const* argbuf, int32_t& arg)
{
	if (auto var = get_script_variable(std::string(argbuf)))
	{
		arg = *var;
		return 1;
	}

	return 0;
}

std::optional<int> check_comparestr(char const* buf)
{
	int cmp = 0;
	if(buf[0] == 'B')
	{
		cmp |= CMP_BOOL;
		++buf;
	}
	if(buf[0] == 'I')
	{
		cmp |= CMP_SETI;
		++buf;
	}
	if(!strcmp(buf,"<"))
		cmp |= CMP_LT;
	else if(!strcmp(buf,">"))
		cmp |= CMP_GT;
	else if(!strcmp(buf,"<="))
		cmp |= CMP_LE;
	else if(!strcmp(buf,">="))
		cmp |= CMP_GE;
	else if(!strcmp(buf,"=="))
		cmp |= CMP_EQ;
	else if(!strcmp(buf,"!="))
		cmp |= CMP_NE;
	else if(!strcmp(buf,"Never"))
		;
	else if(!strcmp(buf,"Always"))
		cmp |= CMP_FLAGS;
	else return std::nullopt; //nonmatching
	return cmp;
}

bool handle_arg(ARGTY ty, char const* buf, int& arg)
{
	switch(ty)
	{
		case ARGTY::READ_REG:
		case ARGTY::WRITE_REG:
		case ARGTY::READWRITE_REG:
		{
			if(!set_argument(buf, arg))
				return false;
			return true;
		}
		case ARGTY::LITERAL_REG:
		{
			if (*buf != '@')
				return false;
			if(!set_argument(++buf, arg))
				return false;
			return true;
		}
		case ARGTY::LITERAL:
		{
			if(!ffcheck(buf))
				return false;
			arg = ffparse(buf);
			return true;
		}
		case ARGTY::COMPARE_OP:
		{
			if(auto cmpval = check_comparestr(buf))
			{
				arg = *cmpval;
				return true;
			}
			return false;
		}
		default:
			break;
	}
	return false;
}

int32_t parse_script_section(char const* combuf, char const* const* argbufs, ffscript& zas, zasm_literals& literals, int32_t &retcode, std::vector<int32_t> *vptr, std::string *sptr)
{
	zas.arg1 = 0;
	zas.arg2 = 0;
	zas.literal = 0;
	zasm_literal literal;
	if(vptr)
		literal.vec = *vptr;
	if(sptr)
		literal.str = *sptr;

	auto sc = get_script_command(std::string(combuf));
	if (!sc)
	{
		retcode=ERR_INSTRUCTION;
		return 0;
	}

	auto i = sc->command;
	zas.command = i;
	bool is_goto = false;

	switch(i)
	{
		case GOTO: case LOOP: //Hardcodes for some control flow opcodes
		case GOTOTRUE: case GOTOFALSE: case GOTOLESS: case GOTOMORE: case GOTOCMP:
			is_goto = true;
			break;
		default:
			break;
	}
	if(is_goto) //hardcoded arg1
	{
		string lbl(argbufs[0]);
		auto it = labels.find(lbl);
		if(it != labels.end())
			zas.arg1 = (*it).second;
		else
			zas.arg1 = atoi(argbufs[0])-1;
	}
	int *args[] = {&zas.arg1, &zas.arg2, &zas.arg3};
	for(int q = (is_goto ? 1 : 0); q < sc->args; ++q)
	{
		if(!handle_arg(sc->arg_type[q], argbufs[q], *(args[q])))
		{
			retcode = ERR_PARAM1+q;
			return 0;
		}
	}
	if(byte b = sc->arr_type)
	{
		if(!(b&0x1) != !sptr) //string
		{
			retcode = ERR_STR;
			return 0;
		}
		if(!(b&0x2) != !vptr) //vector
		{
			retcode = ERR_VEC;
			return 0;
		}
	}
	switch(i)
	{
		case CALLFUNC: //Hardcoded, based on GOTO above
			zas.arg1 -= 1;
			break;
		case GOTOTABLE:
			// The vector is {min_key, default_pc, targets...}; every entry past
			// the key is a label, written 1-indexed like the goto args above.
			if(literal.vec)
				for(size_t q = 1; q < literal.vec->size(); ++q)
					(*literal.vec)[q] -= 1;
			break;
		case GOTORANGES:
			// The vector is {default_pc, start, end, pc, ...}; the default and
			// every third entry after it are labels, the bounds are values.
			if(literal.vec)
			{
				(*literal.vec)[0] -= 1;
				for(size_t q = 3; q < literal.vec->size(); q += 3)
					(*literal.vec)[q] -= 1;
			}
			break;
		default:
			break;
	}

	if (!literals.set(zas, std::move(literal)))
	{
		retcode = ERR_LITERALS;
		return 0;
	}

	return 1;
}

// zscript30: upstream shows an error box and returns D_CLOSE; here the error goes to the
// compiler's console and the result is false.
bool parse_script_string(std::vector<ffscript>& zasm, zasm_literals& literals, std::string const& scriptstr)
{
	string buffer;
	char combuf[SUBBUFSZ] = {0};
	char arg1buf[SUBBUFSZ] = {0};
	char arg2buf[SUBBUFSZ] = {0};
	char arg3buf[SUBBUFSZ] = {0};
	char *argbufs[] = {arg1buf, arg2buf, arg3buf};
	vector<int> arr_vec;
	string arr_str;
	bool has_vec = false;
	bool has_str = false;
	bool stop=false;
	bool success=true;
	int num_commands = 0;
	char const* scrptr = scriptstr.c_str();

	for(int i=0;; i++)
	{
		buffer.clear();

		if(stop)
		{
			num_commands=i+1;
			break;
		}

		bool running = true;
		while(running)
		{
			char c = *scrptr++;

			if(!c) //null terminator
			{
				stop = true;
				--scrptr;
				break;
			}
			else
			{
				buffer += c;
				if(c == ';' || c == '\n' || c == 13)
				{
					if(c != '\n')
					{
						while(true)
						{
							char c2 = *scrptr++;
							if(c2 == '\n') break;
							if(!c2)
							{
								stop=true;
								break;
							}
						}
					}
					buffer.pop_back();
					break;
				}
				else if (c == '"' || c == '{')
				{
					bool vec = c == '{';
					auto j = buffer.size()-1;
					bool bslash = false;
					while(true)
					{
						char c2 = *scrptr++;
						bslash = c2 == '\\';
						if(!c2) //null terminator
						{
							stop=true;
							--scrptr;
							running = false;
							break;
						}
						buffer += vec ? toupper(c2) : c2;
						if(vec)
						{
							if(c2 == '}')
							{
								size_t sz = vecstr_size(buffer.c_str()+j);
								if(sz)
								{
									j += sz;
									break;
								}
							}
						}
						else
						{
							if(!bslash && c2 == '"')
							{
								size_t sz = escstr_size(buffer.c_str()+j);
								if(sz)
								{
									j += sz;
									break;
								}
							}
						}
					}
				}
			}
		}

		int32_t k=0;

		while(buffer[k] == ' ' || buffer[k] == '\t') k++;

		if(buffer[k] == '\0')
		{
			--i;
			continue;
		}

		k=0;

		if(buffer[k] != ' ' && buffer[k] !='\t' && buffer[k] != '\0')
		{
			char lbuf[80] = {0};
			while(buffer[k] != ' ' && buffer[k] !='\t' && buffer[k] != '\0')
			{
				lbuf[k] = buffer[k];
				k++;
			}
			string lbl(lbuf);
			auto it = labels.find(lbl);
			if(it != labels.end())
			{
				zconsole_error("Unable to parse instruction %d. The error was: Duplicate Label. The duplicate label was: \"%s\"",
					i+1, lbuf);
				return false;
			}
			labels[lbl] = i;
			while(buffer[k] == ' ' || buffer[k] == '\t')
			{
				++k;
			}
			if(buffer[k] == '\0')
			{
				--i; //No command on this line
				continue;
			}
		}
	}

	scrptr = scriptstr.c_str(); //reset to start
	stop = false;

	zasm.clear();
	zasm.reserve(num_commands);

	for(int32_t i=0; i<num_commands && !stop; ++i)
	{
		buffer.clear();
		combuf[0]=0;
		for(char* buf : argbufs)
			buf[0] = 0;
		arr_vec.clear();
		arr_str.clear();
		has_vec = false;
		has_str = false;

		bool running = true;
		while(running)
		{
			char c = *scrptr++;

			if(!c) //null terminator
			{
				stop=true;
				--scrptr;
				break;
			}
			else
			{
				c = toupper(c);
				buffer += c;
				if (c == ';' || c == '\n' || c == '\r')
				{
					if (c != '\n')
					{
						while(true)
						{
							char c2 = *scrptr++;
							if(c2 == '\n') break;
							if(!c2) //null terminator
							{
								stop=true;
								break;
							}
						}
					}
					buffer.pop_back();
					break;
				}
				else if (c == '"' || c == '{')
				{
					bool vec = c == '{';
					auto j = buffer.size()-1;
					bool bslash = false;
					while(true)
					{
						char c2 = *scrptr++;
						bslash = c2 == '\\';
						if(!c2) //null terminator
						{
							stop=true;
							--scrptr;
							running = false;
							break;
						}
						buffer += vec ? toupper(c2) : c2;
						if(vec)
						{
							if(c2 == '}')
							{
								size_t sz = vecstr_size(buffer.c_str()+j);
								if(sz)
								{
									j += sz;
									break;
								}
							}
						}
						else
						{
							if(!bslash && c2 == '"')
							{
								size_t sz = escstr_size(buffer.c_str()+j);
								if(sz)
								{
									j += sz;
									break;
								}
							}
						}
					}
				}
			}
		}

		int32_t k=0, l=0;

		while(buffer[k] == ' ' || buffer[k] == '\t') k++;

		if(buffer[k] == '\0')
		{
			--i;
			continue;
		}

		k=0;

		while(buffer[k] != ' ' && buffer[k] != '\t' && buffer[k] != '\0') k++; //label

		while((buffer[k] == ' ' || buffer[k] == '\t') && buffer[k] != '\0')  k++; //ws

		while(buffer[k] != ' ' && buffer[k] != '\t' && buffer[k] != '\0') //command
		{
			combuf[l] = buffer[k];
			k++;
			l++;
		}

		if(l == 0) //No command
		{
			--i;
			continue;
		}

		combuf[l] = '\0';
		l=0;

		while((buffer[k] == ' ' || buffer[k] == '\t') && buffer[k] != '\0') k++; //ws

		bool bad_dstr = false;
		bool bad_dvec = false;
		for(int arg = 0; arg <= NUM_ZS_ARGS; ++arg)
		{
			if(buffer[k] == '"') //string
			{
				if(has_str) bad_dstr = true;
				else
				{
					arr_str = unescape_string(buffer.c_str()+k);
					has_str = true;
				}
				k += escstr_size(buffer.c_str()+k);
			}
			else if(buffer[k] == '{') //array
			{
				if(has_vec) bad_dvec = true;
				else
				{
					unstringify_vector(arr_vec, buffer.c_str()+k, true);
					has_vec = true;
				}
				k += vecstr_size(buffer.c_str()+k);
			}
			else
			{
				if(arg == NUM_ZS_ARGS)
					break;
				while(buffer[k] != ',' && buffer[k] != ' ' && buffer[k] != '\t' && buffer[k] != '\0')
					argbufs[arg][l++] = buffer[k++];
				argbufs[arg][l] = '\0';
				l = 0;
			}
			while((buffer[k] == ' ' || buffer[k] == '\t' || buffer[k] == ',') && buffer[k] != '\0') k++; //ws/comma
		}

		int32_t parse_err = 0;
		ffscript& zas = zasm.emplace_back();
		if(bad_dstr || bad_dvec ||
			!(parse_script_section(combuf, argbufs, zas, literals, parse_err, has_vec ? &arr_vec : nullptr, has_str ? &arr_str : nullptr)))
		{
			if(bad_dstr)
				parse_err = ERR_STR;
			if(bad_dvec)
				parse_err = ERR_VEC;
			std::string error = errstrbuf[parse_err];
			if (parse_err == ERR_LITERALS)
				error += " A script can have at most 65535.";
			zconsole_error("Unable to parse instruction %d. The error was: %s The command was (%s) (%s,%s,%s)%s%s",
				i+1, error.c_str(), combuf, arg1buf, arg2buf, arg3buf,
				has_str ? " (str)" : "", has_vec ? " (vec)" : "");
			stop=true;
			success=false;
			zasm.pop_back();
		}
	}

	return success;
}

// ---- upstream zalleg/packfile: the qst's encodings, into a byte list ------------------------

struct Out
{
	vector<uint8_t> b;

	void c(uint8_t v) { b.push_back(v); }
	void w(uint16_t v) { c(uint8_t(v)); c(uint8_t(v >> 8)); }
	void l(uint32_t v) { w(uint16_t(v)); w(uint16_t(v >> 16)); }
	// p_putcstr: at most 255 characters
	void cstr(string const& s)
	{
		uint8_t sz = uint8_t(s.size() < 255 ? s.size() : 255);
		c(sz);
		b.insert(b.end(), s.begin(), s.begin() + sz);
	}
	// p_putwstr: at most 65535 characters
	void wstr(string const& s)
	{
		uint16_t sz = uint16_t(s.size() < 65535 ? s.size() : 65535);
		w(sz);
		b.insert(b.end(), s.begin(), s.begin() + sz);
	}
	// p_putlstr: upstream casts the length through a word, so it keeps only its low 16 bits
	void lstr(string const& s)
	{
		uint32_t sz = uint16_t(s.size() < UINT32_MAX ? s.size() : UINT32_MAX);
		l(sz);
		b.insert(b.end(), s.begin(), s.begin() + sz);
	}
	void put(vector<uint8_t> const& v) { b.insert(b.end(), v.begin(), v.end()); }
	void str(string const& s) { l(uint32_t(s.size())); b.insert(b.end(), s.begin(), s.end()); }
};

void put_var(Out& o, int32_t v) { o.l(uint32_t(v)); }

// p_putvar(exported_variable)
void put_var(Out& o, exported_variable const& v)
{
	o.cstr(v.name);
	o.lstr(v.helptext);
	o.c(uint8_t(v.btn_type));
	o.w(uint16_t(v.engine_type));
	o.c(uint8_t(v.export_custom_type));
	// p_putwmap of zfix -> string
	uint16_t sz = uint16_t(v.custom_export_names.size() < 65535 ? v.custom_export_names.size() : 65535);
	o.w(sz);
	for(auto& [k, name] : v.custom_export_names)
	{
		if(!sz) break;
		o.l(uint32_t(k.getZLong()));
		o.lstr(name);
		--sz;
	}
	o.l(uint32_t(v.min.getZLong()));
	o.l(uint32_t(v.max.getZLong()));
}

// p_putbmap with a word size
template<typename T>
void put_bmap(Out& o, bounded_map<word, T> const& cont)
{
	word sz = cont.size();
	o.w(sz);
	if(!sz)
		return;
	T dt = cont.defval();
	auto lkey = cont.lastKey();
	word writecnt_v = lkey ? *lkey+1 : 0;
	word writecnt_m = 0;
	for(auto [k,v] : cont.inner())
		if(k < sz && v != dt)
			++writecnt_m;
	bool pairs = (writecnt_m * (sizeof(T)+sizeof(word))) <= writecnt_v * sizeof(T);
	o.c(pairs ? 1 : 0);
	o.w(pairs ? writecnt_m : writecnt_v);
	if(pairs)
	{
		for(auto [k,v] : cont.inner())
		{
			if(k >= sz || v == dt) continue;
			o.w(k);
			put_var(o, v);
		}
	}
	else
	{
		for(word q = 0; q < writecnt_v; ++q)
			put_var(o, cont.get(q));
	}
}

// zq_class.cpp write_one_ffscript_meta
void put_meta(Out& o, zasm_meta const& tmeta)
{
	o.w(tmeta.zasm_v);
	o.w(tmeta.meta_v);
	o.w(tmeta.ffscript_v);
	o.c(uint8_t((int)tmeta.script_type));
	for(int32_t q = 0; q < 8; ++q)
		o.cstr(tmeta.run_idens[q]);
	for(int32_t q = 0; q < 8; ++q)
		o.c(uint8_t(tmeta.run_types[q]));
	o.c(tmeta.flags);
	o.w(tmeta.compiler_v1);
	o.w(tmeta.compiler_v2);
	o.w(tmeta.compiler_v3);
	o.w(tmeta.compiler_v4);
	o.cstr(tmeta.script_name);
	o.cstr(tmeta.author);
	o.wstr(tmeta.script_info);
	o.wstr(tmeta.script_setup);
	o.w(NUM_ZMETA_ATTRIBUTES);
	for(auto q = 0; q < NUM_ZMETA_ATTRIBUTES; ++q)
	{
		o.cstr(tmeta.attributes[q]);
		o.wstr(tmeta.attributes_help[q]);
	}
	for(auto q = 0; q < 16; ++q)
	{
		o.cstr(tmeta.usrflags[q]);
		o.wstr(tmeta.usrflags_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		o.cstr(tmeta.initd_label[q]);
		o.wstr(tmeta.initd_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
		o.c(uint8_t(tmeta.initd_type[q]));
}

// zq_class.cpp write_quest_zasm
void put_zasm(Out& o, vector<ffscript> const& zasm, zasm_literals const& literals)
{
	o.l(uint32_t(zasm.size()));
	for(auto& zas : zasm)
	{
		if(zas.command == 0xFFFF)
			continue;
		o.w(zas.command);
		o.l(uint32_t(zas.arg1));
		o.l(uint32_t(zas.arg2));
		o.l(uint32_t(zas.arg3));
		auto strptr = literals.str(zas);
		auto vecptr = literals.vec(zas);
		uint32_t sz = strptr ? uint32_t(strptr->size()) : 0;
		o.l(sz);
		for(uint32_t q = 0; q < sz; ++q)
			o.c(uint8_t((*strptr)[q]));
		sz = vecptr ? uint32_t(vecptr->size()) : 0;
		o.l(sz);
		for(uint32_t q = 0; q < sz; ++q)
			o.l(uint32_t((*vecptr)[q]));
	}
}

} // namespace

// Writes the -assembled file after a successful compile. Returns false (with the error on the
// console) if the ZASM text doesn't read back or the file can't be written.
// Which scripts' instance variables are no longer where the quest's last compile had them
// (upstream ZQuest's plan_slot_remaps, src/zq/script_slot_remap.cpp: the values set for exported
// variables on placed scripts are stored by variable index and have to follow them). old_path
// holds that compile's debug data as the quest stores it; without one, or with one that doesn't
// decode (a quest from before the debug data), nothing is listed.
static void write_remaps(Out& o, ZScript::ZasmCompilerResult& result, std::string const& old_path)
{
	std::optional<DebugData> old_data;
	if(!old_path.empty())
	{
		if(FILE* f = fopen(old_path.c_str(), "rb"))
		{
			vector<byte> buf;
			byte chunk[65536];
			size_t got;
			while((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
				buf.insert(buf.end(), chunk, chunk + got);
			fclose(f);
			if(!buf.empty())
				old_data = DebugData::decode(buf);
		}
	}

	Out list;
	uint32_t count = 0;
	if(old_data)
	{
		for(auto& [name, data] : result.theScripts)
		{
			auto remap = build_inst_var_remap(*old_data, result.debugData, name);
			if(!remap || remap->is_identity())
				continue;
			++count;
			list.str(name);
			list.l(uint32_t(remap->old_vars.size()));
			for(auto& [id, var] : remap->old_vars)
			{
				list.w(id);
				list.c(uint8_t(var.kind));
			}
			list.l(uint32_t(remap->old_to_new.size()));
			for(auto [old_id, new_id] : remap->old_to_new)
			{
				list.w(old_id);
				list.w(new_id);
				list.c(uint8_t(remap->new_vars.at(new_id).kind));
			}
			list.l(uint32_t(remap->initd_moves.size()));
			for(auto& move : remap->initd_moves)
			{
				list.l(uint32_t(move.initd));
				list.w(move.new_id);
			}
		}
	}
	o.l(count);
	o.put(list.b);
}

bool zs30_write_assembled(ZScript::ZasmCompilerResult& result, std::string const& path, std::string const& old_debug_path)
{
	string zasm_str;
	write_script(result, zasm_str, false, false);

	vector<ffscript> zasm;
	zasm_literals literals;
	if(!parse_script_string(zasm, literals, zasm_str))
		return false;

	Out o;
	o.b.insert(o.b.end(), {'Z', 'S', '3', '0', 'A', 'S', 'M', 0});
	o.l(2);
	o.l(V_FFSCRIPT);
	o.l(METADATA_V);
	o.l(ZASM_VERSION);

	o.l(uint32_t(result.scriptTypes.size()));
	for(auto& [name, id] : result.scriptTypes)
	{
		o.str(name);
		o.l(uint32_t(int32_t(id)));
	}

	o.l(uint32_t(result.theScripts.size()));
	for(auto& [name, data] : result.theScripts)
	{
		o.str(name);
		o.c(data.format);
		Out rec;
		// write_one_ffscript writes a script that isn't valid() (script_data::valid: no end pc,
		// or a stream that is empty or starts with the end marker) as an empty slot: a single 0.
		// An ~Init with nothing to set up, first in the stream, is the usual one (pc 0 to 0).
		if(!data.end_pc || zasm.empty() || zasm[0].command == 0xFFFF)
			rec.c(0);
		else
		{
			rec.c(1);
			put_meta(rec, data.meta);
			rec.l(data.pc);
			rec.l(data.end_pc);
			put_bmap(rec, data.script_d_init);
			put_bmap(rec, data.script_d_exports);
		}
		o.l(uint32_t(rec.b.size()));
		o.put(rec.b);
	}

	Out z;
	put_zasm(z, zasm, literals);
	o.l(uint32_t(z.b.size()));
	o.put(z.b);

	vector<byte> debug = result.debugData.encode();
	o.l(uint32_t(debug.size()));
	o.b.insert(o.b.end(), debug.begin(), debug.end());

	write_remaps(o, result, old_debug_path);

	FILE* f = fopen(path.c_str(), "wb");
	if(!f)
	{
		zconsole_error("Can't write '%s'", path.c_str());
		return false;
	}
	bool ok = fwrite(o.b.data(), 1, o.b.size(), f) == o.b.size();
	ok = fclose(f) == 0 && ok;
	if(!ok)
	{
		zconsole_error("Can't write '%s'", path.c_str());
		remove(path.c_str());
	}
	return ok;
}
