// zscript255: upstream 2.55.17's zscrdata.h, the parts the compiler program needs: the
// compile result file (write_compile_data, write_meta) and the metadata text the ZASM
// listing starts each script with (zasm_meta::get_meta).
#include "zs255_scrdata.h"
#include "parser/Types.h"
#include <cstdio>
#include <sstream>

using std::string;
using std::map;

namespace
{
	// Upstream writes size_t and the enum with fwrite; on 64-bit Windows that is 8 and
	// 4 bytes, little-endian. Written byte by byte here so every platform makes the
	// same file.
	void put_u64(uint64_t v, FILE* f)
	{
		for(int q = 0; q < 8; ++q)
			fputc(int((v >> (8*q)) & 0xFF), f);
	}
	void put_i32(int32_t v, FILE* f)
	{
		uint32_t u = uint32_t(v);
		for(int q = 0; q < 4; ++q)
			fputc(int((u >> (8*q)) & 0xFF), f);
	}
	void write_str(std::string const& str, FILE* f)
	{
		put_u64(str.size(), f);
		fwrite(str.data(), 1, str.size(), f);
	}
	void write_w(word val, FILE* f)
	{
		fputc(val & 0xFF, f);
		fputc((val >> 8) & 0xFF, f);
	}
	void write_b(byte val, FILE* f)
	{
		fputc(val, f);
	}

	void write_meta(zasm_meta const& meta, FILE* f)
	{
		write_w(meta.zasm_v, f);
		write_w(meta.meta_v, f);
		write_w(meta.ffscript_v, f);
		write_b((byte)meta.script_type, f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.run_idens[q], f);
		for(auto q = 0; q < 8; ++q)
			write_b(meta.run_types[q], f);
		write_b(meta.flags, f);
		write_w(meta.compiler_v1, f);
		write_w(meta.compiler_v2, f);
		write_w(meta.compiler_v3, f);
		write_w(meta.compiler_v4, f);
		write_str(meta.script_name, f);
		write_str(meta.author, f);
		for(auto q = 0; q < 10; ++q)
			write_str(meta.attributes[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.attribytes[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.attrishorts[q], f);
		for(auto q = 0; q < 16; ++q)
			write_str(meta.usrflags[q], f);
		for(auto q = 0; q < 10; ++q)
			write_str(meta.attributes_help[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.attribytes_help[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.attrishorts_help[q], f);
		for(auto q = 0; q < 16; ++q)
			write_str(meta.usrflags_help[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.initd[q], f);
		for(auto q = 0; q < 8; ++q)
			write_str(meta.initd_help[q], f);
		for(auto q = 0; q < 8; ++q)
			write_b(meta.initd_type[q], f);
	}
}

bool zs255_write_compile_data(std::string const& path,
	map<string, ZScript::ParserScriptType>& stypes,
	map<string, ZScript::disassembled_script_data>& scripts)
{
	FILE *tempfile = fopen(path.c_str(), "wb");
	if(!tempfile)
		return false;
	
	put_u64(stypes.size(), tempfile);
	for(auto it = stypes.begin(); it != stypes.end(); ++it)
	{
		write_str(it->first, tempfile);
		put_i32(int32_t(it->second.getId()), tempfile);
	}
	
	put_u64(scripts.size(), tempfile);
	for(auto it = scripts.begin(); it != scripts.end(); ++it)
	{
		ZScript::disassembled_script_data& v = it->second;
		write_str(it->first, tempfile);
		
		write_meta(v.first, tempfile);
		
		write_b(v.format, tempfile);
		
		put_u64(v.second.size(), tempfile);
		for(auto it2 = v.second.begin(); it2 != v.second.end(); ++it2)
		{
			write_str((*it2)->toString(), tempfile);
			put_i32((*it2)->getLabel(), tempfile);
		}
	}
	
	bool ok = !ferror(tempfile);
	if(fclose(tempfile))
		ok = false;
	return ok;
}

string get_script_name(ScriptType type)
{
	switch(type)
	{
		case ScriptType::Global:
			return "GLOBAL";
		case ScriptType::FFC:
			return "FFC";
		case ScriptType::Screen:
			return "SCREEN";
		case ScriptType::Player:
			return "HERO";
		case ScriptType::Item:
			return "ITEMDATA";
		case ScriptType::Lwpn:
			return "LWEAPON";
		case ScriptType::NPC:
			return "NPC";
		case ScriptType::Ewpn:
			return "EWEAPON";
		case ScriptType::DMap:
			return "DMAP";
		case ScriptType::ItemSprite:
			return "ITEMSPRITE";
		case ScriptType::Combo:
			return "COMBODATA";
		case ScriptType::Generic: case ScriptType::GenericFrozen:
			return "GENERIC";
		case ScriptType::EngineSubscreen:
			return "SUBSCREEN";
		case ScriptType::None:
		default:
			return "UNKNOWN";
	}
}

//Output metadata as a single string
string zasm_meta::get_meta() const
{
	std::ostringstream oss;
	oss << "#ZASM_VERSION = " << zasm_v
		<< "\n#METADATA_VERSION = " << meta_v
		<< "\n#FFSCRIPT_VERSION = " << ffscript_v
		<< "\n#SCRIPT_NAME = " << script_name;
	if(author.size())
		oss << "\n#AUTHOR = " << author;
	oss << "\n#SCRIPT_TYPE = " << get_script_name(script_type).c_str()
		<< "\n#AUTO_GEN = " << ((flags & ZMETA_AUTOGEN) ? "TRUE" : "FALSE")
		<< "\n#COMPILER_V1 = " << compiler_v1
		<< "\n#COMPILER_V2 = " << compiler_v2
		<< "\n#COMPILER_V3 = " << compiler_v3
		<< "\n#COMPILER_V4 = " << compiler_v4;
	for(auto q = 0; q < 8; ++q)
	{
		if(!run_idens[q].size())
			continue;
		oss << "\n#PARAM_TYPE_" << q << " = " << ZScript::getDataTypeName(run_types[q])
			<< "\n#PARAM_NAME_" << q << " = " << run_idens[q];
	}
	for(auto q = 0; q < 10; ++q)
	{
		if(attributes[q].size())
			oss << "\n#ATTRIBUTE_" << q << " = " << attributes[q];
		if(attributes_help[q].size())
			oss << "\n#ATTRIBUTE_HELP_" << q << " = "
				<< util::escape_characters(attributes_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		if(attribytes[q].size())
			oss << "\n#ATTRIBYTE_" << q << " = " << attribytes[q];
		if(attribytes_help[q].size())
			oss << "\n#ATTRIBYTE_HELP_" << q << " = "
				<< util::escape_characters(attribytes_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		if(attrishorts[q].size())
			oss << "\n#ATTRISHORT_" << q << " = " << attrishorts[q];
		if(attrishorts_help[q].size())
			oss << "\n#ATTRISHORT_HELP_" << q << " = "
				<< util::escape_characters(attrishorts_help[q]);
	}
	for(auto q = 0; q < 16; ++q)
	{
		if(usrflags[q].size())
			oss << "\n#FLAG_" << q << " = " << usrflags[q];
		if(usrflags_help[q].size())
			oss << "\n#FLAG_HELP_" << q << " = "
				<< util::escape_characters(usrflags_help[q]);
	}
	for(auto q = 0; q < 8; ++q)
	{
		if(initd[q].size())
			oss << "\n#INITD_" << q << " = " << initd[q];
		if(initd_help[q].size())
			oss << "\n#INITD_HELP_" << q << " = "
				<< util::escape_characters(initd_help[q]);
		if(initd_type[q] > -1)
			oss << "\n#INITD_TYPE_" << q << " = "
				<< std::to_string(int32_t(initd_type[q]));
	}
	oss << "\n";
	return oss.str();
}

// Upstream's get_script_type: the script type a "#SCRIPT_TYPE" name reads back as. Generic
// and subscreen scripts are not in its list, so they read back as None, as in 2.55.17.
ScriptType get_script_type(string const& name)
{
	if(name=="GLOBAL")
		return ScriptType::Global;
	else if(name=="FFC")
		return ScriptType::FFC;
	else if(name=="SCREEN")
		return ScriptType::Screen;
	else if(name=="HERO" || name=="PLAYER" || name=="LINK")
		return ScriptType::Player;
	else if(name=="ITEMDATA" || name=="ITEM")
		return ScriptType::Item;
	else if(name=="LWEAPON" || name=="LWPN")
		return ScriptType::Lwpn;
	else if(name=="NPC")
		return ScriptType::NPC;
	else if(name=="EWEAPON" || name=="EWPN")
		return ScriptType::Ewpn;
	else if(name=="DMAP")
		return ScriptType::DMap;
	else if(name=="ITEMSPRITE")
		return ScriptType::ItemSprite;
	else if(name=="COMBO" || name=="COMBODATA")
		return ScriptType::Combo;
	
	return ScriptType::None;
}


// Upstream's zasm_meta::parse_meta, as 2.55.17's ZQuest reads a compiled script's metadata
// lines back when it assembles the script into a slot (including its quirks: the
// PARAM_ lines count from 1 while the listing writes them from 0).

//Parse a single line of metadata
bool zasm_meta::parse_meta(const char *buffer)
{
	string line(buffer);
	size_t space_pos = line.find_first_of(" \t=");
	if(space_pos == string::npos) return false;
	string cmd = line.substr(0, space_pos); //The command portion
	size_t end_space_pos = line.find_first_not_of(" \t=", space_pos);
	if(end_space_pos == string::npos) return false;
	string val = line.substr(end_space_pos); //The value portion
	size_t endpos = val.find_last_not_of(" \t\r\n\0");
	if(endpos != string::npos) ++endpos;
	val = val.substr(0, endpos); //trim trailing whitespace
	
	if(cmd == "#ZASM_VERSION")
	{
		zasm_v = atoi(val.c_str());
	}
	else if(cmd == "#METADATA_VERSION")
	{
		meta_v = atoi(val.c_str());
	}
	else if(cmd == "#FFSCRIPT_VERSION")
	{
		ffscript_v = atoi(val.c_str());
	}
	else if(cmd == "#SCRIPT_TYPE" || cmd == "#TYPE")
	{
		util::upperstr(val);
		script_type = get_script_type(val);
	}
	else if(cmd == "#SCRIPT_NAME")
	{
		util::replchar(val, ' ', '_');
		script_name = val;
	}
	else if(cmd == "#AUTHOR")
	{
		author = val;
	}
	else if(cmd == "#AUTO_GEN")
	{
		util::upperstr(val);
		if(val=="TRUE")
			flags |= ZMETA_AUTOGEN;
		else if(val=="FALSE")
			flags &= ~ZMETA_AUTOGEN;
		else
		{
			if(atoi(val.c_str())!=0)
			{
				flags |= ZMETA_AUTOGEN;
			}
			else flags &= ~ZMETA_AUTOGEN;
		}
	}
	else if(cmd == "#COMPILER_V1")
	{
		compiler_v1 = atoi(val.c_str());
	}
	else if(cmd == "#COMPILER_V2")
	{
		compiler_v2 = atoi(val.c_str());
	}
	else if(cmd == "#COMPILER_V3")
	{
		compiler_v3 = atoi(val.c_str());
	}
	else if(cmd == "#COMPILER_V4")
	{
		compiler_v4 = atoi(val.c_str());
	}
	else if(cmd.size() == 13 && !cmd.compare(0,12,"#PARAM_NAME_"))
	{
		byte ind = cmd.at(12) - '1';
		if (ind < 8)
		{
			util::replchar(val, ' ', '_');
			run_idens[ind] = val;
		}
		else return false;
	}
	else if(cmd.size() == 13 && !cmd.compare(0,12,"#PARAM_TYPE_"))
	{
		byte ind = cmd.at(12) - '1';
		if (ind < 8)
		{
			util::replchar(val, ' ', '_');
			run_types[ind] = ZScript::getTypeId(val);
		}
		else return false;
	}
	else if (cmd.size() == 12 && !cmd.compare(0, 11, "#ATTRIBUTE_"))
	{
		byte ind = cmd.at(11) - '0';
		if (ind < 10)
		{
			attributes[ind] = val;
		}
		else return false;
	}
	else if (cmd.size() == 17 && !cmd.compare(0, 16, "#ATTRIBUTE_HELP_"))
	{
		byte ind = cmd.at(16) - '0';
		if (ind < 10)
		{
			attributes_help[ind] = util::unescape_characters(val);
		}
		else return false;
	}
	else if (cmd.size() == 12 && !cmd.compare(0, 11, "#ATTRIBYTE_"))
	{
		byte ind = cmd.at(11) - '0';
		if (ind < 8)
		{
			attribytes[ind] = val;
		}
		else return false;
	}
	else if (cmd.size() == 17 && !cmd.compare(0, 16, "#ATTRIBYTE_HELP_"))
	{
		byte ind = cmd.at(16) - '0';
		if (ind < 8)
		{
			attribytes_help[ind] = util::unescape_characters(val);
		}
		else return false;
	}
	else if (cmd.size() == 13 && !cmd.compare(0, 12, "#ATTRISHORT_"))
	{
		byte ind = cmd.at(12) - '0';
		if (ind < 8)
		{
			attrishorts[ind] = val;
		}
		else return false;
	}
	else if (cmd.size() == 18 && !cmd.compare(0, 17, "#ATTRISHORT_HELP_"))
	{
		byte ind = cmd.at(17) - '0';
		if (ind < 8)
		{
			attrishorts_help[ind] = util::unescape_characters(val);
		}
		else return false;
	}
	else if ((cmd.size() == 7 || cmd.size() == 8) && !cmd.compare(0, 6, "#FLAG_"))
	{
		byte ind = cmd.at(6) - '0';
		if (cmd.size() == 8)
			ind = (ind * 10) + cmd.at(7) - '0';
		if (ind < 16)
		{
			usrflags[ind] = val;
		}
		else return false;
	}
	else if ((cmd.size() == 12 || cmd.size() == 13) && !cmd.compare(0, 11, "#FLAG_HELP_"))
	{
		byte ind = cmd.at(11) - '0';
		if (cmd.size() == 13)
			ind = (ind * 10) + cmd.at(12) - '0';
		if (ind < 16)
		{
			usrflags_help[ind] = util::unescape_characters(val);
		}
		else return false;
	}
	else if (cmd.size() == 8 && !cmd.compare(0, 7, "#INITD_"))
	{
		byte ind = cmd.at(7) - '0';
		if (ind < 8)
		{
			initd[ind] = val;
		}
		else return false;
	}
	else if (cmd.size() == 13 && !cmd.compare(0, 12, "#INITD_HELP_"))
	{
		byte ind = cmd.at(12) - '0';
		if (ind < 8)
		{
			initd_help[ind] = util::unescape_characters(val);
		}
		else return false;
	}
	else if (cmd.size() == 13 && !cmd.compare(0, 12, "#INITD_TYPE_"))
	{
		byte ind = cmd.at(12) - '0';
		if (ind < 8)
		{
			initd_type[ind] = atoi(val.c_str());
		}
		else return false;
	}
	else return false;
	
	return true;
}

