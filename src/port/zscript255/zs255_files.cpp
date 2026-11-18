// zscript255: see zs255_files.h.
#include "zs255_files.h"

#ifdef _WIN32

FILE* zs255_fopen_source(char const* path)
{
	return fopen(path, "r");
}

bool zs255_source_exists(char const* path)
{
	FILE* f = fopen(path, "r");
	if(!f)
		return false;
	fclose(f);
	return true;
}

#else

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	bool same_nocase(std::string const& a, std::string const& b)
	{
		if(a.size() != b.size())
			return false;
		for(size_t q = 0; q < a.size(); ++q)
		{
			char x = a[q], y = b[q];
			if(x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
			if(y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
			if(x != y)
				return false;
		}
		return true;
	}

	// The path on disk that matches path with letter case ignored in each part (the
	// exact name wins where it exists); empty if there is none.
	std::string find_nocase(std::string const& path)
	{
		std::error_code ec;
		fs::path p(path);
		fs::path cur = p.is_absolute() ? p.root_path() : fs::path(".");
		bool first = true;
		for(auto const& part : p.relative_path())
		{
			std::string name = part.string();
			fs::path next = first && !p.is_absolute() ? fs::path(name) : cur / name;
			if(name == "." || name == ".." || fs::exists(next, ec))
			{
				cur = next;
				first = false;
				continue;
			}
			bool found = false;
			for(fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec))
			{
				std::string entry = it->path().filename().string();
				if(same_nocase(entry, name))
				{
					cur = first && !p.is_absolute() ? fs::path(entry) : cur / entry;
					found = true;
					break;
				}
			}
			if(!found)
				return std::string();
			first = false;
		}
		return cur.string();
	}

	// The file fopen(path, "r") opens on Windows: the exact name, else the same name in
	// other letter case; never a folder (Windows can't open one, Linux can).
	std::string find_source(char const* path)
	{
		std::error_code ec;
		if(!*path)
			return std::string();
		if(fs::exists(path, ec))
			return fs::is_directory(path, ec) ? std::string() : std::string(path);
		std::string other = find_nocase(path);
		if(other.empty() || fs::is_directory(other, ec))
			return std::string();
		return other;
	}
}

bool zs255_source_exists(char const* path)
{
	std::string found = find_source(path);
	if(found.empty())
		return false;
	FILE* f = fopen(found.c_str(), "rb");
	if(!f)
		return false;
	fclose(f);
	return true;
}

FILE* zs255_fopen_source(char const* path)
{
	std::string found = find_source(path);
	if(found.empty())
		return nullptr;
	FILE* f = fopen(found.c_str(), "rb");
	if(!f)
		return nullptr;
	// Windows text mode: CR LF reads as LF, Ctrl-Z ends the file.
	FILE* out = std::tmpfile();
	if(!out)
	{
		fclose(f);
		return nullptr;
	}
	int c, held = EOF;
	while((c = fgetc(f)) != EOF)
	{
		if(c == 0x1A)
			break;
		if(held == '\r' && c != '\n')
			fputc('\r', out);
		if(c == '\r')
		{
			held = c;
			continue;
		}
		held = EOF;
		fputc(c, out);
	}
	if(held == '\r')
		fputc('\r', out);
	fclose(f);
	rewind(out);
	return out;
}

#endif
