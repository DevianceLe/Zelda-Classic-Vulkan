/*
 * conf_internal.h - framework state shared by conf.cpp and main.cpp.
 * Tests do not include this.
 */
#ifndef ZC_CONFORMANCE_CONF_INTERNAL_H
#define ZC_CONFORMANCE_CONF_INTERNAL_H

#include <stdio.h>
#include <string>
#include <vector>

namespace conf
{
namespace detail
{

struct Options
{
    std::string data_dir;
    std::string quests_dir;
    std::string tmp_dir;
    std::string dump_dir;              /* empty: no dumps */
    bool expand;                       /* print every Seq item */
    bool echo;                         /* also print check lines to stdout */
    FILE *out;                         /* --out file, or NULL */
    long total_checks;
    std::string current_test;          /* for the crash note */
    Options() : expand(false), echo(true), out(0), total_checks(0) {}
};

struct TestEntry
{
    const char *name;
    TestFn fn;
    const char *file;
};

struct HookEntry
{
    HookPhase phase;
    HookFn fn;
};

Options &options();
std::vector<TestEntry> &tests();
std::vector<HookEntry> &hooks();
int &default_system();                 /* 0 = not overridden by a test file */
bool &default_system_set();

/* Writes one output line (adds '\n') to --out and, if echo, stdout; flushes. */
void write_line(const std::string &line);

/* Creates a directory and its parents; returns true if it exists afterwards. */
bool make_dirs(const std::string &path);

/* Implemented in conf_crash.cpp (no Allegro there). */
void install_crash_handlers();

} /* namespace detail */
} /* namespace conf */

/* Called from the crash handlers; writes a note and flushes. */
extern "C" void conf_crash_note(const char *what, unsigned long code);

#endif
