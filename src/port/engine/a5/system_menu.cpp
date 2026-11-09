// Shows the engine's menu tree (TopMenu, NewMenu) on the ImGui menu bar. The tree is
// translated once into the MENU arrays the dialog host reads; each step refreshes
// the labels and check marks in place, since the host keeps pointers to the entries.
// Written from scratch for zcport.
#include "zalleg/new_menu.h"
#include "gui/jwin.h"

#include <array>
#include <memory>
#include <vector>

void zcui_dialogs_open_menu(int index); // src/port/game/ui_dialogs_host.cpp
int jwin_menu_proc(int msg, DIALOG* d, int c); // src/port/game/jwin_port.cpp

namespace
{
	struct Level
	{
		GuiMenu* source;
		std::vector<MENU> entries;                 // ends with an empty entry
		std::vector<std::array<char, 128>> labels;
	};

	std::vector<std::unique_ptr<Level>> g_levels;
	std::vector<DIALOG> g_dialog;
	DIALOG_PLAYER* g_player = nullptr;

	int run_entry()
	{
		if (active_menu && active_menu->dp)
			static_cast<MenuItem*>(active_menu->dp)->exec();
		return D_O_K;
	}

	Level* build(GuiMenu& menu)
	{
		g_levels.push_back(std::make_unique<Level>());
		Level* level = g_levels.back().get();
		level->source = &menu;
		const size_t count = menu.size();
		level->labels.resize(count);
		level->entries.assign(count + 1, MENU{});
		for (size_t i = 0; i < count; ++i)
		{
			MenuItem* item = menu.at((uint)i);
			MENU& entry = level->entries[i];
			level->labels[i][0] = 0;
			entry.text = level->labels[i].data();
			entry.dp = item;
			if (item->submenu)
				entry.child = build(*item->submenu)->entries.data();
			else if (!item->isEmpty())
				entry.proc = run_entry;
		}
		return level;
	}

	void refresh()
	{
		for (auto& level : g_levels)
		{
			const size_t limit = level->source->chop_index ? *level->source->chop_index : level->source->size();
			for (size_t i = 0; i < level->labels.size(); ++i)
			{
				MenuItem* item = level->source->at((uint)i);
				MENU& entry = level->entries[i];
				std::string text = item->text;
				if (!item->subtext.empty())
					text += "\t" + item->subtext;
				snprintf(level->labels[i].data(), level->labels[i].size(), "%s", text.c_str());
				entry.flags = (item->isDisabled() ? D_DISABLED : 0) | (item->isSelected() ? D_SELECTED : 0);
				// Entries past the menu's cut-off aren't shown: a null text ends the list.
				entry.text = i < limit ? level->labels[i].data() : nullptr;
			}
		}
	}
}

void zcui_system_menu_begin(GuiMenu& top, const std::vector<std::pair<int, int (*)()>>& hotkeys, bool open_first)
{
	g_levels.clear();
	Level* root = build(top);
	refresh();

	g_dialog.clear();
	DIALOG bar{};
	bar.proc = jwin_menu_proc;
	bar.flags = D_USER;
	bar.dp = root->entries.data();
	g_dialog.push_back(bar);
	for (auto& hotkey : hotkeys)
	{
		DIALOG key{};
		key.proc = d_keyboard_proc;
		key.d1 = key.d2 = hotkey.first;
		key.dp = (void*)hotkey.second;
		g_dialog.push_back(key);
	}
	g_dialog.push_back(DIALOG{});

	g_player = init_dialog(g_dialog.data(), -1);
	if (open_first)
		zcui_dialogs_open_menu(0);
}

// False once the menu has closed itself.
bool zcui_system_menu_step()
{
	refresh();
	return g_player && update_dialog(g_player);
}

void zcui_system_menu_end()
{
	if (g_player)
		shutdown_dialog(g_player);
	g_player = nullptr;
	g_levels.clear();
	g_dialog.clear();
}
