// The pieces of upstream's GUI the unified engine doesn't have replacements for
// yet. Written from scratch for zcport. The websocket calls stay inert in the fork.
#include "core/jwinfsel.h"
#include "gui/jwin.h"
#include "zc/websocket_pool.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// The file list behind a script's Directory object: folders first (with a trailing
// slash), then files, each group ignoring case.
void FLIST::clear()
{
	for (int32_t i = 0; i < size; ++i)
	{
		free(name[i]);
		name[i] = nullptr;
	}
	size = 0;
}

void FLIST::load(const char* path)
{
	namespace fs = std::filesystem;
	clear();
	fs::path folder = fs::path(path).parent_path();
	if (folder.empty())
		folder = ".";
	snprintf(dir, sizeof(dir), "%s/", folder.string().c_str());

	std::vector<std::pair<bool, std::string>> found; // (is a file, name)
	std::error_code ec;
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code type_ec;
		const bool is_dir = it->is_directory(type_ec);
		found.emplace_back(!is_dir, it->path().filename().string() + (is_dir ? "/" : ""));
	}
	std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) {
		if (a.first != b.first)
			return a.first < b.first;
		return _stricmp(a.second.c_str(), b.second.c_str()) < 0;
	});
	for (auto const& entry : found)
	{
		if (size >= FLIST_SIZE)
			break;
		name[size++] = _strdup(entry.second.c_str());
	}
}

bool FLIST::get(int32_t index, char* buf)
{
	if (index < 0 || index >= size)
		return false;
	strcpy(buf, name[index]);
	return true;
}
void relativize_path(char* dest, char const* path) { std::strcpy(dest, path); }
std::string relativize_path(std::string src_path) { return src_path; }

// Custom bitmap fonts come with the rest of the graphics work; until then, the default font.
FONT* grab_font_from_bitmap(BITMAP*) { return extract_font_range(font, -1, -1); }

int websocket_pool_connect(std::string, std::string& err) { err = "websockets are not available in this build"; return -1; }
void websocket_pool_send(int, WebSocketMessageType, const std::string&) {}
bool websocket_pool_has_message(int) { return false; }
std::pair<WebSocketMessageType, std::string> websocket_pool_receive(int) { return {WebSocketMessageType::None, std::string()}; }
WebSocketStatus websocket_pool_status(int) { return WebSocketStatus::Closing; }
std::string websocket_pool_error(int) { return "websockets are not available in this build"; }
void websocket_pool_close(int) {}
void websocket_pool_destroy() {}
