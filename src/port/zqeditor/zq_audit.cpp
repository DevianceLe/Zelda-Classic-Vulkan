// zqeditor: the menu audit, a test driver's commands (ZQ_UI_SCRIPT, autotest.cpp). They run
// menu entries by name, so every editor can be opened on a quest without knowing where its
// menu is drawn, and they write down what each entry showed.
//
//   auditlist                 writes every menu entry's path to the log ("entry <path>")
//   auditopen Quest/Items     runs that entry as picking it from the menu does, and returns once
//                             what it opened has been on screen for a moment (or it has ended)
//   auditclose                presses Escape until the entry has ended
//   auditanswer PATH          the next entry's file selector takes PATH (then drive its dialogs
//                             with the script's own key and click commands)
//   auditfile PATH|Entry      runs the entry with PATH as the answer of its file selector, and
//                             presses Enter in the dialogs and boxes it shows (their defaults),
//                             so an export or import runs through; then as auditopen
//
// One line per entry goes to the file named by ZQ_AUDIT_LOG, tab separated: the path, the result
// (ended, stuck, disabled, missing), milliseconds, then what was opened, in order:
//   dlg:<title> (<n> objects)             a dialog
//   box:<title>|<its lines>|[<buttons>]   a message box
//   ui                                    an editor window or file selector of the newer UI
//   file:<the selector's title>           a file selector that was answered (auditfile)
//   changed:<parts>                       parts of the quest a save would now write differently
//                                         that it wouldn't have before the entry ran (an entry
//                                         that is only opened and closed should leave none)
//   page                                  nothing of the above after 2.5 s: a page with its own loop
// Without ZQ_AUDIT_LOG nothing here does anything.
//
// Known limit: after an answered selector has led to a whole quest being loaded (File > Open,
// Import Quest Template / Unencoded Quest), the next box in that session took no clicks or keys
// but Escape in tests. With the real selector (click its Path field, "down CTRL", "key A",
// "up CTRL", "type <path>", "key ENTER") the same steps work, so drive those entries that way.
//
// Written from scratch for zcport.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "zdefs.h"
#include "jwin.h"
#include "zcport/ui.h"
#include "zq_sections.h"
#include "zq_writers.h"

extern "C" {
extern void (*zcport_dialog_watch)(DIALOG *dialog, int opened);     // port/al/gui_engine.cpp
extern int (*zc_autotest_command)(const char *line);                // autotest.cpp
extern const char *(*zcport_filesel_answer)(const char *message);   // port/game/ui_filesel.cpp
void zc_autotest_key(const char *name, int ms);
}

// zquest.cpp: the entry's procedure (NULL: none; *disabled says whether it is there but greyed),
// or every entry's path when all is given.
int (*zq_audit_menu(const char *path, bool *disabled, std::vector<std::string> *all))();

namespace
{

enum { IDLE, ASKED, RUNNING, CHECKING, DONE };     // CHECKING: the entry has ended, its changes are being listed

std::mutex g_lock;
std::string g_log;
std::string g_request;                  // the path the main loop should run next
std::string g_path;                     // the entry in progress
std::vector<std::string> g_events;
std::atomic<int> g_state(IDLE);
std::atomic<int> g_dialogs(0);          // dialogs open in all, the editor's own included
std::atomic<int> g_opened(0);           // dialogs the entry has open
std::atomic<bool> g_on(false);
std::atomic<bool> g_stuck(false);
std::chrono::steady_clock::time_point g_t0;
std::string g_answer;                   // auditfile: the next file selector's answer ("" none)
std::string g_answer_given;             // kept alive for the selector
bool g_answer_any = false;              // auditanswer outside an entry: any next selector takes it

int ms_since(std::chrono::steady_clock::time_point t)
{
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
}

void nap(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string plain(const char *s)
{
    std::string out;

    for(; s && *s; ++s)
        out += ((unsigned char)*s < 32) ? ' ' : *s;

    return out;
}

void write_line(const std::string &line)
{
    FILE *f = fopen(g_log.c_str(), "ab");

    if(f)
    {
        fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }
}

// with g_lock held
void write_entry(const char *result, int ms)
{
    std::string line = g_path + "\t" + result + "\t" + std::to_string(ms);

    for(size_t i = 0; i < g_events.size(); ++i)
        line += "\t" + g_events[i];

    write_line(line);
}

// What a dialog is, for the log: its title, and a message box's lines and buttons too.
std::string describe(DIALOG *d)
{
    int n = 0;

    while(d[n].proc)
        ++n;

    std::string title = (n > 0 && d[0].proc == jwin_win_proc && d[0].dp) ? plain((const char *)d[0].dp) : std::string();

    if(n > 8)
        return "dlg:" + title + " (" + std::to_string(n) + " objects)";

    std::string lines, buttons;
    bool only_text = true;

    for(int i = 1; i < n; ++i)
    {
        if(d[i].flags & D_HIDDEN)
            continue;

        if((d[i].proc == jwin_ctext_proc || d[i].proc == jwin_text_proc) && d[i].dp)
        {
            std::string t = plain((const char *)d[i].dp);

            if(!t.empty())
                lines += (lines.empty() ? "" : " / ") + t;
        }
        else if(d[i].proc == jwin_button_proc && d[i].dp)
            buttons += (buttons.empty() ? "" : ",") + plain((const char *)d[i].dp);
        else
            only_text = false;
    }

    if(!only_text)
        return "dlg:" + title + " (" + std::to_string(n) + " objects)";

    return "box:" + title + "|" + lines + "|[" + buttons + "]";
}

// The entry's first file selector takes the path "auditfile" gave; later ones show.
const char *answer_selector(const char *message)
{
    std::lock_guard<std::mutex> hold(g_lock);

    // "auditanswer" outside an audited entry: the next file selector anywhere takes it (a UI
    // script driving a window's own menus)
    if(g_state != RUNNING && !g_answer_any)
        return NULL;

    if(g_answer.empty())
        return NULL;

    g_answer_any = false;

    g_events.push_back("file:" + plain(message));
    g_answer_given.swap(g_answer);
    g_answer.clear();
    return g_answer_given.c_str();
}

void watch(DIALOG *dialog, int opened)
{
    if(!opened)
    {
        --g_dialogs;

        if(g_state == RUNNING && g_opened > 0)
            --g_opened;

        return;
    }

    ++g_dialogs;

    if(g_state == RUNNING)
    {
        std::string what = describe(dialog);
        std::lock_guard<std::mutex> hold(g_lock);
        g_events.push_back(what);
        ++g_opened;
    }
}

// Escape until only the editor's own dialog is left (a stray prompt would swallow the next entry).
void clear_strays()
{
    for(int i = 0; i < 8 && g_dialogs > 1; ++i)
    {
        zc_autotest_key("ESC", 60);
        nap(250);
    }
}

void open_entry(const std::string &path, bool accept = false)
{
    // the editor's own dialog starts once the quest is loaded
    for(int i = 0; i < 6000 && g_dialogs < 1; ++i)
        nap(50);

    nap(100);
    clear_strays();
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_request = path;
        g_state = ASKED;
    }

    for(int i = 0; i < 400 && g_state == ASKED; ++i)
        nap(25);

    if(g_state == ASKED)
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_request.clear();
        write_line(path + "\tnot started\t0");
        g_state = IDLE;
        return;
    }

    // until what it opened has been up for a moment, or it has ended
    bool ui_noted = false;
    int shown = -1;
    int accepted = 0;

    for(int waited = 0; waited < 30000 && g_state == RUNNING; waited += 20)
    {
        const bool ui = g_opened == 0 && zcui::session_active();

        // auditfile: a dialog or box that has been up for a moment gets Enter (its default)
        if(accept && g_opened > 0 && shown >= 0 && waited - shown >= 400 && accepted < 12)
        {
            ++accepted;
            zc_autotest_key("ENTER", 60);
            shown = -1;
            nap(200);
            waited += 260;
            continue;
        }

        if(ui && !ui_noted)
        {
            std::lock_guard<std::mutex> hold(g_lock);
            g_events.push_back("ui");
            ui_noted = true;
        }

        if(g_opened > 0 || ui)
        {
            if(shown < 0)
                shown = waited;
            else if(waited - shown >= 700)
                break;
        }
        else
        {
            shown = -1;

            // a page of its own (the combo and tile pages run their own loop, with no dialog)
            if(waited >= 2500)
            {
                std::lock_guard<std::mutex> hold(g_lock);
                g_events.push_back("page");
                break;
            }
        }

        nap(20);
    }
}

void close_entry()
{
    for(int i = 0; i < 60 && g_state == RUNNING; ++i)
    {
        zc_autotest_key("ESC", 60);

        for(int k = 0; k < 15 && g_state == RUNNING; ++k)
            nap(20);
    }

    if(g_state == RUNNING)
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_stuck = true;
        write_entry("stuck", ms_since(g_t0));
        return;
    }

    for(int i = 0; i < 6000 && g_state == CHECKING; ++i)
        nap(20);

    nap(150);
    clear_strays();
    g_state = IDLE;
}

int command(const char *line)
{
    if(!g_on)
        return 0;

    if(!strncmp(line, "auditopen ", 10))
    {
        std::string path(line + 10);

        while(!path.empty() && (path[path.size() - 1] == '\r' || path[path.size() - 1] == ' '))
            path.erase(path.size() - 1);

        open_entry(path);
        return 1;
    }

    if(!strncmp(line, "auditfile ", 10))
    {
        std::string rest(line + 10);

        while(!rest.empty() && (rest[rest.size() - 1] == '\r' || rest[rest.size() - 1] == ' '))
            rest.erase(rest.size() - 1);

        const size_t bar = rest.find('|');

        if(bar == std::string::npos)
            return 1;

        {
            std::lock_guard<std::mutex> hold(g_lock);
            g_answer = rest.substr(0, bar);
        }
        open_entry(rest.substr(bar + 1), true);
        {
            std::lock_guard<std::mutex> hold(g_lock);
            g_answer.clear();
        }
        return 1;
    }

    if(!strncmp(line, "auditanswer ", 12))
    {
        std::string path(line + 12);

        while(!path.empty() && (path[path.size() - 1] == '\r' || path[path.size() - 1] == ' '))
            path.erase(path.size() - 1);

        std::lock_guard<std::mutex> hold(g_lock);
        g_answer = path;
        g_answer_any = g_state != RUNNING;
        return 1;
    }

    if(!strncmp(line, "auditclose", 10))
    {
        close_entry();
        return 1;
    }

    if(!strncmp(line, "auditlist", 9))
    {
        open_entry("*");
        g_state = IDLE;
        return 1;
    }

    return 0;
}

struct Start
{
    Start()
    {
        const char *e = getenv("ZQ_AUDIT_LOG");

        if(e && e[0])
        {
            g_log = e;
            g_on = true;
            zcport_dialog_watch = watch;
            zc_autotest_command = command;
            zcport_filesel_answer = answer_selector;
        }
    }
} g_start;

} // namespace

// The parts of the open quest that a save would now write differently from its file.
static std::string changed_parts()
{
    std::string out;
    std::vector<std::string> absent;
    zq_absent_changed(absent);

    for(size_t i = 0; i < absent.size(); ++i)
        out += (out.empty() ? "" : ",") + absent[i];

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_part_changed(zq_quest_file.parts[i]))
            out += (out.empty() ? "" : ",") + zq_part_name(zq_quest_file.parts[i]);

    return out;
}

// The editor's main loop: runs the entry a script asked for.
void zq_audit_poll()
{
    if(!g_on || g_state != ASKED)
        return;

    std::string path;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        path.swap(g_request);
        g_path = path;
        g_events.clear();
    }

    if(path.empty())
        return;

    if(path == "*")
    {
        std::vector<std::string> all;
        zq_audit_menu(NULL, NULL, &all);
        std::lock_guard<std::mutex> hold(g_lock);

        for(size_t i = 0; i < all.size(); ++i)
            write_line("entry " + all[i]);

        g_state = DONE;
        return;
    }

    bool disabled = false;
    int (*proc)() = zq_audit_menu(path.c_str(), &disabled, NULL);

    if(!proc || disabled)
    {
        std::lock_guard<std::mutex> hold(g_lock);
        write_entry(disabled ? "disabled" : "missing", 0);
        g_state = DONE;
        return;
    }

    static std::string before;
    static bool have_before = false;

    if(!have_before)
    {
        // what counts as changed before any entry ran (nothing should)
        before = changed_parts();
        have_before = true;
        std::lock_guard<std::mutex> hold(g_lock);
        write_line("at the start\tchanged:" + (before.empty() ? std::string("(nothing)") : before));
    }

    g_opened = 0;
    g_stuck = false;
    g_t0 = std::chrono::steady_clock::now();
    g_state = RUNNING;
    proc();
    const int ms = ms_since(g_t0);
    g_state = CHECKING;
    const std::string after = changed_parts();
    {
        std::lock_guard<std::mutex> hold(g_lock);

        if(after != before)
            g_events.push_back("changed:" + (after.empty() ? std::string("(nothing any more)") : after));

        write_entry(g_stuck ? "ended late" : "ended", ms);
    }
    before = after;
    g_state = DONE;
}
