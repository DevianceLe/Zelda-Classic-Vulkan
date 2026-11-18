// zscript255: upstream 2.55.17's base/util.cpp, only the functions its ZScript compiler
// uses (copied unchanged), plus the trace hooks it calls, which write nothing.
#include "base/util.h"
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <sys/stat.h>

using namespace std;
namespace fs = std::filesystem;

namespace util
{
	void upperstr(string& str)
	{
		for(int32_t q = str.size() - 1; q >= 0; --q)
			str[q] = toupper(str[q]);
	}

	void lowerstr(string& str)
	{
		for(int32_t q = str.size() - 1; q >= 0; --q)
			str[q] = tolower(str[q]);
	}

	void trimstr(string& str)
	{
		str.erase(0, str.find_first_not_of("\t\n\v\f\r "));
		str.erase(str.find_last_not_of("\t\n\v\f\r ") + 1);
	}

	// https://stackoverflow.com/a/5888676/2788187
	size_t split(const std::string &txt, std::vector<std::string> &strs, char ch)
	{
		size_t pos = txt.find( ch );
		size_t initialPos = 0;
		strs.clear();

		// Decompose statement
		while( pos != std::string::npos ) {
			strs.push_back( txt.substr( initialPos, pos - initialPos ) );
			initialPos = pos + 1;

			pos = txt.find( ch, initialPos );
		}

		// Add the last one
		strs.push_back( txt.substr( initialPos, std::min( pos, txt.size() ) - initialPos + 1 ) );

		return strs.size();
	}

	// https://stackoverflow.com/a/6089413/2788187
	std::istream &portable_get_line(std::istream &is, std::string &t)
	{
		t.clear();

		// The characters in the stream are read one-by-one using a std::streambuf.
		// That is faster than reading them one-by-one using the std::istream.
		// Code that uses streambuf this way must be guarded by a sentry object.
		// The sentry object performs various tasks,
		// such as thread synchronization and updating the stream state.

		std::istream::sentry se(is, true);
		std::streambuf *sb = is.rdbuf();

		for (;;)
		{
			int c = sb->sbumpc();
			switch (c)
			{
			case '\n':
				return is;
			case '\r':
				if (sb->sgetc() == '\n')
					sb->sbumpc();
				return is;
			case std::streambuf::traits_type::eof():
				// Also handle the case when the last line has no line ending
				if (t.empty())
					is.setstate(std::ios::eofbit);
				return is;
			default:
				t += (char)c;
			}
		}
	}

	string cropPath(string filepath)
	{
		size_t lastslash = filepath.find_last_of("/");
		size_t lastbslash = filepath.find_last_of("\\");
		size_t last = (lastslash == string::npos) ? lastbslash : (lastbslash == string::npos ? lastslash : (lastslash > lastbslash ? lastslash : lastbslash));
		if(last != string::npos) filepath = filepath.substr(last+1);
		return filepath;
	}

	void replstr(std::string& str, std::string const& from, std::string const& to)
	{
		for(int32_t q = str.size() - from.size(); q >= 0; --q)
		{
			if(!str.compare(q,from.size(),from))
			{
				str = str.replace(q,from.size(),to);
				q -= from.size()-1;
			}
		}
	}

	std::string escape_characters(std::string const& str)
	{
		std::string temp = str;
		replstr(temp, "\\", "\\\\");
		replstr(temp, "\"", "\\\"");
		replstr(temp, "'", "\\'");
		replstr(temp, "\a", "\\a");
		replstr(temp, "\b", "\\b");
		replstr(temp, "\f", "\\f");
		replstr(temp, "\n", "\\n");
		replstr(temp, "\r", "\\r");
		replstr(temp, "\t", "\\t");
		replstr(temp, "\v", "\\v");
		return temp;
	}
	std::string disallow_escapes(std::string const& str)
	{
		std::string temp = str;
		replstr(temp, "\\\\", "\\");
		replstr(temp, "\\\"", "\"");
		replstr(temp, "\\'", "'");
		replstr(temp, "\\a", "");
		replstr(temp, "\\b", "");
		replstr(temp, "\\f", "");
		replstr(temp, "\\n", "");
		replstr(temp, "\\r", "");
		replstr(temp, "\\t", "");
		replstr(temp, "\\v", "");
		return temp;
	}
	// Undoes `escape_characters`. This has to consume the string in one pass -
	// replacing each escape in turn would let the backslash freed by "\\\\" be
	// eaten by a later pass, turning `C:\new` into `C:<newline>ew`.
	std::string unescape_characters(std::string const& str)
	{
		std::string temp;
		temp.reserve(str.size());
		for(size_t q = 0; q < str.size(); ++q)
		{
			if(str[q] != '\\' || q+1 == str.size())
			{
				temp += str[q];
				continue;
			}

			switch(str[++q])
			{
				case '\\': temp += '\\'; break;
				case '"': temp += '"'; break;
				case '\'': temp += '\''; break;
				case 'a': temp += '\a'; break;
				case 'b': temp += '\b'; break;
				case 'f': temp += '\f'; break;
				case 'n': temp += '\n'; break;
				case 'r': temp += '\r'; break;
				case 't': temp += '\t'; break;
				case 'v': temp += '\v'; break;
				// Not an escape this understands - leave it alone.
				default: temp += '\\'; temp += str[q]; break;
			}
		}
		return temp;
	}

	void replchar(std::string& str, char from, char to)
	{
		for(int32_t q = str.size() - 1; q >= 0; --q)
		{
			if(str.at(q)==from)
				str[q] = to;
		}
	}

	void replchar(char* buf, char from, char to)
	{
		for(int32_t i = 0; buf[i]!=0; ++i)
		{
			if(buf[i]==from)
				buf[i] = to;
		}
	}

	void removechar(std::string& str, char to_delete)
	{
		size_t pos = str.find_first_of(to_delete);
		while(pos != std::string::npos)
		{
			str.erase(pos,1);
			pos = str.find_first_of(to_delete);
		}
	}

	void regulate_path(char* buf)
	{
		for(int32_t q = 0; buf[q]; ++q)
		{
#ifdef _WIN32
			if (buf[q] == '/') buf[q] = '\\';
#else
			if (buf[q] == '\\') buf[q] = '/';
#endif
		}
	}

	void regulate_path(string& buf)
	{
		for(int32_t q = 0; buf[q]; ++q)
		{
#ifdef _WIN32
			if (buf[q] == '/') buf[q] = '\\';
#else
			if (buf[q] == '\\') buf[q] = '/';
#endif
		}
	}

	static void reverse( char *s )
	{
		if ( *s )
		{
			for ( char *first = s, *last = s + strlen( s ); first < --last; ++first )
			{
				char c = *first;
				*first = *last;
				*last = c;
			}
		}
	}

	char* zc_itoa(int32_t value, char* str, int32_t base)
	{
#ifdef _WIN32
		return _itoa(value, str, base);
#else
		static char dig[] =
			"0123456789"
			"abcdefghijklmnopqrstuvwxyz";
		int32_t n = 0, neg = 0;
		uint32_t v;
		if (base == 10 && value < 0)
		{
			value = -value;
			neg = 1;
		}
		v = value;
		do
		{
			str[n++] = dig[v%base];
			v /= base;
		} while (v);
		if (neg)
		{
			str[n++] = '-';
		}
		str[n] = '\0';
		reverse(str);
		return str;
#endif
	}

	int32_t zc_xtoi(const char *hexstr)
	{
		int32_t val=0;
		bool neg = false;
		if(*hexstr == '-')
		{
			neg = true;
			++hexstr;
		}
		while(isxdigit(*hexstr))
		{
			val<<=4;

			if(*hexstr<='9')
				val += *hexstr-'0';
			else val+= ((*hexstr)|0x20)-'a'+10;

			++hexstr;
		}

		return neg ? -val : val;
	}

	int32_t ffparse2(const char *string) //bounds result safely between -214748.3648 and +214748.3647
	{
		char tempstring1[32] = {0};
		strcpy(tempstring1, string);

		char *ptr=strchr(tempstring1, '.');
		if(!ptr)
		{
			return vbound(atoi(tempstring1),-214748,214748)*10000;
		}

		int32_t ret=0;

		for(int32_t i=0; i<4; ++i)
		{
			tempstring1[strlen(string)+i]='0';
		}

		ptr=strchr(tempstring1, '.');
		*ptr=0;
		ret=vbound(atoi(tempstring1),-214748,214748)*10000;

		++ptr;
		char *ptr2=ptr;
		ptr2+=4;
		*ptr2=0;

		int32_t decval = abs(atoi(ptr));
		if(tempstring1[0] == '-')
		{
			if(ret == -2147480000)
				decval = vbound(decval, 0, 3648);
			ret-=decval;
		}
		else
		{
			if(ret == 2147480000)
				decval = vbound(decval, 0, 3647);
			ret+=decval;
		}

		return ret;
	}

	// Upstream sends these to its Allegro trace log; the compiler's own output goes
	// through zconsole_* instead, so they write nothing here.
	void safe_al_trace(const char*)
	{
	}
	void safe_al_trace(std::string const&)
	{
	}

	string escape_string(char const* str)
	{
		ostringstream oss;
		oss << "\"";
		size_t len = strlen(str);
		for(size_t q = 0; q < len; ++q)
		{
			char c = str[q];
			if(c < ' ' || c > '~')
			{
				switch(c)
				{
					case '\n': oss << "\\n"; break;
					case '\t': oss << "\\t"; break;
					case '\a': oss << "\\a"; break;
					case '\b': oss << "\\b"; break;
					case '\f': oss << "\\f"; break;
					case '\r': oss << "\\r"; break;
					case '\v': oss << "\\v"; break;
					case '"': oss << "\\\""; break;
					case '\\': oss << "\\\\"; break;
					default:
					{
						oss << "\\x" << std::setfill('0') << std::setw(2) << std::hex << c;
						break;
					}
				}
			}
			else oss << c;
		}
		oss << "\"";
		return oss.str();
	}
	string escape_string(string const& str)
	{
		ostringstream oss;
		oss << "\"";
		size_t len = str.size();
		for(size_t q = 0; q < len; ++q)
		{
			char c = str[q];
			switch(c)
			{
				case '\n': oss << "\\n"; break;
				case '\t': oss << "\\t"; break;
				case '\a': oss << "\\a"; break;
				case '\b': oss << "\\b"; break;
				case '\f': oss << "\\f"; break;
				case '\r': oss << "\\r"; break;
				case '\v': oss << "\\v"; break;
				case '"': oss << "\\\""; break;
				case '\\': oss << "\\\\"; break;
				default:
				{
					if(c < ' ' || c > '~')
						oss << "\\x" << std::setfill('0') << std::setw(2) << std::hex << (int32_t(c)&0xFF);
					else oss << c;
					break;
				}
			}
		}
		oss << "\"";
		return oss.str();
	}

	string stringify_vector(vector<int32_t> const& vec, bool dec)
	{
		ostringstream oss;
		oss << "{";
		for(size_t q = 0; q < vec.size(); ++q)
		{
			if(dec)
			{
				int32_t val = vec[q];
				if(val % 10000)
				{
					char buf[16] = {0};
					strcpy(buf, zslongToFix(val).str().c_str());
					for(auto q = strlen(buf)-1; buf[q] == '0'; --q)
					{
						//Kill trailing zeroes
						buf[q] = 0;
					}
					oss << buf;
				}
				else
				{
					oss << std::to_string(vec[q]/10000);
				}
			}
			else oss << std::to_string(vec[q]);
			if(q+1 < vec.size())
				oss << ",";
		}
		oss << "}";
		return oss.str();
	}

	// zscript255: upstream's readers for the escaped strings and {..} lists in ZASM text,
	// used by the assembler (zs255_assemble.cpp) as 2.55.17's ZQuest uses them.
	string unescape_string(char const* str)
	{
		ostringstream oss;
		bool in_str = false;
		size_t len = strlen(str);
		for(size_t q = 0; q < len; ++q)
		{
			if(str[q] == '"')
			{
				if(in_str) break;
				else in_str = true;
			}
			else if(in_str)
			{
				if(str[q] == '\\')
				{
					if(++q < len)
					{
						switch(str[q])
						{
							case 'n': oss << '\n'; break;
							case 't': oss << '\t'; break;
							case 'a': oss << '\a'; break;
							case 'b': oss << '\b'; break;
							case 'f': oss << '\f'; break;
							case 'r': oss << '\r'; break;
							case 'v': oss << '\v'; break;
							case '\\': oss << '\\'; break;
							case '"': oss << '"'; break;
							case 'x':
							{
								if(q+2 < len)
								{
									char hb[3] = {str[q+1],str[q+2],0};
									oss << char(zc_xtoi(hb));
								}
								q += 2;
								break;
							}
							default: oss << str[q]; break;
						}
					}
				}
				else oss << str[q];
			}
		}
		return oss.str();
	}
	string unescape_string(string const& str)
	{
		ostringstream oss;
		bool in_str = false;
		size_t len = str.size();
		for(size_t q = 0; q < len; ++q)
		{
			if(str[q] == '"')
			{
				if(in_str) break;
				else in_str = true;
			}
			else if(in_str)
			{
				if(str[q] == '\\')
				{
					if(++q < len)
					{
						switch(str[q])
						{
							case 'n': oss << '\n'; break;
							case 't': oss << '\t'; break;
							case 'a': oss << '\a'; break;
							case 'b': oss << '\b'; break;
							case 'f': oss << '\f'; break;
							case 'r': oss << '\r'; break;
							case 'v': oss << '\v'; break;
							case '\\': oss << '\\'; break;
							case '"': oss << '"'; break;
							case 'x':
							{
								if(q+2 < len)
								{
									char hb[3] = {str[q+1],str[q+2],0};
									oss << char(zc_xtoi(hb));
								}
								q += 2;
								break;
							}
							default: oss << str[q]; break;
						}
					}
				}
				else oss << str[q];
			}
		}
		return oss.str();
	}
	size_t escstr_size(char const* str)
	{
		bool in_str = false;
		size_t len = strlen(str);
		size_t start = 0, q;
		bool complete = false;
		for(q = 0; q < len; ++q)
		{
			if(str[q] == '"')
			{
				if(in_str)
				{
					complete = true;
					break;
				}
				else
				{
					start = q;
					in_str = true;
				}
			}
			else if(in_str)
			{
				if(str[q] == '\\')
				{
					if(++q < len)
					{
						if(str[q] == 'x')
						{
							q += 2;
						}
					}
				}
			}
		}
		if(!complete) return 0;
		if(!in_str) return 0;
		return q-start+1;
	}
	
	void unstringify_vector(vector<int32_t>& vec, string const& str, bool dec)
	{
		vec.clear();
		bool in_str = false;
		size_t len = str.size();
		for(size_t q = 0; q < len; ++q)
		{
			if(str[q] == '{')
				in_str = true;
			else if(str[q] == '}' && in_str)
				break;
			else if(in_str)
			{
				char buf[16] = {0};
				size_t ind = 0;
				while(str[q] == ',' && q+1 < len) ++q;
				for(char c = str[q]; c != ',' && c != '}' && q+1 < len; c = str[++q])
				{
					buf[ind++] = c;
				}
				if(buf[0])
				{
					if(dec)
						vec.push_back(ffparse2(buf));
					else vec.push_back(atoi(buf));
				}
				if(str[q] == '}') break;
			}
		}
	}
	void unstringify_vector(vector<int32_t>& vec, char const* str, bool dec)
	{
		vec.clear();
		bool in_str = false;
		size_t len = strlen(str);
		for(size_t q = 0; q < len; ++q)
		{
			if(str[q] == '{')
				in_str = true;
			else if(str[q] == '}' && in_str)
				break;
			else if(in_str)
			{
				char buf[16] = {0};
				size_t ind = 0;
				while(str[q] == ',' && q+1 < len) ++q;
				for(char c = str[q]; c != ',' && c != '}' && q+1 < len; c = str[++q])
				{
					buf[ind++] = c;
				}
				if(buf[0])
				{
					if(dec)
						vec.push_back(ffparse2(buf));
					else vec.push_back(atoi(buf));
				}
				if(str[q] == '}') break;
			}
		}
	}
	size_t vecstr_size(char const* str)
	{
		bool in_str = false;
		size_t len = strlen(str);
		size_t start = 0, q;
		bool complete = false;
		for(q = 0; q < len; ++q)
		{
			if(!in_str && str[q] == '{')
			{
				in_str = true;
				start = q;
			}
			else if(in_str)
			{
				if(str[q] == '}')
				{
					complete = true;
					break;
				}
				size_t ind = 0;
				while(str[q] == ',' && q+1 < len) ++q;
				for(char c = str[q]; c != ',' && c != '}' && q+1 < len; c = str[++q])
				{}
				if(str[q] == '}')
				{
					complete = true;
					break;
				}
			}
		}
		if(!complete) return 0;
		if(!in_str) return 0;
		return q-start+1;
	}
}

bool fileexists(const char *filename)
{
	std::ifstream ifile(filename);
	if(ifile) return true;
	return false;
}
