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

// ---- internal arrays (upstream's zc/scripting/arrays.h) ----------------------------------------

// An element of an internal array of `size`: false (logged) when the index is out of range;
// under qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX an array upstream marks compatBoundIndex binds
// it into range instead.
static bool z30d_index(long index, long size, bool compat_bound, long &out)
{
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
        return false;
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
    const long ptr = get_register(sarg1) / 10000;

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

        // (an item whose weapon has no script keeps its arguments here, as the weapon table
        // keeps only scripted ones)
        static std::map<int, std::vector<long> > unscripted;
        const Q30ScriptRef *r = q30_item_weapon_script((int)id);
        std::vector<long> &u = unscripted[(int)id];

        if(u.size() != 8)
            u.assign(8, 0);

        if(!write)
        {
            ret = r ? r->initd[n] : u[n];
            return true;
        }

        if(!r)
        {
            u[n] = value;
            return true;
        }

        long initd[8];
        memcpy(initd, r->initd, sizeof(initd));
        initd[n] = value;
        q30_set_item_weapon_script((int)id, r->script, initd);
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
    case Z30R_MESSAGEDATAFLAGSARR:
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
    case Z30R_MESSAGEDATATEXTHEI: break;
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
        else if(!(ref & Z30_MAPDATA_TMP))
            m->door[n] = (byte)z30d_bound(value / 10000, 0, 255);
        else if((ref & 0xFFFF) == 0)
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

    if((ref & Z30_MAPDATA_TMP) && (ref & 0xFFFF) == 0)
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
static long zasm30_next_weapdata = 1;

static Q30WeaponData *z30d_weapdata(long ref, bool log)
{
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
}

static bool z30d_weapdata_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_CREATEWEAPDATA:
        Z30_REF(WEAPDATA) = zasm30_next_weapdata;
        zasm30_weapdatas[zasm30_next_weapdata] = Q30WeaponData();
        ri->d[2] = zasm30_next_weapdata++;
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

// ---- the entry points -------------------------------------------------------------------------

static bool zasm30_data_reg(const long arg, long &ret, const long value, const bool write)
{
    if(!q30_active())
        return false;

    return z30d_ffc_reg(arg, ret, value, write) || z30d_mapdata_reg(arg, ret, value, write) || z30d_item_reg(arg, ret, value, write)
           || z30d_msg_reg(arg, ret, value, write) || z30d_npcdata_reg(arg, ret, value, write) || z30d_dmap_reg(arg, ret, value, write)
           || z30d_bottle_reg(arg, ret, value, write) || z30d_weapdata_reg(arg, ret, value, write);
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
        const long ptr = get_register(sarg1) / 10000, id = Z30_REF(BOTTLETYPE) - 1;

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
        const long ptr = get_register(sarg1) / 10000;
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
        const long ptr = get_register(sarg1) / 10000, id = Z30_REF(NPCDATA);

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
