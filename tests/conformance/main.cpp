/*
 * main.cpp - entry point of every conformance program (conf_<name>_ref and
 * conf_<name>_port).
 *
 *   conf_<name>_<side> [--data DIR] [--quests DIR] [--filter SUBSTR] [--test NAME]...
 *                      [--out FILE] [--tmp DIR] [--dump DIR] [--expand]
 *                      [--system none|auto] [--jitter SEED] [--quiet] [--list]
 *
 * Exit codes: 0 finished (whatever the results), 2 bad arguments,
 * 3 abort(), 4 install_allegro failed, 5 Allegro ASSERT (reference side),
 * anything larger: an unhandled exception code (e.g. 0xC0000005).
 */
#include "conf.h"
#include "conf_internal.h"

/* We are a console program on both sides: whatever the headers did to main
 * (Allegro's magic main, SDL_main, ...), this is the real one. */
#ifdef main
#undef main
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define CONF_EXIT _exit
#else
#include <unistd.h>
#define CONF_EXIT _exit
#endif

#ifndef CONF_DEFAULT_DATA_DIR
#define CONF_DEFAULT_DATA_DIR "."
#endif
#ifndef CONF_DEFAULT_QUESTS_DIR
#define CONF_DEFAULT_QUESTS_DIR "."
#endif

using namespace conf::detail;

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "  --data DIR        game data directory (default: %s)\n"
            "  --quests DIR      custom quests directory (default: %s)\n"
            "  --filter SUBSTR   run only tests whose name contains SUBSTR\n"
            "  --test NAME       run only test NAME (repeatable, exact match)\n"
            "  --out FILE        also write the check lines to FILE\n"
            "  --tmp DIR         scratch directory for files tests write (default: conf_tmp)\n"
            "  --dump DIR        write bitmap/memory dumps of checks into DIR\n"
            "  --expand          print every item of sequence checks\n"
            "  --system none|auto  system driver for install_allegro\n"
            "  --jitter SEED     disturb the heap before each test (determinism check)\n"
            "  --quiet           do not echo check lines to stdout\n"
            "  --list            list the selected test names and exit\n",
            prog, CONF_DEFAULT_DATA_DIR, CONF_DEFAULT_QUESTS_DIR);
}

#ifdef CONF_SIDE_REF
/* Allegro's default ASSERT action can open a message box; report and leave. */
static int conf_assert_handler(const char *msg)
{
    conf_crash_note(msg ? msg : "ASSERT", 5);
    CONF_EXIT(5); /* does not return */
}
#endif

static void run_hooks(conf::HookPhase phase)
{
    std::vector<HookEntry> &h = hooks();

    for(size_t i = 0; i < h.size(); ++i)
        if(h[i].phase == phase)
            h[i].fn();
}

/* State every test starts from (the game's settings). */
static void reset_state()
{
    set_color_depth(8);
    set_color_conversion(COLORCONV_NONE);
    packfile_password(NULL);
}

/*
 * --jitter SEED: shift the heap layout before each test and leave non-zero
 * garbage in freed blocks, so a test (or port code) that reads memory it does
 * not own gives different output from a normal run. Allocates through Allegro
 * too, because the reference DLL has its own C runtime heap.
 */
static void heap_jitter(unsigned seed)
{
    static std::vector<void *> keep_mem;
    static std::vector<BITMAP *> keep_bmp;
    conf::Rng r(seed);

    for(int i = 0; i < 48; ++i)
    {
        size_t n = 16 + (size_t)r.range(0, 65536);
        void *p = malloc(n);

        if(p)
        {
            memset(p, r.range(1, 255), n);

            if(i & 1)
                free(p);
            else
                keep_mem.push_back(p);
        }

        BITMAP *b = create_bitmap_ex(8, 1 + r.range(0, 255), 1 + r.range(0, 63));

        if(b)
        {
            clear_to_color(b, r.range(1, 255));

            if(i & 1)
                destroy_bitmap(b);
            else
                keep_bmp.push_back(b);
        }
    }
}

int main(int argc, char **argv)
{
    install_crash_handlers();

    Options &o = options();
    o.data_dir = CONF_DEFAULT_DATA_DIR;
    o.quests_dir = CONF_DEFAULT_QUESTS_DIR;
    o.tmp_dir = "conf_tmp";

    std::string out_path, filter, system_arg;
    std::vector<std::string> only;
    bool list = false;
    unsigned jitter = 0;

    for(int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        bool has_value = i + 1 < argc;

        if(a == "--help" || a == "-h")
        {
            usage(argv[0]);
            return 0;
        }
        else if(a == "--expand")
            o.expand = true;
        else if(a == "--list")
            list = true;
        else if(a == "--quiet")
            o.echo = false;
        else if(has_value && a == "--data")
            o.data_dir = argv[++i];
        else if(has_value && a == "--quests")
            o.quests_dir = argv[++i];
        else if(has_value && a == "--filter")
            filter = argv[++i];
        else if(has_value && a == "--test")
            only.push_back(argv[++i]);
        else if(has_value && a == "--out")
            out_path = argv[++i];
        else if(has_value && a == "--tmp")
            o.tmp_dir = argv[++i];
        else if(has_value && a == "--dump")
            o.dump_dir = argv[++i];
        else if(has_value && a == "--system")
            system_arg = argv[++i];
        else if(has_value && a == "--jitter")
            jitter = (unsigned)strtoul(argv[++i], 0, 0);
        else
        {
            fprintf(stderr, "%s: bad argument '%s'\n", argv[0], a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    /* select tests (registration order) */
    std::vector<TestEntry> &all = tests();
    std::vector<TestEntry> sel;

    for(size_t k = 0; k < only.size(); ++k)
    {
        bool found = false;

        for(size_t i = 0; i < all.size(); ++i)
            if(only[k] == all[i].name)
                found = true;

        if(!found)
        {
            fprintf(stderr, "%s: no test named '%s'\n", argv[0], only[k].c_str());
            return 2;
        }
    }

    for(size_t i = 0; i < all.size(); ++i)
    {
        if(!only.empty())
        {
            bool wanted = false;

            for(size_t k = 0; k < only.size(); ++k)
                if(only[k] == all[i].name)
                    wanted = true;

            if(!wanted)
                continue;
        }

        if(!filter.empty() && !strstr(all[i].name, filter.c_str()))
            continue;

        sel.push_back(all[i]);
    }

    if(list)
    {
        for(size_t i = 0; i < sel.size(); ++i)
            printf("%s\n", sel[i].name);

        return 0;
    }

    int system_id = default_system_set() ? default_system() : SYSTEM_NONE;

    if(system_arg == "none")
        system_id = SYSTEM_NONE;
    else if(system_arg == "auto")
        system_id = SYSTEM_AUTODETECT;
    else if(!system_arg.empty())
    {
        fprintf(stderr, "%s: --system must be none or auto\n", argv[0]);
        return 2;
    }

    if(!out_path.empty())
    {
        o.out = fopen(out_path.c_str(), "w");

        if(!o.out)
        {
            fprintf(stderr, "%s: cannot write '%s'\n", argv[0], out_path.c_str());
            return 2;
        }
    }

    if(!make_dirs(o.tmp_dir))
    {
        fprintf(stderr, "%s: cannot create tmp dir '%s'\n", argv[0], o.tmp_dir.c_str());
        return 2;
    }

    if(!o.dump_dir.empty() && !make_dirs(o.dump_dir))
    {
        fprintf(stderr, "%s: cannot create dump dir '%s'\n", argv[0], o.dump_dir.c_str());
        return 2;
    }

    char header[128];
    sprintf(header, "# conformance side=%s tests=%u", conf::side(), (unsigned)sel.size());
    write_line(header);

    o.current_test = "(init)";
    run_hooks(conf::HOOK_PRE_INIT);

    if(install_allegro(system_id, &errno, atexit) != 0)
    {
        write_line(std::string("init/install_allegro: FAIL ") + allegro_error);
        return 4;
    }

    /* Allegro may have installed handlers of its own; ours come last. */
    install_crash_handlers();
#ifdef CONF_SIDE_REF
    register_assert_handler(conf_assert_handler);
#endif

    reset_state();
    run_hooks(conf::HOOK_POST_INIT);

    for(size_t i = 0; i < sel.size(); ++i)
    {
        o.current_test = sel[i].name;

        if(jitter)
            heap_jitter(jitter + (unsigned)i * 7919u);

        reset_state();
        conf::Ctx ctx(sel[i].name);
        sel[i].fn(ctx);
        char end[256];
        sprintf(end, "# end %.180s checks=%d", sel[i].name, ctx.checks());
        write_line(end);
    }

    char done[128];
    sprintf(done, "# done tests=%u checks=%ld", (unsigned)sel.size(), o.total_checks);
    write_line(done);

    o.current_test = "(allegro_exit)";
    allegro_exit();

    if(o.out)
    {
        fclose(o.out);
        o.out = 0;
    }

    return 0;
}
