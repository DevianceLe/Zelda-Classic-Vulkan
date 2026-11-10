// Tables the 2.55 and 3.0 quest readers fill. See q30.h.

#include "precompiled.h" //always first

#include <string.h>

#include "q30.h"

namespace
{

std::map<int, Q30ScriptRef> g_combo_scripts;
std::map<int, int> g_msg_wrap_extra;
std::map<int, Q30ScriptRef> g_dmap_scripts[q30_dmap_kinds];
std::map<int, std::pair<int, int> > g_dmap_subscreens;    // overlay, map

void put(std::map<int, Q30ScriptRef> &table, int key, word script, const long *initd)
{
    if(!script)
    {
        table.erase(key);
        return;
    }

    Q30ScriptRef ref;
    ref.script = script;
    if(initd)
        memcpy(ref.initd, initd, sizeof(ref.initd));
    table[key] = ref;
}

const Q30ScriptRef *get(const std::map<int, Q30ScriptRef> &table, int key)
{
    std::map<int, Q30ScriptRef>::const_iterator it = table.find(key);
    return it == table.end() ? NULL : &it->second;
}

} // namespace

void q30_set_msg_wrap_extra(int str, int extra)
{
    if(extra)
        g_msg_wrap_extra[str] = extra;
    else
        g_msg_wrap_extra.erase(str);
}

int q30_msg_wrap_extra(int str)
{
    extern word quest_header_zelda_version;

    if(quest_header_zelda_version < 0x255) // a 2.53 quest loaded after a newer one
        return 0;

    std::map<int, int>::const_iterator it = g_msg_wrap_extra.find(str);
    return it == g_msg_wrap_extra.end() ? 0 : it->second;
}

void q30_clear_tables()
{
    g_combo_scripts.clear();

    for(int i = 0; i < q30_dmap_kinds; ++i)
        g_dmap_scripts[i].clear();

    g_dmap_subscreens.clear();
}

void q30_set_combo_script(int combo, word script, const long *initd)
{
    put(g_combo_scripts, combo, script, initd);
}

const Q30ScriptRef *q30_combo_script(int combo)
{
    return get(g_combo_scripts, combo);
}

void q30_set_dmap_script(int dmap, int kind, word script, const long *initd)
{
    if(kind >= 0 && kind < q30_dmap_kinds)
        put(g_dmap_scripts[kind], dmap, script, initd);
}

const Q30ScriptRef *q30_dmap_script(int dmap, int kind)
{
    return (kind >= 0 && kind < q30_dmap_kinds) ? get(g_dmap_scripts[kind], dmap) : NULL;
}

void q30_set_dmap_subscreens(int dmap, int overlay, int map)
{
    g_dmap_subscreens[dmap] = std::make_pair(overlay, map);

    if((overlay || map >= 0) && getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 dmapsub: dmap %d overlay %d map %d\n", dmap, overlay, map);
}

int q30_dmap_overlay_subscreen(int dmap)
{
    std::map<int, std::pair<int, int> >::const_iterator it = g_dmap_subscreens.find(dmap);
    return it == g_dmap_subscreens.end() ? 0 : it->second.first;
}

int q30_dmap_map_subscreen(int dmap)
{
    std::map<int, std::pair<int, int> >::const_iterator it = g_dmap_subscreens.find(dmap);
    return it == g_dmap_subscreens.end() ? -1 : it->second.second;
}
