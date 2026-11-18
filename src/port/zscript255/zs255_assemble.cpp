// zscript255: port of upstream 2.55.17's ZASM assembler (zq/ffasm.cpp parse_script_string,
// parse_script_section, set_argument, ffparse, ffcheck) and its script record writer
// (zq/zq_class.cpp write_one_ffscript), without Allegro or the editor. See zs255_assemble.h.
//
// Kept as upstream wrote it, line handling and quirks included, so the records come out as
// 2.55.17's ZQuest stores them. What differs: messages are returned instead of shown, the
// label table starts empty for each script (ZQuest's only holds labels from ZASM files
// imported earlier in the session, and compiled text has none), and the name lookups use
// maps built from the same tables in the same order (first match wins, as upstream's
// linear searches), so they give the same numbers faster.
#include "zs255_assemble.h"
#include "zs255_scrdata.h"
#include "parser/Compiler.h"
#include "parser/Types.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

using std::string;

namespace zs255_asm
{
namespace
{
	// toupper as the C locale has it (2.55.17's ZQuest never changes the locale).
	inline char ascii_upper(char c)
	{
		return (c >= 'a' && c <= 'z') ? char(c - ('a' - 'A')) : c;
	}
	string upper_copy(char const* s)
	{
		string r(s);
		for(char& c : r)
			c = ascii_upper(c);
		return r;
	}

	// command_list by exact name: the first index with that name, as the strcmp loop in
	// parse_script_section finds it.
	std::unordered_map<string, int32_t> const& command_index()
	{
		static std::unordered_map<string, int32_t> map = []
		{
			std::unordered_map<string, int32_t> m;
			for(int32_t i = 0; i < num_commands; ++i)
				m.emplace(command_list[i].name, i);
			return m;
		}();
		return map;
	}

	// variable_list by name without case: every name set_argument tries ("D0".."D7", "A1",
	// "A2", "GD0".. and the plain names), first match kept.
	std::unordered_map<string, int32_t> const& variable_index()
	{
		static std::unordered_map<string, int32_t> map = []
		{
			std::unordered_map<string, int32_t> m;
			char tempvar[80];
			for(int32_t i = 0; variable_list[i].id > -1; ++i)
			{
				if(variable_list[i].maxcount > 1)
				{
					for(int32_t j = 0; j < variable_list[i].maxcount; ++j)
					{
						if(strcmp(variable_list[i].name, "A") == 0)
							snprintf(tempvar, sizeof(tempvar), "%s%d", variable_list[i].name, j+1);
						else snprintf(tempvar, sizeof(tempvar), "%s%d", variable_list[i].name, j);
						int32_t mult = variable_list[i].multiple > 1 ? variable_list[i].multiple : 1;
						m.emplace(upper_copy(tempvar), variable_list[i].id + (j * mult));
					}
				}
				else m.emplace(upper_copy(variable_list[i].name), variable_list[i].id);
			}
			return m;
		}();
		return map;
	}

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
		std::string tempstring1(string);
		tempstring1 += "0000";

		size_t dot = tempstring1.find('.');
		tempstring1[dot] = 0;
		ret=atoi(tempstring1.c_str())*10000;

		// the four digits after the point
		std::string frac = tempstring1.substr(dot + 1, 4);

		if(negcheck<0)
			ret-=atoi(frac.c_str());
		else ret+=atoi(frac.c_str());

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

	bool set_argument(char const* argbuf, int32_t& arg)
	{
		auto const& vars = variable_index();
		auto it = vars.find(upper_copy(argbuf));
		if(it == vars.end())
			return false;
		arg = it->second;
		return true;
	}

	// ustrnicmp(a, b, n) == 0 for the ASCII names used here
	bool prefix_nocase(char const* a, char const* b, size_t n)
	{
		for(size_t q = 0; q < n; ++q)
		{
			char ca = ascii_upper(a[q]), cb = ascii_upper(b[q]);
			if(ca != cb)
				return false;
			if(!ca)
				return true;
		}
		return true;
	}

#define ERR_INSTRUCTION 0
#define ERR_PARAM1 1
#define ERR_PARAM2 2
#define ERR_STR    3
#define ERR_VEC    4

	int32_t parse_script_section(char const* combuf, char const* arg1buf, char const* arg2buf,
		script_record& script, int32_t com, int32_t &retcode,
		std::vector<int32_t> *vptr, std::string *sptr,
		std::map<std::string, int32_t> const& labels)
	{
		auto& zas = script.zasm[com];
		zas.arg1 = 0;
		zas.arg2 = 0;
		zas.vec.reset();
		zas.str.reset();
		if(vptr)
		{
			zas.vec.reset(new std::vector<int32_t>(*vptr));
		}
		if(sptr)
		{
			zas.str.reset(new std::string(*sptr));
		}

		auto const& commands = command_index();
		auto found = commands.find(combuf);
		if(found == commands.end())
		{
			retcode=ERR_INSTRUCTION;
			return 0;
		}
		int32_t i = found->second;
		zas.command = word(i);

		if((prefix_nocase(combuf,"GOTO",4)||prefix_nocase(combuf,"LOOP",4)) && !(upper_copy(combuf) == "GOTOR"))
		{
			string lbl(arg1buf);
			auto it = labels.find(lbl);
			if(it != labels.end())
			{
				zas.arg1 = (*it).second;
			}
			else
			{
				zas.arg1 = atoi(arg1buf)-1;
			}

			if(prefix_nocase(combuf,"LOOP",4))
			{
				if(command_list[i].arg2_type==1)  //this should NEVER happen with a loop, as arg2 needs to be a variable
				{
					if(!ffcheck(arg2buf))
					{
						retcode=ERR_PARAM2;
						return 0;
					}

					zas.arg2 = ffparse(arg2buf);
				}
				else
				{
					if(!set_argument(arg2buf, zas.arg2))
					{
						retcode=ERR_PARAM2;
						return 0;
					}
				}
			}
		}
		else
		{
			if(command_list[i].args>0)
			{
				if(command_list[i].arg1_type==1)
				{
					if(!ffcheck(arg1buf))
					{
						retcode=ERR_PARAM1;
						return 0;
					}

					zas.arg1 = ffparse(arg1buf);
				}
				else
				{
					if(!set_argument(arg1buf, zas.arg1))
					{
						retcode=ERR_PARAM1;
						return 0;
					}
				}

				if(command_list[i].args>1)
				{
					if(command_list[i].arg2_type==1)
					{
						if(!ffcheck(arg2buf))
						{
							retcode=ERR_PARAM2;
							return 0;
						}

						zas.arg2 = ffparse(arg2buf);
					}
					else
					{
						if(!set_argument(arg2buf, zas.arg2))
						{
							retcode=ERR_PARAM2;
							return 0;
						}
					}
				}
			}
			byte b = command_list[i].arr_type;
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

		return 1;
	}

	void null_script(script_record& script, size_t newSize)
	{
		if (newSize < 1)
			newSize = 1;
		script.zasm.clear();
		script.zasm.resize(newSize);
		script.zasm[0].clear();
		script.meta.zero();
		script.size = newSize;
	}
	void disable(script_record& script)
	{
		if(script.zasm.size())
		{
			script.zasm[0].clear();
			script.size = 1;
		}
	}
	void recalc_size(script_record& script)
	{
		for(size_t q = 0; q < script.zasm.size(); ++q)
		{
			if(script.zasm[q].command == 0xFFFF)
			{
				script.size = q+1;
				return;
			}
		}
		// upstream reads on past the end here; every assembled script ends in 0xFFFF
		script.size = script.zasm.size();
	}
}

bool assemble(std::string const& scriptstr, script_record& rec, std::string& error)
{
	std::map<std::string, int32_t> labels;
	std::string buffer;
	// SUBBUFSZ (0x200) buffers, as upstream; compiled ZASM never comes near
	char combuf[0x200] = {0};
	char arg1buf[0x200] = {0};
	char arg2buf[0x200] = {0};
	std::vector<int32_t> arr_vec;
	std::string arr_str;
	bool has_vec = false;
	bool has_str = false;
	bool stop=false;
	bool success=true;
	bool meta_done=false;
	int32_t num_commands;
	char const* scrptr = scriptstr.c_str();
	error.clear();

	// first pass: count the commands and note the labels
	for(int32_t i=0;; i++)
	{
		buffer.clear();

		if(stop)
		{
			num_commands=i+1;
			break;
		}

		bool meta = false;
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
				if(buffer[0] == '#' && !meta_done) //Metadata line
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
					--i;
					meta = true;
					break;
				}
				else if(c == ';' || c == '\n' || c == 13)
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
						buffer += vec ? ascii_upper(c2) : c2;
						if(vec)
						{
							if(c2 == '}')
							{
								size_t sz = util::vecstr_size(buffer.c_str()+j);
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
								size_t sz = util::escstr_size(buffer.c_str()+j);
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
		if(meta) continue;
		else meta_done = true;
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
			std::string lbl;
			while(buffer[k] != ' ' && buffer[k] !='\t' && buffer[k] != '\0')
			{
				lbl += buffer[k];
				k++;
			}
			auto it = labels.find(lbl);
			if(it != labels.end())
			{
				char buf[512];
				snprintf(buf, sizeof(buf), "Unable to parse instruction %d from script %s"
					"\nThe error was: Duplicate Label"
					"\nThe duplicate label was: \"%s\"", i+1, rec.meta.script_name.c_str(), lbl.c_str());
				error = buf;
				disable(rec);
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
	meta_done = false;

	null_script(rec, num_commands);

	for(int32_t i=0; i<num_commands; ++i)
	{
		if(stop)
		{
			rec.zasm[i].clear();
			break;
		}
		else
		{
			buffer.clear();
			combuf[0]=0;
			arg1buf[0]=0;
			arg2buf[0]=0;
			bool meta_mode = false;
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
					c = ascii_upper(c);
					buffer += c;
					if (buffer[0] == '#' && !meta_done) //Metadata line
					{
						meta_mode = true;
						char temp;
						bool buffering = true;
						while ((temp = (*scrptr++)) != '\n')
						{
							if (!temp) //null terminator
							{
								stop = true;
								break;
							}
							if (temp == '\r')
								buffering = false;
							if(buffering)
								buffer += temp;
						}
						break;
					}
					else if (c == ';' || c == '\n' || c == '\r')
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
							buffer += vec ? ascii_upper(c2) : c2;
							if(vec)
							{
								if(c2 == '}')
								{
									size_t sz = util::vecstr_size(buffer.c_str()+j);
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
									size_t sz = util::escstr_size(buffer.c_str()+j);
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

			if(meta_mode)
			{
				rec.meta.parse_meta(buffer.c_str());
				--i; continue;
			}
			meta_done = true;

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

			if(buffer[k] == '"') //string
			{
				arr_str = util::unescape_string(buffer.c_str()+k);
				has_str = true;
				k += util::escstr_size(buffer.c_str()+k);
			}
			else if(buffer[k] == '{') //array
			{
				util::unstringify_vector(arr_vec, buffer.c_str()+k, true);
				has_vec = true;
				k += util::vecstr_size(buffer.c_str()+k);
			}
			else while(buffer[k] != ',' && buffer[k] != ' ' && buffer[k] != '\t' && buffer[k] != '\0') //arg1
			{
				arg1buf[l] = buffer[k];
				k++;
				l++;
			}

			arg1buf[l] = '\0';
			l=0;

			while((buffer[k] == ' ' || buffer[k] == '\t' || buffer[k] == ',') && buffer[k] != '\0') k++; //ws/comma

			bool bad_dstr = false;
			bool bad_dvec = false;
			if(buffer[k] == '"') //string
			{
				if(has_str) bad_dstr = true;
				else
				{
					arr_str = util::unescape_string(buffer.c_str()+k);
					has_str = true;
				}
				k += util::escstr_size(buffer.c_str()+k);
			}
			else if(buffer[k] == '{') //array
			{
				if(has_vec) bad_dvec = true;
				else
				{
					util::unstringify_vector(arr_vec, buffer.c_str()+k, true);
					has_vec = true;
				}
				k += util::vecstr_size(buffer.c_str()+k);
			}
			else while(buffer[k] != ' ' && buffer[k] != '\t' && buffer[k] != '\0') //arg2
			{
				arg2buf[l] = buffer[k];
				k++;
				l++;
			}

			arg2buf[l] = '\0';

			while((buffer[k] == ' ' || buffer[k] == '\t' || buffer[k] == ',') && buffer[k] != '\0') k++; //ws/comma

			if(buffer[k] == '"') //string
			{
				if(has_str) bad_dstr = true;
				else
				{
					arr_str = util::unescape_string(buffer.c_str()+k);
					has_str = true;
				}
				k += util::escstr_size(buffer.c_str()+k);
			}
			else if(buffer[k] == '{') //array
			{
				if(has_vec) bad_dvec = true;
				else
				{
					util::unstringify_vector(arr_vec, buffer.c_str()+k, true);
					has_vec = true;
				}
				k += util::vecstr_size(buffer.c_str()+k);
			}

			// upstream looks for a string or list once more here
			while((buffer[k] == ' ' || buffer[k] == '\t' || buffer[k] == ',') && buffer[k] != '\0') k++; //ws/comma

			if(buffer[k] == '"') //string
			{
				if(has_str) bad_dstr = true;
				else
				{
					arr_str = util::unescape_string(buffer.c_str()+k);
					has_str = true;
				}
				k += util::escstr_size(buffer.c_str()+k);
			}
			else if(buffer[k] == '{') //array
			{
				if(has_vec) bad_dvec = true;
				else
				{
					util::unstringify_vector(arr_vec, buffer.c_str()+k, true);
					has_vec = true;
				}
				k += util::vecstr_size(buffer.c_str()+k);
			}

			int32_t parse_err = 0;
			if(bad_dstr || bad_dvec ||
				!(parse_script_section(combuf, arg1buf, arg2buf, rec, i, parse_err,
					has_vec ? &arr_vec : nullptr, has_str ? &arr_str : nullptr, labels)))
			{
				if(bad_dstr) parse_err = 3;
				if(bad_dvec) parse_err = 4;
				const char* errstrbuf[] =
				{
					"invalid instruction!",
					"parameter 1 invalid!",
					"parameter 2 invalid!",
					"string parameter invalid!",
					"vector parameter invalid!"
				};
				char vstrbuf[64] = {0};
				if(has_str || has_vec)
					snprintf(vstrbuf, sizeof(vstrbuf), " (%s%s%s)",has_str ? "str" : "", has_str&&has_vec ? "," : "", has_vec ? "vec" : "");
				char buf[2048];
				snprintf(buf, sizeof(buf), "Unable to parse instruction %d from script %s"
					"\nThe error was: %s"
					"\nThe command was (%s) (%s,%s)%s"
					,i+1,rec.meta.script_name.c_str()
					,errstrbuf[parse_err]
					,combuf,arg1buf,arg2buf,vstrbuf);
				if(error.empty())
					error = buf;
				stop=true;
				success=false;
				rec.zasm[i].str.reset();
				rec.zasm[i].vec.reset();
				disable(rec);
			}
		}
	}

	recalc_size(rec);
	return success;
}

namespace
{
	void put_b(std::vector<uint8_t>& out, uint8_t v)
	{
		out.push_back(v);
	}
	void put_w(std::vector<uint8_t>& out, word v)
	{
		out.push_back(uint8_t(v & 0xFF));
		out.push_back(uint8_t(v >> 8));
	}
	void put_l(std::vector<uint8_t>& out, uint32_t v)
	{
		for(int q = 0; q < 4; ++q)
			out.push_back(uint8_t((v >> (8*q)) & 0xFF));
	}
	// packfile.h p_putcstr / p_putwstr
	void put_cstr(std::vector<uint8_t>& out, std::string const& str)
	{
		size_t sz = str.size() < 255 ? str.size() : 255;
		put_b(out, uint8_t(sz));
		out.insert(out.end(), str.begin(), str.begin() + sz);
	}
	void put_wstr(std::vector<uint8_t>& out, std::string const& str)
	{
		size_t sz = str.size() < 65535 ? str.size() : 65535;
		put_w(out, word(sz));
		out.insert(out.end(), str.begin(), str.begin() + sz);
	}
}

void write_record(script_record const& rec, std::vector<uint8_t>& out)
{
	size_t num_commands = rec.size;
	put_l(out, uint32_t(num_commands));

	//Metadata
	zasm_meta const& tmeta = rec.meta;
	put_w(out, tmeta.zasm_v);
	put_w(out, tmeta.meta_v);
	put_w(out, tmeta.ffscript_v);
	put_b(out, uint8_t(tmeta.script_type));
	for(int32_t q = 0; q < 8; ++q)
		put_cstr(out, tmeta.run_idens[q]);
	for(int32_t q = 0; q < 8; ++q)
		put_b(out, tmeta.run_types[q]);
	put_b(out, tmeta.flags);
	put_w(out, tmeta.compiler_v1);
	put_w(out, tmeta.compiler_v2);
	put_w(out, tmeta.compiler_v3);
	put_w(out, tmeta.compiler_v4);
	put_cstr(out, tmeta.script_name);
	put_cstr(out, tmeta.author);
	for(auto q = 0; q < 10; ++q)
	{
		put_cstr(out, tmeta.attributes[q]);
		put_wstr(out, tmeta.attributes_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		put_cstr(out, tmeta.attribytes[q]);
		put_wstr(out, tmeta.attribytes_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		put_cstr(out, tmeta.attrishorts[q]);
		put_wstr(out, tmeta.attrishorts_help[q]);
	}
	for(auto q = 0; q < 16; ++q)
	{
		put_cstr(out, tmeta.usrflags[q]);
		put_wstr(out, tmeta.usrflags_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		put_cstr(out, tmeta.initd[q]);
		put_wstr(out, tmeta.initd_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
		put_b(out, uint8_t(tmeta.initd_type[q]));

	for(size_t j=0; j<num_commands && j < rec.zasm.size(); j++)
	{
		auto& zas = rec.zasm[j];
		put_w(out, zas.command);

		if(zas.command==0xFFFF)
		{
			break;
		}
		else
		{
			put_l(out, uint32_t(zas.arg1));
			put_l(out, uint32_t(zas.arg2));

			uint32_t sz = 0;
			if(zas.str)
				sz = uint32_t(zas.str->size());
			put_l(out, sz);
			if(sz)
				out.insert(out.end(), zas.str->begin(), zas.str->end());
			sz = 0;
			if(zas.vec)
				sz = uint32_t(zas.vec->size());
			put_l(out, sz);
			if(sz) //vector found
			{
				for(int32_t v : *zas.vec)
					put_l(out, uint32_t(v));
			}
		}
	}
}

bool assemble_compiled(ZScript::disassembled_script_data const& data, script_record& rec, std::string& error)
{
	// zquest.cpp handle_slot: the compiled script written out as text (metadata lines, then
	// " COMMAND args" lines), then assembled.
	std::string scriptstr;
	data.write(scriptstr, false);
	return assemble(scriptstr, rec, error);
}
}

bool zs255_write_assembled(std::string const& path,
	std::map<std::string, ZScript::ParserScriptType>& stypes,
	std::map<std::string, ZScript::disassembled_script_data>& scripts,
	std::string& error)
{
	std::vector<uint8_t> out;
	out.insert(out.end(), {'Z','S','2','5','5','A','S','M'});
	auto put_l = [&out](uint32_t v)
	{
		for(int q = 0; q < 4; ++q)
			out.push_back(uint8_t((v >> (8*q)) & 0xFF));
	};
	put_l(1);
	put_l(V_FFSCRIPT);
	put_l(METADATA_V);
	put_l(uint32_t(scripts.size()));
	for(auto& p : scripts)
	{
		zs255_asm::script_record rec;
		std::string err;
		if(!zs255_asm::assemble_compiled(p.second, rec, err))
		{
			error = "Can't assemble script '" + p.first + "': " + err;
			return false;
		}
		std::vector<uint8_t> bytes;
		zs255_asm::write_record(rec, bytes);
		put_l(uint32_t(p.first.size()));
		out.insert(out.end(), p.first.begin(), p.first.end());
		auto t = stypes.find(p.first);
		put_l(uint32_t(t == stypes.end() ? -1 : int32_t(t->second.getId())));
		put_l(uint32_t(bytes.size()));
		out.insert(out.end(), bytes.begin(), bytes.end());
	}
	FILE* f = fopen(path.c_str(), "wb");
	if(!f)
	{
		error = "Can't write '" + path + "'";
		return false;
	}
	bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
	if(fclose(f))
		ok = false;
	if(!ok)
		error = "Can't write '" + path + "'";
	return ok;
}
