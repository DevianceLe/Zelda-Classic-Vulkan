// ZQuest's music editor for 3.0 Nightly quests (Dear ImGui): the quest's music objects (the AMUS
// section), each a name, a MIDI and an enhanced music file with its track, loop points and
// crossfades, which DMaps and screens play by number. A port of upstream's music list
// (MusicListerDialog, zq/dialog/info_lister.cpp) and its Edit Music dialog
// (zq/dialog/edit_music.cpp): add, edit, copy and paste, clear, delete, reorder, and clean up
// the entries that play nothing; preview the MIDI and the enhanced file (from its start or its
// loop's last second, with Pause / Resume). Deleting or moving an entry moves every DMap's
// and screen's reference along, as upstream's update_quest_music does (zq255_music_remap,
// zqw_amus.cpp). Enhanced files are looked for where the players look (find_for_quest).
//
// Like upstream's list, every change goes into the quest at once; the AMUS writer
// (zqw_amus.cpp) saves the list in the quest's own AMUS version. Quests without music objects
// (2.55, and 3.0 quests from before them) keep each DMap's own music in the DMap editor.
//
// The window is a UI layer (the platform thread, UI lock held) shown during a modal session the
// game thread holds open; the game thread does the changes, the file picker and the previews.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zcmusic.h"
#include "zcmusic_port.h"
#include "zcport/ui.h"
#include "zq_imgui_fit.h"
#include "zq_q255_store.h"

#include <imgui.h>
#include <imgui_internal.h>

extern bool saved;
extern zctune *customtunes;
extern char *filepath, *temppath, *tmusicpath;
extern ZCMUSIC *zcmusic;
extern int midi_volume;
struct EXT_LIST;        // zquest.cpp's file picker (only its pointer passes through here)
bool getname(const char *prompt, const char *ext, EXT_LIST *list, const char *def, bool usefilename);
int stopMusic();
bool zq255_has_music_objects();                                             // zqw_amus.cpp
bool zq255_music_remap(const std::map<int, int> &changes, std::string &why);

namespace
{

const int MAX_MUSIC = 65535;            // upstream's MAX_QUEST_MUSIC
const int MAX_MIDIS_30 = 512;           // 3.0's MAXCUSTOMMIDIS
const char *const g_builtin[7] = { "Dungeon", "Ending", "Game Over", "Level 9", "Overworld", "Title", "McGuffin" };

enum Req
{
    REQ_NONE, REQ_SAVE_EDIT, REQ_DELETE, REQ_SWAP, REQ_PASTE, REQ_CLEAR, REQ_CLEANUP, REQ_LOAD, REQ_PLAY_MIDI,
    REQ_PLAY_ENH, REQ_PLAY_LOOP, REQ_PAUSE, REQ_STOP, REQ_INFO
};

struct MusicEd
{
    bool open, close, focus, busy;
    std::vector<Zq255Store::Music> view;    // the quest's list as it stands (the game thread refreshes it)
    std::vector<std::string> tune_names;    // the quest's tunes 1-512, for the MIDI list
    int sel;                                // the entry picked, from 0; view.size() is <New Music>
    int copied;                             // -1: nothing copied
    bool sorted;                            // shown by name (no reordering then)

    bool edit_open;                         // the Edit Music window
    int edit_slot;
    Zq255Store::Music edit;
    int edit_tracks;                        // tracks in the enhanced file (0: not known)
    int edit_type;                          // its ZCMF_ type, 0 while it isn't found (upstream's enhanced_type)
    bool playing;                           // a preview plays (upstream's is_playing)
    bool enh_playing;                       // ... and it is the enhanced file
    int saved_pos;                          // where a paused preview stopped (upstream's musicpreview_saved)
    int pos;                                // the enhanced preview's position, seconds * 10000 (-1: none)

    int req, req_a, req_b;                  // a change for the game thread
    std::string message;                    // what went wrong, or what a preview found
    std::map<int, int> moved;               // every reference this session moved: number before -> now

    MusicEd() : open(false), close(false), focus(false), busy(false), sel(0), copied(-1), sorted(false), edit_open(false),
                edit_slot(0), edit_tracks(0), edit_type(0), playing(false), enh_playing(false), saved_pos(0), pos(-1),
                req(REQ_NONE), req_a(0), req_b(0) {}
};

MusicEd g_me;

float sc() { return zcui::scale(); }


std::string tune_name(int m)
{
    if(m >= 1 && m <= (int)g_me.tune_names.size())
        return g_me.tune_names[m - 1];

    return "";
}

// upstream's midinames(true, true) entry for a MIDI value
std::string midi_label(int m)
{
    char b[96];

    if(m >= -7 && m <= -1)
        snprintf(b, sizeof(b), "%s (%03d)", g_builtin[-(m + 1)], m);
    else if(m == 0)
        snprintf(b, sizeof(b), "(None) (000)");
    else
    {
        std::string n = tune_name(m);

        if(n.empty() || n[0] == ' ')
            n = "zzUntitled";

        snprintf(b, sizeof(b), "%s (%03d)", n.c_str(), m);
    }

    return b;
}

// upstream's music_names(true) entry: the name and number, "zz01" for an unnamed one
std::string entry_label(int q)
{
    char b[600];

    if(q >= (int)g_me.view.size())
        snprintf(b, sizeof(b), "<New Music> (%05d)", q + 1);
    else if(g_me.view[q].name.empty())
        snprintf(b, sizeof(b), "zz%02d (%05d)", q + 1, q + 1);
    else
        snprintf(b, sizeof(b), "%s (%05d)", g_me.view[q].name.c_str(), q + 1);

    return b;
}

bool music_is_empty(const Zq255Store::Music &m)
{
    return m.path.empty() && !m.midi;
}

// upstream's empty_music: nothing it could play
bool plays_nothing(const Zq255Store::Music &m)
{
    if(!m.path.empty())
        return false;

    if(!m.midi || m.midi < -7)
        return true;

    if(m.midi < 0)
        return false;       // a built-in tune

    if(m.midi > MAX_MIDIS_30)
        return true;

    if(m.midi <= MAXCUSTOMTUNES)
        return customtunes[m.midi - 1].data == NULL;

    const size_t at = (size_t)(m.midi - MAXCUSTOMTUNES - 1);
    return at >= zq255.high_tunes.size() || zq255.high_tunes[at].data == NULL;
}

void info_text(int q)
{
    if(q >= (int)g_me.view.size())
        ImGui::TextDisabled("A new entry at the end of the list.");
    else
    {
        const Zq255Store::Music &m = g_me.view[q];

        if(music_is_empty(m))
            ImGui::TextUnformatted("[Empty]");
        else
        {
            ImGui::TextUnformatted(m.name.c_str());

            if(m.midi)
                ImGui::Text("MIDI: %d '%s'", (int)m.midi, m.midi < 0 ? (m.midi >= -7 ? g_builtin[-(m.midi + 1)] : "(?ERROR?)")
                                                                      : tune_name(m.midi).c_str());
            else
                ImGui::TextUnformatted("MIDI: (None)");

            if(m.path.empty())
                ImGui::TextUnformatted("[No Enhanced Music]");
            else
            {
                ImGui::Text("Enh: '%s' #%d", m.path.c_str(), (int)m.track);
                ImGui::Text("Loop: %ld - %ld", (long)m.loop_start, (long)m.loop_end);
                ImGui::Text("Crossfade: In %ld, Out %ld", (long)m.xfade_in, (long)m.xfade_out);
            }
        }
    }

    ImGui::Text("Copied: %d", g_me.copied + 1);
}

// ---- the Edit Music window ---------------------------------------------------------------

// a loop point in seconds with four decimals, as upstream's FIXED_DECIMAL fields show it
bool seconds_field(const char *label, long &v)
{
    double s = v / 10000.0;
    ImGui::SetNextItemWidth(130 * sc());

    if(!ImGui::InputDouble(label, &s, 0, 0, "%.4f"))
        return false;

    if(s < 0)
        s = 0;

    if(s > 214748.3647)
        s = 214748.3647;

    v = (long)(s * 10000.0 + 0.5);
    return true;
}

bool frames_field(const char *label, long &v)
{
    int x = (int)v;
    ImGui::SetNextItemWidth(130 * sc());

    if(!ImGui::InputInt(label, &x))
        return false;

    v = x < 0 ? 0 : x > 65535 ? 65535 : x;
    return true;
}

void edit_window()
{
    MusicEd &e = g_me;
    char title[700];
    snprintf(title, sizeof(title), "Edit Music (%d) '%s'###zqmusicedit", e.edit_slot + 1, e.edit.name.c_str());
    zq_fit_next_window(560 * sc(), 470 * sc());
    ImGui::SetNextWindowFocus();
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        zq_keep_window_inside(560 * sc(), 470 * sc());

        // the fields scroll; OK and Cancel stay at the bottom
        const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("fields", ImVec2(0, -bottom)))
        {
        char name[513];
        snprintf(name, sizeof(name), "%s", e.edit.name.c_str());
        ImGui::SetNextItemWidth(360 * sc());

        if(ImGui::InputText("Name", name, sizeof(name)))
            e.edit.name = name;

        ImGui::SetNextItemWidth(360 * sc());

        if(ImGui::BeginCombo("MIDI", midi_label(e.edit.midi).c_str(), ImGuiComboFlags_HeightLarge))
        {
            for(int m = -7; m <= MAX_MIDIS_30; ++m)
            {
                ImGui::PushID(m);

                if(ImGui::Selectable(midi_label(m).c_str(), m == e.edit.midi))
                    e.edit.midi = (short)m;

                if(m == e.edit.midi)
                    ImGui::SetItemDefaultFocus();

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();

        if(ImGui::Button("Preview##midi"))
            e.req = REQ_PLAY_MIDI;

        ImGui::SeparatorText("Enhanced Music");

        // upstream's disableEnhancedMusic: the file's settings only once the file is found where
        // the player looks for it; loop points (and Preview Loop) only for the types that loop
        // there (MP3, OGG and modules)
        const bool no_enh = e.edit.path.empty() || !e.edit_type;
        const bool no_loop = no_enh || !(e.edit_type == ZCMF_MP3 || e.edit_type == ZCMF_OGG || e.edit_type == ZCMF_DUH);
        ImGui::BeginDisabled(true);
        char path[300];
        snprintf(path, sizeof(path), "%s", e.edit.path.c_str());
        ImGui::SetNextItemWidth(360 * sc());
        ImGui::InputText("File", path, sizeof(path), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(no_enh || e.edit_tracks < 2);
        {
            int track = e.edit.track + 1;
            ImGui::SetNextItemWidth(130 * sc());

            if(ImGui::InputInt("Track", &track))
            {
                const int most = e.edit_tracks > 1 ? e.edit_tracks : 1;
                track = track < 1 ? 1 : track > most ? most : track;
                e.edit.track = (byte)(track - 1);
            }

            if(e.edit_tracks > 1)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(of %d)", e.edit_tracks);
            }
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(no_loop);
        ImGui::TextUnformatted("Loop points (seconds)");
        seconds_field("Start", e.edit.loop_start);
        ImGui::SameLine();
        seconds_field("End", e.edit.loop_end);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(no_enh);
        ImGui::TextUnformatted("Crossfades (frames)");
        frames_field("In", e.edit.xfade_in);
        ImGui::SameLine();
        frames_field("Out", e.edit.xfade_out);
        ImGui::EndDisabled();

        // upstream's progress label: where the enhanced preview is
        if(e.enh_playing && e.pos > 0)
            ImGui::Text("%.4fs", e.pos / 10000.0);
        else
            ImGui::TextUnformatted("");

        ImGui::BeginDisabled(no_enh);

        if(ImGui::Button("Preview##enh"))
        {
            e.saved_pos = 0;
            e.req = REQ_PLAY_ENH;
        }

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(no_loop);

        if(ImGui::Button("Preview Loop"))
        {
            e.saved_pos = 0;
            e.req = REQ_PLAY_LOOP;
        }

        ImGui::EndDisabled();

        if(ImGui::Button("Load..."))
            e.req = REQ_LOAD;

        ImGui::SameLine();

        if(ImGui::Button("Clear##enh"))
        {
            // upstream's EnhancedMusic::clear: the file and everything that goes with it
            e.edit.path.clear();
            e.edit.loop_start = e.edit.loop_end = e.edit.xfade_in = e.edit.xfade_out = 0;
            e.edit.track = 0;
            e.edit_tracks = 0;
            e.edit_type = 0;
            e.req = REQ_STOP;
        }

        if(e.edit_type == 0 && !e.edit.path.empty())
            ImGui::TextWrapped("'%s' isn't in the quest's folder, the program's folder or a \"music\" or \"questname_music\" "
                               "folder of either, so its settings can't be changed here (as in upstream's editor).",
                               e.edit.path.c_str());

        // upstream's Pause / Resume button (Stop for a MIDI preview)
        ImGui::Separator();
        ImGui::BeginDisabled(!(e.saved_pos || e.playing));

        if(ImGui::Button(e.saved_pos ? "Resume" : e.playing && !e.enh_playing ? "Stop" : "Pause"))
            e.req = REQ_PAUSE;

        ImGui::EndDisabled();

        if(!e.message.empty())
            ImGui::TextWrapped("%s", e.message.c_str());
        }

        ImGui::EndChild();
        ImGui::Separator();

        if(ImGui::Button("OK", ImVec2(90 * sc(), 0)))
            e.req = REQ_SAVE_EDIT;

        ImGui::SameLine();

        if(ImGui::Button("Cancel", ImVec2(90 * sc(), 0)))
        {
            e.edit_open = false;
            e.req = REQ_STOP;
        }
    }

    ImGui::End();

    if(!open)
    {
        e.edit_open = false;
        e.req = REQ_STOP;
    }
}

// ---- the list ---------------------------------------------------------------------------

void start_edit(int q)
{
    MusicEd &e = g_me;
    e.edit_slot = q;
    e.edit = q < (int)e.view.size() ? e.view[q] : Zq255Store::Music();
    e.edit_tracks = 0;
    e.edit_type = 0;
    e.saved_pos = 0;
    e.edit_open = true;
    e.message.clear();
    e.req = REQ_INFO;       // the game thread finds the file: its type and tracks
}

void list_layer(void *)
{
    MusicEd &e = g_me;

    if(!e.open || e.busy)
        return;

    if(e.edit_open)
    {
        edit_window();
        return;
    }

    zq_fit_next_window(720 * sc(), 520 * sc());

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    bool open = true;
    const int n = (int)e.view.size();
    const bool can_add = n < MAX_MUSIC;

    if(e.sel > n || (e.sel == n && !can_add))
        e.sel = n > 0 ? n - 1 : 0;

    if(ImGui::Begin("Music###zqmusic30", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        zq_keep_window_inside(720 * sc(), 520 * sc());
        ImGui::Checkbox("Sort by name", &e.sorted);
        const float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;

        // the entries, in order or by name; <New Music> stays last
        std::vector<int> order;

        for(int q = 0; q < n; ++q)
            order.push_back(q);

        if(e.sorted)
        {
            struct ByName
            {
                bool operator()(int a, int b) const { return entry_label(a) < entry_label(b); }
            };
            std::stable_sort(order.begin(), order.end(), ByName());
        }

        if(can_add)
            order.push_back(n);

        // the list takes up to 360 points, less in a narrow window
        float list_w = ImGui::GetContentRegionAvail().x * 0.55f;
        list_w = list_w > 360 * sc() ? 360 * sc() : list_w < 150 ? 150 : list_w;

        if(ImGui::BeginChild("list", ImVec2(list_w, -bottom), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clip;
            clip.Begin((int)order.size());

            while(clip.Step())
                for(int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
                {
                    const int q = order[r];
                    ImGui::PushID(q);

                    if(ImGui::Selectable(entry_label(q).c_str(), q == e.sel, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        e.sel = q;

                        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            start_edit(q);
                    }

                    ImGui::PopID();
                }
        }

        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("side", ImVec2(0, -bottom));     // scrolls in a small window
        info_text(e.sel);
        ImGui::Separator();
        const bool real = e.sel < n;

        if(ImGui::Button("Edit", ImVec2(110 * sc(), 0)))
            start_edit(e.sel);

        ImGui::BeginDisabled(!real);

        if(ImGui::Button("Copy", ImVec2(110 * sc(), 0)))
            e.copied = e.sel;

        ImGui::EndDisabled();
        ImGui::BeginDisabled(e.copied < 0 || e.copied >= n);

        if(ImGui::Button("Paste", ImVec2(110 * sc(), 0)))
        {
            e.req = REQ_PASTE;
            e.req_a = e.sel;
            e.req_b = e.copied;
        }

        ImGui::EndDisabled();
        ImGui::BeginDisabled(!real || music_is_empty(e.view[real ? e.sel : 0]));

        if(ImGui::Button("Clear", ImVec2(110 * sc(), 0)))
            ImGui::OpenPopup("Clear music?");

        ImGui::EndDisabled();
        ImGui::BeginDisabled(!real);

        if(ImGui::Button("Delete", ImVec2(110 * sc(), 0)))
            ImGui::OpenPopup("Delete music?");

        ImGui::EndDisabled();
        ImGui::BeginDisabled(e.sorted || !real || e.sel == 0);

        if(ImGui::Button("Up", ImVec2(110 * sc(), 0)))
        {
            e.req = REQ_SWAP;
            e.req_a = e.sel;
            e.req_b = e.sel - 1;
        }

        ImGui::EndDisabled();
        ImGui::BeginDisabled(e.sorted || !real || e.sel + 1 >= n);

        if(ImGui::Button("Down", ImVec2(110 * sc(), 0)))
        {
            e.req = REQ_SWAP;
            e.req_a = e.sel;
            e.req_b = e.sel + 1;
        }

        ImGui::EndDisabled();
        bool any_empty = false;

        for(int q = 0; q < n && !any_empty; ++q)
            any_empty = plays_nothing(e.view[q]);

        ImGui::BeginDisabled(!any_empty);

        if(ImGui::Button("Cleanup", ImVec2(110 * sc(), 0)))
            ImGui::OpenPopup("Clean up music?");

        ImGui::EndDisabled();

        if(e.sorted)
            ImGui::TextDisabled("Music can't be reordered\nwhile sorted by name.");

        if(!e.message.empty())
            ImGui::TextWrapped("%s", e.message.c_str());

        // the confirmations upstream asks for
        if(ImGui::BeginPopupModal("Delete music?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("This will delete Music #%d, '%s'.", e.sel + 1, real ? e.view[e.sel].name.c_str() : "");
            ImGui::TextUnformatted("Anything using this entry will be cleared to using '0', the (None) entry.");

            if(ImGui::Button("Delete", ImVec2(90 * sc(), 0)))
            {
                e.req = REQ_DELETE;
                e.req_a = e.sel;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if(ImGui::Button("Cancel", ImVec2(90 * sc(), 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if(ImGui::BeginPopupModal("Clear music?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Clear Music #%d, '%s'?", e.sel + 1, real ? e.view[e.sel].name.c_str() : "");
            ImGui::TextUnformatted("This will reset it to default.");

            if(ImGui::Button("Clear", ImVec2(90 * sc(), 0)))
            {
                e.req = REQ_CLEAR;
                e.req_a = e.sel;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if(ImGui::Button("Cancel", ImVec2(90 * sc(), 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if(ImGui::BeginPopupModal("Clean up music?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("This will clear any entries that have no playable music set.");
            ImGui::TextUnformatted("Anything using these entries will be cleared to using '0', the (None) entry.");

            if(ImGui::Button("Clean up", ImVec2(90 * sc(), 0)))
            {
                e.req = REQ_CLEANUP;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if(ImGui::Button("Cancel", ImVec2(90 * sc(), 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        ImGui::EndChild();
        ImGui::Separator();

        if(ImGui::Button("Close", ImVec2(90 * sc(), 0)))
            e.close = true;

        ImGui::SameLine();
        ImGui::TextDisabled("%d music object%s; changes go into the quest at once.", n, n == 1 ? "" : "s");

        if(!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            e.close = true;
    }

    ImGui::End();

    if(!open)
        e.close = true;
}

// ---- the game thread ---------------------------------------------------------------------

void refresh_view()
{
    zcui::Lock lock;
    g_me.view = zq255.music;
}

// After a delete or a reorder: the references move, and so do the numbers this session handed
// out before (for an editor that holds one).
bool apply_changes(const std::map<int, int> &ch)
{
    std::string why;
    const bool ok = zq255_music_remap(ch, why);
    std::map<int, int> &mv = g_me.moved;

    for(std::map<int, int>::iterator it = mv.begin(); it != mv.end(); ++it)
    {
        std::map<int, int>::const_iterator c = ch.find(it->second);

        if(c != ch.end())
            it->second = c->second;
    }

    for(std::map<int, int>::const_iterator c = ch.begin(); c != ch.end(); ++c)
        if(!mv.count(c->first))
            mv[c->first] = c->second;

    if(!ok)
    {
        zcui::Lock lock;
        g_me.message = "Not every DMap or screen could be moved along: " + why + ".";
    }

    return ok;
}

// upstream's delete_quest_music: the entries proc picks go, the rest move up
void delete_where(const std::vector<bool> &del)
{
    std::map<int, int> ch;
    std::vector<Zq255Store::Music> keep;

    for(size_t q = 0; q < zq255.music.size(); ++q)
    {
        if(q < del.size() && del[q])
            ch[(int)q + 1] = 0;
        else
        {
            keep.push_back(zq255.music[q]);
            ch[(int)q + 1] = (int)keep.size();
        }
    }

    zq255.music.swap(keep);
    apply_changes(ch);
    saved = false;
}

// ---- where an enhanced music file is ------------------------------------------------------
//
// Upstream's zcmusic_find_for_quest (components/sound/zcmusic.cpp), as our player has it
// (q30_audio.cpp load_for_quest), so the editor takes exactly the files the players find: the
// program's folder, the quest's folder, the "<quest file name>_music" and "music" folders in
// each (the quest's name with its extension: "MyQuest.qst_music"), then every folder inside the
// quest's folder. Folder and file names compare without regard to case.

// The file name part of a path.
const char *name_part(const char *path)
{
    const char *name = path;

    for(const char *p = path; *p; ++p)
        if(*p == '/' || *p == '\\')
            name = p + 1;

    return name;
}

std::string with_slash(std::string dir)
{
    if(!dir.empty() && dir[dir.size() - 1] != '/' && dir[dir.size() - 1] != '\\')
        dir += '/';

    return dir;
}

// 'name' in 'dir' (dir ends in a slash), exactly or in another case; false if neither.
bool find_in_dir(const std::string &dir, const std::string &name, std::string &found)
{
    std::string exact = dir + name;

    if(exists(exact.c_str()))
    {
        found = exact;
        return true;
    }

    std::string pattern = dir + "*";
    struct al_ffblk info;

    if(al_findfirst(pattern.c_str(), &info, FA_ALL) != 0)
        return false;

    bool ok = false;

    do
    {
        if(stricmp(info.name, name.c_str()) == 0)
        {
            found = dir + info.name;
            ok = true;
            break;
        }
    }
    while(al_findnext(&info) == 0);

    al_findclose(&info);
    return ok;
}

// a folder (a path without its last slash)
bool is_folder(std::string path)
{
    while(path.size() > 1 && (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\'))
        path.erase(path.size() - 1);

    int attrib = 0;
    return !path.empty() && file_exists(path.c_str(), FA_ALL, &attrib) && (attrib & FA_DIREC);
}

// upstream's add_dir: a folder that exists, once
void add_dir(std::vector<std::string> &dirs, const std::string &dir)
{
    if(!is_folder(dir))
        return;

    const std::string d = with_slash(dir);

    for(size_t i = 0; i < dirs.size(); ++i)
        if(dirs[i] == d)
            return;

    dirs.push_back(d);
}

// a folder named 'name' (any case) inside 'dir'
void add_sub(std::vector<std::string> &dirs, const std::string &dir, const std::string &name)
{
    std::string found;

    if(find_in_dir(dir, name, found))
        add_dir(dirs, found);
}

// Every place 'filename' is in, in the order the players try them; empty if none.
std::vector<std::string> find_for_quest(const std::string &filename)
{
    std::vector<std::string> paths;

    if(filename.empty())
        return paths;

    char exe_path[2048];
    get_executable_name(exe_path, sizeof(exe_path));
    const std::string exe_dir(exe_path, name_part(exe_path) - exe_path);
    const char *quest = filepath ? filepath : "";
    std::string quest_dir(quest, name_part(quest) - quest);

    if(quest_dir.empty())
        quest_dir = "./";

    const std::string music_folder = std::string(name_part(quest)) + "_music";
    std::vector<std::string> dirs;
    add_dir(dirs, exe_dir);
    add_dir(dirs, quest_dir);
    add_sub(dirs, with_slash(exe_dir), music_folder);
    add_sub(dirs, with_slash(exe_dir), "music");
    add_sub(dirs, quest_dir, music_folder);
    add_sub(dirs, quest_dir, "music");

    // the last resort: the quest folder's own folders (a music pack unpacked into its own folder)
    std::string pattern = quest_dir + "*";
    struct al_ffblk info;

    if(al_findfirst(pattern.c_str(), &info, FA_ALL) == 0)
    {
        do
        {
            if((info.attrib & FA_DIREC) && info.name[0] != '.')
                add_dir(dirs, quest_dir + info.name);
        }
        while(al_findnext(&info) == 0);

        al_findclose(&info);
    }

    for(size_t i = 0; i < dirs.size(); ++i)
    {
        std::string found;

        if(find_in_dir(dirs[i], filename, found))
            paths.push_back(found);
    }

    return paths;
}

// upstream's zcmusic_load_for_quest: the first of those places whose file loads (NULL: none).
// *found_any says whether the file was found at all (upstream's "Error loading file" otherwise).
ZCMUSIC *load_music_file(const std::string &name, bool *found_any = NULL)
{
    const std::vector<std::string> paths = find_for_quest(name);

    if(found_any)
        *found_any = !paths.empty();

    for(size_t i = 0; i < paths.size(); ++i)
        if(ZCMUSIC *m = zcmusic_port_load_whole(paths[i].c_str()))
            return m;

    return NULL;
}

// upstream's silenceMusicPreview
void silence()
{
    stopMusic();
    zcui::Lock lock;
    g_me.playing = g_me.enh_playing = false;
    g_me.pos = -1;
}

// upstream's musicPreview: the enhanced file from its start, from a second before the loop's end
// (Preview Loop), or from where Pause stopped it, with the loop points set.
void preview_enhanced(bool loop)
{
    Zq255Store::Music m;
    int saved;
    {
        zcui::Lock lock;
        m = g_me.edit;
        saved = g_me.saved_pos;
    }

    silence();

    if(m.path.empty())
        return;

    zcmusic = load_music_file(m.path);
    zcui::Lock lock;
    g_me.saved_pos = 0;

    if(!zcmusic)
    {
        g_me.message = "The file can't be found or loaded, so it can't be previewed.";
        return;
    }

    const int t = zcmusic_get_tracks(zcmusic);
    zcmusic_play(zcmusic, midi_volume);

    if(t > 1)
        zcmusic_change_track(zcmusic, m.track < t ? m.track : t - 1);

    if(loop)
        zcmusic_set_curpos(zcmusic, m.loop_end - 10000 > 0 ? (int)(m.loop_end - 10000) : 0);

    if(saved > 0)
        zcmusic_set_curpos(zcmusic, saved);

    zcmusic_set_loop(zcmusic, m.loop_start / 10000.0, m.loop_end / 10000.0);
    g_me.playing = g_me.enh_playing = true;
    g_me.message.clear();
}

void handle(int req, int a, int b)
{
    MusicEd &e = g_me;

    switch(req)
    {
    case REQ_SAVE_EDIT:
    {
        Zq255Store::Music m;
        int slot;
        {
            zcui::Lock lock;
            m = e.edit;
            slot = e.edit_slot;
            e.edit_open = false;
            e.saved_pos = 0;
        }

        silence();

        if(slot >= (int)zq255.music.size())
        {
            if((int)zq255.music.size() < MAX_MUSIC)
                zq255.music.push_back(m);
        }
        else
            zq255.music[slot] = m;

        saved = false;
        break;
    }

    case REQ_DELETE:
    {
        std::vector<bool> del(zq255.music.size(), false);

        if(a >= 0 && a < (int)del.size())
        {
            del[a] = true;
            delete_where(del);
        }

        break;
    }

    case REQ_SWAP:
        if(a >= 0 && b >= 0 && a < (int)zq255.music.size() && b < (int)zq255.music.size() && a != b)
        {
            std::swap(zq255.music[a], zq255.music[b]);
            std::map<int, int> ch;
            ch[a + 1] = b + 1;
            ch[b + 1] = a + 1;
            apply_changes(ch);
            saved = false;
            zcui::Lock lock;
            e.sel = b;
        }

        break;

    case REQ_PASTE:
        if(b >= 0 && b < (int)zq255.music.size() && a >= 0 && a <= (int)zq255.music.size())
        {
            const Zq255Store::Music m = zq255.music[b];

            if(a == (int)zq255.music.size())
                zq255.music.push_back(m);
            else
                zq255.music[a] = m;

            saved = false;
        }

        break;

    case REQ_CLEAR:
        if(a >= 0 && a < (int)zq255.music.size())
        {
            zq255.music[a] = Zq255Store::Music();
            saved = false;
        }

        break;

    case REQ_CLEANUP:
    {
        std::vector<bool> del(zq255.music.size(), false);

        for(size_t q = 0; q < zq255.music.size(); ++q)
            del[q] = plays_nothing(zq255.music[q]);

        delete_where(del);
        break;
    }

    case REQ_LOAD:
    {
        {
            zcui::Lock lock;
            e.busy = true;
        }

        silence();
        std::string picked;

        if(getname("Load Enhanced Music", zcmusic_types, NULL, tmusicpath, false))
        {
            strcpy(tmusicpath, temppath);
            picked = get_filename(tmusicpath);
        }

        {
            zcui::Lock lock;
            e.busy = false;
            e.focus = true;
        }

        if(picked.empty())
            break;

        if(picked.size() > 255)
        {
            zcui::Lock lock;
            e.message = "Filename too long (>256 characters).";
            break;
        }

        // upstream's zcmusic_load_for_quest: the file must be where the players look for it; the
        // name it was found by is kept, with its type and track count
        bool found = false;
        ZCMUSIC *m = load_music_file(picked, &found);
        zcui::Lock lock;

        if(!m)
        {
            e.message = found ? "Error loading file." :
                        "File not found.\nEnhanced music files must be saved in the same folder as the quest, the ZC program "
                        "folder, or in a \"music\" or \"questname_music\" subfolder of the two.";
            break;
        }

        const int t = zcmusic_get_tracks(m);
        e.edit.path = m->filename;
        e.edit.track = 0;
        e.edit_tracks = t < 2 ? 1 : t;
        e.edit_type = m->type;
        zcmusic_unload_file(m);
        e.message.clear();
        break;
    }

    case REQ_INFO:
    {
        // upstream's load_enhanced_info, when the Edit window opens: the file's type and tracks
        std::string path;
        {
            zcui::Lock lock;
            path = e.edit.path;
        }

        int type = 0, tracks = 0;

        if(ZCMUSIC *m = path.empty() ? NULL : load_music_file(path))
        {
            type = m->type;
            tracks = zcmusic_get_tracks(m);
            tracks = tracks < 2 ? 1 : tracks;
            zcmusic_unload_file(m);
        }

        zcui::Lock lock;

        if(e.edit.path == path)
        {
            e.edit_type = type;
            e.edit_tracks = tracks;
        }

        break;
    }

    case REQ_PLAY_MIDI:
    {
        int midi;
        {
            zcui::Lock lock;
            midi = e.edit.midi;
            e.saved_pos = 0;
        }

        silence();
        void *data = NULL;

        if(midi >= 1 && midi <= MAXCUSTOMTUNES)
            data = customtunes[midi - 1].data;
        else if(midi > MAXCUSTOMTUNES && (size_t)(midi - MAXCUSTOMTUNES - 1) < zq255.high_tunes.size())
            data = zq255.high_tunes[midi - MAXCUSTOMTUNES - 1].data;

        zcui::Lock lock;

        if(midi < 0)
            e.message = "This internal MIDI cannot be previewed in the editor at this time.";
        else if(data)
        {
            play_midi((MIDI *)data, true);
            e.playing = true;
            e.message.clear();
        }
        else if(midi)
            e.message = "That tune is empty.";

        break;
    }

    case REQ_PLAY_ENH:
        preview_enhanced(false);
        break;

    case REQ_PLAY_LOOP:
        preview_enhanced(true);
        break;

    case REQ_PAUSE:
    {
        // upstream's Pause / Resume: a paused preview resumes where it stopped; Pause (or Stop,
        // for a MIDI) keeps where the enhanced file was
        int saved;
        {
            zcui::Lock lock;
            saved = e.saved_pos;
        }

        if(saved)
            preview_enhanced(false);
        else
        {
            // (a file without positions, a GME one, gives -10000: Resume then starts it again)
            const int at = zcmusic ? zcmusic_get_curpos(zcmusic) : 0;
            silence();
            zcui::Lock lock;
            e.saved_pos = at;
        }

        break;
    }

    case REQ_STOP:
        silence();
        {
            zcui::Lock lock;
            e.saved_pos = 0;
        }
        break;
    }

    if(req == REQ_SAVE_EDIT || req == REQ_DELETE || req == REQ_SWAP || req == REQ_PASTE || req == REQ_CLEAR || req == REQ_CLEANUP)
        refresh_view();
}

// the quest's tune names, read once per session on the game thread
void read_tune_names()
{
    std::vector<std::string> names;

    for(int i = 0; i < MAX_MIDIS_30; ++i)
    {
        const char *t = "";

        if(i < MAXCUSTOMTUNES)
            t = customtunes[i].title;
        else if((size_t)(i - MAXCUSTOMTUNES) < zq255.high_tunes.size())
            t = zq255.high_tunes[i - MAXCUSTOMTUNES].title;

        names.push_back(std::string(t, strnlen(t, 36)));
    }

    zcui::Lock lock;
    g_me.tune_names.swap(names);
}

} // namespace

// Audio > Music (3.0 quests): the music objects, at entry `start` (1-based; 0 or less: the first).
// *shown: false when the open quest has none of its own (the caller says why). *moved, when
// asked for: every music number this session moved (before -> after; 0 for a deleted one), so
// an editor holding one can follow it.
bool zq_edit_music_newer(int start, bool *shown, std::map<int, int> *moved)
{
    *shown = zq255_has_music_objects();

    if(!*shown || !zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_me.open)
            return false;

        g_me.open = true;
        g_me.close = g_me.busy = g_me.edit_open = false;
        g_me.focus = true;
        g_me.view = zq255.music;
        g_me.sel = start > 0 ? start - 1 : 0;
        g_me.req = REQ_NONE;
        g_me.message.clear();
        g_me.moved.clear();
    }

    read_tune_names();
    const int layer = zcui::add_layer(list_layer, NULL, 610);
    zcui::begin_session();

    for(;;)
    {
        zcui::wait_frame(100);
        bool done;
        int req, a, b;
        {
            zcui::Lock lock;
            done = g_me.close;
            req = g_me.req;
            a = g_me.req_a;
            b = g_me.req_b;
            g_me.req = REQ_NONE;
        }

        if(req == REQ_LOAD || req == REQ_INFO || req == REQ_PLAY_MIDI || req == REQ_PLAY_ENH || req == REQ_PLAY_LOOP ||
           req == REQ_PAUSE || req == REQ_STOP)
            handle(req, a, b);  // the file picker and the previews: they lock only around the window's state
        else if(req != REQ_NONE)
        {
            zcui::Lock lock;    // ZQuest's data changes under the lock: the record editor reads the list
            handle(req, a, b);
        }

        // the enhanced preview's position, for the Edit window (upstream's progress label)
        {
            const int pos = zcmusic ? zcmusic_get_curpos(zcmusic) : -1;
            zcui::Lock lock;
            g_me.pos = pos;

            if(!zcmusic)
                g_me.enh_playing = false;
        }

        if(done || !zcui::available())
            break;
    }

    silence();
    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_me.open = false;
    g_me.saved_pos = 0;

    if(moved)
        *moved = g_me.moved;

    return true;
}

// The music objects' names for other editors' dropdowns (the record editor's DMap and screen
// music): upstream's music_names(true, ...) text for entry n (1-based), "" past the end.
std::string zq_music_label(int n)
{
    char b[600];

    if(n == -1)
        return "Use Default (-001)";

    if(n == 0)
        return "(None) (000)";

    if(n < 1 || n > (int)zq255.music.size())
    {
        snprintf(b, sizeof(b), "(no such entry) (%05d)", n);
        return b;
    }

    const Zq255Store::Music &m = zq255.music[n - 1];

    if(m.name.empty())
        snprintf(b, sizeof(b), "zz%02d (%05d)", n, n);
    else
        snprintf(b, sizeof(b), "%s (%05d)", m.name.c_str(), n);

    return b;
}
