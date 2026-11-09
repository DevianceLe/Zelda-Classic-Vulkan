/*
 * test_ucfg_cfgexit.cpp - what allegro_exit() does with the config: the
 * player relies on it to write zc.cfg when it quits (after
 * save_game_configs() already flushed it). Its own program, because the
 * test shuts Allegro down and starts it again.
 */
#include "conf.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

namespace
{

void write_file(const std::string &path, const std::string &data)
{
    FILE *f = fopen(path.c_str(), "wb");

    if(f)
    {
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    }
}

std::string read_text(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");

    if(!f)
        return "(missing)";

    std::string out;
    char buf[1024];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);

    fclose(f);
    std::string r;

    for(size_t i = 0; i < out.size(); ++i)
    {
        unsigned char c = (unsigned char)out[i];

        if(c >= 0x20 && c < 0x7F && c != '\\')
            r += (char)c;
        else
            r += conf::nm("\\x%02x", c);
    }

    return r;
}

} // namespace

CONF_TEST(ucfg_exit_writeback)
{
    /* The config the program starts with (allegro.cfg next to the program,
     * absent here) is empty. */
    ctx.check_str("initial", get_config_string("system", "language", "DEF"));

    std::string a = ctx.tmp("exit_a.cfg");
    std::string b = ctx.tmp("exit_b.cfg");
    write_file(a, "[zeldadx]\nfullscreen = 0\n");
    write_file(b, "[x]\nk = 1\n");
    set_config_file(a.c_str());
    set_config_int("zeldadx", "fullscreen", 1);
    set_config_string("zeldadx", "#hidden", "h");
    flush_config_file();
    set_config_int("zeldadx", "resx", 640);       /* dirty again at exit */
    ctx.checkf("before_exit", "%s", read_text(a).c_str());

    allegro_exit();
    ctx.checkf("after_exit", "%s", read_text(a).c_str());

    int r = install_allegro(SYSTEM_NONE, &errno, atexit);
    ctx.check_int("reinstall", r);
    set_color_depth(8);
    set_color_conversion(COLORCONV_NONE);
    ctx.check_str("after_reinstall", get_config_string("zeldadx", "fullscreen", "DEF"));
    ctx.check_str("hidden_after_reinstall", get_config_string("zeldadx", "#hidden", "DEF"));

    /* An unchanged config is not written at exit. */
    set_config_file(b.c_str());
    remove(b.c_str());
    allegro_exit();
    ctx.checkf("unchanged_at_exit", "%s", read_text(b).c_str());
    r = install_allegro(SYSTEM_NONE, &errno, atexit);
    ctx.check_int("reinstall2", r);
    set_color_depth(8);
    set_color_conversion(COLORCONV_NONE);
    set_config_file(ctx.tmp("parked.cfg").c_str());
}
