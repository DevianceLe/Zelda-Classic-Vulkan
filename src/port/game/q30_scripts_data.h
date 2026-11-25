// The editor data objects 2.55 and 3.0 scripts read and write: bottle types and bottle shops,
// DMap names, titles, intros and music files, itemdata, npcdata and messagedata fields, a
// mapdata's FFCs, doors, ExDoors and screen states, mapdata->isSolidLayer(), and weapondata
// objects. Ported from upstream 3.0 (scripting/types/*.cpp, ffscript.cpp), with its bounds,
// scaling and side effects, onto the data this engine and the 2.55/3.0 readers keep.
// Included once, from q30_scripts_cmds.h: zasm30_data_reg runs first in the register reads and
// writes (q30_scripts_regs.h), zasm30_data_command first in zasm30_command.

#ifndef ZC_Q30_SCRIPTS_DATA_H
#define ZC_Q30_SCRIPTS_DATA_H

extern char *guy_string[];
FONT *get_zc_font(int index);       // script_drawing.cpp
void update_subscreens(int dmap);
byte q30_chart_get(int di);         // q30_triggers.cpp: 3.0's map chart, dmap << 7 | screen
void q30_chart_set(int di, byte v);
extern word animated_combo_table4[MAXCOMBOS][2];   // tiles.cpp: [0] combo, [1] clock
extern word animated_combos;

// ---- internal arrays (upstream's zc/scripting/arrays.h) ----------------------------------------

// An element of an internal array of `size`: false (logged) when the index is out of range;
// under qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX an array upstream marks compatBoundIndex binds
// it into range instead.
static bool z30d_index(long index, long size, bool compat_bound, long &out)
{
    if(zasm30_size_probe)       // (an internal array's size: q30_scripts_arrays.h)
    {
        zasm30_probed_size = size;
        return false;
    }

    if(compat_bound && q30_compat_rule(Z30_QR_OLD_ARRAY_INDEX) && !(index >= 0 && index < size))
    {
        if(size <= 0)
            return false;

        out = vbound(index, 0L, size - 1);
        return true;
    }

    return z30_array_index(index, size, out);
}

static long z30d_bound(long v, long lo, long hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// ---- bottle types and bottle shops (bottledata.cpp, bottleshopdata.cpp) -----------------------

enum { Z30D_BT_PERC0 = 0x01, Z30D_BT_CURESHJINX = 0x08, Z30D_BT_CURESWJINX = 0x10, Z30D_BT_CUREITJINX = 0x20,
       Z30D_BT_ALLOWIFFULL = 0x40, Z30D_BT_AUTOONDEATH = 0x80 };

// bottledata refs are the type (1-64), bottleshopdata refs the shop + 1 (1-256)
static Q30BottleType *z30d_bottle()
{
    const long ref = Z30_REF(BOTTLETYPE);
    return (ref > 0 && ref <= 64) ? q30_bottle_type_edit((int)ref - 1) : NULL;
}

static Q30BottleShop *z30d_bshop()
{
    const long ref = Z30_REF(BOTTLESHOP);
    return (ref > 0 && ref <= 256) ? q30_bottle_shop_edit((int)ref - 1) : NULL;
}

static bool z30d_bottle_reg(const long arg, long &ret, const long value, const bool write)
{
    const long v = value / 10000;
    long n;

    switch(arg)
    {
    case Z30R_BOTTLENEXT:
    {
        Q30BottleType *b = z30d_bottle();

        if(write)
        {
            if(b)
                b->next_type = (byte)z30d_bound(v, 0, 64);
        }
        else
            ret = b ? b->next_type * 10000 : -10000;
        return true;
    }
    case Z30R_BOTTLEAMOUNT:
    case Z30R_BOTTLECOUNTER:
    case Z30R_BOTTLEPERCENT:
    {
        Q30BottleType *b = z30d_bottle();

        if(!b)
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!z30_array_index(Z30_INDEX, 3, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        if(arg == Z30R_BOTTLEAMOUNT)
        {
            if(write)
                b->amount[n] = (word)z30d_bound(v, 0, 65535);
            else
                ret = b->amount[n] * 10000;
        }
        else if(arg == Z30R_BOTTLECOUNTER)
        {
            if(write)
                b->counter[n] = (signed char)z30d_bound(v, -1, Q30_MAX_COUNTERS - 1);
            else
                ret = b->counter[n] * 10000;
        }
        else if(write)
        {
            if(value)
                b->flags |= (byte)(Z30D_BT_PERC0 << n);
            else
                b->flags &= (byte)~(Z30D_BT_PERC0 << n);
        }
        else
            ret = (b->flags & (Z30D_BT_PERC0 << n)) ? 10000 : 0;
        return true;
    }
    case Z30R_BOTTLEFLAGS:
    {
        static const byte bits[5] = { Z30D_BT_AUTOONDEATH, Z30D_BT_ALLOWIFFULL, Z30D_BT_CURESWJINX, Z30D_BT_CUREITJINX,
                                      Z30D_BT_CURESHJINX };
        Q30BottleType *b = z30d_bottle();

        if(!b || !z30_array_index(Z30_INDEX, 5, n))
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!write)
            ret = (b->flags & bits[n]) ? 10000 : 0;
        else if(value)
            b->flags |= bits[n];
        else
            b->flags &= (byte)~bits[n];
        return true;
    }
    case Z30R_BSHOPCOMBO:
    case Z30R_BSHOPCSET:
    case Z30R_BSHOPFILL:
    case Z30R_BSHOPPRICE:
    case Z30R_BSHOPSTR:
    {
        Q30BottleShop *s = z30d_bshop();

        if(!s)
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!z30_array_index(Z30_INDEX, 3, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        switch(arg)
        {
        // (upstream binds a shop's combo to the counter numbers, -1 becoming 65535 in the word)
        case Z30R_BSHOPCOMBO: if(write) s->comb[n] = (word)z30d_bound(v, -1, Q30_MAX_COUNTERS - 1); else ret = s->comb[n] * 10000; break;
        case Z30R_BSHOPCSET: if(write) s->cset[n] = (byte)z30d_bound(v, 0, 11); else ret = s->cset[n] * 10000; break;
        case Z30R_BSHOPFILL: if(write) s->fill[n] = (byte)z30d_bound(v, 0, 64); else ret = s->fill[n] * 10000; break;
        case Z30R_BSHOPPRICE: if(write) s->price[n] = (word)z30d_bound(v, 0, 65535); else ret = s->price[n] * 10000; break;
        default: if(write) s->str[n] = (word)z30d_bound(v, 0, 65535); else ret = s->str[n] * 10000; break;
        }
        return true;
    }
    default:
        return false;
    }
}

// ---- DMaps (dmapdata.cpp; the string commands in ffscript.cpp) -------------------------------

static bool z30d_dmap_ok(long id)
{
    if(id < 0 || id >= MAXDMAPS)
    {
        Z_scripterrlog("Invalid DMap ID: %ld\n", id);
        return false;
    }

    return true;
}

static std::string z30d_chars(const char *s, size_t max)
{
    size_t n = 0;

    while(n < max && s[n])
        ++n;

    return std::string(s, n);
}

static std::string z30d_dmap_title(int id)
{
    const char *full = q30_dmap_title(id);
    return full ? std::string(full) : z30d_chars(DMaps[id].title, 20);
}

// upstream find_or_make_dmap_music, then the object's enhanced path: a DMap without a music
// object here plays 2.53's file name, which is what upstream's object for it holds
static std::string z30d_dmap_music_path(int id)
{
    const long m = q30_dmap_music(id);
    return m > 0 ? std::string(q30_musicdata_path(m)) : z30d_chars(DMaps[id].tmusic, 55);
}

static void z30d_set_dmap_music_path(int id, const std::string &path)
{
    const long m = q30_dmap_music(id);

    if(m > 0)
        q30_musicdata_set_path_quiet(m, path.c_str());
    else
    {
        memset(DMaps[id].tmusic, 0, sizeof(DMaps[id].tmusic));
        strncpy(DMaps[id].tmusic, path.c_str(), sizeof(DMaps[id].tmusic) - 1);
    }
}

// A DMap's gravity and terminal velocity (DMAP v23+ with dmfCUSTOM_GRAVITY; zfix values, x10000),
// which only scripts look at here; the reader keeps them (q255_maps.cpp).
static std::map<int, std::pair<long, long> > zasm30_dmap_gravity;

void q30_set_dmap_gravity(int dmap, long gravity, long terminal_v)
{
    if(dmap < 0)
        zasm30_dmap_gravity.clear();
    else
        zasm30_dmap_gravity[dmap] = std::make_pair(gravity, terminal_v);
}

enum { Z30D_QR_SCRIPTS_SHARE_INITD = 604 };     // upstream's qr_SCRIPTS_SHARE_INITD

// A DMap's script slot: its script and InitD (none: 0s).
static Q30ScriptRef z30d_dmap_slot(long id, int kind)
{
    const Q30ScriptRef *r = unsigned(id) < MAXDMAPS ? q30_dmap_script((int)id, kind) : NULL;
    return r ? *r : Q30ScriptRef();
}

// the rest of dmapdata.cpp: the fields 2.53's dmap has (map tiles, track), the tables beside it
// (floor, mirror, subscreens, intro string, music object, camera, gravity), the subscreen scripts
// and Charted[] (game->bmaps)
static bool z30d_dmap_more_reg(const long arg, long &ret, const long value, const bool write)
{
    const long id = Z30_REF(DMAPDATA);
    const bool ok = unsigned(id) < MAXDMAPS;
    long n = 0;

    switch(arg)
    {
    case Z30R_DMAPDATAFLOOR:
        if(!write) ret = ok ? (byte)q30_dmap_floor((int)id) * 10000 : 0;
        else if(z30d_dmap_ok(id)) q30_set_dmap_floor((int)id, (byte)(value / 10000));
        return true;
    case Z30R_DMAPDATAINTROSTRINGID:
        if(!write) ret = ok ? q30_dmap_intro_string((int)id) * 10000 : 0;
        else if(z30d_dmap_ok(id))
            q30_set_dmap_texts((int)id, z30d_dmap_title((int)id).c_str(), (long)(dword)(value / 10000));
        return true;
    case Z30R_DMAPDATAMIRRDMAP:
        if(!write) ret = ok ? q30_dmap_mirror((int)id) * 10000 : 0;
        else if(z30d_dmap_ok(id)) q30_set_dmap_mirror((int)id, (int)z30d_bound(value / 10000, -1, MAXDMAPS - 1));
        return true;
    case Z30R_DMAPDATASUBSCRO:
    case Z30R_DMAPDATASUBSCRM:
    {
        const bool map = arg == Z30R_DMAPDATASUBSCRM;

        if(!write)
        {
            ret = ok ? (byte)(map ? q30_dmap_map_subscreen((int)id) : q30_dmap_overlay_subscreen((int)id)) * 10000 : 0;
            return true;
        }

        if(!z30d_dmap_ok(id))
            return true;

        int overlay = q30_dmap_overlay_subscreen((int)id), mapsub = q30_dmap_map_subscreen((int)id);
        const int v = (byte)(value / 10000);
        const bool changed = map ? mapsub != v : overlay != v;
        (map ? mapsub : overlay) = v;
        q30_set_dmap_subscreens((int)id, overlay, mapsub);

        if(changed && id == currdmap)
            update_subscreens(-1);
        return true;
    }
    case Z30R_DMAPDATAMINIMAPTILE: case Z30R_DMAPDATAMINIMAPCSET:
    case Z30R_DMAPDATALARGEMAPTILE: case Z30R_DMAPDATALARGEMAPCSET:
    {
        if(!ok || !z30d_index(Z30_INDEX, 2, false, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        dmap &d = DMaps[id];
        const bool tile = arg == Z30R_DMAPDATAMINIMAPTILE || arg == Z30R_DMAPDATALARGEMAPTILE;
        const bool mini = arg == Z30R_DMAPDATAMINIMAPTILE || arg == Z30R_DMAPDATAMINIMAPCSET;

        if(tile)
        {
            tile_id &t = mini ? (n ? d.minimap_2_tile : d.minimap_1_tile) : (n ? d.largemap_2_tile : d.largemap_1_tile);
            if(!write) ret = t * 10000;
            else t = (tile_id)(value / 10000);
        }
        else
        {
            byte &c = mini ? (n ? d.minimap_2_cset : d.minimap_1_cset) : (n ? d.largemap_2_cset : d.largemap_1_cset);
            if(!write) ret = c * 10000;
            else c = (byte)(value / 10000);
        }
        return true;
    }
    // the DMap's music object (upstream's find_or_make_dmap_music); a DMap that plays 2.53's
    // fields has only its track here
    case Z30R_DMAPDATALOOPSTART: case Z30R_DMAPDATALOOPEND: case Z30R_DMAPDATAXFADEIN:
    case Z30R_DMAPDATAXFADEOUT: case Z30R_DMAPDATAMUISCTRACK:
    {
        const int field = arg == Z30R_DMAPDATALOOPSTART ? q30md_LOOPSTART : arg == Z30R_DMAPDATALOOPEND ? q30md_LOOPEND
                          : arg == Z30R_DMAPDATAXFADEIN ? q30md_XFADEIN : arg == Z30R_DMAPDATAXFADEOUT ? q30md_XFADEOUT : q30md_TRACK;
        const long m = ok ? q30_dmap_music((int)id) : 0;

        if(!write)
            ret = m > 0 ? q30_musicdata_get(m, field) : (ok && field == q30md_TRACK) ? DMaps[id].tmusictrack * 10000 : 0;
        else if(m > 0)
            q30_musicdata_set(m, field, field == q30md_TRACK ? (byte)(value / 10000) * 10000L : value);
        else if(ok && field == q30md_TRACK)
            DMaps[id].tmusictrack = (byte)(value / 10000);
        return true;
    }
    case Z30R_DMAPDATA_MUSIC:
        if(!write) ret = ok ? q30_dmap_music((int)id) : 0;
        else if(z30d_dmap_ok(id)) q30_set_dmap_music_script((int)id, value);
        return true;
    case Z30R_DMAPDATA_GRAVITY_STRENGTH:
    case Z30R_DMAPDATA_TERMINAL_VELOCITY:
    {
        const bool g = arg == Z30R_DMAPDATA_GRAVITY_STRENGTH;

        if(!write)
        {
            std::map<int, std::pair<long, long> >::const_iterator it = zasm30_dmap_gravity.find((int)id);
            ret = (!ok || it == zasm30_dmap_gravity.end()) ? 0 : g ? it->second.first : it->second.second;
        }
        else if(z30d_dmap_ok(id))
        {
            std::pair<long, long> &p = zasm30_dmap_gravity[(int)id];
            (g ? p.first : p.second) = value;
        }
        return true;
    }
    // the DMap's own camera follow settings (in effect with dmfCUSTOM_VIEWPORT_FOLLOW; q30_regions.cpp)
    case Z30R_DMAPDATA_VIEWPORT_DEADZONE_WIDTH: case Z30R_DMAPDATA_VIEWPORT_DEADZONE_HEIGHT:
    case Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_X: case Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_Y:
    case Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_SPEED: case Z30R_DMAPDATA_VIEWPORT_RECENTER_SPEED:
    case Z30R_DMAPDATA_VIEWPORT_RECENTER_DELAY:
    {
        const int which = arg == Z30R_DMAPDATA_VIEWPORT_DEADZONE_WIDTH ? Q30_VP_DEADZONE_W
                          : arg == Z30R_DMAPDATA_VIEWPORT_DEADZONE_HEIGHT ? Q30_VP_DEADZONE_H
                          : arg == Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_X ? Q30_VP_LOOKAHEAD_X
                          : arg == Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_Y ? Q30_VP_LOOKAHEAD_Y
                          : arg == Z30R_DMAPDATA_VIEWPORT_LOOKAHEAD_SPEED ? Q30_VP_LOOKAHEAD_SPEED
                          : arg == Z30R_DMAPDATA_VIEWPORT_RECENTER_SPEED ? Q30_VP_RECENTER_SPEED : Q30_VP_RECENTER_DELAY;
        const bool speed = which == Q30_VP_LOOKAHEAD_SPEED || which == Q30_VP_RECENTER_SPEED;

        if(!write)
        {
            ret = ok ? q30_dmap_viewport_follow((int)id, which) : 0;
            if(!speed)
                ret *= 10000;
            return true;
        }

        if(!ok)
            return true;

        if(speed)
        {
            // fractional pixels a frame, 0 to 240 (upstream's validate_viewport_follow_speed)
            if(value < 0 || value > 240 * 10000)
                Z_scripterrlog("Invalid value: %ld.%04ld - must be >= 0 and <= 240\n", value / 10000, labs(value % 10000));
            else
                q30_set_dmap_viewport_follow((int)id, which, value);
            return true;
        }

        const long v = value / 10000;
        const long hi = which == Q30_VP_DEADZONE_W ? 240 : which == Q30_VP_DEADZONE_H ? 160 : which == Q30_VP_RECENTER_DELAY ? 65535 : 127;
        const long lo = which == Q30_VP_LOOKAHEAD_X || which == Q30_VP_LOOKAHEAD_Y ? -128 : 0;

        if(v < lo || v > hi)
            Z_scripterrlog("Invalid value: %ld - must be >= %ld and <= %ld\n", v, lo, hi);
        else
            q30_set_dmap_viewport_follow((int)id, which, v);
        return true;
    }
    case Z30R_DMAPDATAPSUBSCRIPT:
    {
        Q30ScriptRef r = z30d_dmap_slot(id, q30_dmap_passive_subscreen);

        if(!write)
        {
            ret = r.script * 10000;
            return true;
        }

        if(!z30d_dmap_ok(id))
            return true;

        const word v = (word)z30d_bound(value / 10000, 0, Q30_DMAP_SLOTS - 1);

        // (upstream deallocates what the script owned; the same script running on this DMap goes on)
        if(id == currdmap && v != r.script)
            zasm30_drop_instance(SCRIPT_PASSIVESUB, 0);

        q30_set_dmap_script((int)id, q30_dmap_passive_subscreen, v, r.initd);
        return true;
    }
    case Z30R_DMAPDATASUBINITD:
    case Z30R_DMAPDATAPSUBINITD:
    {
        const int kind = arg == Z30R_DMAPDATASUBINITD ? q30_dmap_active_subscreen : q30_dmap_passive_subscreen;

        if(!ok || !z30d_index(Z30_INDEX, 8, false, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        Q30ScriptRef r = z30d_dmap_slot(id, kind);

        if(!write)
        {
            ret = r.initd[n];
            return true;
        }

        r.initd[n] = value;
        q30_set_dmap_script((int)id, kind, r.script, r.initd);

        if(kind == q30_dmap_active_subscreen && q30_compat_rule(Z30D_QR_SCRIPTS_SHARE_INITD))
        {
            Q30ScriptRef p = z30d_dmap_slot(id, q30_dmap_passive_subscreen);
            p.initd[n] = value;
            q30_set_dmap_script((int)id, q30_dmap_passive_subscreen, p.script, p.initd);
        }
        return true;
    }
    // Charted[128]: the map marks of the DMap's screens, upstream 3.0's chart (dmap << 7 | screen,
    // q30_chart_*); a column out of the DMap's reads -1 and takes no write, as upstream's
    case Z30R_DMAPDATACHARTED:
    {
        if(!ok || !z30d_index(Z30_INDEX, MAPSCRSNORMAL, false, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        const dmap &d = DMaps[id];
        const int col = (int)(n & 15) - (d.type == dmOVERW ? 0 : d.xoff);

        if((d.type & dmfTYPE) != dmOVERW ? unsigned(col) > 7 : unsigned(col) > 15)
        {
            if(!write)
                ret = -10000;
            return true;
        }

        const int di = ((int)id << 7) + (int)(n & 0x7F);

        if(!write)
            ret = q30_chart_get(di) * 10000;
        else
            q30_chart_set(di, (byte)(value / 10000));
        return true;
    }
    default:
        return false;
    }
}

static bool z30d_dmap_reg(const long arg, long &ret, const long value, const bool write)
{
    const long id = Z30_REF(DMAPDATA);

    switch(arg)
    {
    case Z30R_DMAPDATASUBSCRP:
        if(unsigned(id) >= MAXDMAPS)
        {
            if(!write)
                ret = 0;
        }
        else if(!write)
            ret = (byte)DMaps[id].passive_subscreen * 10000;
        else
        {
            const bool changed = DMaps[id].passive_subscreen != (byte)(value / 10000);
            DMaps[id].passive_subscreen = (byte)(value / 10000);

            if(changed && id == currdmap)
                update_subscreens(-1);
        }
        return true;

    case Z30R_DMAPDATAMAPSCRIPT:
    {
        const Q30ScriptRef *r = unsigned(id) < MAXDMAPS ? q30_dmap_script((int)id, q30_dmap_onmap) : NULL;

        if(!write)
            ret = r ? r->script * 10000 : 0;
        else if(unsigned(id) < MAXDMAPS)
        {
            long initd[8];
            memset(initd, 0, sizeof(initd));

            if(r)
                memcpy(initd, r->initd, sizeof(initd));

            q30_set_dmap_script((int)id, q30_dmap_onmap, (word)z30d_bound(value / 10000, 0, Q30_DMAP_SLOTS - 1), initd);
        }
        return true;
    }
    default:
        return z30d_dmap_more_reg(arg, ret, value, write);
    }
}

static bool z30d_dmap_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_DMAPDATAGETNAMER: case Z30C_DMAPDATAGETNAMEV: case Z30C_DMAPDATASETNAMER: case Z30C_DMAPDATASETNAMEV:
    case Z30C_DMAPDATAGETTITLER: case Z30C_DMAPDATAGETTITLEV: case Z30C_DMAPDATASETTITLER: case Z30C_DMAPDATASETTITLEV:
    case Z30C_DMAPDATAGETINTROR: case Z30C_DMAPDATAGETINTROV: case Z30C_DMAPDATANSETITROR: case Z30C_DMAPDATASETINTROV:
    case Z30C_DMAPDATAGETMUSICR: case Z30C_DMAPDATAGETMUSICV: case Z30C_DMAPDATASETMUSICR: case Z30C_DMAPDATASETMUSICV:
        break;
    default:
        return false;
    }

    const long id = Z30_REF(DMAPDATA);
    const long ptr = zasm30_aptr(get_register(sarg1));

    if(!z30d_dmap_ok(id))
        return true;

    dmap &d = DMaps[id];
    std::string s;

    switch(cmd)
    {
    case Z30C_DMAPDATAGETNAMER: case Z30C_DMAPDATAGETNAMEV:
        ArrayH::setArray(ptr, z30d_chars(d.name, 20));
        break;
    case Z30C_DMAPDATASETNAMER: case Z30C_DMAPDATASETNAMEV:
        ArrayH::getString(ptr, s, 22);
        strncpy(d.name, s.c_str(), 21);
        d.name[20] = 0;
        break;
    case Z30C_DMAPDATAGETTITLER: case Z30C_DMAPDATAGETTITLEV:
        ArrayH::setArray(ptr, z30d_dmap_title((int)id));
        break;
    case Z30C_DMAPDATASETTITLER: case Z30C_DMAPDATASETTITLEV:
    {
        if(q30_compat_rule(561))     // qr_OLD_DMAP_INTRO_STRINGS: 20 characters, as 2.53's title
        {
            ArrayH::getString(ptr, s, 21);
            s = s.substr(0, 20);
        }
        else
            ArrayH::getString(ptr, s, (word)vbound((long)ArrayH::getSize(ptr), 0L, 65535L));

        memset(d.title, 0, sizeof(d.title));
        strncpy(d.title, s.c_str(), 20);
        q30_set_dmap_texts((int)id, s.c_str(), q30_dmap_intro_string((int)id));
        break;
    }
    case Z30C_DMAPDATAGETINTROR: case Z30C_DMAPDATAGETINTROV:
        ArrayH::setArray(ptr, z30d_chars(d.intro, 72));
        break;
    case Z30C_DMAPDATANSETITROR: case Z30C_DMAPDATASETINTROV:
        ArrayH::getString(ptr, s, 73);
        strncpy(d.intro, s.c_str(), 72);
        d.intro[72] = 0;
        break;
    case Z30C_DMAPDATAGETMUSICR: case Z30C_DMAPDATAGETMUSICV:
        ArrayH::setArray(ptr, z30d_dmap_music_path((int)id));
        break;
    default:
        ArrayH::getString(ptr, s, 256);
        z30d_set_dmap_music_path((int)id, s);
        break;
    }

    return true;
}

// ---- itemdata (itemdata.cpp) ------------------------------------------------------------------

enum { Z30D_ITEM_JINX_IMMUNE = 1u << 28 };

// 2.53's own cost fields, which most items' use still asks (checkmagiccost): the reader's
// item_apply_cost_counter, again after a script changed the first cost
static void z30d_item_cost_sync(long id)
{
    itemdata &it = Q30_IDATA(id);
    const int ctr = q30_item_cost_value((int)id, 0), amt = q30_item_cost_value((int)id, 1);

    it.magic = (byte)((ctr == 1 || ctr == 4) ? z30d_bound(amt, 0, 255) : 0);

    if(ctr == 1)
        it.flags |= ITEM_RUPEE_MAGIC;
    else
        it.flags &= ~ITEM_RUPEE_MAGIC;
}

static bool z30d_item_reg(const long arg, long &ret, const long value, const bool write)
{
    int size_field = -1, cost_slot = -1;

    switch(arg)
    {
    case Z30R_IDATAHXOFS: size_field = 0; break;
    case Z30R_IDATAHYOFS: size_field = 1; break;
    case Z30R_IDATAHXSZ: size_field = 2; break;
    case Z30R_IDATAHYSZ: size_field = 3; break;
    case Z30R_IDATAHZSZ: size_field = 4; break;
    case Z30R_IDATADXOFS: size_field = 5; break;
    case Z30R_IDATADYOFS: size_field = 6; break;
    case Z30R_IDATATILEW: size_field = 7; break;
    case Z30R_IDATATILEH: size_field = 8; break;
    case Z30R_IDATACOSTCOUNTER: cost_slot = 0; break;
    case Z30R_IDATAMAGCOST: cost_slot = 1; break;
    case Z30R_IDATACOSTCOUNTER2: cost_slot = 2; break;
    case Z30R_IDATACOST2: cost_slot = 3; break;
    case Z30R_IDATAUSEWPN: case Z30R_IDATAUSEDEF: case Z30R_IDATAUSESOUND2: case Z30R_IDATAPSCRIPT:
    case Z30R_IDATAJINXIMMUNE: case Z30R_IDATAWPNINITD:
        break;
    default:
        return false;
    }

    const long id = ri->idata, v = value / 10000;

    if(unsigned(id) >= unsigned(q30_items_end()))
    {
        Z_scripterrlog("Invalid itemdata access: %ld\n", id);

        if(!write)
            ret = (arg == Z30R_IDATAJINXIMMUNE || arg == Z30R_IDATAWPNINITD) ? 0 : -10000;
        return true;
    }

    if(size_field >= 0)
    {
        if(write)
            q30_set_item_size_value((int)id, size_field, v);
        else
            ret = q30_item_size_value((int)id, size_field) * 10000;
        return true;
    }

    if(cost_slot >= 0)
    {
        if(write)
        {
            // counters -1 (none) to the last; amounts a short
            q30_set_item_cost_value((int)id, cost_slot, (cost_slot & 1) ? (int)z30d_bound(v, -32768, 32767)
                                    : (int)z30d_bound(v, -1, Q30_MAX_COUNTERS - 1));
            z30d_item_cost_sync(id);
        }
        else
            ret = q30_item_cost_value((int)id, cost_slot) * 10000;
        return true;
    }

    switch(arg)
    {
    case Z30R_IDATAUSEWPN:
    case Z30R_IDATAUSEDEF:
    {
        const bool def = arg == Z30R_IDATAUSEDEF;

        if(!write)
            ret = q30_item_weapon_use((int)id, def) * 10000;
        else if(def)
            q30_set_item_weapon_use((int)id, q30_item_weapon_use((int)id, false), (int)z30d_bound(v, 0, 255));
        else
            q30_set_item_weapon_use((int)id, (int)z30d_bound(v, 0, 255), q30_item_weapon_use((int)id, true));
        return true;
    }
    case Z30R_IDATAUSESOUND2:
        if(write)
            q30_set_item_usesound2((int)id, (int)z30d_bound(v, 0, Q30_MAX_SFX));
        else
            ret = q30_item_usesound2((int)id) * 10000;
        return true;

    // the collect (pickup) script; 2.53 runs it from itemdata at each pickup
    case Z30R_IDATAPSCRIPT:
        if(write)
            Q30_IDATA(id).collect_script = (word)z30d_bound(v, 0, NUMSCRIPTITEM - 1);
        else
            ret = Q30_IDATA(id).collect_script * 10000;
        return true;

    case Z30R_IDATAJINXIMMUNE:
    {
        const dword f = q30_item_flags((int)id);

        if(!write)
            ret = (f & Z30D_ITEM_JINX_IMMUNE) ? 10000 : 0;
        else
            q30_set_item_flags32((int)id, value ? (f | Z30D_ITEM_JINX_IMMUNE) : (f & ~(dword)Z30D_ITEM_JINX_IMMUNE));
        return true;
    }

    // itemdata->WeaponInitD[8]: the item's weapon's script arguments, raw
    default:
    {
        long n;

        if(!z30d_index(Z30_INDEX, 8, true, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        // (the item's weapon data has them: the item reader's record, which the weapons the
        // item makes take on; the weapon script table follows it)
        Q30ItemRec *rec = q30_item_rec((int)id);

        if(!rec)
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!write)
        {
            ret = rec->weap.initd[n];
            return true;
        }

        rec->weap.initd[n] = value;
        q30_set_item_weapon_script((int)id, rec->weap.script, rec->weap.initd);
        return true;
    }
    }
}

// An item that works while its kind is jinxed (upstream checkitem_jinx); never for 2.53 quests,
// whose items have no such flag.
bool q30_item_jinx_immune(int item)
{
    return item >= 0 && q30_item_flag(item, Z30D_ITEM_JINX_IMMUNE);
}

// ---- npcdata (npcdata.cpp) --------------------------------------------------------------------

static bool z30d_npcdata_reg(const long arg, long &ret, const long value, const bool write)
{
    switch(arg)
    {
    case Z30R_NPCDATATOUCHDAMAGE: case Z30R_NPCDATACSET: case Z30R_NPCDATAETILE: case Z30R_NPCDATAHOMING:
        break;
    default:
        return false;
    }

    const long id = Z30_REF(NPCDATA);

    if(unsigned(id) >= MAXGUYS)
    {
        Z_scripterrlog("Invalid npcdata ID: %ld\n", id);

        if(!write)
            ret = -10000;
        return true;
    }

    guydata &g = guysbuf[id];
    const long v = value / 10000;

    switch(arg)
    {
    case Z30R_NPCDATATOUCHDAMAGE: if(write) g.dp = (short)z30d_bound(v, 0, 32767); else ret = g.dp * 10000; break;
    case Z30R_NPCDATACSET: if(write) g.cset = (short)z30d_bound(v, 0, 32767); else ret = g.cset * 10000; break;
    case Z30R_NPCDATAETILE: if(write) g.e_tile = (tile_id)z30d_bound(v, 0, 214747); else ret = (long)g.e_tile * 10000; break;
    default: if(write) g.homing = (short)z30d_bound(v, 0, 32767); else ret = g.homing * 10000; break;
    }

    return true;
}

// ---- messagedata (messagedata.cpp) ------------------------------------------------------------

static bool z30d_msg_reg(const long arg, long &ret, const long value, const bool write)
{
    switch(arg)
    {
    case Z30R_MESSAGEDATACSET: case Z30R_MESSAGEDATAFLAGS: case Z30R_MESSAGEDATAFONT: case Z30R_MESSAGEDATAH:
    case Z30R_MESSAGEDATAHSPACE: case Z30R_MESSAGEDATALISTPOS: case Z30R_MESSAGEDATANEXT: case Z30R_MESSAGEDATAPORTCSET:
    case Z30R_MESSAGEDATAPORTHEI: case Z30R_MESSAGEDATAPORTTILE: case Z30R_MESSAGEDATAPORTWID: case Z30R_MESSAGEDATAPORTX:
    case Z30R_MESSAGEDATAPORTY: case Z30R_MESSAGEDATASFX: case Z30R_MESSAGEDATATEXTHEI: case Z30R_MESSAGEDATATILE:
    case Z30R_MESSAGEDATATRANS: case Z30R_MESSAGEDATAVSPACE: case Z30R_MESSAGEDATAW: case Z30R_MESSAGEDATAX:
    case Z30R_MESSAGEDATAY: case Z30R_MESSAGEDATA_SHADOW_TYPE: case Z30R_MESSAGEDATA_SHADOW_COLOR:
    case Z30R_MESSAGEDATA_ICON_MORE_SPRITE: case Z30R_MESSAGEDATA_ICON_MORE_ANCHOR: case Z30R_MESSAGEDATA_ICON_MORE_X:
    case Z30R_MESSAGEDATA_ICON_MORE_Y: case Z30R_MESSAGEDATA_ICON_SCROLLUP_SPRITE: case Z30R_MESSAGEDATA_ICON_SCROLLUP_ANCHOR:
    case Z30R_MESSAGEDATA_ICON_SCROLLUP_X: case Z30R_MESSAGEDATA_ICON_SCROLLUP_Y: case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_SPRITE:
    case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_ANCHOR: case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_X: case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_Y:
    case Z30R_MESSAGEDATA_SCROLL_SPEED_ACTIVE: case Z30R_MESSAGEDATA_SCROLL_SPEED_PASSIVE: case Z30R_MESSAGEDATA_ADVSFX:
    case Z30R_MESSAGEDATA_MENUMOVE_SFX: case Z30R_MESSAGEDATA_MENUCLOSE_SFX: case Z30R_MESSAGEDATAMARGINS:
    case Z30R_MESSAGEDATAFLAGSARR: case Z30R_MESSAGEDATATEXTWID:
        break;
    default:
        return false;
    }

    const long id = Z30_REF(MSGDATA), v = value / 10000;
    const bool array = arg == Z30R_MESSAGEDATAMARGINS || arg == Z30R_MESSAGEDATAFLAGSARR;

    if(BC::checkMessage(id, "messagedata") != SH::_NoError)
    {
        if(!write)
            ret = array ? 0 : -10000;
        return true;
    }

    // the string as the 2.55/3.0 message engine (q30_msg.cpp) shows it, and 2.53's copy
    static Q30MsgStr none;
    Q30MsgStr *q = q30_msg_string_edit((int)id);
    Q30MsgStr &s = q ? *q : none;
    MsgStr &m = MsgStrings[id];

    if(!q)
        none = Q30MsgStr();

    if(array)
    {
        long n;

        if(!z30_array_index(Z30_INDEX, arg == Z30R_MESSAGEDATAMARGINS ? 4 : 8, n))
        {
            if(!write)
                ret = arg == Z30R_MESSAGEDATAMARGINS ? z30_array_default(-10000) : 0;
            return true;
        }

        if(arg == Z30R_MESSAGEDATAMARGINS)
        {
            if(write)
                s.margins[n] = (short)z30d_bound(v, 0, 255);
            else
                ret = s.margins[n] * 10000;
        }
        else if(!write)
            ret = (s.stringflags & (1 << n)) ? 10000 : 0;
        else
        {
            if(value)
                s.stringflags |= (byte)(1 << n);
            else
                s.stringflags &= (byte)~(1 << n);

            m.stringflags = s.stringflags;
        }
        return true;
    }

    if(!write)
    {
        long r;

        switch(arg)
        {
        case Z30R_MESSAGEDATACSET: r = s.cset; break;
        case Z30R_MESSAGEDATAFLAGS: r = s.stringflags; break;
        case Z30R_MESSAGEDATAFONT: r = s.font; break;
        case Z30R_MESSAGEDATAH: r = s.h; break;
        case Z30R_MESSAGEDATAHSPACE: r = s.hspace; break;
        case Z30R_MESSAGEDATALISTPOS: r = m.listpos; break;
        case Z30R_MESSAGEDATANEXT: r = s.nextstring; break;
        case Z30R_MESSAGEDATAPORTCSET: r = s.portrait_cset; break;
        case Z30R_MESSAGEDATAPORTHEI: r = s.portrait_th; break;
        case Z30R_MESSAGEDATAPORTTILE: r = s.portrait_tile; break;
        case Z30R_MESSAGEDATAPORTWID: r = s.portrait_tw; break;
        case Z30R_MESSAGEDATAPORTX: r = s.portrait_x; break;
        case Z30R_MESSAGEDATAPORTY: r = s.portrait_y; break;
        case Z30R_MESSAGEDATASFX: r = s.sfx; break;
        case Z30R_MESSAGEDATATEXTHEI: r = text_height(get_zc_font(s.font)); break;
        case Z30R_MESSAGEDATATEXTWID:       // (upstream's do_msgwidth: the text as it is, trailing spaces off)
        {
            std::string t = q ? s.text : std::string(m.s);
            const size_t last = t.find_last_not_of(' ');
            t = last == std::string::npos ? std::string() : t.substr(0, last + 1);
            r = text_length(get_zc_font(q ? s.font : m.font), t.c_str());
            break;
        }
        case Z30R_MESSAGEDATATILE: r = s.tile; break;
        case Z30R_MESSAGEDATATRANS: r = s.trans ? 1 : 0; break;
        case Z30R_MESSAGEDATAVSPACE: r = s.vspace; break;
        case Z30R_MESSAGEDATAW: r = s.w; break;
        case Z30R_MESSAGEDATAX: r = s.x; break;
        case Z30R_MESSAGEDATAY: r = s.y; break;
        case Z30R_MESSAGEDATA_SHADOW_TYPE: r = s.shadow_type; break;
        case Z30R_MESSAGEDATA_SHADOW_COLOR: r = s.shadow_color; break;
        case Z30R_MESSAGEDATA_ICON_MORE_SPRITE: r = s.icons[0].sprite; break;
        case Z30R_MESSAGEDATA_ICON_MORE_ANCHOR: r = s.icons[0].anchor; break;
        case Z30R_MESSAGEDATA_ICON_MORE_X: r = s.icons[0].x; break;
        case Z30R_MESSAGEDATA_ICON_MORE_Y: r = s.icons[0].y; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLUP_SPRITE: r = s.icons[1].sprite; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLUP_ANCHOR: r = s.icons[1].anchor; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLUP_X: r = s.icons[1].x; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLUP_Y: r = s.icons[1].y; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_SPRITE: r = s.icons[2].sprite; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_ANCHOR: r = s.icons[2].anchor; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_X: r = s.icons[2].x; break;
        case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_Y: r = s.icons[2].y; break;
        case Z30R_MESSAGEDATA_SCROLL_SPEED_ACTIVE: r = s.active_scroll_speed; break;
        case Z30R_MESSAGEDATA_SCROLL_SPEED_PASSIVE: r = s.passive_scroll_speed; break;
        case Z30R_MESSAGEDATA_ADVSFX: r = s.adv_sfx; break;
        case Z30R_MESSAGEDATA_MENUMOVE_SFX: r = s.menu_move_sfx; break;
        default: r = s.menu_close_sfx; break;
        }

        ret = r * 10000;
        return true;
    }

    const long last = msg_count > 0 ? msg_count - 1 : 0;

    switch(arg)
    {
    case Z30R_MESSAGEDATACSET: m.cset = s.cset = (byte)z30d_bound(v, 0, 15); break;
    case Z30R_MESSAGEDATAFLAGS: m.stringflags = s.stringflags = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATAFONT: s.font = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATAH: s.h = (word)z30d_bound(v, 0, 65535); break;
    case Z30R_MESSAGEDATAHSPACE: m.hspace = s.hspace = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATALISTPOS: m.listpos = (word)z30d_bound(v, 1, last); break;
    case Z30R_MESSAGEDATANEXT: m.nextstring = s.nextstring = (word)z30d_bound(v, 0, last); break;
    case Z30R_MESSAGEDATAPORTCSET: s.portrait_cset = (byte)z30d_bound(v, 0, 15); break;
    case Z30R_MESSAGEDATAPORTHEI: s.portrait_th = (byte)z30d_bound(v, 0, 14); break;
    case Z30R_MESSAGEDATAPORTTILE: s.portrait_tile = z30d_bound(v, 0, NEWMAXTILES); break;
    case Z30R_MESSAGEDATAPORTWID: s.portrait_tw = (byte)z30d_bound(v, 0, 16); break;
    case Z30R_MESSAGEDATAPORTX: s.portrait_x = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATAPORTY: s.portrait_y = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATASFX:
        s.sfx = (word)z30d_bound(v, 0, Q30_MAX_SFX);
        m.sfx = (byte)(s.sfx < 256 ? s.sfx : 0);
        break;
    case Z30R_MESSAGEDATATEXTHEI: case Z30R_MESSAGEDATATEXTWID: break;
    case Z30R_MESSAGEDATATILE: s.tile = z30d_bound(v, 0, NEWMAXTILES); m.tile = (tile_id)(s.tile < NEWMAXTILES ? s.tile : 0); break;
    case Z30R_MESSAGEDATATRANS: m.trans = s.trans = value != 0; break;
    case Z30R_MESSAGEDATAVSPACE: m.vspace = s.vspace = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATAW: s.w = (word)z30d_bound(v, 0, 65535); break;
    case Z30R_MESSAGEDATAX: m.x = s.x = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATAY: m.y = s.y = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_SHADOW_TYPE: s.shadow_type = (byte)z30d_bound(v, 0, 10); break;    // sstsMAX - 1
    case Z30R_MESSAGEDATA_SHADOW_COLOR: s.shadow_color = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATA_ICON_MORE_SPRITE: s.icons[0].sprite = (short)z30d_bound(v, -1, 8191); break;   // MAXSPRITES - 1
    case Z30R_MESSAGEDATA_ICON_MORE_ANCHOR: s.icons[0].anchor = (byte)z30d_bound(v, 0, 10); break;       // max_anchor - 1
    case Z30R_MESSAGEDATA_ICON_MORE_X: s.icons[0].x = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_ICON_MORE_Y: s.icons[0].y = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLUP_SPRITE: s.icons[1].sprite = (short)z30d_bound(v, -1, 8191); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLUP_ANCHOR: s.icons[1].anchor = (byte)z30d_bound(v, 0, 10); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLUP_X: s.icons[1].x = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLUP_Y: s.icons[1].y = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_SPRITE: s.icons[2].sprite = (short)z30d_bound(v, -1, 8191); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_ANCHOR: s.icons[2].anchor = (byte)z30d_bound(v, 0, 10); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_X: s.icons[2].x = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_ICON_SCROLLDOWN_Y: s.icons[2].y = (short)z30d_bound(v, SHRT_MIN, SHRT_MAX); break;
    case Z30R_MESSAGEDATA_SCROLL_SPEED_ACTIVE: s.active_scroll_speed = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATA_SCROLL_SPEED_PASSIVE: s.passive_scroll_speed = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_MESSAGEDATA_ADVSFX: s.adv_sfx = (word)z30d_bound(v, 0, Q30_MAX_SFX); break;
    case Z30R_MESSAGEDATA_MENUMOVE_SFX: s.menu_move_sfx = (word)z30d_bound(v, 0, Q30_MAX_SFX); break;
    default: s.menu_close_sfx = (word)z30d_bound(v, 0, Q30_MAX_SFX); break;
    }

    return true;
}

// ---- mapdata: FFCs, doors, ExDoors, screen states (mapdata.cpp) --------------------------------

// A mapdata's FFC arrays and NumFFCs[] number the FFCs from 1, as upstream's (resolve_ffc_handle);
// the arrays also take a negative index counted back from the screen's last FFC with a combo.
static int z30d_last_ffc(const mapscr *m)
{
    for(int i = m->ffc_count() - 1; i >= 0; --i)
    {
        if(m->ffdata[i])
            return i + 1;
    }

    return 0;
}

// The slot (0-based) of a mapdata FFC array's element, or -1 (logged) when there is none.
static long z30d_ffc_slot(const mapscr *m, long index, bool array)
{
    if(array && index < 0 && !q30_compat_rule(Z30_QR_OLD_ARRAY_INDEX))
    {
        const long size = z30d_last_ffc(m);
        index += size;

        if(index < 0)
        {
            Z_scripterrlog("Invalid array index %ld for internal array of size %ld\n", index, size);
            return -1;
        }
    }

    if(index - 1 < 0 || index - 1 >= Q30_MAXFFCS)
    {
        Z_scripterrlog("Invalid FFC index %ld (must be 1 to %d)\n", index, (int)Q30_MAXFFCS);
        return -1;
    }

    return index - 1;
}

enum { Z30D_FF_DATA, Z30D_FF_CSET, Z30D_FF_DELAY, Z30D_FF_X, Z30D_FF_Y, Z30D_FF_VX, Z30D_FF_VY, Z30D_FF_AX, Z30D_FF_AY,
       Z30D_FF_FLAGS, Z30D_FF_TW, Z30D_FF_TH, Z30D_FF_EW, Z30D_FF_EH, Z30D_FF_LINK, Z30D_FF_SCRIPT, Z30D_FF_INITD };

static int z30d_ffc_field(long arg)
{
    switch(arg)
    {
    case Z30R_MAPDATAFFDATA: return Z30D_FF_DATA;
    case Z30R_MAPDATAFFCSET: return Z30D_FF_CSET;
    case Z30R_MAPDATAFFDELAY: return Z30D_FF_DELAY;
    case Z30R_MAPDATAFFX: return Z30D_FF_X;
    case Z30R_MAPDATAFFY: return Z30D_FF_Y;
    case Z30R_MAPDATAFFXDELTA: return Z30D_FF_VX;
    case Z30R_MAPDATAFFYDELTA: return Z30D_FF_VY;
    case Z30R_MAPDATAFFXDELTA2: return Z30D_FF_AX;
    case Z30R_MAPDATAFFYDELTA2: return Z30D_FF_AY;
    case Z30R_MAPDATAFFFLAGS: return Z30D_FF_FLAGS;
    case Z30R_MAPDATAFFWIDTH: return Z30D_FF_TW;
    case Z30R_MAPDATAFFHEIGHT: return Z30D_FF_TH;
    case Z30R_MAPDATAFFEFFECTWIDTH: return Z30D_FF_EW;
    case Z30R_MAPDATAFFEFFECTHEIGHT: return Z30D_FF_EH;
    case Z30R_MAPDATAFFLINK: return Z30D_FF_LINK;
    case Z30R_MAPDATAFFSCRIPT: return Z30D_FF_SCRIPT;
    case Z30R_MAPDATAINTID: return Z30D_FF_INITD;
    default: return -1;
    }
}

static bool z30d_ffc_reg(const long arg, long &ret, const long value, const bool write)
{
    const int field = z30d_ffc_field(arg);

    if(field < 0 && arg != Z30R_MAPDATANUMFF)
        return false;

    mapscr *m = z30_mapdata_now();
    // (NumFFCs[] and InitD[][] take the FFC from 1 without the arrays' negative indexes)
    const bool array = field >= 0 && field != Z30D_FF_INITD;

    if(!m)
    {
        if(!write)
            ret = array ? z30_array_default(-10000) : (arg == Z30R_MAPDATANUMFF ? 0 : -10000);
        return true;
    }

    const long slot = z30d_ffc_slot(m, Z30_INDEX, array);

    if(arg == Z30R_MAPDATANUMFF)
    {
        if(!write)
            ret = (slot >= 0 && slot < m->ffc_count() && m->ffdata[slot] != 0) ? 10000 : 0;
        return true;
    }

    const long d = ri->d[1] / 10000;    // InitD's second index (rINDEX2)

    if(slot < 0 || (field == Z30D_FF_INITD && (d < 0 || d > 7)))
    {
        if(!write)
            ret = -10000;
        return true;
    }

    if(!write)
    {
        // an FFC past the screen's slots reads as an empty one, as upstream's scratch FFC
        const bool have = slot < m->ffc_count();
        long r = 0;

        switch(field)
        {
        case Z30D_FF_DATA: r = have ? m->ffdata[slot] * 10000 : 0; break;
        case Z30D_FF_CSET: r = have ? m->ffcset[slot] * 10000 : 0; break;
        case Z30D_FF_DELAY: r = have ? m->ffdelay[slot] * 10000 : 0; break;
        case Z30D_FF_X: r = have ? m->ffx[slot] : 0; break;
        case Z30D_FF_Y: r = have ? m->ffy[slot] : 0; break;
        case Z30D_FF_VX: r = have ? m->ffxdelta[slot] : 0; break;
        case Z30D_FF_VY: r = have ? m->ffydelta[slot] : 0; break;
        case Z30D_FF_AX: r = have ? m->ffxdelta2[slot] : 0; break;
        case Z30D_FF_AY: r = have ? m->ffydelta2[slot] : 0; break;
        case Z30D_FF_FLAGS: r = have ? (long)m->ffflags[slot] * 10000 : 0; break;
        case Z30D_FF_TW: r = ((have ? m->ffwidth[slot] >> 6 : 0) + 1) * 10000; break;
        case Z30D_FF_TH: r = ((have ? m->ffheight[slot] >> 6 : 0) + 1) * 10000; break;
        case Z30D_FF_EW: r = ((have ? m->ffwidth[slot] & 0x3F : 15) + 1) * 10000; break;
        case Z30D_FF_EH: r = ((have ? m->ffheight[slot] & 0x3F : 15) + 1) * 10000; break;
        case Z30D_FF_LINK: r = have ? m->fflink[slot] * 10000 : 0; break;
        case Z30D_FF_SCRIPT: r = have ? m->ffscript[slot] * 10000 : 0; break;
        default: r = have ? m->initd[slot][d] : 0; break;
        }

        ret = r;
        return true;
    }

    const long v = value / 10000;

    // upstream's value checks: refused writes change nothing
    if((field == Z30D_FF_DATA && (v < 0 || v >= MAXCOMBOS)) || ((field == Z30D_FF_TW || field == Z30D_FF_EW || field == Z30D_FF_EH)
       && (v < 0 || v >= 10000)) || (field == Z30D_FF_TH && (v < 0 || v >= 4)))
    {
        Z_scripterrlog("Attempted to set invalid value for array: %ld\n", v);
        return true;
    }

    // (the script numbers this engine can run; upstream stores any)
    if(field == Z30D_FF_SCRIPT && (v < 0 || v >= NUMSCRIPTFFC))
        return true;

    m = z30_ffc_room(m, slot);

    if(slot >= m->ffc_count())
        return true;

    switch(field)
    {
    case Z30D_FF_DATA: m->ffdata[slot] = (word)v; break;
    case Z30D_FF_CSET: m->ffcset[slot] = (byte)v; break;
    case Z30D_FF_DELAY: m->ffdelay[slot] = (word)v; break;
    case Z30D_FF_X: m->ffx[slot] = value; break;
    case Z30D_FF_Y: m->ffy[slot] = value; break;
    case Z30D_FF_VX: m->ffxdelta[slot] = value; break;
    case Z30D_FF_VY: m->ffydelta[slot] = value; break;
    case Z30D_FF_AX: m->ffxdelta2[slot] = value; break;
    case Z30D_FF_AY: m->ffydelta2[slot] = value; break;
    case Z30D_FF_FLAGS: m->ffflags[slot] = (dword)v; break;
    // tile sizes 1-4 and effect sizes 1-64 are what this engine's FFCs hold
    case Z30D_FF_TW: m->ffwidth[slot] = (byte)((m->ffwidth[slot] & 0x3F) | ((z30d_bound(v, 1, 4) - 1) << 6)); break;
    case Z30D_FF_TH: m->ffheight[slot] = (byte)((m->ffheight[slot] & 0x3F) | ((z30d_bound(v, 1, 4) - 1) << 6)); break;
    case Z30D_FF_EW: m->ffwidth[slot] = (byte)((m->ffwidth[slot] & ~0x3F) | ((z30d_bound(v, 1, 64) - 1) & 0x3F)); break;
    case Z30D_FF_EH: m->ffheight[slot] = (byte)((m->ffheight[slot] & ~0x3F) | ((z30d_bound(v, 1, 64) - 1) & 0x3F)); break;
    case Z30D_FF_LINK: m->fflink[slot] = (byte)v; break;
    case Z30D_FF_SCRIPT: m->ffscript[slot] = (word)v; break;
    default: m->initd[slot][d] = value; break;
    }

    return true;
}

// upstream nextscr2 (the side warps of the screen Link is on decide where a scrolling warp leads)
static int z30d_nextscr_mi(int mi, int dir)
{
    int map = mi / MAPSCRSNORMAL, s = mi % MAPSCRSNORMAL;

    switch(dir)
    {
    case up: s -= 16; break;
    case down: s += 16; break;
    case left: s -= 1; break;
    case right: s += 1; break;
    }

    const int index = (tmpscr->sidewarpindex >> (dir * 2)) & 3;

    if(tmpscr->sidewarptype[index] == 3)    // scrolling warp
    {
        const byte bits[4] = { wfUP, wfDOWN, wfLEFT, wfRIGHT };

        if(tmpscr->flags2 & bits[dir & 3])
        {
            map = DMaps[tmpscr->sidewarpdmap[index]].map;
            s = tmpscr->sidewarpscr[index] + DMaps[tmpscr->sidewarpdmap[index]].xoff;
        }
    }

    return (s < 0 || s >= 128) ? -1 : (map << 7) + s;
}

// upstream setmapflag_mi / unsetmapflag_mi: the screen's state, carried over along its carryover
// chain as the screen Link is on says (its nocarry and no-item-reset flags); 2.53's own
// setmapflag carries only some states and unsetmapflag clears the current screen's.
static void z30d_mapflag(int mi, word flag, bool on)
{
    if(on)
        game->maps[mi] |= flag;
    else if((flag == mITEM || flag == mBELOW) && (tmpscr->flags4 & fNOITEMRESET))
        ;
    else
        game->maps[mi] &= (word)~flag;

    if((tmpscr->nocarry & flag) == flag)
        return;

    int map = mi >> 7, scr = mi & 127;
    int nmap = TheMaps[map * MAPSCRS + scr].nextmap, nscr = TheMaps[map * MAPSCRS + scr].nextscr;
    std::vector<int> done;
    bool looped = (nmap == map + 1 && nscr == scr);

    while(nmap != 0 && nmap <= map_count && !looped && nscr < 128)
    {
        const int next = ((nmap - 1) << 7) + nscr;

        if(on)
            game->maps[next] |= flag;
        else
            game->maps[next] &= (word)~flag;

        const int cmap = nmap - 1, cscr = nscr;
        nmap = TheMaps[cmap * MAPSCRS + cscr].nextmap;
        nscr = TheMaps[cmap * MAPSCRS + cscr].nextscr;
        const int key = ((nmap - 1) << 7) + nscr;

        for(size_t i = 0; i < done.size(); ++i)
        {
            if(done[i] == key)
                looped = true;
        }

        done.push_back(key);
    }
}

static bool z30d_mapdata_reg(const long arg, long &ret, const long value, const bool write)
{
    long n;

    switch(arg)
    {
    // mapdata->GetExDoor(dir, index) / SetExDoor(dir, index): the screen's and the next one's
    case Z30R_MAPDATAEXDOOR:
    {
        const long ref = Z30_REF(MAPDATA);
        const int mi = z30_mapdata(ref) ? z30_mapdata_mi(ref) : -1;
        const int dir = SH::read_stack(SPR + 1) / 10000, ind = SH::read_stack(SPR) / 10000;

        if(!write)
            ret = 0;

        if(mi < 0)
            return true;

        if(unsigned(dir) > 3)
            Z_scripterrlog("Invalid dir '%d' passed to 'mapdata->%sExDoor()'; must be 0-3\n", dir, write ? "Set" : "Get");
        else if(unsigned(ind) > 7)
            Z_scripterrlog("Invalid index '%d' passed to 'mapdata->%sExDoor()'; must be 0-7\n", ind, write ? "Set" : "Get");
        else if(!write)
            ret = q30_get_xdoor(mi, dir, ind) ? 10000 : 0;
        else
        {
            q30_set_xdoor(mi, dir, ind, true);
            const int next = z30d_nextscr_mi(mi, dir);

            if(next >= 0)
                q30_set_xdoor(next, oppositeDir[dir], ind, true);
        }
        return true;
    }

    // mapdata->Door[4]: on the screen being played a write puts the door's combos there, as
    // Screen->Door does, without changing the array (as upstream's)
    case Z30R_MAPDATADOOR:
    {
        const long ref = Z30_REF(MAPDATA);
        mapscr *m = z30_mapdata(ref);

        if(!m || !z30_array_index(Z30_INDEX, 4, n))
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!write)
            ret = m->door[n] * 10000;
        else if(ref > 0)
            m->door[n] = (byte)z30d_bound(value / 10000, 0, 255);
        else if(z30_md_here(ref) && z30_md_layer(ref) == 0)
            putdoor(scrollbuf, 0, (int)n, (int)z30d_bound(value / 10000, 0, 255), true, true);
        return true;
    }

    // Screen->Pattern, the enemy pattern SpawnScreenEnemies() uses
    case Z30R_SCREENDATAPATTERN:
        if(write)
            tmpscr->pattern = (byte)z30d_bound(value / 10000, 0, 255);
        else
            ret = tmpscr->pattern * 10000;
        return true;

    // mapdata->State[17]: the screen's saved states (this engine keeps 16: the 17th, no enemies
    // returning, reads off and can't be set)
    case Z30R_MAPDATASCREENSTATED:
    {
        const long ref = Z30_REF(MAPDATA);

        if(!z30_mapdata(ref) || !z30_array_index(Z30_INDEX, 17, n))
        {
            if(!write)
                ret = 0;
            return true;
        }

        const int mi = z30_mapdata_mi(ref);

        if(!write)
            ret = (mi >= 0 && n < 16 && ((game->maps[mi] >> n) & 1)) ? 10000 : 0;
        else if(mi >= 0 && n < 16)
            z30d_mapflag(mi, (word)(1 << n), value != 0);
        return true;
    }
    default:
        return false;
    }
}

// upstream _walkflag_layer_new: one point of screen m against its solidity, in a field of max_x
// by max_y pixels
static bool z30d_walkflag_layer(int x, int y, const mapscr *m, int max_x, int max_y)
{
    if(!get_bit(quest_rules, qr_LTTPWALK))
    {
        max_x -= 7;
        max_y -= 7;
    }

    if(x < 0 || y < 0 || x >= max_x || y >= max_y)
        return false;

    if(!m)
        return true;

    const int pos = ((x % 256) >> 4) + ((y % 176) & 0xF0);
    const newcombo &c = combobuf[m->data[pos]];
    const bool dried = iswater_type(c.type) && DRIEDLAKE;
    int b = 1;

    if(x & 8)
        b <<= 2;

    if(y & 8)
        b <<= 1;

    return (c.walk & b) && !dried;
}

// mapdata->isSolidLayer(x, y, layer) (do_mapdataissolid_layer)
static void z30d_issolid_layer()
{
    const int x = ri->d[0] / 10000, y = ri->d[1] / 10000, layer = ri->d[2] / 10000;
    const long ref = Z30_REF(MAPDATA);
    const mapscr *m = z30_mapdata(ref);

    if(!m || layer < 0 || layer > 6)
    {
        set_register(sarg1, 10000);
        return;
    }

    if(ref < 0 && z30_md_layer(ref) == 0)
    {
        // the live screen of the layer, as Screen->isSolidLayer(); a layer that isn't there isn't solid
        bool solid = false;

        if(q30_region_on())
        {
            if(x >= 0 && y >= 0 && x < q30_region_width() && y < q30_region_height())
            {
                int pos;
                bool current;
                const mapscr *s = q30_region_rpos_screen(q30_region_rpos_at(x, y), layer, &pos, &current);

                if(s && s->valid)
                    solid = z30d_walkflag_layer(x, y, s, q30_region_width(), q30_region_height());
            }
        }
        else
        {
            const mapscr *s = layer == 0 ? tmpscr : &tmpscr2[layer - 1];

            if(s->valid)
                solid = z30d_walkflag_layer(x, y, s, 256, 176);
        }

        set_register(sarg1, solid ? 10000 : 0);
        return;
    }

    if(layer > 0)
    {
        // layermap is the map + 1 (0: no layer)
        const long index = (m->layermap[layer - 1] - 1) * MAPSCRS + m->layerscreen[layer - 1];

        if(m->layermap[layer - 1] == 0 || index < 0 || index >= (long)TheMaps.size())
        {
            set_register(sarg1, 10000);
            return;
        }

        m = &TheMaps[index];
    }

    set_register(sarg1, z30d_walkflag_layer(x, y, m, q30_region_width(), q30_region_height()) ? 10000 : 0);
}

// ---- Screen->SpawnScreenEnemies() (upstream scriptloadenemies and spawnEnemy) ----------------

extern int sle_clk;
bool is_ceiling_pattern(int i);
bool slowguy(int id);
bool is_starting_pos(int i, int x, int y, int t);
int placeenemy(int i);

// One of the screen's enemies, placed as the engine places them on entry: its enemy flag, else
// the pattern's NES positions, else anywhere free.
static void z30d_spawn_enemy(int &pos, int clk, int &fastguys, int i, int &guycnt, int &loadcnt)
{
    const int id = tmpscr->enemy[i];
    const bool ceiling = is_ceiling_pattern(tmpscr->pattern), sideview = (tmpscr->flags7 & fSIDEVIEW) != 0;
    int x = 0, y = 0, t = -1;
    bool placed = false;

    for(int sy = 0; sy < 176 && !placed; sy += 16)
    {
        for(int sx = 0; sx < 256 && !placed; sx += 16)
        {
            if(MAPFLAG(sx, sy) != mfENEMY0 + i && MAPCOMBOFLAG(sx, sy) != mfENEMY0 + i)
                continue;

            if(!ok2add(id))
            {
                if(loadcnt < 10 && id > 0 && id < MAXGUYS)
                    ++loadcnt;
            }
            else
            {
                addenemy(sx, (ceiling && sideview) ? -(150 + 50 * guycnt) : sy, (ceiling && !sideview) ? 150 + 50 * guycnt : 0, id, -15);
                ++guycnt;
                placed = true;
            }
        }
    }

    if(!placed)
    {
        if((tmpscr->pattern == pRANDOM || tmpscr->pattern == pCEILING) && !sideview && id > 0 && id < MAXGUYS)
        {
            do
            {
                pos %= 9;
                x = stx[loadside][pos];
                y = sty[loadside][pos];
                ++pos;
                ++t;
            }
            while(t < 20 && !is_starting_pos(i, x, y, t));
        }

        if(t < 0 || t >= 20)
        {
            const int r = placeenemy(i);

            if(r < 0)
                return;

            x = (r & 15) << 4;
            y = r & 0xF0;
        }

        int c = clk;

        if(!slowguy(id))
            ++fastguys;
        else if(fastguys > 0)
            c = -15 * (i - fastguys + 2);
        else
            c = -15 * (i + 1);

        if(BSZ && id > 0 && id < MAXGUYS)
            c = (guysbuf[id].family == eeLEV && guysbuf[id].misc1 == 1) ? -15 * (i + 1) : -15;

        if(!ok2add(id))
        {
            if(loadcnt < 10 && id > 0 && id < MAXGUYS)
                ++loadcnt;
            return;
        }

        addenemy(x, (ceiling && sideview) ? -(150 + 50 * guycnt) : y, (ceiling && !sideview) ? 150 + 50 * guycnt : 0, id, c);
        ++guycnt;
    }

    // the screen's leader and its item carrier, as on entry
    const int index = guys.idFirst(id, 0xFFF);

    if(index < 0)
        return;

    if(i == 0 && (tmpscr->enemyflags & efLEADER))
        ((enemy *)guys.spr(index))->leader = true;

    if((tmpscr->hasitem & (4 | 2)) && !(((enemy *)guys.spr(index))->flags & guy_doesntcount))
    {
        bool carrier = false;

        for(int q = 0; q < guys.Count(); ++q)
            carrier = carrier || ((enemy *)guys.spr(q))->itemguy;

        if(!carrier)
            ((enemy *)guys.spr(index))->itemguy = true;
    }
}

// Spawns the screen's enemies now; false when it doesn't (enemies coming in from the sides, or
// none to spawn). Side-loaded screens (upstream's script_side_load_enemies) aren't done here.
static bool z30d_script_load_enemies()
{
    enum { Z30D_PNOSPAWN = 6 };

    loaded_enemies = true;

    if(sle_clk || tmpscr->pattern == Z30D_PNOSPAWN)
        return false;

    if(tmpscr->pattern == pSIDES || tmpscr->pattern == pSIDESR)
    {
        zasm30_log_once("SpawnScreenEnemies on a side-loading screen", tmpscr->pattern);
        return false;
    }

    int pos = rand() % 9, clk = -15, fastguys = 0, guycnt = 0, loadcnt = 10;

    for(int i = 0; i < loadcnt && tmpscr->enemy[i] > 0; ++i)
    {
        z30d_spawn_enemy(pos, clk, fastguys, i, guycnt, loadcnt);
        --clk;
    }

    return true;
}

// ---- weapondata objects (weapondata.cpp) ------------------------------------------------------
// Game->CreateWeaponData() makes one; its fields are upstream's weapon_data. Applied to a weapon,
// the parts this engine's weapons have take effect: step, sizes, script and InitD, timeout,
// unblockable, weapon and move flags, glow, and the imitated weapon.

enum { Z30D_WD_GLOW_RAD = 0x1, Z30D_WD_SET_STEP = 0x2 };    // upstream wdata_glow_rad, wdata_set_step

static std::map<long, Q30WeaponData> zasm30_weapdatas;

static Q30WeaponData *z30d_weapdata_view_data(long ref);   // (below: a combo's weapon data)

static Q30WeaponData *z30d_weapdata(long ref, bool log)
{
    if(Q30WeaponData *view = z30d_weapdata_view_data(ref))
        return view;

    std::map<long, Q30WeaponData>::iterator it = zasm30_weapdatas.find(ref);

    if(it != zasm30_weapdatas.end())
        return &it->second;

    if(log)
        Z_scripterrlog("Invalid weapondata using UID = %ld\n", ref);

    return NULL;
}

static bool z30d_weapdata_reg(const long arg, long &ret, const long value, const bool write)
{
    switch(arg)
    {
    case Z30R_WEAPDATA_LIGHT_SHAPE: case Z30R_WEAPDATA_SIZE_BITFLAGS: case Z30R_WEAPDATA_HIT_OFFSET_X:
    case Z30R_WEAPDATA_HIT_OFFSET_Y: case Z30R_WEAPDATA_HIT_WIDTH: case Z30R_WEAPDATA_HIT_HEIGHT: case Z30R_WEAPDATA_HIT_ZHEIGHT:
    case Z30R_WEAPDATA_X_OFFSET: case Z30R_WEAPDATA_Y_OFFSET: case Z30R_WEAPDATA_TILE_W: case Z30R_WEAPDATA_TILE_H:
    case Z30R_WEAPDATA_STEP: case Z30R_WEAPDATA_BOUNCE_MULT: case Z30R_WEAPDATA_BOUNCE_ADD: case Z30R_WEAPDATA_UNBLOCKABLE:
    case Z30R_WEAPDATA_TIMEOUT: case Z30R_WEAPDATA_USEWEAPON: case Z30R_WEAPDATA_USEDEFENCE: case Z30R_WEAPDATA_LIFTLEVEL:
    case Z30R_WEAPDATA_LIFTTIME: case Z30R_WEAPDATA_LIFTHEIGHT: case Z30R_WEAPDATA_SCRIPT: case Z30R_WEAPDATA_PIERCE:
    case Z30R_WEAPDATA_VIEWPORT_SUSPEND_RANGE: case Z30R_WEAPDATA_VIEWPORT_DESPAWN_RANGE: case Z30R_WEAPDATA_VALID:
    case Z30R_WEAPDATA_MISC_FLAGS: case Z30R_WEAPDATA_MOVE_FLAGS: case Z30R_WEAPDATA_WEAP_FLAGS:
    case Z30R_WEAPDATA_BURN_SPRITES: case Z30R_WEAPDATA_BURN_LIGHTRAD: case Z30R_WEAPDATA_BURN_LIGHTOFFSET:
    case Z30R_WEAPDATA_INITD:
        break;
    default:
        return false;
    }

    Q30WeaponData *d = z30d_weapdata(Z30_REF(WEAPDATA), arg != Z30R_WEAPDATA_VALID);
    const long v = value / 10000;

    if(!write)
        ret = 0;

    if(!d)
        return true;

    long n;

    switch(arg)
    {
    case Z30R_WEAPDATA_VALID: if(!write) ret = 10000; break;
    case Z30R_WEAPDATA_LIGHT_SHAPE: if(write) d->glow_shape = (byte)z30d_bound(v, 0, 255); else ret = d->glow_shape * 10000; break;
    case Z30R_WEAPDATA_SIZE_BITFLAGS: if(write) d->override_flags = v; else ret = d->override_flags * 10000; break;
    case Z30R_WEAPDATA_HIT_OFFSET_X: if(write) d->hxofs = v; else ret = d->hxofs * 10000; break;
    case Z30R_WEAPDATA_HIT_OFFSET_Y: if(write) d->hyofs = v; else ret = d->hyofs * 10000; break;
    case Z30R_WEAPDATA_HIT_WIDTH: if(write) d->hxsz = v; else ret = d->hxsz * 10000; break;
    case Z30R_WEAPDATA_HIT_HEIGHT: if(write) d->hysz = v; else ret = d->hysz * 10000; break;
    case Z30R_WEAPDATA_HIT_ZHEIGHT: if(write) d->hzsz = v; else ret = d->hzsz * 10000; break;
    case Z30R_WEAPDATA_X_OFFSET: if(write) d->xofs = v; else ret = d->xofs * 10000; break;
    case Z30R_WEAPDATA_Y_OFFSET: if(write) d->yofs = v; else ret = d->yofs * 10000; break;
    case Z30R_WEAPDATA_TILE_W:
        if(!write)
            ret = d->tilew * 10000;
        else if(BC::checkBounds(v, 0, 20, "weapondata->TileWidth") == SH::_NoError)
            d->tilew = v;
        break;
    case Z30R_WEAPDATA_TILE_H:
        if(!write)
            ret = d->tileh * 10000;
        else if(BC::checkBounds(v, 0, 20, "weapondata->TileHeight") == SH::_NoError)
            d->tileh = v;
        break;
    // Step is in hundredths of a pixel, as weapon->Step
    case Z30R_WEAPDATA_STEP: if(write) d->step = value / 100; else ret = d->step * 100; break;
    case Z30R_WEAPDATA_BOUNCE_MULT: if(write) d->bounce_mult = value; else ret = d->bounce_mult; break;
    case Z30R_WEAPDATA_BOUNCE_ADD: if(write) d->bounce_add = value; else ret = d->bounce_add; break;
    case Z30R_WEAPDATA_UNBLOCKABLE: if(write) d->unblockable = (byte)(v & 0xF); else ret = d->unblockable * 10000; break;   // WPNUNB_ALL
    case Z30R_WEAPDATA_TIMEOUT: if(write) d->timeout = v > 0 ? v : 0; else ret = d->timeout * 10000; break;
    case Z30R_WEAPDATA_USEWEAPON: if(write) d->imitate_weapon = (byte)z30d_bound(v, 0, 255); else ret = d->imitate_weapon * 10000; break;
    case Z30R_WEAPDATA_USEDEFENCE: if(write) d->default_defense = (byte)z30d_bound(v, 0, 255); else ret = d->default_defense * 10000; break;
    case Z30R_WEAPDATA_LIFTLEVEL: if(write) d->lift_level = (byte)z30d_bound(v, 0, 255); else ret = d->lift_level * 10000; break;
    case Z30R_WEAPDATA_LIFTTIME: if(write) d->lift_time = (byte)z30d_bound(v, 0, 255); else ret = d->lift_time * 10000; break;
    case Z30R_WEAPDATA_LIFTHEIGHT: if(write) d->lift_height = value; else ret = d->lift_height; break;
    case Z30R_WEAPDATA_SCRIPT:
        if(!write)
            ret = d->script * 10000;
        else
        {
            d->script = (word)z30d_bound(v, 0, NUMSCRIPTWEAPONS - 1);

            if(q30_quest_rule(655))
                memset(d->initd, 0, sizeof(d->initd));
        }
        break;
    case Z30R_WEAPDATA_PIERCE: if(write) d->pierce_count = (short)z30d_bound(v, -1, 32767); else ret = d->pierce_count * 10000; break;
    case Z30R_WEAPDATA_VIEWPORT_SUSPEND_RANGE: if(write) d->viewport_suspend_range = v; else ret = d->viewport_suspend_range * 10000; break;
    case Z30R_WEAPDATA_VIEWPORT_DESPAWN_RANGE: if(write) d->viewport_despawn_range = v; else ret = d->viewport_despawn_range * 10000; break;

    // the arrays: Flags[2], MoveFlags[11], WeaponFlags[], BurnSprites[5], BurnLightRadius[5],
    // BurnLightOffset[5], InitD[8]
    case Z30R_WEAPDATA_MISC_FLAGS: case Z30R_WEAPDATA_MOVE_FLAGS: case Z30R_WEAPDATA_WEAP_FLAGS:
    {
        const long size = arg == Z30R_WEAPDATA_MISC_FLAGS ? 2 : arg == Z30R_WEAPDATA_MOVE_FLAGS ? 11 : 16;

        if(!z30_array_index(Z30_INDEX, size, n))
            break;

        dword bits = arg == Z30R_WEAPDATA_MISC_FLAGS ? d->flags : arg == Z30R_WEAPDATA_MOVE_FLAGS ? d->moveflags : d->wflags;

        if(!write)
        {
            ret = (bits & (1u << n)) ? 10000 : 0;
            break;
        }

        bits = value ? (bits | (1u << n)) : (bits & ~(1u << n));

        if(arg == Z30R_WEAPDATA_MISC_FLAGS)
            d->flags = (word)bits;
        else if(arg == Z30R_WEAPDATA_MOVE_FLAGS)
            d->moveflags = bits;
        else
            d->wflags = (word)bits;
        break;
    }
    case Z30R_WEAPDATA_BURN_SPRITES: case Z30R_WEAPDATA_BURN_LIGHTRAD: case Z30R_WEAPDATA_BURN_LIGHTOFFSET:
        if(!z30_array_index(Z30_INDEX, 5, n))
            break;

        if(arg == Z30R_WEAPDATA_BURN_SPRITES)
        {
            if(write) d->burnsprs[n] = (word)z30d_bound(v, 0, 8191); else ret = d->burnsprs[n] * 10000;
        }
        else if(arg == Z30R_WEAPDATA_BURN_LIGHTRAD)
        {
            if(write) d->light_rads[n] = (byte)z30d_bound(v, 0, 255); else ret = d->light_rads[n] * 10000;
        }
        else if(write)
            d->light_offsets[n] = v;
        else
            ret = d->light_offsets[n] * 10000;
        break;
    default:    // InitD[8], raw
        if(!z30_array_index(Z30_INDEX, 8, n))
            break;

        if(write)
            d->initd[n] = value;
        else
            ret = d->initd[n];
        break;
    }

    return true;
}

void q30_weapon_seed_data(weapon *w, const Q30WeaponData &d, bool lw);    // q30_weapon_fx.h: pierce, bounce, lifting, burning sprites

// upstream weapon::load_weap_data, for what this engine's weapons have
static void z30d_apply_weapdata(const Q30WeaponData &d, long uid, bool lweapon)
{
    weapon *w = (weapon *)(lweapon ? Lwpns : Ewpns).getByUID(uid);

    if(!w)
    {
        Z_scripterrlog("Invalid %s using UID = %ld\n", lweapon ? "lweapon" : "eweapon", uid);
        return;
    }

    const int id = w->id;
    // (the melee and special weapons keep their move flags; these and the bug net keep their step)
    const bool melee = id == wSword || id == wHammer || id == wWand || id == wBomb || id == wSBomb || id == wWhistle || id == wCatching
                       || id == wHookshot || id == wHSHandle || id == wHSChain || id == wSSparkle || id == wFSparkle || id == wSmack
                       || id == wPhantom || id == wStomp;
    const bool keep_step = id == wSword || id == wHammer || id == wWand || id == wHSHandle || id == wHSChain || id == wWhistle;
    Zasm30SpriteState &st = zasm30_sprite_state(uid);

    if(!melee)
        st.move_flags = (long)d.moveflags;

    st.glow_shape = d.glow_shape;

    if(d.flags & Z30D_WD_GLOW_RAD)
    {
        st.glow_rad = d.light_rads[0];
        st.glow_ofs = d.light_offsets[0];
    }

    enum { O_TILE_W = 0x1, O_TILE_H = 0x2, O_HIT_W = 0x4, O_HIT_H = 0x8, O_HIT_Z = 0x10, O_HIT_X = 0x20, O_HIT_Y = 0x40,
           O_DRAW_X = 0x80, O_DRAW_Y = 0x100 };

    if(d.override_flags)
    {
        w->extend = 3;

        if(d.override_flags & O_TILE_W) w->txsz = (int)d.tilew;
        if(d.override_flags & O_TILE_H) w->tysz = (int)d.tileh;
        if(d.override_flags & O_HIT_W) w->hxsz = (int)d.hxsz;
        if(d.override_flags & O_HIT_H) w->hysz = (int)d.hysz;
        if(d.override_flags & O_HIT_Z) w->hzsz = (int)d.hzsz;
        if(d.override_flags & O_HIT_X) w->hxofs = (int)d.hxofs;
        if(d.override_flags & O_HIT_Y) w->hyofs = (int)d.hyofs;
        if(d.override_flags & O_DRAW_X) w->xofs = (int)d.xofs;
        if(d.override_flags & O_DRAW_Y) w->yofs = (int)d.yofs + (q30_quest_rule(319) ? playing_field_offset : original_playing_field_offset);   // qr_OLD_DRAWOFFSET
    }

    if((d.flags & Z30D_WD_SET_STEP) && !keep_step)
        w->step = d.step / 10000.0;

    if(lweapon)
        zasm30_store_set(Z30R_LWPNUSEWEAPON, uid, 0, d.imitate_weapon * 10000);
    st.unblockable = d.unblockable;
    st.weap_timeout = d.timeout;
    st.weapon_flags = d.wflags;
    st.script = d.script;
    memcpy(st.initd, d.initd, sizeof(st.initd));
    q30_weapon_seed_data(w, d, lweapon);
}

static bool z30d_weapdata_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_CREATEWEAPDATA:
    {
        const long id = zasm30_object_id_new();     // (upstream's shared object ids)
        Z30_REF(WEAPDATA) = id;
        zasm30_weapdatas[id] = Q30WeaponData();
        ri->d[2] = id;
    }
        return true;

    case Z30C_WEAPDATA_COPY:
    {
        const Q30WeaponData *src = z30d_weapdata(Z30_REF(WEAPDATA), true);
        Q30WeaponData *dest = src ? z30d_weapdata(get_register(sarg1), true) : NULL;

        if(dest)
            *dest = *src;
        return true;
    }
    case Z30C_WEAPDATA_APPLY:
    {
        const Q30WeaponData *d = z30d_weapdata(Z30_REF(WEAPDATA), true);
        const long uid = get_register(sarg1);

        if(d)
            z30d_apply_weapdata(*d, uid, Lwpns.getByUID(uid) != NULL);
        return true;
    }
    default:
        return false;
    }
}

// ---- r16: combodata lift and animation fields, mapdata / Screen-> plain fields, npcdata --------

// A combo's 3.0 data for a read (the combo's defaults when the quest gave it none).
static const Q30Combo &z30d_combo_rd(long id)
{
    static const Q30Combo blank;
    const Q30Combo *c = q30_combo((int)id);
    return c ? *c : blank;
}

// combodata->AClk: the clock of an animated combo (2.53's animated_combo_table4), 0 for others
static word *z30d_combo_aclk(long id)
{
    for(word x = 0; x < animated_combos; ++x)
        if(animated_combo_table4[x][0] == id)
            return &animated_combo_table4[x][1];

    return NULL;
}

// weapondata objects that are views of a combo's weapon data (upstream's wdata_type): one ref
// per (type, index), as upstream's make_new_user_weapondata reuses them
enum { Z30D_WDV_COMBO_MISC = 1, Z30D_WDV_COMBO_LIFT, Z30D_WDV_NPCDATA, Z30D_WDV_NPC, Z30D_WDV_ITEM };
static std::map<std::pair<int, long>, long> zasm30_weapdata_view_refs;
static std::map<long, std::pair<int, long> > zasm30_weapdata_views;

static long z30d_weapdata_view(int type, long index)
{
    std::map<std::pair<int, long>, long>::iterator it = zasm30_weapdata_view_refs.find(std::make_pair(type, index));

    if(it != zasm30_weapdata_view_refs.end())
        return it->second;

    const long ref = zasm30_object_id_new();
    zasm30_weapdata_view_refs[std::make_pair(type, index)] = ref;
    zasm30_weapdata_views[ref] = std::make_pair(type, index);
    return ref;
}

static Q30WeaponData *z30d_guy_weap_view(bool npc, long index);    // (r17, below)

static Q30WeaponData *z30d_weapdata_view_data(long ref)
{
    std::map<long, std::pair<int, long> >::iterator it = zasm30_weapdata_views.find(ref);

    if(it == zasm30_weapdata_views.end())
        return NULL;

    if(it->second.first == Z30D_WDV_NPCDATA || it->second.first == Z30D_WDV_NPC)
        return z30d_guy_weap_view(it->second.first == Z30D_WDV_NPC, it->second.second);
    if(it->second.first == Z30D_WDV_ITEM)  // an item's weapon data (the item reader's record)
    {
        Q30ItemRec *r = q30_item_rec((int)it->second.second);
        return r ? &r->weap : NULL;
    }

    Q30Combo *c = q30_combo_for_write((int)it->second.second);

    if(!c)
        return NULL;

    return it->second.first == Z30D_WDV_COMBO_MISC ? &c->misc_weap : &c->lift_weap;
}

static bool z30d_combo_lift_reg(const long arg, long &ret, const long value, const bool write)
{
    switch(arg)
    {
    case Z30R_COMBODACLK: case Z30R_COMBODATA_CSET2_FLASH_DURATION: case Z30R_COMBODATA_DELAY:
    case Z30R_COMBODATA_LIFT_WEAPONDATA: case Z30R_COMBODATA_MISC_WEAPONDATA: case Z30R_COMBODCSET2FLAGS:
    case Z30R_COMBODLIFTBREAKSFX: case Z30R_COMBODLIFTBREAKSPRITE: case Z30R_COMBODLIFTDAMAGE: case Z30R_COMBODLIFTFLAGS:
    case Z30R_COMBODLIFTGFXCCSET: case Z30R_COMBODLIFTGFXCOMBO: case Z30R_COMBODLIFTGFXSPRITE: case Z30R_COMBODLIFTGFXTYPE:
    case Z30R_COMBODLIFTHEIGHT: case Z30R_COMBODLIFTITEM: case Z30R_COMBODLIFTLEVEL: case Z30R_COMBODLIFTLIGHTRAD:
    case Z30R_COMBODLIFTLIGHTSHAPE: case Z30R_COMBODLIFTSFX: case Z30R_COMBODLIFTTIME: case Z30R_COMBODLIFTUNDERCMB:
    case Z30R_COMBODLIFTUNDERCS: case Z30R_COMBODLIFTWEAPONITEM: case Z30R_COMBOD_LARGECOMBO_DIRS:
        break;
    default:
        return false;
    }

    const long id = Z30_REF(COMBODATA);

    if(id < 0 || id >= MAXCOMBOS)   // checkComboRef
    {
        Z_scripterrlog("Invalid combodata ID: %ld\n", id);
        if(!write)
            ret = (arg == Z30R_COMBODCSET2FLAGS || arg == Z30R_COMBODATA_LIFT_WEAPONDATA || arg == Z30R_COMBODATA_MISC_WEAPONDATA
                   || arg == Z30R_COMBODLIFTFLAGS || arg == Z30R_COMBOD_LARGECOMBO_DIRS) ? 0 : -10000;
        return true;
    }

    const long v = value / 10000;
    long n;

    if(!write)
    {
        const Q30Combo &c = z30d_combo_rd(id);

        switch(arg)
        {
        case Z30R_COMBODACLK: { word *clk = z30d_combo_aclk(id); ret = (clk ? *clk : 0) * 10000; break; }
        case Z30R_COMBODATA_CSET2_FLASH_DURATION: ret = c.cs2_flash_duration * 10000; break;
        case Z30R_COMBODATA_DELAY: ret = c.delay * 10000; break;
        case Z30R_COMBODATA_LIFT_WEAPONDATA: ret = Z30_REF(WEAPDATA) = z30d_weapdata_view(Z30D_WDV_COMBO_LIFT, id); break;
        case Z30R_COMBODATA_MISC_WEAPONDATA: ret = Z30_REF(WEAPDATA) = z30d_weapdata_view(Z30D_WDV_COMBO_MISC, id); break;
        case Z30R_COMBODCSET2FLAGS: ret = ((combobuf[id].csets & 0xF0) >> 4) * 10000; break;
        case Z30R_COMBODLIFTBREAKSFX: ret = c.liftbreaksfx * 10000; break;
        case Z30R_COMBODLIFTBREAKSPRITE: ret = c.liftbreaksprite * 10000; break;
        case Z30R_COMBODLIFTDAMAGE: ret = c.liftdmg * 10000; break;
        case Z30R_COMBODLIFTGFXCCSET: ret = c.liftcs * 10000; break;
        case Z30R_COMBODLIFTGFXCOMBO: ret = c.liftcmb * 10000; break;
        case Z30R_COMBODLIFTGFXSPRITE: ret = c.liftsprite * 10000; break;
        case Z30R_COMBODLIFTGFXTYPE: ret = c.liftgfx * 10000; break;
        case Z30R_COMBODLIFTHEIGHT: ret = c.lifthei * 10000; break;
        case Z30R_COMBODLIFTITEM: ret = c.liftitm * 10000; break;
        case Z30R_COMBODLIFTLEVEL: ret = c.liftlvl * 10000; break;
        case Z30R_COMBODLIFTLIGHTRAD: ret = c.lift_weap.light_rads[0] * 10000; break;
        case Z30R_COMBODLIFTLIGHTSHAPE: ret = c.lift_weap.glow_shape * 10000; break;
        case Z30R_COMBODLIFTSFX: ret = c.liftsfx * 10000; break;
        case Z30R_COMBODLIFTTIME: ret = c.lifttime * 10000; break;
        case Z30R_COMBODLIFTUNDERCMB: ret = c.liftundercmb * 10000; break;
        case Z30R_COMBODLIFTUNDERCS: ret = c.liftundercs * 10000; break;
        case Z30R_COMBODLIFTWEAPONITEM: ret = c.lift_parent_item * 10000; break;
        case Z30R_COMBODLIFTFLAGS:      // LiftFlags[8]: the flag's bit (x10000); LF_BREAKONSOLID is the weapon's
            if(!z30d_index(Z30_INDEX, 8, false, n)) { ret = 0; break; }
            ret = ((1L << n) == 0x40 ? (long)(c.lift_weap.wflags & 0x4) : (long)(c.liftflags & (1 << n))) * 10000;
            break;
        default:                        // LargeComboDirs[4]
            if(!z30d_index(Z30_INDEX, 4, false, n)) { ret = 0; break; }
            ret = (c.large_combo_dirs & (1 << n)) ? 10000 : 0;
            break;
        }
        return true;
    }

    if(arg == Z30R_COMBODATA_LIFT_WEAPONDATA || arg == Z30R_COMBODATA_MISC_WEAPONDATA)
        return true;        // read only

    if(arg == Z30R_COMBODACLK)
    {
        if(word *clk = z30d_combo_aclk(id))
            *clk = (word)z30d_bound(v, 0, 255);
        return true;
    }

    if(arg == Z30R_COMBODCSET2FLAGS)    // (upstream takes the raw value here)
    {
        combobuf[id].csets = (byte)((combobuf[id].csets & 0xF) | ((value & 0xF) << 4));
        return true;
    }

    Q30Combo *c = q30_combo_for_write((int)id);

    if(!c)
        return true;

    switch(arg)
    {
    case Z30R_COMBODATA_CSET2_FLASH_DURATION: c->cs2_flash_duration = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODATA_DELAY: c->delay = (word)z30d_bound(v, 0, 65535); break;
    case Z30R_COMBODLIFTBREAKSFX: c->liftbreaksfx = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTBREAKSPRITE: c->liftbreaksprite = (short)z30d_bound(v, -4, 255); break;
    case Z30R_COMBODLIFTDAMAGE: c->liftdmg = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTGFXCCSET: c->liftcs = (byte)z30d_bound(v, 0, 13); break;
    case Z30R_COMBODLIFTGFXCOMBO: c->liftcmb = (word)z30d_bound(v, 0, MAXCOMBOS - 1); break;
    case Z30R_COMBODLIFTGFXSPRITE: c->liftsprite = (word)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTGFXTYPE: c->liftgfx = (byte)z30d_bound(v, 0, 2); break;
    case Z30R_COMBODLIFTHEIGHT: c->lifthei = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTITEM: c->liftitm = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTLEVEL: c->liftlvl = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTLIGHTRAD: c->lift_weap.light_rads[0] = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTLIGHTSHAPE: c->lift_weap.glow_shape = (byte)z30d_bound(v, 0, 2); break;
    case Z30R_COMBODLIFTSFX: c->liftsfx = (word)z30d_bound(v, 0, Z30_MAX_SFX_UP); break;
    case Z30R_COMBODLIFTTIME: c->lifttime = (byte)z30d_bound(v, 0, 255); break;
    case Z30R_COMBODLIFTUNDERCMB: c->liftundercmb = (word)z30d_bound(v, 0, MAXCOMBOS - 1); break;
    case Z30R_COMBODLIFTUNDERCS: c->liftundercs = (byte)z30d_bound(v, 0, 13); break;
    case Z30R_COMBODLIFTWEAPONITEM: c->lift_parent_item = (word)z30d_bound(v, 0, Z30_MAX_ITEMS_UP - 1); break;
    case Z30R_COMBODLIFTFLAGS:
        if(!z30d_index(Z30_INDEX, 8, false, n)) break;
        if((1L << n) == 0x40)
            c->lift_weap.wflags = (word)(value ? (c->lift_weap.wflags | 0x4) : (c->lift_weap.wflags & ~0x4));
        else
            c->liftflags = (byte)(value ? (c->liftflags | (1 << n)) : (c->liftflags & ~(1 << n)));
        break;
    default:
        if(!z30d_index(Z30_INDEX, 4, false, n)) break;
        c->large_combo_dirs = (byte)(value ? (c->large_combo_dirs | (1 << n)) : (c->large_combo_dirs & ~(1 << n)));
        break;
    }

    return true;
}

// The high 16 bits of a screen's noreset / nocarry and its exstate_reset / exstate_carry (MAP
// v34+), which 2.53's mapscr has no room for; index map*MAPSCRS+screen.
struct Z30dCarry { dword noreset_hi, nocarry_hi, exreset, excarry; };
static std::map<int, Z30dCarry> zasm30_screen_carry;

void q30_set_screen_carry(int index, dword noreset, dword nocarry, dword exreset, dword excarry)
{
    if(index < 0)
    {
        zasm30_screen_carry.clear();
        return;
    }

    Z30dCarry c = { noreset >> 16, nocarry >> 16, exreset, excarry };
    zasm30_screen_carry[index] = c;
}

// the map screen index (map*MAPSCRS+screen) of a mapdata ref or of the current screen; -1 for
// one screen of a scrolling region
static int z30d_screen_index(long ref, bool screen)
{
    if(screen)
        return currmap * MAPSCRS + currscr;

    if(ref > 0)
        return (int)(ref - 1);

    return z30_md_here(ref) ? currmap * MAPSCRS + currscr : -1;
}

// mapdata-> and Screen-> fields 2.53's mapscr holds (upstream's GET/SET_MAPDATA_VAR_* and the
// screendata ones): the mapdata's screen, or the current one.
extern std::map<long, byte> g_q30_door_none;   // q255_maps.cpp: doors the quest left as dNONE

static bool z30d_scr_reg(const long arg, long &ret, const long value, const bool write)
{
    enum { F_NONE, F_VALID, F_CSENS, F_HOLDUP, F_SECRET, F_BOSS, F_MAP, F_SCREEN, F_MIDI, F_MUSIC, F_NORESET, F_NOCARRY,
           F_NORESETARR, F_NOCARRYARR, F_EXRESET, F_EXCARRY, F_UNIMPL, F_COLOUR, F_DOORCSET, F_ENEMYFLAGS,
           F_EXITDIR, F_GUY, F_LENSLAYER, F_NEXTMAP, F_NEXTSCR, F_ROOM, F_SIDEWARPIDX, F_STAIRX, F_STAIRY, F_TWTICS,
           F_UNDERCMB, F_UNDERCS, F_ARRX, F_ARRY, F_WARPRETC, F_DOOR, F_PATH, F_LAYEROPAC, F_SWDMAP, F_SWSCR, F_SWTYPE,
           F_SWOVFLAGS, F_TWOVFLAGS, F_HASITEM, F_ZERO, F_GUYCOUNT };
    int f = F_NONE;
    bool screen = false;

    switch(arg)
    {
    case Z30R_MAPDATAVALID: f = F_VALID; break;                 case Z30R_SCREENDATAVALID: f = F_VALID; screen = true; break;
    case Z30R_MAPDATACSENSITIVE: f = F_CSENS; break;            case Z30R_SCREENDATACSENSITIVE: f = F_CSENS; screen = true; break;
    case Z30R_MAPDATAHOLDUPSFX: f = F_HOLDUP; break;            case Z30R_SCREENDATAHOLDUPSFX: f = F_HOLDUP; screen = true; break;
    case Z30R_MAPDATASECRETSFX: f = F_SECRET; break;
    case Z30R_SCREENDATABOSSSFX: f = F_BOSS; screen = true; break;
    case Z30R_MAPDATAMAP: f = F_MAP; break;
    case Z30R_MAPDATASCREEN: f = F_SCREEN; break;               case Z30R_SCREEN_INDEX: f = F_SCREEN; screen = true; break;
    case Z30R_MAPDATASCREENMIDI: f = F_MIDI; break;             case Z30R_SCREENDATASCREENMIDI: f = F_MIDI; screen = true; break;
    case Z30R_MAPDATA_MUSIC: f = F_MUSIC; break;
    case Z30R_MAPDATANORESET: f = F_NORESET; break;             case Z30R_SCREENDATANORESET: f = F_NORESET; screen = true; break;
    case Z30R_MAPDATANOCARRY: f = F_NOCARRY; break;             case Z30R_SCREENDATANOCARRY: f = F_NOCARRY; screen = true; break;
    case Z30R_MAPDATANORESETARR: f = F_NORESETARR; break;       case Z30R_SCREENDATANORESETARR: f = F_NORESETARR; screen = true; break;
    case Z30R_MAPDATANOCARRYARR: f = F_NOCARRYARR; break;       case Z30R_SCREENDATANOCARRYARR: f = F_NOCARRYARR; screen = true; break;
    case Z30R_MAPDATAEXRESET: f = F_EXRESET; break;             case Z30R_SCREENDATAEXRESET: f = F_EXRESET; screen = true; break;
    case Z30R_MAPDATAEXCARRY: f = F_EXCARRY; break;             case Z30R_SCREENDATAEXCARRY: f = F_EXCARRY; screen = true; break;
    case Z30R_MAPDATASCRIPTENTRY: case Z30R_MAPDATASCRIPTEXIT: case Z30R_MAPDATASCRIPTOCCUPANCY: f = F_UNIMPL; break;
    case Z30R_SCREENDATACOLOUR: f = F_COLOUR; screen = true; break;
    case Z30R_MAPDATADOORCOMBOSET: f = F_DOORCSET; break;       case Z30R_SCREENDATADOORCOMBOSET: f = F_DOORCSET; screen = true; break;
    case Z30R_MAPDATAENEMYFLAGS: f = F_ENEMYFLAGS; break;       case Z30R_SCREENDATAENEMYFLAGS: f = F_ENEMYFLAGS; screen = true; break;
    case Z30R_SCREENDATAEXITDIR: f = F_EXITDIR; screen = true; break;
    case Z30R_SCREENDATAGUY: f = F_GUY; screen = true; break;
    case Z30R_SCREENDATALENSLAYER: f = F_LENSLAYER; screen = true; break;
    case Z30R_MAPDATANEXTMAP: f = F_NEXTMAP; break;             case Z30R_SCREENDATANEXTMAP: f = F_NEXTMAP; screen = true; break;
    case Z30R_MAPDATANEXTSCREEN: f = F_NEXTSCR; break;          case Z30R_SCREENDATANEXTSCREEN: f = F_NEXTSCR; screen = true; break;
    case Z30R_SCREENDATAROOM: f = F_ROOM; screen = true; break;
    case Z30R_MAPDATASIDEWARPINDEX: f = F_SIDEWARPIDX; break;   case Z30R_SCREENDATASIDEWARPINDEX: f = F_SIDEWARPIDX; screen = true; break;
    case Z30R_SCREENDATASTAIRX: f = F_STAIRX; screen = true; break;
    case Z30R_SCREENDATASTAIRY: f = F_STAIRY; screen = true; break;
    case Z30R_MAPDATATIMEDWARPTICS: f = F_TWTICS; break;        case Z30R_SCREENDATATIMEDWARPTICS: f = F_TWTICS; screen = true; break;
    case Z30R_MAPDATAUNDERCOMBO: f = F_UNDERCMB; break;
    case Z30R_MAPDATAUNDERCSET: f = F_UNDERCS; break;
    case Z30R_SCREENDATAWARPARRIVALX: f = F_ARRX; screen = true; break;
    case Z30R_SCREENDATAWARPARRIVALY: f = F_ARRY; screen = true; break;
    case Z30R_MAPDATAWARPRETURNC: f = F_WARPRETC; break;        case Z30R_SCREENDATAWARPRETURNC: f = F_WARPRETC; screen = true; break;
    case Z30R_SCREENDATADOOR: f = F_DOOR; screen = true; break;
    case Z30R_SCREENDATAPATH: f = F_PATH; screen = true; break;
    case Z30R_SCREENDATALAYEROPACITY: f = F_LAYEROPAC; screen = true; break;
    case Z30R_SCREENDATASIDEWARPDMAP: f = F_SWDMAP; screen = true; break;
    case Z30R_SCREENDATASIDEWARPSC: f = F_SWSCR; screen = true; break;
    case Z30R_SCREENDATASIDEWARPTYPE: f = F_SWTYPE; screen = true; break;
    case Z30R_SCREENDATASIDEWARPOVFLAGS: f = F_SWOVFLAGS; screen = true; break;
    case Z30R_SCREENDATATILEWARPOVFLAGS: f = F_TWOVFLAGS; screen = true; break;
    case Z30R_MAPDATAHASITEM: f = F_HASITEM; break;
    case Z30R_MAPDATALENSLAYER: f = F_LENSLAYER; break;
    case Z30R_MAPDATAVIEWX: case Z30R_MAPDATAVIEWY: case Z30R_MAPDATASCREENWIDTH: case Z30R_MAPDATASCREENHEIGHT:
        if(!write)
            ret = 0;     // (upstream reads nothing here)
        return true;
    case Z30R_MAPDATAGUYCOUNT: f = F_GUYCOUNT; break;          case Z30R_SCREENDATAGUYCOUNT: f = F_GUYCOUNT; screen = true; break;
    default:
        return false;
    }

    if(f == F_GUYCOUNT)     // game->guys[] of the screen (screens 0x80 and up have none)
    {
        const int mi = screen ? (currscr < MAPSCRSNORMAL ? currmap * MAPSCRSNORMAL + currscr : -1)
                       : (z30_mapdata(Z30_REF(MAPDATA)) ? z30_mapdata_mi(Z30_REF(MAPDATA)) : -1);

        if(!write)
            ret = mi < 0 ? -10000 : game->guys[mi] * 10000;
        else if(mi >= 0)
            game->guys[mi] = (byte)z30d_bound(value / 10000, 0, 10);
        return true;
    }

    if(f == F_UNIMPL)
    {
        if(!write)
        {
            Z_scripterrlog("Unimplemented: %s\n", arg == Z30R_MAPDATASCRIPTENTRY ? "ScriptEntry" : arg == Z30R_MAPDATASCRIPTEXIT ? "ExitScript" : "ScriptOccupancy");
            ret = -10000;
        }
        return true;
    }

    const long ref = Z30_REF(MAPDATA);
    mapscr *m = screen ? tmpscr : z30_mapdata(ref);

    if(!m)
    {
        if(!write)
            ret = (f == F_MUSIC) ? -1 : (f == F_NORESETARR || f == F_NOCARRYARR || f == F_EXRESET || f == F_EXCARRY) ? 0 : -10000;
        return true;
    }

    const int index = z30d_screen_index(ref, screen);
    const long v = value / 10000;
    long n = 0;

    // the arrays: their index first
    long size = 0;

    switch(f)
    {
    case F_NORESETARR: case F_NOCARRYARR: size = 17; break;    // mMAXIND
    case F_EXRESET: case F_EXCARRY: size = 32; break;
    case F_DOOR: case F_PATH: case F_SWDMAP: case F_SWSCR: case F_SWTYPE: size = 4; break;
    case F_LAYEROPAC: size = 7; break;
    default: break;
    }

    if(size && !z30d_index(Z30_INDEX, size, false, n))
    {
        if(!write)
            ret = 0;
        return true;
    }

    static Z30dCarry none;
    memset(&none, 0, sizeof(none));
    Z30dCarry &cr = index >= 0 ? zasm30_screen_carry[index] : none;
    const dword noreset = (cr.noreset_hi << 16) | m->noreset, nocarry = (cr.nocarry_hi << 16) | m->nocarry;

    if(!write)
    {
        switch(f)
        {
        case F_VALID: ret = m->valid * 10000; break;
        case F_CSENS: ret = m->csensitive * 10000; break;
        case F_HOLDUP: ret = m->holdupsfx * 10000; break;
        case F_SECRET: ret = m->secretsfx * 10000; break;
        case F_BOSS: ret = m->bosssfx * 10000; break;
        case F_MAP: ret = (index >= 0 ? index / MAPSCRS + 1 : currmap + 1) * 10000; break;
        case F_SCREEN: ret = (index >= 0 ? index % MAPSCRS : currscr) * 10000; break;
        case F_MIDI: ret = m->screen_midi < 0 ? -40000 : m->screen_midi * 10000; break;    // -4: the DMap's
        case F_MUSIC: ret = m->screen_midi < 0 ? -1 : m->screen_midi; break;
        case F_NORESET: ret = (long)noreset * 10000; break;
        case F_NOCARRY: ret = (long)nocarry * 10000; break;
        case F_NORESETARR: ret = (noreset & (1u << n)) ? 10000 : 0; break;
        case F_NOCARRYARR: ret = (nocarry & (1u << n)) ? 10000 : 0; break;
        case F_EXRESET: ret = (cr.exreset & (1u << n)) ? 10000 : 0; break;
        case F_EXCARRY: ret = (cr.excarry & (1u << n)) ? 10000 : 0; break;
        case F_COLOUR: ret = m->color * 10000; break;
        case F_DOORCSET: ret = m->door_combo_set * 10000; break;
        case F_ENEMYFLAGS: ret = m->enemyflags * 10000; break;
        case F_EXITDIR: ret = m->exitdir * 10000; break;
        case F_GUY: ret = m->guy * 10000; break;
        case F_LENSLAYER: ret = m->lens_layer * 10000; break;
        case F_NEXTMAP: ret = m->nextmap * 10000; break;
        case F_NEXTSCR: ret = m->nextscr * 10000; break;
        case F_ROOM: ret = m->room * 10000; break;
        case F_SIDEWARPIDX: ret = m->sidewarpindex * 10000; break;
        case F_STAIRX: ret = m->stairx * 10000; break;
        case F_STAIRY: ret = m->stairy * 10000; break;
        case F_TWTICS: ret = m->timedwarptics * 10000; break;
        case F_UNDERCMB: ret = m->undercombo * 10000; break;
        case F_UNDERCS: ret = m->undercset * 10000; break;
        case F_ARRX: ret = m->warparrivalx * 10000; break;
        case F_ARRY: ret = m->warparrivaly * 10000; break;
        case F_WARPRETC: ret = m->warpreturnc * 10000; break;
        case F_DOOR:    // (a door the quest left as dNONE reads 16, as upstream's)
        {
            const long mi = screen ? currmap * MAPSCRS + currscr : Z30_REF(MAPDATA) - 1;
            std::map<long, byte>::const_iterator it = g_q30_door_none.find(mi);
            ret = (m->door[n] == dWALL && it != g_q30_door_none.end() && ((it->second >> n) & 1)) ? 16 * 10000 : m->door[n] * 10000;
            break;
        }
        case F_PATH: ret = m->path[n] * 10000; break;
        case F_LAYEROPAC: ret = (n == 0 ? 255 : m->layeropacity[n - 1]) * 10000; break;
        case F_SWDMAP: ret = m->sidewarpdmap[n] * 10000; break;
        case F_SWSCR: ret = m->sidewarpscr[n] * 10000; break;
        case F_SWTYPE: ret = m->sidewarptype[n] * 10000; break;
        case F_SWOVFLAGS: ret = m->sidewarpoverlayflags * 10000; break;
        case F_TWOVFLAGS: ret = m->tilewarpoverlayflags * 10000; break;
        case F_HASITEM: ret = m->hasitem * 10000; break;
        default: break;
        }
        return true;
    }

    switch(f)
    {
    case F_VALID: m->valid = (byte)z30d_bound(v, 0, 255); break;
    case F_CSENS: m->csensitive = (byte)z30d_bound(v, 0, 255); break;
    case F_HOLDUP: m->holdupsfx = (byte)z30d_bound(v, 0, 255); break;
    case F_SECRET: m->secretsfx = (byte)z30d_bound(v, 0, 255); break;
    case F_BOSS: m->bosssfx = (byte)z30d_bound(v, 0, 255); break;
    case F_MIDI: m->screen_midi = (short)(v < -3 ? -1 : v); break;
    case F_MUSIC: m->screen_midi = (short)(value < 0 ? -1 : value); break;
    case F_NORESET: case F_NOCARRY: case F_NORESETARR: case F_NOCARRYARR:
    {
        const bool reset = f == F_NORESET || f == F_NORESETARR;
        dword bits = reset ? noreset : nocarry;

        if(f == F_NORESET || f == F_NOCARRY)
            bits = (dword)z30d_bound(v, -214747, 214747);
        else
            bits = value ? (bits | (1u << n)) : (bits & ~(1u << n));

        if(reset)
        {
            m->noreset = (word)bits;
            cr.noreset_hi = bits >> 16;
        }
        else
        {
            m->nocarry = (word)bits;
            cr.nocarry_hi = bits >> 16;
        }
        break;
    }
    case F_EXRESET: cr.exreset = value ? (cr.exreset | (1u << n)) : (cr.exreset & ~(1u << n)); break;
    case F_EXCARRY: cr.excarry = value ? (cr.excarry | (1u << n)) : (cr.excarry & ~(1u << n)); break;
    case F_COLOUR: m->color = (word)z30d_bound(v, -214747, 214747); break;
    case F_DOORCSET: m->door_combo_set = (word)z30d_bound(v, -214747, 214747); break;
    case F_ENEMYFLAGS: m->enemyflags = (byte)z30d_bound(v, 0, 255); break;
    case F_EXITDIR: m->exitdir = (byte)z30d_bound(v, 0, 255); break;
    case F_GUY: m->guy = (byte)z30d_bound(v, 0, 255); break;
    case F_LENSLAYER: m->lens_layer = (byte)z30d_bound(v, 0, 255); break;
    case F_NEXTMAP: m->nextmap = (byte)z30d_bound(v, 0, 255); break;
    case F_NEXTSCR: m->nextscr = (byte)z30d_bound(v, 0, 255); break;
    case F_ROOM: m->room = (byte)z30d_bound(v, 0, 255); break;
    case F_SIDEWARPIDX: m->sidewarpindex = (byte)z30d_bound(v, 0, 255); break;
    case F_STAIRX: m->stairx = (byte)z30d_bound(v, 0, 255); break;
    case F_STAIRY: m->stairy = (byte)z30d_bound(v, 0, 255); break;
    case F_TWTICS: m->timedwarptics = (word)z30d_bound(v, -214747, 214747); break;
    case F_UNDERCMB: m->undercombo = (word)z30d_bound(v, -214747, 214747); break;
    case F_UNDERCS: m->undercset = (byte)z30d_bound(v, 0, 255); break;
    case F_ARRX: m->warparrivalx = (byte)z30d_bound(v, 0, 255); break;
    case F_ARRY: m->warparrivaly = (byte)z30d_bound(v, 0, 255); break;
    case F_WARPRETC: m->warpreturnc = (word)z30d_bound(v, -214747, 214747); break;
    case F_DOOR:
        m->door[n] = (byte)z30d_bound(v, 0, 255);
        if(g_q30_door_none.count(screen ? currmap * MAPSCRS + currscr : Z30_REF(MAPDATA) - 1))
            g_q30_door_none[screen ? currmap * MAPSCRS + currscr : Z30_REF(MAPDATA) - 1] &= (byte)~(1 << n);
        break;
    case F_PATH: m->path[n] = (byte)z30d_bound(v, 0, 255); break;
    case F_LAYEROPAC: if(n > 0) m->layeropacity[n - 1] = (byte)z30d_bound(v, 0, 255); break;
    case F_SWDMAP: m->sidewarpdmap[n] = (word)z30d_bound(v, 0, MAXDMAPS - 1); break;
    case F_SWSCR: m->sidewarpscr[n] = (byte)z30d_bound(v, 0, 255); break;
    case F_SWTYPE: m->sidewarptype[n] = (byte)z30d_bound(v, 0, 255); break;
    case F_SWOVFLAGS: m->sidewarpoverlayflags = (byte)z30d_bound(v, 0, 255); break;
    case F_TWOVFLAGS: m->tilewarpoverlayflags = (byte)z30d_bound(v, 0, 255); break;
    case F_HASITEM: m->hasitem = (byte)z30d_bound(v, 0, 255); break;
    default: break;     // Map, Screen: read only
    }

    return true;
}

// What 2.53's mapscr has no room for (index map*MAPSCRS+screen): the lens layers (MAP v28+), the
// hidden layers and hidden script draw layers (v22+), the gravity (v35+, zfix values, x10000) and
// entry_x / entry_y (only scripts set them). Only scripts look at them here.
struct Z30dScreenExtra
{
    byte lens_show, lens_hide, hidelayers, hidescriptlayers, entry_x, entry_y;
    long gravity, terminal_v;
};
static std::map<int, Z30dScreenExtra> zasm30_screen_extras;

void q30_set_screen_extras(int index, byte lens_show, byte lens_hide, byte hidelayers, byte hidescriptlayers,
                           long gravity, long terminal_v, int which)
{
    if(index < 0)
    {
        zasm30_screen_extras.clear();
        return;
    }

    Z30dScreenExtra &e = zasm30_screen_extras[index];

    if(which & 1) { e.lens_show = lens_show; e.lens_hide = lens_hide; }
    if(which & 2) { e.hidelayers = hidelayers; e.hidescriptlayers = hidescriptlayers; }
    if(which & 4) { e.gravity = gravity; e.terminal_v = terminal_v; }
}

// upstream's sprite::get_gravity / get_terminalv for a 2.55/3.0 quest: the screen's own gravity (flags10
// fSCREEN_GRAVITY) or else the DMap's (dmfCUSTOM_GRAVITY), zfix values (x10000); false when neither sets one
bool q30_custom_gravity(int index, bool screen_grav, int dmap, long &gravity, long &terminal_v)
{
    gravity = terminal_v = 0;
    if(screen_grav)
    {
        std::map<int, Z30dScreenExtra>::const_iterator it = zasm30_screen_extras.find(index);

        if(it != zasm30_screen_extras.end())
        {
            gravity = it->second.gravity;
            terminal_v = it->second.terminal_v;
        }
        return true;
    }
    if(unsigned(dmap) < MAXDMAPS && (DMaps[dmap].flags & 0x800000))      // dmfCUSTOM_GRAVITY
    {
        std::map<int, std::pair<long, long> >::const_iterator it = zasm30_dmap_gravity.find(dmap);

        if(it != zasm30_dmap_gravity.end())
        {
            gravity = it->second.first;
            terminal_v = it->second.second;
        }
        return true;
    }
    return false;
}

// The screen's hidden combo layers and hidden script draw layers (bit n = layer n), for drawing:
// upstream's do_layer / putscr leave a hidden layer's combos out, do_primitives a hidden layer's
// script drawing. 0 for a screen with none (every 2.53 quest's).
int q30_screen_hidden_layers(int index, bool script)
{
    std::map<int, Z30dScreenExtra>::const_iterator it = zasm30_screen_extras.find(index);

    if(it == zasm30_screen_extras.end())
        return 0;

    return script ? it->second.hidescriptlayers : it->second.hidelayers;
}

enum { Z30D_QR_CLEARINITDONSCRIPTCHANGE = 655 };     // upstream's qr_CLEARINITDONSCRIPTCHANGE
bool q30_get_exstate(int index, int bit);   // q30_triggers.cpp
void q30_set_exstate(int index, int bit, bool on);
static int zasm30_portal_count();            // q30_scripts_sys.h
static long zasm30_portal_uid(int i);
static long zasm30_load_ffc(long id);       // q30_scripts_region.h

// A sprite list's UIDs, read only (upstream's Screen->NPCs[] and the others)
static bool z30d_uid_array(sprite_list &l, long &ret, bool write)
{
    long n;

    if(!write)
        ret = z30_array_index(Z30_INDEX, l.Count(), n) ? l.spr((int)n)->getUID() : 0;
    else
        Z_scripterrlog("Tried to write to a read-only array\n");
    return true;
}

// upstream's mapdata-> and Screen-> fields of these, the screen's flag bits and warp return
// squares, its region id and script, and the screen's sprite lists
static bool z30d_scr_more_reg(const long arg, long &ret, const long value, const bool write)
{
    enum { X_NONE, X_ENTRYX, X_ENTRYY, X_GRAV, X_TERMV, X_LENSHIDE, X_LENSSHOW, X_HIDE, X_HIDESCR, X_SWRET, X_TWRET,
           X_FLAG, X_FLAGS, X_SCRFLAGSD, X_REGION, X_SCRIPT, X_EXSTATE };
    int f = X_NONE;
    bool screen = false;

    switch(arg)
    {
    case Z30R_SCREEN_NPCS: return z30d_uid_array(guys, ret, write);
    case Z30R_SCREEN_ITEMS: return z30d_uid_array(items, ret, write);
    case Z30R_SCREEN_LWEAPONS: return z30d_uid_array(Lwpns, ret, write);
    case Z30R_SCREEN_EWEAPONS: return z30d_uid_array(Ewpns, ret, write);
    case Z30R_SCREEN_PORTALS:
    {
        long n;
        if(write)
            Z_scripterrlog("Tried to write to a read-only array\n");
        else
            ret = z30_array_index(Z30_INDEX, zasm30_portal_count(), n) ? zasm30_portal_uid((int)n) : 0;
        return true;
    }
    case Z30R_PORTALCOUNT:
        if(!write)
            ret = zasm30_portal_count() * 10000;
        return true;
    // Screen->FFCs[]: each FFC's reference, as this engine numbers them (upstream gives the sprite UID)
    case Z30R_SCREEN_FFCS:
    {
        long n;
        if(write)
            Z_scripterrlog("Tried to write to a read-only array\n");
        else
            ret = z30_array_index(Z30_INDEX, Q30_MAXFFCS * (q30_region_id() ? 64 : 1), n) ? zasm30_load_ffc(n) : 0;
        return true;
    }
    // Screen->NumFFCs[id]: the FFC has a combo (an id with none reads false)
    case Z30R_SCREENDATANUMFF:
    {
        if(write)
            return true;

        const long id = Z30_INDEX;
        ret = 0;

        if(id >= 0 && id < Q30_MAXFFCS && tmpscr->ffdata[id])
            ret = 10000;
        return true;
    }
    case Z30R_MAPDATAENTRYX: f = X_ENTRYX; break;
    case Z30R_MAPDATAENTRYY: f = X_ENTRYY; break;
    case Z30R_MAPDATA_GRAVITY_STRENGTH: f = X_GRAV; break;       case Z30R_SCREENDATA_GRAVITY_STRENGTH: f = X_GRAV; screen = true; break;
    case Z30R_MAPDATA_TERMINAL_VELOCITY: f = X_TERMV; break;     case Z30R_SCREENDATA_TERMINAL_VELOCITY: f = X_TERMV; screen = true; break;
    case Z30R_MAPDATALENSHIDES: f = X_LENSHIDE; break;           case Z30R_SCREENLENSHIDES: f = X_LENSHIDE; screen = true; break;
    case Z30R_MAPDATALENSSHOWS: f = X_LENSSHOW; break;           case Z30R_SCREENLENSSHOWS: f = X_LENSSHOW; screen = true; break;
    case Z30R_MAPDATALAYERINVIS: f = X_HIDE; break;
    case Z30R_MAPDATASCRIPTDRAWS: f = X_HIDESCR; break;          case Z30R_SCREENDATASCRIPTDRAWS: f = X_HIDESCR; screen = true; break;
    case Z30R_MAPDATASWARPRETSQR: f = X_SWRET; break;
    case Z30R_MAPDATATWARPRETSQR: f = X_TWRET; break;
    case Z30R_MAPDATA_FLAG: f = X_FLAG; break;
    case Z30R_SCREENDATAFLAGS: f = X_FLAGS; screen = true; break;
    case Z30R_MAPDATASCREENFLAGSD: f = X_SCRFLAGSD; break;
    case Z30R_MAPDATAREGIONID: f = X_REGION; break;
    case Z30R_MAPDATASCRIPT: f = X_SCRIPT; break;                case Z30R_SCREENSCRIPT: f = X_SCRIPT; screen = true; break;
    case Z30R_MAPDATAEXSTATED: f = X_EXSTATE; break;
    default:
        return false;
    }

    const long ref = Z30_REF(MAPDATA);
    mapscr *m = screen ? tmpscr : z30_mapdata(ref);
    const int index = z30d_screen_index(ref, screen);

    if(!m)
    {
        if(!write)
            ret = (f == X_GRAV || f == X_TERMV) ? -10000 : 0;
        return true;
    }

    // the arrays: their index first
    long size = 0, n = 0;

    switch(f)
    {
    case X_LENSHIDE: case X_LENSSHOW: case X_HIDE: case X_HIDESCR: size = 7; break;
    case X_SWRET: case X_TWRET: size = 4; break;
    case X_FLAG: size = 8 * 11; break;
    case X_FLAGS: size = 11; break;
    case X_SCRFLAGSD: size = 10; break;
    case X_EXSTATE: size = 32; break;
    default: break;
    }

    if(size && !z30d_index(Z30_INDEX, size, f == X_SCRFLAGSD, n))
    {
        if(!write)
            ret = (f == X_SWRET || f == X_TWRET || f == X_FLAGS) ? z30_array_default(-10000) : 0;
        return true;
    }

    static Z30dScreenExtra none;
    memset(&none, 0, sizeof(none));
    Z30dScreenExtra &e = index >= 0 ? zasm30_screen_extras[index] : none;
    const long v = value / 10000;

    switch(f)
    {
    case X_ENTRYX: case X_ENTRYY:
    {
        byte &b = f == X_ENTRYX ? e.entry_x : e.entry_y;
        if(!write) ret = b * 10000;
        else b = (byte)v;
        return true;
    }
    case X_GRAV: case X_TERMV:
    {
        long &g = f == X_GRAV ? e.gravity : e.terminal_v;
        if(!write) ret = g;
        else g = value;
        return true;
    }
    case X_LENSHIDE: case X_LENSSHOW: case X_HIDE: case X_HIDESCR:
    {
        byte &b = f == X_LENSHIDE ? e.lens_hide : f == X_LENSSHOW ? e.lens_show : f == X_HIDE ? e.hidelayers : e.hidescriptlayers;

        if(!write)
        {
            ret = (b & (1 << n)) ? 10000 : 0;
            return true;
        }

        b = (byte)(value ? (b | (1 << n)) : (b & ~(1 << n)));

        if(value && f == X_LENSHIDE)        // (a layer the lens hides it doesn't also show)
            e.lens_show &= (byte)~(1 << n);
        else if(value && f == X_LENSSHOW)
            e.lens_hide &= (byte)~(1 << n);
        return true;
    }
    case X_SWRET: case X_TWRET:
    {
        const int shift = (f == X_SWRET ? 8 : 0) + (int)n * 2;

        if(!write)
            ret = ((m->warpreturnc >> shift) & 3) * 10000;
        else
            m->warpreturnc = (word)((m->warpreturnc & ~(3 << shift)) | (z30d_bound(v, 0, 3) << shift));
        return true;
    }
    case X_FLAG: case X_FLAGS:
    {
        // (this engine's screen has 10 flag bytes; upstream's 11th reads 0)
        const long byte_i = f == X_FLAG ? n / 8 : n;
        byte dummy = 0;
        byte &b = byte_i < 10 ? (&m->flags)[byte_i] : dummy;

        if(f == X_FLAGS)
        {
            if(!write) ret = b * 10000;
            else b = (byte)v;
        }
        else if(!write)
            ret = (b & (1 << (n % 8))) ? 10000 : 0;
        else
            b = (byte)(value ? (b | (1 << (n % 8))) : (b & ~(1 << (n % 8))));
        return true;
    }
    case X_SCRFLAGSD:
        if(!write)
            ret = get_screenflags(m, (int)n);
        else
            Z_scripterrlog("Tried to write to a read-only array\n");
        return true;
    case X_REGION:
    {
        const int map = index >= 0 ? index / MAPSCRS : currmap, scr = index >= 0 ? index % MAPSCRS : currscr;
        int id = 0;

        if(map < (int)q255_map_region_ids.size() && scr < 128 && q255_map_region_ids[map].size() >= 64)
        {
            const byte b = q255_map_region_ids[map][(scr / 16) * 8 + (scr % 16) / 2];
            id = (scr & 1) ? (b & 0xF) : ((b >> 4) & 0xF);
        }

        if(!write)
            ret = id * 10000;
        else if(v < 0 || v > 9)
            Z_scripterrlog("Invalid value: %ld - must be >= 0 and <= 9\n", v);
        else if(map < (int)q255_map_region_ids.size() && scr < 128 && q255_map_region_ids[map].size() >= 64)
        {
            // (the regions are worked out again when a screen next loads)
            byte &b = q255_map_region_ids[map][(scr / 16) * 8 + (scr % 16) / 2];
            b = (byte)((scr & 1) ? ((b & 0xF0) | v) : ((b & 0x0F) | (v << 4)));
        }
        return true;
    }
    case X_SCRIPT:
    {
        std::map<int, Zasm30ScreenScript>::iterator it = index >= 0 ? zasm30_screen_scripts.find(index) : zasm30_screen_scripts.end();

        if(!write)
        {
            ret = it != zasm30_screen_scripts.end() ? it->second.script * 10000 : 0;
            return true;
        }

        if(index < 0)
            return true;

        Zasm30ScreenScript &s = zasm30_screen_scripts[index];

        if(it == zasm30_screen_scripts.end())
            memset(s.initd, 0, sizeof(s.initd));

        if(q30_quest_rule(Z30D_QR_CLEARINITDONSCRIPTCHANGE))
            memset(s.initd, 0, sizeof(s.initd));

        s.script = (word)z30d_bound(v, 0, 255);

        // the current screen's script starts again (upstream's on_reassign_script_engine_data)
        if(index == currmap * MAPSCRS + currscr)
            zasm30_screen_index = -1;
        return true;
    }
    case X_EXSTATE:     // (upstream's get_mi: the screen's state, as the triggers keep it)
    {
        const int mi = z30_mapdata_mi(ref);

        if(mi < 0)
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(!write)
            ret = q30_get_exstate(mi, (int)n) ? 10000 : 0;
        else
            q30_set_exstate(mi, (int)n, value != 0);
        return true;
    }
    default:
        return false;
    }
}

// npcdata-> fields 2.53's guydata holds (upstream's GET/SET_NPCDATA_VAR_*)
static bool z30d_npcdata_plain_reg(const long arg, long &ret, const long value, const bool write)
{
    short guydata::*s16 = NULL;
    byte guydata::*b8 = NULL;

    switch(arg)
    {
    case Z30R_NPCDATABGSFX: s16 = &guydata::bgsfx; break;
    case Z30R_NPCDATADROPSET: s16 = &guydata::item_set; break;
    case Z30R_NPCDATAEFRAMERATE: s16 = &guydata::e_frate; break;
    case Z30R_NPCDATAFRAMERATE: s16 = &guydata::frate; break;
    case Z30R_NPCDATAHALT: s16 = &guydata::hrate; break;
    case Z30R_NPCDATAHUNGER: s16 = &guydata::grumble; break;
    case Z30R_NPCDATARANDOM: s16 = &guydata::rate; break;
    case Z30R_NPCDATATYPE: s16 = &guydata::family; break;
    case Z30R_NPCDATAEHEIGHT: b8 = &guydata::e_height; break;
    case Z30R_NPCDATAEWIDTH: b8 = &guydata::e_width; break;
    case Z30R_NPCDATAHEIGHT: b8 = &guydata::height; break;
    case Z30R_NPCDATAWIDTH: b8 = &guydata::width; break;
    case Z30R_NPCDATASHEIGHT: b8 = &guydata::s_height; break;
    case Z30R_NPCDATASWIDTH: b8 = &guydata::s_width; break;
    default:
        return false;
    }

    const long id = Z30_REF(NPCDATA);

    if(unsigned(id) >= MAXGUYS)
    {
        Z_scripterrlog("Invalid npcdata ID: %ld\n", id);
        if(!write)
            ret = -10000;
        return true;
    }

    guydata &g = guysbuf[id];
    const long v = value / 10000;

    if(s16)
    {
        if(write) g.*s16 = (short)z30d_bound(v, 0, 32767); else ret = g.*s16 * 10000;
    }
    else
    {
        if(write) g.*b8 = (byte)z30d_bound(v, 0, 255); else ret = g.*b8 * 10000;
    }

    return true;
}

// ---- r17: npcdata fields guydata has no room for, an enemy's weapon data ------------------------

int q30_guy_death_sprite(int guy);                              // guys.cpp
int q30_guy_spawn_sprite(int guy);
void q30_set_guy_death_sprite(int guy, int death, int spawn);
byte q255_guy_defense_253(byte d);                              // q255_guys.cpp: 2.53's outcome for a stored one
int q30_guy_defense(int guy, int slot);                         // q30_triggers.cpp

// what the 2.55/3.0 enemy reader keeps of each enemy (q255_guys.cpp)
static std::map<int, Q30GuyData> zasm30_guy_data;
// each enemy's own copy of its weapon data (upstream's enemy::weap_data), by uid
static std::map<long, Q30WeaponData> zasm30_npc_weapdata;

void q30_set_guy_data(int guy, const Q30GuyData &d)
{
    zasm30_guy_data[guy] = d;
}

// the enemy's 2.55/3.0 data (guys.cpp's enemy constructor), NULL when the reader kept none
const Q30GuyData *q30_guy_data(int guy)
{
    std::map<int, Q30GuyData>::const_iterator it = zasm30_guy_data.find(guy);
    return it == zasm30_guy_data.end() ? NULL : &it->second;
}

static Q30GuyData &z30d_guy(long id)
{
    std::map<int, Q30GuyData>::iterator it = zasm30_guy_data.find((int)id);

    if(it == zasm30_guy_data.end())
        it = zasm30_guy_data.insert(std::make_pair((int)id, Q30GuyData())).first;

    return it->second;
}

// upstream's enemy constructor: weap_data = d->weap_data
void q30_npc_weapdata_made(sprite *e)
{
    const int id = e->id & 0xFFF;
    std::map<int, Q30GuyData>::const_iterator it = zasm30_guy_data.find(id);

    if(it != zasm30_guy_data.end())
        zasm30_npc_weapdata[e->getUID()] = it->second.weap;
    else
        zasm30_npc_weapdata.erase(e->getUID());
}

static void z30d_npc_weapdata_released(long uid)
{
    zasm30_npc_weapdata.erase(uid);
}

static Q30WeaponData *z30d_guy_weap_view(bool npc, long index)
{
    if(!npc)
        return unsigned(index) < MAXGUYS ? &z30d_guy(index).weap : NULL;

    if(!guys.getByUID(index))
        return NULL;

    std::map<long, Q30WeaponData>::iterator it = zasm30_npc_weapdata.find(index);

    if(it == zasm30_npc_weapdata.end())
        it = zasm30_npc_weapdata.insert(std::make_pair(index, Q30WeaponData())).first;

    return &it->second;
}

// upstream weapon::load_weap_data for a weapon an enemy fires (the weapon constructor's
// get_weapon_data_for_id), as far as this engine's weapons have the fields. The weapon's
// script comes from the guy's weapon script table (zasm30_eweapon_state), which is the same data.
void q30_enemy_weapon_made(weapon *w)
{
    if(w->parentid < 0 || !(w->id > wEnemyWeapons || (w->id >= wScript1 && w->id <= wScript10)))
        return;

    enemy *e = (enemy *)guys.getByUID(w->parentid);
    std::map<long, Q30WeaponData>::const_iterator it = e ? zasm30_npc_weapdata.find(e->getUID()) : zasm30_npc_weapdata.end();

    if(it == zasm30_npc_weapdata.end())
        return;

    const Q30WeaponData &d = it->second;
    const int id = w->id;
    Zasm30SpriteState &st = zasm30_sprite_state(w->getUID());

    st.move_flags = (long)d.moveflags;
    st.glow_shape = d.glow_shape;

    if(d.flags & Z30D_WD_GLOW_RAD)
    {
        st.glow_rad = d.light_rads[0];
        st.glow_ofs = d.light_offsets[0];
    }

    enum { O_TILE_W = 0x1, O_TILE_H = 0x2, O_HIT_W = 0x4, O_HIT_H = 0x8, O_HIT_Z = 0x10, O_HIT_X = 0x20, O_HIT_Y = 0x40,
           O_DRAW_X = 0x80, O_DRAW_Y = 0x100 };

    // (calculate_weapon_size: every enemy weapon; only the script weapons are extended, unless
    // qr_CUSTOM_WEAPON_BROKEN_SIZE)
    if(d.override_flags)
    {
        if(id >= wScript1 && id <= wScript10 && !q30_quest_rule(423))
            w->extend = 3;

        if(d.override_flags & O_TILE_W) w->txsz = (int)d.tilew;
        if(d.override_flags & O_TILE_H) w->tysz = (int)d.tileh;
        if(d.override_flags & O_HIT_W) w->hxsz = (int)d.hxsz;
        if(d.override_flags & O_HIT_H) w->hysz = (int)d.hysz;
        if(d.override_flags & O_HIT_Z) w->hzsz = (int)d.hzsz;
        if(d.override_flags & O_HIT_X) w->hxofs = (int)d.hxofs;
        if(d.override_flags & O_HIT_Y) w->hyofs = (int)d.hyofs;
        if(d.override_flags & O_DRAW_X) w->xofs = (int)d.xofs;
        if(d.override_flags & O_DRAW_Y) w->yofs = (int)d.yofs + (q30_quest_rule(319) ? playing_field_offset : original_playing_field_offset);   // qr_OLD_DRAWOFFSET
    }

    // (an enemy's fire: a fast Gleeok's (misc 3 = 1) or Ghoma's (misc 1 = 2) goes twice the
    // data's step, a Patra's keeps its own, others' the data's slowed for a diagonal only, as upstream)
    if(d.flags & Z30D_WD_SET_STEP)
    {
        if(id == ewFlame || id == ewFlame2)
        {
            switch(e->family)
            {
            case eeGLEEOK: if(e->dmisc3 == 1) w->step = d.step * 2 / 10000.0; break;
            case eeGHOMA: if(e->dmisc1 == 2) w->step = d.step * 2 / 10000.0; break;
            case eePATRA: break;
            default: if(w->dir > right) w->step = d.step * .707 / 10000.0; break;
            }
        }
        else
            w->step = d.step / 10000.0;
    }

    st.unblockable = d.unblockable;
    st.weap_timeout = d.timeout;
    st.weapon_flags = d.wflags;
    q30_weapon_seed_data(w, d, false);
}

// npcdata-> registers of the data above (upstream npcdata.cpp, its GET/SET_NPCDATA_VAR_* and
// arrays)
static bool z30d_npcdata_ex_reg(const long arg, long &ret, const long value, const bool write)
{
    long Q30GuyData::*l32 = NULL;
    short Q30GuyData::*s8 = NULL;       // (bytes: firesfx)
    long size = 0;                      // an array's
    long dflt = -10000;                 // its compat default

    switch(arg)
    {
    case Z30R_NPCDATAFROZENCSET: l32 = &Q30GuyData::frozencset; break;
    case Z30R_NPCDATAFROZENTILE: l32 = &Q30GuyData::frozentile; break;
    case Z30R_NPCDATAHITHEIGHT: l32 = &Q30GuyData::hysz; break;
    case Z30R_NPCDATAHITWIDTH: l32 = &Q30GuyData::hxsz; break;
    case Z30R_NPCDATAHITZ: l32 = &Q30GuyData::hzsz; break;
    case Z30R_NPCDATAHXOFS: l32 = &Q30GuyData::hxofs; break;
    case Z30R_NPCDATAHYOFS: l32 = &Q30GuyData::hyofs; break;
    case Z30R_NPCDATASIZEFLAG: l32 = &Q30GuyData::sizeflags; break;
    case Z30R_NPCDATATILEHEIGHT: l32 = &Q30GuyData::tysz; break;
    case Z30R_NPCDATATILEWIDTH: l32 = &Q30GuyData::txsz; break;
    case Z30R_NPCDATAXOFS: l32 = &Q30GuyData::xofs; break;
    case Z30R_NPCDATAYOFS: l32 = &Q30GuyData::yofs; break;
    case Z30R_NPCDATAZOFS: l32 = &Q30GuyData::zofs; break;
    case Z30R_NPCDATAFIRESFX: s8 = &Q30GuyData::firesfx; break;
    case Z30R_NPCDATAFIRESFXSECONDARY: s8 = &Q30GuyData::firesfx_secondary; break;
    case Z30R_NPCDATAATTRIBUTE: size = 32; break;
    case Z30R_NPCDATADEFENSE: size = 41; break;
    case Z30R_NPCDATAINITD: case Z30R_NPCDATAWEAPONINITD: size = 8; break;
    case Z30R_NPCDATAFLAG: size = 64; break;
    case Z30R_NPCDATASHIELD: size = 5; break;
    case Z30R_NPCDATABEHAVIOUR: case Z30R_NPCDATAMOVEFLAGS: size = 16; dflt = 0; break;
    case Z30R_NPCDATAWMOVEFLAGS: size = 11; dflt = 0; break;
    case Z30R_NPCDATAFLAGS1: case Z30R_NPCDATAFLAGS2: case Z30R_NPCDATASCRIPT: case Z30R_NPCDATAWEAPONSCRIPT:
    case Z30R_NPCDATA_WEAPONDATA: case Z30R_NPCDDEATHSPR: case Z30R_NPCDSHADOWSPR: case Z30R_NPCDSPAWNSPR:
        break;
    default:
        return false;
    }

    const long id = Z30_REF(NPCDATA);

    if(unsigned(id) >= MAXGUYS)
    {
        Z_scripterrlog("Invalid npcdata ID: %ld\n", id);
        if(!write)
            ret = size ? z30_array_default(dflt) : -10000;
        return true;
    }

    guydata &g = guysbuf[id];
    Q30GuyData &x = z30d_guy(id);
    const long v = value / 10000;

    if(l32)
    {
        if(write) x.*l32 = z30d_bound(v, 0, 214747); else ret = x.*l32 * 10000;
        return true;
    }

    if(s8)
    {
        if(write) x.*s8 = (short)z30d_bound(v, 0, 255); else ret = x.*s8 * 10000;
        return true;
    }

    long n = 0;

    if(size && !z30_array_index(Z30_INDEX, size, n))
    {
        if(!write)
            ret = z30_array_default(dflt);
        return true;
    }

    switch(arg)
    {
    case Z30R_NPCDATAATTRIBUTE:
        if(!write)
        {
            ret = x.attributes[n] * 10000;
            return true;
        }

        x.attributes[n] = v;

        switch(n)       // (2.53's misc fields the enemies use)
        {
        case 0: g.misc1 = v; break; case 1: g.misc2 = v; break; case 2: g.misc3 = v; break; case 3: g.misc4 = v; break;
        case 4: g.misc5 = v; break; case 5: g.misc6 = v; break; case 6: g.misc7 = v; break;
        case 7:     // (a bubble's jinx targets are bit flags: 2.53's guydata keeps its own numbers)
            if(g.family == eeWALK && (g.misc7 == e7tTEMPJINX || g.misc7 == e7tPERMJINX || g.misc7 == e7tUNJINX))
                g.misc8 = (v & 3) == 1 ? e8tSWORD : (v & 3) == 2 ? e8tITEM : (v & 3) == 3 ? e8tALL : e8tLAST;
            else
                g.misc8 = v;
            break;
        case 8: g.misc9 = v; break; case 9: g.misc10 = v; break; case 10: g.misc11 = v; break; case 11: g.misc12 = v; break;
        case 12: g.misc13 = v; break; case 13: g.misc14 = v; break; case 14: g.misc15 = v; break;
        }
        return true;

    case Z30R_NPCDATADEFENSE:
    {
        byte defs[41];

        for(int q = 0; q < 41; ++q)
            defs[q] = (byte)q30_guy_defense((int)id, q);

        if(!write)
        {
            ret = defs[n] * 10000;
            return true;
        }

        defs[n] = (byte)z30d_bound(v, 0, 214747);
        q30_set_guy_defenses((int)id, defs);

        if(n < edefQUAKE)
            g.defense[n] = q255_guy_defense_253(defs[n]);
        else if(n == 19)        // (the file's Quake slot)
            g.defense[edefQUAKE] = q255_guy_defense_253(defs[n]);
        return true;
    }

    case Z30R_NPCDATAINITD: case Z30R_NPCDATAWEAPONINITD:
    {
        const bool wpn = arg == Z30R_NPCDATAWEAPONINITD;
        const Q30ScriptRef *r = wpn ? q30_guy_weapon_script((int)id) : q30_guy_script((int)id);
        long initd[8];
        word script = r ? r->script : 0;

        if(r)
            memcpy(initd, r->initd, sizeof(initd));
        else
            memset(initd, 0, sizeof(initd));

        if(!write)
        {
            ret = initd[n];
            return true;
        }

        initd[n] = z30d_bound(value, 0, 214747);

        if(wpn)
        {
            q30_set_guy_weapon_script((int)id, script, initd);
            x.weap.initd[n] = initd[n];
        }
        else
            q30_set_guy_script((int)id, script, initd);
        return true;
    }

    case Z30R_NPCDATAFLAG:      // (upstream's 64-bit flags: 2.53's flags, then flags2)
    {
        dword &f = n < 32 ? g.flags : g.flags2;
        const dword bit = 1u << (n & 31);

        if(write) f = value ? (f | bit) : (f & ~bit);
        else ret = (f & bit) ? 10000 : 0;
        return true;
    }

    case Z30R_NPCDATASHIELD:    // guy_shield_front, _left, _right, _back, guy_bkshield
    {
        const dword bit = 0x01000000u << n;

        if(write) g.flags = value ? (g.flags | bit) : (g.flags & ~bit);
        else ret = (g.flags & bit) ? 10000 : 0;
        return true;
    }

    case Z30R_NPCDATABEHAVIOUR:
    {
        const dword bit = 1u << n;

        if(write) x.editorflags = value ? (x.editorflags | bit) : (x.editorflags & ~bit);
        else ret = (x.editorflags & bit) ? 10000 : 0;
        return true;
    }

    case Z30R_NPCDATAMOVEFLAGS:
    {
        const dword bit = 1u << n, f = q30_guy_moveflags((int)id);

        if(write) q30_set_guy_moveflags((int)id, value ? (f | bit) : (f & ~bit));
        else ret = (f & bit) ? 10000 : 0;
        return true;
    }

    case Z30R_NPCDATAWMOVEFLAGS:
    {
        const dword bit = 1u << n;

        if(write) x.weap.moveflags = value ? (x.weap.moveflags | bit) : (x.weap.moveflags & ~bit);
        else ret = (x.weap.moveflags & bit) ? 10000 : 0;
        return true;
    }

    case Z30R_NPCDATAFLAGS1: case Z30R_NPCDATAFLAGS2:
        if(write)
        {
            g.flags = (dword)z30d_bound(v, 0, 32767);   // (upstream sets the whole of its flags from either)
            g.flags2 = 0;
        }
        else
            ret = (long)(int)(dword)((arg == Z30R_NPCDATAFLAGS1 ? g.flags : g.flags2) * 10000u);
        return true;

    case Z30R_NPCDATASCRIPT: case Z30R_NPCDATAWEAPONSCRIPT:
    {
        const bool wpn = arg == Z30R_NPCDATAWEAPONSCRIPT;
        const Q30ScriptRef *r = wpn ? q30_guy_weapon_script((int)id) : q30_guy_script((int)id);

        if(!write)
        {
            ret = (r ? r->script : 0) * 10000;
            return true;
        }

        long initd[8];

        if(r && !q30_quest_rule(516))   // qr_CLEARINITDONSCRIPTCHANGE
            memcpy(initd, r->initd, sizeof(initd));
        else
            memset(initd, 0, sizeof(initd));

        const word script = (word)z30d_bound(v, 0, wpn ? NUMSCRIPTWEAPONS - 1 : NUMSCRIPTGUYS - 1);

        if(wpn)
        {
            q30_set_guy_weapon_script((int)id, script, initd);
            x.weap.script = script;
            memcpy(x.weap.initd, initd, sizeof(initd));
        }
        else
            q30_set_guy_script((int)id, script, initd);
        return true;
    }

    case Z30R_NPCDATA_WEAPONDATA:
        if(!write)
            ret = Z30_REF(WEAPDATA) = z30d_weapdata_view(Z30D_WDV_NPCDATA, id);
        return true;

    case Z30R_NPCDDEATHSPR: case Z30R_NPCDSPAWNSPR:
    {
        const bool death = arg == Z30R_NPCDDEATHSPR;

        if(!write)
            ret = (death ? q30_guy_death_sprite((int)id) : q30_guy_spawn_sprite((int)id)) * 10000;
        else
        {
            const int s = (int)z30d_bound(v, 0, MAXWPNS - 1);
            q30_set_guy_death_sprite((int)id, death ? s : q30_guy_death_sprite((int)id), death ? q30_guy_spawn_sprite((int)id) : s);
        }
        return true;
    }

    default:    // Z30R_NPCDSHADOWSPR
        if(write) x.spr_shadow = (word)z30d_bound(v, 0, MAXWPNS - 1); else ret = x.spr_shadow * 10000;
        return true;
    }
}

// ---- the entry points -------------------------------------------------------------------------

// ---- r17: itemdata's 3.0 fields (itemdata.cpp) and its weapon data ---------------------------
// The item reader's record (q30_item_rec) has what 2.53's itemdata has no room for; the plain
// fields of itemsbuf and the 32 flag bits are read and written where the other itemdata
// registers do it.

// upstream's item_flags past what IDATAFLAGS covers (bits of q30_item_flags)
enum
{
    Z30D_IF_GAMEDATA = 1u << 0, Z30D_IF_EDIBLE = 1u << 1, Z30D_IF_COMBINE = 1u << 2, Z30D_IF_DOWNGRADE = 1u << 3,
    Z30D_IF_KEEP_OLD = 1u << 6, Z30D_IF_RUPEE_MAGIC = 1u << 7, Z30D_IF_UNUSED = 1u << 8, Z30D_IF_GAIN_OLD = 1u << 9,
    Z30D_IF_PASSIVE_SCRIPT = 1u << 23, Z30D_IF_VALIDATE = 1u << 24, Z30D_IF_SIDESWIM_DISABLED = 1u << 25,
    Z30D_IF_VALIDATE_2 = 1u << 27, Z30D_IF_FLIP_JINX = 1u << 29
};
enum { Z30D_WF_UPDATE_IGNITE_SPRITE = 1u << 9 };     // WFLAG_UPDATE_IGNITE_SPRITE

static dword z30d_item_flags(long id)
{
    return (q30_item_flags((int)id) & ~0xFFFFu) | Q30_IDATA(id).flags;
}

static void z30d_set_item_flag(long id, dword bit, bool on)
{
    const dword f = on ? (z30d_item_flags(id) | bit) : (z30d_item_flags(id) & ~bit);
    Q30_IDATA(id).flags = (word)(f & 0xFFFF);
    q30_set_item_flags32((int)id, f);

    if(bit == Z30D_IF_PASSIVE_SCRIPT)
        q30_set_item_passive_script((int)id, on);
}

static Q30ItemRec &z30d_item_rec(long id)
{
    static Q30ItemRec none;
    Q30ItemRec *r = q30_item_rec((int)id);
    return r ? *r : none;
}

// the item's sprite script's InitD (the engine runs the script from q30_item_sprite_script; an
// item with no script keeps its InitD here)
static std::map<int, std::vector<long> > zasm30_item_sprite_initd;

static long *z30d_item_sprite_initd(long id)
{
    std::vector<long> &v = zasm30_item_sprite_initd[(int)id];

    if(v.size() != 8)
    {
        const Q30ScriptRef *r = q30_item_sprite_script((int)id);
        v.assign(8, 0);

        if(r)
            v.assign(r->initd, r->initd + 8);
    }

    return &v[0];
}

void q30_set_item_moveflags(int id, long flags);    // q30_scripts_sys.h

static bool z30d_item30_reg(const long arg, long &ret, const long value, const bool write)
{
    dword flag = 0;
    bool zero_default = false;

    switch(arg)
    {
    case Z30R_IDATACOMBINE: flag = Z30D_IF_COMBINE; break;
    case Z30R_IDATACONSTSCRIPT: flag = Z30D_IF_PASSIVE_SCRIPT; break;
    case Z30R_IDATADOWNGRADE: flag = Z30D_IF_DOWNGRADE; break;
    case Z30R_IDATAEDIBLE: flag = Z30D_IF_EDIBLE; break;
    case Z30R_IDATAFLAGUNUSED: flag = Z30D_IF_UNUSED; break;
    case Z30R_IDATAGAINLOWER: flag = Z30D_IF_GAIN_OLD; break;
    case Z30R_IDATAJINXSWAP: flag = Z30D_IF_FLIP_JINX; break;
    case Z30R_IDATAKEEPOLD: flag = Z30D_IF_KEEP_OLD; break;
    case Z30R_IDATARUPEECOST: flag = Z30D_IF_RUPEE_MAGIC; break;
    case Z30R_IDATASSWIMDISABLED: flag = Z30D_IF_SIDESWIM_DISABLED; break;
    case Z30R_IDATAVALIDATE: flag = Z30D_IF_VALIDATE; break;
    case Z30R_IDATAVALIDATE2: flag = Z30D_IF_VALIDATE_2; break;
    case Z30R_IDATACOLLECTFLAGS: case Z30R_IDATAOVERRIDEFLWEAP: case Z30R_IDATAPFLAGS: case Z30R_IDATAPICKUPLITEMLEVEL:
    case Z30R_IDATAPICKUPLITEMS: case Z30R_IDATAUSEBURNSPR: case Z30R_IDATAWEAPZOFS:
        zero_default = true;
        break;
    case Z30R_IDATACOOLDOWN: case Z30R_IDATADRAWLAYER: case Z30R_IDATADUPLICATES: case Z30R_IDATADURATION:
    case Z30R_IDATAFLASHCSET: case Z30R_IDATAGRADUAL: case Z30R_IDATAMAGICTIMER: case Z30R_IDATAMAGICTIMER2:
    case Z30R_IDATAMISC: case Z30R_IDATAPICKUP: case Z30R_IDATAPSOUND: case Z30R_IDATAPSTRING: case Z30R_IDATASPRSCRIPT:
    case Z30R_IDATATILEHWEAP: case Z30R_IDATATILEWWEAP: case Z30R_IDATAWEAPHXOFS: case Z30R_IDATAWEAPHXSZ:
    case Z30R_IDATAWEAPHYOFS: case Z30R_IDATAWEAPHYSZ: case Z30R_IDATAWEAPHZSZ: case Z30R_IDATAWEAPONSCRIPT:
    case Z30R_IDATAWEAPXOFS: case Z30R_IDATAWEAPYOFS: case Z30R_IDATAWRANGE: case Z30R_IDATA_WEAPONDATA:
    case Z30R_IDATAATTRIB_L: case Z30R_IDATABURNINGLIGHTRAD: case Z30R_IDATABURNINGSPR: case Z30R_IDATACOLLECTINITD:
    case Z30R_IDATAMISCD: case Z30R_IDATAMOVEFLAGS: case Z30R_IDATAWMOVEFLAGS: case Z30R_IDATASPRITEINITD:
        break;
    default:
        return false;
    }

    const long id = ri->idata, v = value / 10000;

    if(arg == Z30R_IDATAWEAPZOFS)
    {
        Z_scripterrlog("itemdata::WeaponDrawZOffset is not implemented\n");

        if(!write)
            ret = 0;
        return true;
    }

    if(unsigned(id) >= unsigned(q30_items_end()))
    {
        Z_scripterrlog("Invalid itemdata access: %ld\n", id);

        if(!write)
        {
            switch(arg)     // the arrays: their failed read
            {
            case Z30R_IDATAATTRIB_L: ret = z30_array_default(-1); break;
            case Z30R_IDATAMOVEFLAGS: case Z30R_IDATAWMOVEFLAGS: ret = 0; break;
            case Z30R_IDATABURNINGLIGHTRAD: case Z30R_IDATABURNINGSPR: case Z30R_IDATACOLLECTINITD:
            case Z30R_IDATAMISCD: case Z30R_IDATASPRITEINITD:
                ret = z30_array_default(-10000);
                break;
            default: ret = (flag || zero_default) ? 0 : -10000; break;
            }
        }
        return true;
    }

    if(flag)
    {
        if(write)
            z30d_set_item_flag(id, flag, value != 0);
        else
            ret = (z30d_item_flags(id) & flag) ? 10000 : 0;
        return true;
    }

    itemdata &it = Q30_IDATA(id);
    Q30ItemRec &r = z30d_item_rec(id);
    Q30WeaponData &wd = r.weap;
    long n = 0;

    switch(arg)
    {
    case Z30R_IDATACOLLECTFLAGS: if(write) r.collectflags = z30d_bound(v, 0, 214747); else ret = r.collectflags * 10000; break;
    case Z30R_IDATACOOLDOWN: if(write) r.cooldown = v > 0 ? v : 0; else ret = r.cooldown * 10000; break;
    case Z30R_IDATADRAWLAYER: if(write) r.drawlayer = (byte)z30d_bound(v, 0, 7); else ret = r.drawlayer * 10000; break;
    case Z30R_IDATADUPLICATES: if(write) r.duplicates = z30d_bound(v, 0, 255); else ret = r.duplicates * 10000; break;
    case Z30R_IDATADURATION: if(write) r.weapduration = z30d_bound(v, 0, 255); else ret = r.weapduration * 10000; break;
    case Z30R_IDATAWRANGE: if(write) r.weaprange = z30d_bound(v, 0, 255); else ret = r.weaprange * 10000; break;
    case Z30R_IDATAFLASHCSET:
        if(write) it.csets = (byte)((it.csets & 0xF) | (z30d_bound(v, 0, 15) << 4)); else ret = (it.csets >> 4) * 10000;
        break;
    case Z30R_IDATAGRADUAL:
        if(write) it.amount = value ? (it.amount | 0x8000) : (it.amount & 0x7FFF); else ret = (it.amount & 0x8000) ? 10000 : 0;
        break;
    case Z30R_IDATAMAGICTIMER: case Z30R_IDATAMAGICTIMER2:
    {
        long &t = r.magiccosttimer[arg == Z30R_IDATAMAGICTIMER2 ? 1 : 0];
        if(write) t = z30d_bound(v, 0, 214747); else ret = t * 10000;
        break;
    }
    case Z30R_IDATAMISC: if(write) it.misc = (byte)v; else ret = it.misc * 10000; break;
    case Z30R_IDATAPICKUP: case Z30R_IDATAPSTRING: case Z30R_IDATAPFLAGS:
    {
        long pickup;
        int mode, pstring, psflags;
        q30_item_pickup_info((int)id, &pickup, &mode, &pstring, &psflags);

        if(!write)
        {
            ret = (arg == Z30R_IDATAPICKUP ? pickup : arg == Z30R_IDATAPSTRING ? pstring : psflags) * 10000;
            break;
        }

        if(arg == Z30R_IDATAPICKUP)
            pickup = v;
        else if(arg == Z30R_IDATAPSTRING)
            pstring = (int)z30d_bound(v, 1, 255);
        else
            psflags = (int)z30d_bound(v, 0, 214748);

        q30_set_item_pickup((int)id, pickup, mode, pstring, psflags);
        break;
    }
    case Z30R_IDATAPICKUPLITEMLEVEL:
        if(write) r.pickup_litem_level = (short)z30d_bound(v, -1, MAXLEVELS - 1); else ret = r.pickup_litem_level * 10000;
        break;
    case Z30R_IDATAPICKUPLITEMS:    // & LI_ALL
        if(write) r.pickup_litems = (word)(z30d_bound(v, 0, 214748) & 0xFFFF); else ret = r.pickup_litems * 10000;
        break;
    case Z30R_IDATAPSOUND: if(write) it.playsound = (byte)z30d_bound(v, 0, Q30_MAX_SFX); else ret = it.playsound * 10000; break;
    case Z30R_IDATASPRSCRIPT:
    {
        const Q30ScriptRef *sr = q30_item_sprite_script((int)id);

        if(!write)
            ret = (sr ? sr->script : 0) * 10000;
        else
        {
            long *initd = z30d_item_sprite_initd(id);
            q30_set_item_sprite_script((int)id, (word)z30d_bound(v, 0, 256), initd);   // NUMSCRIPTSITEMSPRITE
        }
        break;
    }
    case Z30R_IDATASPRITEINITD: case Z30R_IDATACOLLECTINITD:
    {
        if(!z30d_index(Z30_INDEX, 8, false, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            break;
        }

        if(arg == Z30R_IDATACOLLECTINITD)
        {
            if(write) r.collect_initd[n] = value; else ret = r.collect_initd[n];
            break;
        }

        long *initd = z30d_item_sprite_initd(id);

        if(!write)
            ret = initd[n];
        else
        {
            initd[n] = value;
            const Q30ScriptRef *sr = q30_item_sprite_script((int)id);

            if(sr)
                q30_set_item_sprite_script((int)id, sr->script, initd);
        }
        break;
    }
    case Z30R_IDATAOVERRIDEFLWEAP: if(write) wd.override_flags = v; else ret = wd.override_flags * 10000; break;
    case Z30R_IDATATILEWWEAP: if(write) wd.tilew = v; else ret = wd.tilew * 10000; break;
    case Z30R_IDATATILEHWEAP: if(write) wd.tileh = v; else ret = wd.tileh * 10000; break;
    case Z30R_IDATAWEAPHXOFS: if(write) wd.hxofs = v; else ret = wd.hxofs * 10000; break;
    case Z30R_IDATAWEAPHYOFS: if(write) wd.hyofs = v; else ret = wd.hyofs * 10000; break;
    case Z30R_IDATAWEAPHXSZ: if(write) wd.hxsz = v; else ret = wd.hxsz * 10000; break;
    case Z30R_IDATAWEAPHYSZ: if(write) wd.hysz = v; else ret = wd.hysz * 10000; break;
    case Z30R_IDATAWEAPHZSZ: if(write) wd.hzsz = v; else ret = wd.hzsz * 10000; break;
    case Z30R_IDATAWEAPXOFS: if(write) wd.xofs = v; else ret = wd.xofs * 10000; break;
    case Z30R_IDATAWEAPYOFS: if(write) wd.yofs = v; else ret = wd.yofs * 10000; break;
    case Z30R_IDATAWEAPONSCRIPT:
        if(!write)
            ret = wd.script * 10000;
        else
        {
            wd.script = (word)z30d_bound(v, 0, 255);
            q30_set_item_weapon_script((int)id, wd.script, wd.initd);
        }
        break;
    case Z30R_IDATAUSEBURNSPR:
        if(write) wd.wflags = value ? (wd.wflags | Z30D_WF_UPDATE_IGNITE_SPRITE) : (wd.wflags & ~Z30D_WF_UPDATE_IGNITE_SPRITE);
        else ret = (wd.wflags & Z30D_WF_UPDATE_IGNITE_SPRITE) ? 10000 : 0;
        break;
    case Z30R_IDATA_WEAPONDATA: if(!write) ret = z30d_weapdata_view(Z30D_WDV_ITEM, id); break;
    case Z30R_IDATAATTRIB_L:    // itemdata->LAttributes[10]: the attributes, raw
    {
        if(!z30d_index(Z30_INDEX, 10, true, n))
        {
            if(!write)
                ret = z30_array_default(-1);
            break;
        }

        long *const misc[10] = { &it.misc1, &it.misc2, &it.misc3, &it.misc4, &it.misc5, &it.misc6, &it.misc7, &it.misc8, &it.misc9, &it.misc10 };

        if(write) *misc[n] = value; else ret = *misc[n];
        break;
    }
    case Z30R_IDATAMISCD:       // itemdata->WeaponMisc[32]
        if(!z30d_index(Z30_INDEX, 32, true, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            break;
        }

        if(write) r.wpn_misc_d[n] = v; else ret = r.wpn_misc_d[n] * 10000;
        break;
    case Z30R_IDATAMOVEFLAGS: case Z30R_IDATAWMOVEFLAGS:
    {
        if(!z30d_index(Z30_INDEX, 11, false, n))
        {
            if(!write)
                ret = 0;
            break;
        }

        if(arg == Z30R_IDATAWMOVEFLAGS)
        {
            if(write) wd.moveflags = value ? (wd.moveflags | (1u << n)) : (wd.moveflags & ~(1u << n));
            else ret = (wd.moveflags & (1u << n)) ? 10000 : 0;
            break;
        }

        long f = q30_item_moveflags_of((int)id);

        if(!write)
            ret = (f & (1L << n)) ? 10000 : 0;
        else
        {
            f = value ? (f | (1L << n)) : (f & ~(1L << n));
            q30_set_item_moveflags((int)id, f);
        }
        break;
    }
    case Z30R_IDATABURNINGSPR: case Z30R_IDATABURNINGLIGHTRAD:
        if(!z30d_index(Z30_INDEX, 5, false, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            break;
        }

        if(arg == Z30R_IDATABURNINGSPR)
        {
            if(write) wd.burnsprs[n] = (word)z30d_bound(v, 0, 255); else ret = wd.burnsprs[n] * 10000;    // MAXSPRITES-1
        }
        else if(write)
            wd.light_rads[n] = (byte)z30d_bound(v, 0, 255);
        else
            ret = wd.light_rads[n] * 10000;
        break;
    }

    return true;
}

// upstream's weapon::load_weap_data, from the weapon constructor (get_weapon_data_for_id): a
// weapon an item makes takes on the item's weapon data, for what this engine's weapons have
// (pierce, bounce, lifting and the burning sprites: q30_weapon_fx.h). Magic takes the Book of
// Magic's, sparkles their item's only under qr_SPARKLES_INHERIT_PROPERTIES.

void q30_weapon_load_item_data(weapon *w)
{
    enum { QR_OLD_DRAWOFFSET = 319, QR_SPARKLES_INHERIT_PROPERTIES = 355, QR_CUSTOM_WEAPON_BROKEN_SIZE = 460 };
    enum { O_TILE_W = 0x1, O_TILE_H = 0x2, O_HIT_W = 0x4, O_HIT_H = 0x8, O_HIT_Z = 0x10, O_HIT_X = 0x20, O_HIT_Y = 0x40,
           O_DRAW_X = 0x80, O_DRAW_Y = 0x100 };

    if(!q30_active() || w->id >= wEnemyWeapons)
        return;

    const int id = w->id;
    int itemid = w->parentitem;

    if(id == wMagic)
    {
        const int book = current_item_id(itype_book);

        if(book >= 0)
            itemid = book;
    }
    else if((id == wSSparkle || id == wFSparkle) && !q30_quest_rule(QR_SPARKLES_INHERIT_PROPERTIES))
        return;

    const Q30ItemRec *rec = itemid >= 0 ? q30_item_rec(itemid) : NULL;

    if(!rec)
        return;

    const Q30WeaponData &d = rec->weap;
    Zasm30SpriteState &st = zasm30_sprite_state(w->getUID());
    const bool melee = id == wSword || id == wHammer || id == wWand || id == wBomb || id == wSBomb || id == wWhistle || id == wCatching
                       || id == wHookshot || id == wHSHandle || id == wHSChain || id == wSSparkle || id == wFSparkle || id == wSmack
                       || id == wPhantom || id == wStomp;

    if(!melee)
        st.move_flags = (long)d.moveflags;

    if(d.flags & Z30D_WD_GLOW_RAD)
    {
        st.glow_rad = d.light_rads[0];
        st.glow_ofs = d.light_offsets[0];
    }

    st.glow_shape = d.glow_shape;

    // compute_do_size: the weapons that take the item's sizes
    const bool scripted = (id >= wScript1 && id <= wScript10);
    bool sized = false;

    switch(id)
    {
    case wSword: case wWand: case wHammer: case wCByrna: case wWhistle: case wWind: case wArrow: case wFire:
    case wLitBomb: case wBomb: case wLitSBomb: case wSBomb: case wBait: case wMagic: case wBrang:
        sized = d.override_flags != 0;
        break;
    default:
        sized = scripted && d.override_flags != 0;
        break;
    }

    if(sized && (!scripted || !q30_quest_rule(QR_CUSTOM_WEAPON_BROKEN_SIZE)))
        w->extend = 3;

    if(sized)   // calculate_weapon_size
    {
        const long f = d.override_flags;
        int xofs = (int)d.xofs, yofs = (int)d.yofs + (q30_quest_rule(QR_OLD_DRAWOFFSET) ? playing_field_offset : original_playing_field_offset);

        if(id == wSword)
        {
            if(w->dir == up && (f & O_DRAW_Y)) yofs -= 16;
            if(w->dir == left && (f & O_DRAW_X)) xofs -= 16;
        }
        else if(id == wArrow && (w->dir == left || w->dir == l_down || w->dir == right) && (f & O_DRAW_Y))
            yofs += 1;
        else if(id == wFire && BSZ && (f & O_DRAW_Y))
            yofs += 2;

        if(f & O_DRAW_X) w->xofs = xofs;
        if(f & O_DRAW_Y) w->yofs = yofs;
        if(f & O_TILE_W) w->txsz = (int)d.tilew;
        if(f & O_TILE_H) w->tysz = (int)d.tileh;
        if(f & O_HIT_X) w->hxofs = (int)d.hxofs;
        if(f & O_HIT_Y) w->hyofs = (int)d.hyofs;
        if(f & O_HIT_W) w->hxsz = (int)d.hxsz;
        if(f & O_HIT_H) w->hysz = (int)d.hysz;
        if(f & O_HIT_Z) w->hzsz = (int)d.hzsz;
    }

    if((d.flags & Z30D_WD_SET_STEP) && !(id == wSword || id == wHammer || id == wWand || id == wHSHandle || id == wHSChain || id == wWhistle))
        w->step = d.step / 10000.0;

    // (a lit bomb keeps them for its explosion, which takes them from the item itself)
    if(id != wLitBomb && id != wLitSBomb)
    {
        zasm30_store_set(Z30R_LWPNUSEWEAPON, w->getUID(), 0, d.imitate_weapon * 10000);   // (kept as written)
        zasm30_store_set(Z30R_LWPNUSEDEFENCE, w->getUID(), 0, d.default_defense);
    }

    st.unblockable = d.unblockable;
    st.weap_timeout = d.timeout;    // (counted down by weapon::animate_body, q30_weapon_fx.h)
    st.weapon_flags = d.wflags;
    st.script = d.script;
    memcpy(st.initd, d.initd, sizeof(st.initd));
    st.script_started = true;   // (zasm30_lweapon_state: the script is the item's, as made)
    q30_weapon_seed_data(w, d, true);   // lifting, piercing, bouncing, burning sprites
}

// ---- r18: combo data and position state arrays, FFC script flags, message segments ----------
byte &q30_pos_state(int mi, int key);       // q30_triggers.cpp (keys: layer * 176 + pos, 1000 + FFC)

// upstream's legacy SCC argument counts by code (components/scc/scc.cpp scc_commands; -1 unknown)
static const signed char Z30D_SCC_ARGS[] = {
    -1,2,1,3,2,2,3,-1,2,2,2,2,2,-1,-1,-1,1,1,6,4,1,1,0,5,6,0,2,1,5,0,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,5,5,0,2,1,2,4,1,-1,1,3,3,5,3,5,4,4,1,2,4,2,3,3,1,1,1,1,1,1,3,3,1,1,2 };

// messagedata->Segments[]: upstream's ParsedMsgStr (parse_legacy_binary_msg_str) flattened as
// segmentsAsZFixArray: per segment its type (1 text, 2 command, 0 unknown code), source start and
// length, and for a command its code, argument count and arguments
static void z30d_msg_segments(const std::string &str, std::vector<long> &out)
{
    size_t last = 0, prev = 0, cur = 0;

    for(size_t i = str.size(); i > 0; --i)
        if(str[i - 1] != ' ')
        {
            last = i;
            break;
        }

    while(cur < last)
    {
        const byte c = (byte)str[cur];

        if(c >= 32 && c <= 126)
        {
            ++cur;
            continue;
        }

        if(prev != cur)
        {
            out.push_back(1); out.push_back((long)prev); out.push_back((long)(cur - prev));
            prev = cur;
        }

        const int code = c - 1;
        const int want = (code >= 0 && code < (int)sizeof(Z30D_SCC_ARGS)) ? Z30D_SCC_ARGS[code] : -1;

        if(want < 0)
        {
            ++cur;
            out.push_back(0); out.push_back((long)prev); out.push_back((long)(cur - prev));
            prev = cur;
            continue;
        }

        const size_t start = cur;
        long args[8];
        int found = 0;

        for(; found < want; ++found)
        {
            if(cur + 1 >= str.size())
                break;

            long v = ((byte)str[++cur] - 1) & 0xFF;

            if(cur + 2 < str.size() && (byte)str[cur + 1] == 255)
            {
                v += 254 * (byte)str[cur + 2];
                cur += 2;
            }

            args[found] = v;
        }

        ++cur;
        prev = cur;
        out.push_back(2); out.push_back((long)start); out.push_back((long)(cur - start));
        out.push_back(code); out.push_back(found);
        for(int j = 0; j < found; ++j)
            out.push_back(args[j]);
    }

    if(prev < last)
    {
        out.push_back(1); out.push_back((long)prev); out.push_back((long)(last - prev));
    }
}

static bool z30d_r18_reg(const long arg, long &ret, const long value, const bool write)
{
    long n = Z30_INDEX;

    switch(arg)
    {
    // mapdata->ComboData[] / Screen->ComboData[]: the combo's id as a combodata (read only)
    case Z30R_MAPDATACOMBODATAD:
    case Z30R_COMBODATAD:
    {
        if(write)
        {
            Z_scripterrlog("Tried to write to a read-only array\n");
            return true;
        }

        mapscr *m = NULL;

        if(arg == Z30R_MAPDATACOMBODATAD)
            m = z30_mapdata_combo(Z30_REF(MAPDATA), n);
        else if(q30_region_on())
        {
            int pos = 0;
            bool current;
            m = q30_region_rpos_screen((int)n, 0, &pos, &current);
            n = pos;
        }
        else
            m = (n >= 0 && n < 176) ? tmpscr : NULL;

        ret = (m && n >= 0 && n < (long)m->data.size()) ? m->data[n] : (arg == Z30R_COMBODATAD ? -1 : -10000);
        return true;
    }
    // mapdata->ComboPosState[] / Screen->ComboPosState[] / mapdata->FFCPosState[]: the states
    // combo triggers set on a position (upstream's game->pos_states / ffcpos_states)
    case Z30R_MAPDATACOMBOPOSSTATE:
    case Z30R_SCREENCOMBOPOSSTATE:
    case Z30R_MAPDATAFFCPOSSTATE:
    {
        int mi = -1;
        int key;

        if(arg == Z30R_SCREENCOMBOPOSSTATE)
            mi = homescr < MAPSCRSNORMAL ? currmap * MAPSCRSNORMAL + homescr : -1;
        else if(z30_mapdata(Z30_REF(MAPDATA)))
            mi = z30_mapdata_mi(Z30_REF(MAPDATA));

        if(arg == Z30R_MAPDATAFFCPOSSTATE)
        {
            if(n < 1)
                mi = -1;
            key = 1000 + (int)((n - 1) % Q30_MAXFFCS);
        }
        else
        {
            if(n < 0)
                mi = -1;
            key = (int)(n % 176);
        }

        if(!write)
            ret = mi < 0 ? 0 : q30_pos_state(mi, key) * 10000;
        else if(mi >= 0)
            q30_pos_state(mi, key) = (byte)(value / 10000);
        return true;
    }
    // mapdata->FFCInitialised[] (1..) and Screen->FFCInitialised (rINDEX 0..): an FFC's script
    // has started (upstream's script engine data); only the screen being played has any
    case Z30R_MAPDATAFFINITIALISED:
    case Z30R_SCREENDATAFFINITIALISED:
    {
        const long ref = Z30_REF(MAPDATA);
        const bool here = arg == Z30R_SCREENDATAFFINITIALISED || z30_md_here(ref);
        const long i = arg == Z30R_MAPDATAFFINITIALISED ? n - 1 : n;
        const bool ok = here && i >= 0 && i < 32;

        if(!write)
            ret = (ok && tmpscr->initialized[i]) ? 10000 : 0;
        else if(ok)
            tmpscr->initialized[i] = value != 0;
        return true;
    }
    // messagedata->Segments[] (read only)
    case Z30R_MESSAGEDATASEGMENTS:
    {
        if(write)
        {
            Z_scripterrlog("Tried to write to a read-only array\n");
            return true;
        }

        const long id = Z30_REF(MSGDATA);
        ret = 0;

        if(id > 0 && id < msg_count)
        {
            std::vector<long> seg;
            z30d_msg_segments(std::string(MsgStrings[id].s), seg);

            if(z30_array_index(n, (long)seg.size(), n))
                ret = seg[n] * 10000;     // (zfix values)
        }
        return true;
    }
    default:
        return false;
    }
}

static bool zasm30_data_reg(const long arg, long &ret, const long value, const bool write)
{
    if(!q30_active())
        return false;

    if(z30d_r18_reg(arg, ret, value, write))
        return true;

    return z30d_ffc_reg(arg, ret, value, write) || z30d_mapdata_reg(arg, ret, value, write) || z30d_item_reg(arg, ret, value, write)
           || z30d_msg_reg(arg, ret, value, write) || z30d_npcdata_reg(arg, ret, value, write) || z30d_dmap_reg(arg, ret, value, write)
           || z30d_bottle_reg(arg, ret, value, write) || z30d_weapdata_reg(arg, ret, value, write)
           || z30d_combo_lift_reg(arg, ret, value, write) || z30d_scr_reg(arg, ret, value, write) || z30d_npcdata_plain_reg(arg, ret, value, write)
           || z30d_scr_more_reg(arg, ret, value, write)
           || z30d_npcdata_ex_reg(arg, ret, value, write)
           || z30d_item30_reg(arg, ret, value, write);
}

static bool zasm30_data_command(word cmd)
{
    if(z30d_dmap_command(cmd) || z30d_weapdata_command(cmd))
        return true;

    switch(cmd)
    {
    case Z30C_LOADBOTTLETYPE:
    {
        const long id = get_register(sarg1) / 10000;

        if(id < 1 || id > 64)
            Z_scripterrlog("Invalid BottleType ID: %ld\n", id);

        Z30_REF(BOTTLETYPE) = (id < 1 || id > 64) ? 0 : id;
        return true;
    }
    case Z30C_LOADBSHOPDATA:
    {
        const long id = get_register(sarg1) / 10000;

        if(id < 0 || id > 255)
            Z_scripterrlog("Invalid BottleShopType ID: %ld\n", id);

        Z30_REF(BOTTLESHOP) = (id < 0 || id > 255) ? 0 : id + 1;
        return true;
    }
    case Z30C_BOTTLENAMEGET:
    case Z30C_BOTTLENAMESET:
    {
        const long ptr = zasm30_aptr(get_register(sarg1)), id = Z30_REF(BOTTLETYPE) - 1;

        if(unsigned(id) > 63)
        {
            Z_scripterrlog("Invalid bottledata ID (%ld) passed to bottledata->%sName().\n", cmd == Z30C_BOTTLENAMEGET ? id : id + 1,
                           cmd == Z30C_BOTTLENAMEGET ? "Get" : "Set");
            return true;
        }

        Q30BottleType *b = q30_bottle_type_edit((int)id);

        if(cmd == Z30C_BOTTLENAMEGET)
            ArrayH::setArray(ptr, z30d_chars(b->name, 32));
        else
        {
            std::string s;
            ArrayH::getString(ptr, s, 31);
            strcpy(b->name, s.c_str());
        }
        return true;
    }
    case Z30C_BSHOPNAMEGET:
    case Z30C_BSHOPNAMESET:
    {
        // (upstream's SetName takes the shop after the loaded one, its ref not less one)
        const long ptr = zasm30_aptr(get_register(sarg1));
        const long id = cmd == Z30C_BSHOPNAMEGET ? Z30_REF(BOTTLESHOP) - 1 : Z30_REF(BOTTLESHOP);

        if(unsigned(id) > 255)
        {
            Z_scripterrlog("Invalid bottleshopdata ID (%ld) passed to bottleshopdata->%sName().\n", id + (cmd == Z30C_BSHOPNAMEGET ? 1 : 0),
                           cmd == Z30C_BSHOPNAMEGET ? "Get" : "Set");
            return true;
        }

        Q30BottleShop *s = q30_bottle_shop_edit((int)id);

        if(cmd == Z30C_BSHOPNAMEGET)
            ArrayH::setArray(ptr, z30d_chars(s->name, 32));
        else
        {
            std::string t;
            ArrayH::getString(ptr, t, 31);
            strcpy(s->name, t.c_str());
        }
        return true;
    }
    case Z30C_NPCDATAGETNAME:
    {
        const long ptr = zasm30_aptr(get_register(sarg1)), id = Z30_REF(NPCDATA);

        if(unsigned(id) > 511)
            Z_scripterrlog("Invalid npc ID (%ld) passed to npcdata->GetName().\n", id);
        else
            ArrayH::setArray(ptr, std::string(guy_string[id] ? guy_string[id] : ""));
        return true;
    }
    case Z30C_MAPDATAISSOLIDLYR:
        z30d_issolid_layer();
        return true;
    case Z30C_SCREENDOSPAWN:
        ri->d[2] = z30d_script_load_enemies() ? 10000 : 0;    // rEXP1
        return true;
    // (upstream do_loadmessagedata: string 0 and those past the last make an invalid messagedata)
    case Z30C_LOADMESSAGEDATAR:
    case Z30C_LOADMESSAGEDATAV:
    {
        const long id = SH::get_arg(sarg1, cmd == Z30C_LOADMESSAGEDATAV) / 10000;

        if(id < 1 || id > msg_count - 1)
            Z_scripterrlog("Invalid Message ID passed to Game->LoadMessageData: %ld\n", id);

        Z30_REF(MSGDATA) = (id < 1 || id > msg_count - 1) ? -1 : id;
        return true;
    }
    default:
        return false;
    }
}

#endif
