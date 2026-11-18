// zscript255: the utility functions of upstream 2.55.17's base/util.h that its ZScript
// compiler uses, with upstream's code (base/util.cpp in zscript255). The rest of
// upstream's util.h (file dialogs, web links, process launching) is left out.
#ifndef _UTIL_H_
#define _UTIL_H_

#include "base/zdefs.h"
#include <sstream>
#include <string>
#include <optional>
#include <string_view>
#include <sys/types.h>
#include <sys/stat.h>
#include <filesystem>
#include <vector>
#include <functional>
#include <algorithm>

namespace fs = std::filesystem;

namespace util
{
	void upperstr(std::string& str);
	void lowerstr(std::string& str);
	void trimstr(std::string& str);
	size_t split(const std::string &txt, std::vector<std::string> &strs, char ch);
	std::istream &portable_get_line(std::istream &is, std::string &t);
	std::string cropPath(std::string filepath);
	void replstr(std::string& str, std::string const& from, std::string const& to);
	std::string escape_characters(std::string const& str);
	std::string disallow_escapes(std::string const& str);
	std::string unescape_characters(std::string const& str);
	void replchar(std::string& str, char from, char to);
	void replchar(char* buf, char from, char to);
	void removechar(std::string& str, char to_delete);
	void regulate_path(char* buf);
	void regulate_path(std::string& buf);
	char* zc_itoa(int32_t value, char* str, int32_t base = 10);
	int32_t zc_xtoi(const char *hexstr);
	int32_t ffparse2(const char *string);
	void safe_al_trace(const char* str);
	void safe_al_trace(std::string const& str);
	std::string escape_string(char const* str);
	std::string escape_string(std::string const& str);
	std::string stringify_vector(std::vector<int32_t> const& vec, bool dec);
	std::string unescape_string(char const* str);
	std::string unescape_string(std::string const& str);
	size_t escstr_size(char const* str);
	void unstringify_vector(std::vector<int32_t>& vec, std::string const& str, bool dec);
	void unstringify_vector(std::vector<int32_t>& vec, char const* str, bool dec);
	size_t vecstr_size(char const* str);
}

bool fileexists(const char *filename);

#endif
