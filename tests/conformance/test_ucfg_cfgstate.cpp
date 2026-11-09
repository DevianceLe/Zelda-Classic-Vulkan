/*
 * test_ucfg_cfgstate.cpp - config sources other than one file:
 * set_config_data, push/pop_config_state, override_config_file/data and
 * hooked sections. The player uses none of them; they are here because the
 * port provides them.
 *
 * The order matters and is deliberate: on the reference build,
 * override_config_file() followed by override_config_data(NULL) breaks the
 * next pop_config_state() (and everything after it in the process), and
 * pop_config_state() while an override is active restores the wrong
 * config. Neither is covered; the override tests come last.
 */
#include "conf.h"

#include <stdio.h>
#include <string.h>
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

int g_hook_int_calls = 0;

int hook_int(const char *name, int def)
{
    ++g_hook_int_calls;
    return (int)strlen(name) * 100 + def;
}

const char *hook_str(const char *name, const char *def)
{
    (void)def;
    return strcmp(name, "known") == 0 ? "hooked" : "other";
}

std::string g_hook_set;

void hook_set(const char *name, const char *value)
{
    g_hook_set += std::string(name) + "=" + (value ? value : "(null)") + ";";
}

} // namespace

CONF_TEST(ucfg_state_data)
{
    const char *const data = "[d]\nk = data\nn = 5\n[e]\nx = 1\n";

    set_config_data(data, (int)strlen(data));
    ctx.check_str("k", get_config_string("d", "k", "DEF"));
    ctx.check_int("n", get_config_int("d", "n", -1));
    set_config_string("d", "k", "changed");
    ctx.check_str("k_after_set", get_config_string("d", "k", "DEF"));
    set_config_int("d", "new", 3);
    ctx.check_int("new_ignored", get_config_int("d", "new", -1));
    set_config_string(0, "g", "x");
    ctx.check_str("global_ignored", get_config_string(0, "g", "DEF"));
    set_config_string("e", "x", 0);
    ctx.check_str("removed", get_config_string("e", "x", "DEF"));
    flush_config_file();
    set_config_data("", 0);
    ctx.check_str("empty", get_config_string("d", "k", "DEF"));
    set_config_data(data, 9);
    ctx.check_str("partial", get_config_string("d", "k", "DEF"));

    /* A file config set from data is written to no file. */
    std::string path = ctx.tmp("state_data.cfg");
    write_file(path, "[f]\nk = file\n");
    set_config_file(path.c_str());
    set_config_string("f", "k", "dirty");
    set_config_data(data, (int)strlen(data));
    ctx.checkf("file_after_data", "%s", read_text(path).c_str());
    set_config_file(ctx.tmp("parked.cfg").c_str());
}

CONF_TEST(ucfg_state_pushpop)
{
    std::string m = ctx.tmp("state_main.cfg"), o = ctx.tmp("state_other.cfg");
    write_file(m, "[s]\na = main\n");
    write_file(o, "[s]\na = other\n");
    set_config_file(m.c_str());
    set_config_string("s", "b", "main_dirty");
    push_config_state();
    ctx.checkf("main_file_after_push", "%s", read_text(m).c_str());
    set_config_file(o.c_str());
    ctx.check_str("pushed", get_config_string("s", "a", "DEF"));
    set_config_string("s", "a", "other2");
    pop_config_state();
    ctx.check_str("popped_a", get_config_string("s", "a", "DEF"));
    ctx.check_str("popped_b", get_config_string("s", "b", "DEF"));
    ctx.checkf("other_file_after_pop", "%s", read_text(o).c_str());

    /* Nested */
    push_config_state();
    set_config_file(o.c_str());
    push_config_state();
    set_config_data("[s]\na = data\n", 13);
    ctx.check_str("nested_data", get_config_string("s", "a", "DEF"));
    pop_config_state();
    ctx.check_str("nested_other", get_config_string("s", "a", "DEF"));
    pop_config_state();
    ctx.check_str("nested_main", get_config_string("s", "a", "DEF"));
    flush_config_file();
    ctx.checkf("main_file_end", "%s", read_text(m).c_str());
    set_config_file(ctx.tmp("parked.cfg").c_str());
}

CONF_TEST(ucfg_state_hooks)
{
    std::string m = ctx.tmp("state_hook.cfg");
    write_file(m, "[h]\nknown = file\nn = 4\n[x]\nk = 1\n");
    set_config_file(m.c_str());
    ctx.check_bool("hooked_before", config_is_hooked("h") != 0);
    hook_config_section("h", hook_int, hook_str, hook_set);
    ctx.check_bool("hooked", config_is_hooked("h") != 0);
    ctx.check_bool("hooked_brackets", config_is_hooked("[h]") != 0);
    ctx.check_bool("hooked_case", config_is_hooked("H") != 0);
    ctx.check_bool("other_hooked", config_is_hooked("x") != 0);
    ctx.check_str("str_known", get_config_string("h", "known", "DEF"));
    ctx.check_str("str_other", get_config_string("h", "zzz", "DEF"));
    g_hook_int_calls = 0;
    ctx.check_int("int", get_config_int("h", "n", 7));
    ctx.check_int("int_calls", g_hook_int_calls);
    ctx.check_int("hex", get_config_hex("h", "n", 7));
    g_hook_set.clear();
    set_config_string("h", "a", "1");
    set_config_int("h", "b", 2);
    set_config_string("h", "c", 0);
    ctx.check_str("set_calls", g_hook_set.c_str());
    ctx.check_str("unhooked_section", get_config_string("x", "k", "DEF"));
    hook_config_section("h", 0, 0, 0);
    ctx.check_bool("unhooked", config_is_hooked("h") != 0);
    ctx.check_str("after_unhook", get_config_string("h", "known", "DEF"));
    flush_config_file();
    ctx.checkf("file", "%s", read_text(m).c_str());
    set_config_file(ctx.tmp("parked.cfg").c_str());
}

CONF_TEST(ucfg_state_override)
{
    std::string main_cfg = ctx.tmp("ov_main.cfg");
    std::string over = ctx.tmp("ov_over.cfg");
    write_file(main_cfg, "[s]\na = main\nb = main\n");
    write_file(over, "[s]\na = over\n");
    set_config_file(main_cfg.c_str());
    override_config_file(over.c_str());
    ctx.check_str("a", get_config_string("s", "a", "DEF"));
    ctx.check_str("b", get_config_string("s", "b", "DEF"));
    set_config_string("s", "b", "set");
    set_config_string("s", "c", "new");
    set_config_string(0, "g", "glob");
    ctx.check_str("b_after", get_config_string("s", "b", "DEF"));
    flush_config_file();
    ctx.checkf("main_flushed", "%s", read_text(main_cfg).c_str());
    ctx.checkf("over_before_release", "%s", read_text(over).c_str());

    const char **names = 0;
    int n = list_config_entries("s", &names);
    std::string listed;

    for(int i = 0; i < n; ++i)
        listed += std::string(names[i]) + ";";

    free_config_entries(&names);
    ctx.check_str("entries", listed.c_str());
    n = list_config_sections(&names);
    listed.clear();

    for(int i = 0; i < n; ++i)
        listed += std::string(names[i]) + ";";

    free_config_entries(&names);
    ctx.check_str("sections", listed.c_str());

    override_config_file(NULL);
    ctx.checkf("over_released", "%s", read_text(over).c_str());
    ctx.check_str("b_released", get_config_string("s", "b", "DEF"));
    ctx.check_str("c_released", get_config_string("s", "c", "DEF"));

    /* Switching the main file keeps the override. */
    std::string main2 = ctx.tmp("ov_main2.cfg");
    write_file(main2, "[s]\na = main2\n");
    override_config_file(over.c_str());
    set_config_file(main2.c_str());
    ctx.check_str("override_kept", get_config_string("s", "c", "DEF"));
    ctx.check_str("main2", get_config_string("s", "d", "DEF"));
    override_config_file(NULL);
    ctx.check_str("main2_alone", get_config_string("s", "a", "DEF"));

    /* Override from data (last: see the top of the file). */
    const char *const odata = "[s]\nb = odata\n";
    override_config_data(odata, (int)strlen(odata));
    ctx.check_str("odata_b", get_config_string("s", "b", "DEF"));
    set_config_string("s", "b", "x");
    ctx.check_str("odata_b_set", get_config_string("s", "b", "DEF"));
    set_config_string("s", "q", "y");
    ctx.check_str("odata_q_new", get_config_string("s", "q", "DEF"));
    override_config_data(NULL, 0);
    ctx.check_str("odata_released", get_config_string("s", "b", "DEF"));
    set_config_file(ctx.tmp("parked.cfg").c_str());
}
