#include "CompilerUtils.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

std::string XTableHelper::formatStr(std::string const* format, ...)
{
	// zscript255: upstream vsprintf()s into a static buffer of formatBufferSize bytes,
	// which a longer message (an error listing many function overloads) runs past.
	// Here the buffer grows to fit; a message that fits reads exactly as upstream's.
	static std::vector<char> buffer(formatBufferSize);

	va_list args;
	va_start(args, format);
	va_list measure;
	va_copy(measure, args);
	int32_t len = vsnprintf(buffer.data(), buffer.size(), format->c_str(), measure);
	va_end(measure);
	if(len >= 0 && size_t(len) >= buffer.size())
	{
		buffer.resize(size_t(len) + 1);
		vsnprintf(buffer.data(), buffer.size(), format->c_str(), args);
	}
	va_end(args);

	if(len < 0)
		return std::string();
	return std::string(buffer.data());
}

