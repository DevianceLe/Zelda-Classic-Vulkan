// zscript255: readers for the files zscript255 writes, for the program that runs it (ZQuest).
// Header only, standard library only, everything in namespace zs255_result, so it can be
// included anywhere without pulling in the compiler.
//
//   -assembled <file>  every compiled script as 2.55.17's ZQuest stores it in a slot
//                      (layout in zs255_assemble.h): read_assembled()
//   -messages <file>   every message, "<code> <text>" per line, \\ \n \r escaped:
//                      read_messages()
//   exit code / linked result: 0 compiled; otherwise the first error's number, or -1
//
// Script type ids in the -assembled file are 2.55.17's ScriptTypeID (parser/Types.h).
#ifndef ZS255_RESULT_H
#define ZS255_RESULT_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zs255_result
{
	enum ScriptTypeId : int32_t
	{
		type_none = -1, type_global = 1, type_ffc = 2, type_item = 3, type_npc = 4,
		type_eweapon = 5, type_lweapon = 6, type_hero = 7, type_screen = 8, type_dmap = 9,
		type_itemsprite = 10, type_untyped = 11, type_combo = 12, type_subscreen = 13,
		type_generic = 14
	};

	struct AssembledScript
	{
		std::string name;     // "~Init" is the hidden global init script (global slot 0)
		int32_t type = type_none;
		std::vector<uint8_t> record; // the slot's bytes in the FFSC section, as written
	};

	enum MessageCode : int32_t
	{
		msg_info = -9998, msg_error = -9997, msg_warning = -9996, msg_debug = -9995
	};
	struct Message
	{
		int32_t code = msg_info;
		std::string text;
	};

	namespace detail
	{
		inline bool read_all(std::string const& path, std::vector<uint8_t>& data)
		{
			data.clear();
			FILE* f = fopen(path.c_str(), "rb");
			if(!f)
				return false;
			uint8_t buf[65536];
			size_t n;
			while((n = fread(buf, 1, sizeof(buf), f)) > 0)
				data.insert(data.end(), buf, buf + n);
			bool ok = !ferror(f);
			fclose(f);
			return ok;
		}
		inline bool get_u32(std::vector<uint8_t> const& d, size_t& pos, uint32_t& v)
		{
			if(pos + 4 > d.size())
				return false;
			v = uint32_t(d[pos]) | (uint32_t(d[pos+1]) << 8) | (uint32_t(d[pos+2]) << 16) | (uint32_t(d[pos+3]) << 24);
			pos += 4;
			return true;
		}
	}

	// Reads a -assembled file. false if it is missing or not a whole file of format 1
	// for script section version 22 and metadata version 5.
	inline bool read_assembled(std::string const& path, std::vector<AssembledScript>& out)
	{
		out.clear();
		std::vector<uint8_t> d;
		if(!detail::read_all(path, d) || d.size() < 8 || std::string(d.begin(), d.begin() + 8) != "ZS255ASM")
			return false;
		size_t pos = 8;
		uint32_t format, ffscript_v, meta_v, count;
		if(!detail::get_u32(d, pos, format) || !detail::get_u32(d, pos, ffscript_v) ||
			!detail::get_u32(d, pos, meta_v) || !detail::get_u32(d, pos, count))
			return false;
		if(format != 1 || ffscript_v != 22 || meta_v != 5)
			return false;
		for(uint32_t q = 0; q < count; ++q)
		{
			AssembledScript s;
			uint32_t len, type, rec;
			if(!detail::get_u32(d, pos, len) || pos + len > d.size())
				return false;
			s.name.assign(d.begin() + pos, d.begin() + pos + len);
			pos += len;
			if(!detail::get_u32(d, pos, type) || !detail::get_u32(d, pos, rec) || pos + rec > d.size())
				return false;
			s.type = int32_t(type);
			s.record.assign(d.begin() + pos, d.begin() + pos + rec);
			pos += rec;
			out.push_back(std::move(s));
		}
		return pos == d.size();
	}

	// Reads a -messages file.
	inline bool read_messages(std::string const& path, std::vector<Message>& out)
	{
		out.clear();
		std::vector<uint8_t> d;
		if(!detail::read_all(path, d))
			return false;
		size_t pos = 0;
		while(pos < d.size())
		{
			size_t end = pos;
			while(end < d.size() && d[end] != '\n')
				++end;
			std::string line(d.begin() + pos, d.begin() + end);
			pos = end + 1;
			size_t sp = line.find(' ');
			if(sp == std::string::npos)
				continue;
			Message m;
			m.code = int32_t(std::strtol(line.substr(0, sp).c_str(), nullptr, 10));
			for(size_t i = sp + 1; i < line.size(); ++i)
			{
				char c = line[i];
				if(c == '\\' && i + 1 < line.size())
				{
					char e = line[++i];
					m.text += e == 'n' ? '\n' : e == 'r' ? '\r' : e;
				}
				else m.text += c;
			}
			out.push_back(std::move(m));
		}
		return true;
	}

	// The record of an empty slot (what 2.55.17 writes for a slot with no script): size 1,
	// zeroed metadata (initd types -1, run types 1), the closing 0xFFFF.
	inline std::vector<uint8_t> empty_record()
	{
		std::vector<uint8_t> r;
		auto b = [&r](uint8_t v) { r.push_back(v); };
		auto w = [&b](uint16_t v) { b(uint8_t(v)); b(uint8_t(v >> 8)); };
		b(1); b(0); b(0); b(0);           // size 1
		w(0); w(0); w(0); b(0);           // zasm_v, meta_v, ffscript_v, script type
		for(int q = 0; q < 8; ++q) b(0);  // run_idens (empty cstr)
		for(int q = 0; q < 8; ++q) b(1);  // run_types (ZMETA_NULL_TYPE)
		b(0);                              // flags
		for(int q = 0; q < 4; ++q) w(0);  // compiler_v1..4
		b(0); b(0);                        // script_name, author
		for(int q = 0; q < 10 + 8 + 8 + 16 + 8; ++q) { b(0); w(0); } // (cstr, wstr) pairs
		for(int q = 0; q < 8; ++q) b(0xFF); // initd_type -1
		w(0xFFFF);                         // end of commands
		return r;
	}
}

#endif
