// 3.0 scrolling regions in the script engine. Included once, at the end of ffscript.cpp (it uses
// that file's statics). See q30_regions.h, "every screen of a region live".
//
// Upstream runs the FFC scripts, the screen script and the combo scripts of every screen of a
// scrolling region at once. This engine keeps the scripts of one screen in its globals:
// ffcScriptData, ffmisc, the FFC frames, the screen script and the combo script instances. While
// Link is in a region, each of its other screens keeps its own in a Zasm30ScreenState, which
// q30_regions.cpp swaps with the globals while that screen runs, and at a hand-off, so none of
// them starts over.
//
// In a region an FFC is named as upstream names it: its ID is (its screen's index in the region,
// row by row) * 128 + its slot + 1, and its X and Y are region pixels. A script can so reach an
// FFC of another screen of the region: its register is read and written in that screen's table
// (Zasm30OtherFFCs). Nothing here changes anything for 2.53 quests or outside regions.

// ---- each screen's own scripts ----------------------------------------------------------------

struct Zasm30ScreenState
{
    // its FFCs' scripts
    refInfo ffc_ri[32];                 // ffcScriptData
    long ffc_misc[32][16];              // ffmisc
    Zasm30Frame *ffc_frames;            // zasm30_ffc_frames (32)
    dword ffc_waitdraw;                 // zasm30_ffc_waitdraw
    // its screen script
    refInfo screen_ri;
    long screen_stack[ZSCRIPT_STACK_ALLOC];
    Zasm30Frame screen_frame;           // zasm30_frames[34]
    bool screen_done;
    int screen_index;
    // its combo scripts
    std::vector<word> combo_cache;
    int combo_cache_screen;
    std::vector<Zasm30Instance *> combos;
    // its FFCs (their scripts and InitD), for the collector; its slot's, wherever it is
    const mapscr *scr;
};

static std::set<Zasm30ScreenState *> zasm30_screen_states;

void *zasm30_screen_state_new(const mapscr *scr)
{
    Zasm30ScreenState *s = new Zasm30ScreenState;
    memset(s->ffc_misc, 0, sizeof(s->ffc_misc));
    s->ffc_frames = new Zasm30Frame[32];
    s->ffc_waitdraw = 0;
    memset(s->screen_stack, 0, sizeof(s->screen_stack));
    s->screen_done = true;
    s->screen_index = -1;
    s->combo_cache_screen = -1;
    s->scr = scr;
    zasm30_screen_states.insert(s);
    return s;
}

void zasm30_screen_state_delete(void *state)
{
    Zasm30ScreenState *s = (Zasm30ScreenState *)state;

    if(!s)
        return;

    for(size_t n = 0; n < s->combos.size(); ++n)
        zasm30_retire_instance(s->combos[n]);

    delete [] s->ffc_frames;
    zasm30_screen_states.erase(s);
    delete s;
}

// The globals and the state change places: the screen whose state it was is the current one now.
void zasm30_screen_state_swap(void *state)
{
    Zasm30ScreenState *s = (Zasm30ScreenState *)state;

    if(!s)
        return;

    for(int i = 0; i < 32; ++i)
    {
        std::swap(ffcScriptData[i], s->ffc_ri[i]);

        for(int n = 0; n < 16; ++n)
            std::swap(ffmisc[i][n], s->ffc_misc[i][n]);
    }

    zasm30_ffc_frame(0);    // (the globals have theirs)
    std::swap(zasm30_ffc_frames, s->ffc_frames);
    std::swap(zasm30_ffc_waitdraw, s->ffc_waitdraw);

    std::swap(zasm30_screen_ri, s->screen_ri);

    for(int n = 0; n < ZSCRIPT_STACK_ALLOC; ++n)
        std::swap(zasm30_screen_stack[n], s->screen_stack[n]);

    std::swap(zasm30_frames[34], s->screen_frame);
    std::swap(zasm30_screen_done, s->screen_done);
    std::swap(zasm30_screen_index, s->screen_index);

    std::swap(zasm30_combo_cache, s->combo_cache);
    std::swap(zasm30_combo_cache_screen, s->combo_cache_screen);

    // the combo script instances: the table's go to the state, the state's into the table
    std::vector<Zasm30Instance *> out;

    for(std::map<std::pair<int,long>, Zasm30Instance*>::iterator it = zasm30_instances.begin(); it != zasm30_instances.end();)
    {
        if(it->first.first == SCRIPT_COMBO)
        {
            out.push_back(it->second);
            zasm30_instances.erase(it++);
        }
        else
            ++it;
    }

    for(size_t n = 0; n < s->combos.size(); ++n)
        zasm30_instances[std::make_pair((int)SCRIPT_COMBO, s->combos[n]->key)] = s->combos[n];

    s->combos.swap(out);
}

// Whether region screen `index` (map * MAPSCRS + screen) has a screen script to run.
bool zasm30_screen_has_script(int index)
{
    std::map<int, Zasm30ScreenScript>::const_iterator it = zasm30_screen_scripts.find(index);
    return it != zasm30_screen_scripts.end() && it->second.script && it->second.script < 256 && screenscripts[it->second.script];
}

// A screen's script and InitD (ZC_REGION_LIST=2 lists them); false when it has neither.
bool zasm30_screen_script_info(int index, int *script, long *initd)
{
    std::map<int, Zasm30ScreenScript>::const_iterator it = zasm30_screen_scripts.find(index);

    if(it == zasm30_screen_scripts.end())
        return false;

    *script = it->second.script;
    memcpy(initd, it->second.initd, sizeof(it->second.initd));
    return true;
}

// Whether a screen's state has Waitdraw halves to run (what: Q30_EACH_FFC_WAITDRAW or
// Q30_EACH_COMBO_WAITDRAW).
bool zasm30_screen_state_busy(void *state, int what)
{
    const Zasm30ScreenState *s = (const Zasm30ScreenState *)state;

    if(!s)
        return false;

    if(what == Q30_EACH_FFC_WAITDRAW)
        return s->ffc_waitdraw != 0;

    if(what == Q30_EACH_COMBO_WAITDRAW)
    {
        for(size_t n = 0; n < s->combos.size(); ++n)
            if(s->combos[n]->waitdraw && s->combos[n]->doscript)
                return true;
    }

    return false;
}

// The collector's roots on the region's other screens (zasm30_collect).
static void zasm30_mark_region_states(std::vector<long> &work)
{
    for(std::set<Zasm30ScreenState *>::const_iterator it = zasm30_screen_states.begin(); it != zasm30_screen_states.end(); ++it)
    {
        const Zasm30ScreenState *s = *it;

        for(int i = 0; i < 32; ++i)
        {
            if(s->scr && s->scr->ffscript[i])
                zasm30_mark_script(s->ffc_ri[i], NULL, s->ffc_frames[i], work);

            for(int n = 0; n < 16; ++n)
                zasm30_mark(s->ffc_misc[i][n], work);

            for(int n = 0; s->scr && n < 8; ++n)
                zasm30_mark(s->scr->initd[i][n], work);
        }

        zasm30_mark_script(s->screen_ri, s->screen_stack, s->screen_frame, work);

        for(size_t n = 0; n < s->combos.size(); ++n)
            zasm30_mark_script(s->combos[n]->ri, NULL, s->combos[n]->frame, work);
    }
}

// ---- naming an FFC ------------------------------------------------------------------------------

// A 3.0 script's reference to slot `index` of screen `screen`: upstream's sprite id for that FFC,
// 0x7FF00000 + its ID - 1 (outside a region the current screen is index 0 of it).
static long zasm30_ffc_ref(int screen, int index)
{
    const int off = q30_region_screen_index(screen);
    return 0x7FF00000L + (off > 0 ? off : 0) * 128L + index;
}

// The FFC a reference names: its slot, and its screen (-1: the current one). False when there is
// none (its slot is past this engine's 32, or its screen isn't in the region): *index is then
// 127, an FFC upstream has and this engine doesn't, which reads as missing without a log line.
static bool zasm30_ffc_from_ref(long ref, int *screen, int *index)
{
    *screen = -1;
    *index = 127;

    if(ref < 0x7FF00000L)
        return false;

    const long id = ref - 0x7FF00000L;
    const int scr = q30_region_index_screen((int)(id / 128));

    if(scr < 0 || id % 128 >= MAXFFCS || (scr != currscr && !q30_region_ffc_table(scr)))
        return false;

    *screen = scr == currscr ? -1 : scr;
    *index = (int)(id % 128);
    return true;
}

// Screen->LoadFFC(id): the reference for ID id + 1, or 0 (NULL) for none.
static long zasm30_load_ffc(long id)
{
    int screen, index;
    return (id >= 0 && zasm30_ffc_from_ref(0x7FF00000L + id, &screen, &index)) ? 0x7FF00000L + id : 0;
}

// Screen->LoadFFC(screen, index): slot index (0-based) of a screen of the region (the current
// screen outside one), or 0.
static long zasm30_load_ffc_at(long screen, long index)
{
    if(index < 0 || index >= MAXFFCS || q30_region_screen_index((int)screen) < 0)
        return 0;

    return zasm30_load_ffc((long)q30_region_screen_index((int)screen) * 128 + index);
}

// REFFFC set by a 3.0 script: the slot goes to ri->ffcref as before, the screen to its frame.
static void zasm30_select_ffc(long value)
{
    int screen, index;
    zasm30_ffc_from_ref(value, &screen, &index);
    ri->ffcref = (byte)index;
    zasm30_frame().ffc_screen = screen;
}

// The selected FFC's reference (REFFFC read, ffc->ID).
static long zasm30_selected_ffc_ref()
{
    const int screen = zasm30_frame().ffc_screen < 0 ? currscr : zasm30_frame().ffc_screen;
    return zasm30_ffc_ref(screen, ri->ffcref);
}

// The screen the running script's Screen-> means (upstream's ri->screenref): in a scrolling
// region the region's top-left screen for global, generic, item, DMap and subscreen scripts, which
// upstream gives cur_screen; for the others the current screen (an FFC, screen or combo script's
// own, as it stands in for the current one, or the Hero's). currscr outside regions.
static int zasm30_script_screen()
{
    if(!q30_region_on())
        return currscr;

    switch(curScriptType)
    {
    case SCRIPT_GLOBAL: case SCRIPT_GENERIC: case SCRIPT_GENERICFROZEN: case SCRIPT_ITEM: case SCRIPT_DMAP:
    case SCRIPT_PASSIVESUB: case SCRIPT_ACTIVESUB: case SCRIPT_ONMAP: case SCRIPT_ENGINESUB:
        return q30_region_origin();
    }

    return currscr;
}

// An FFC script starting a run: `this` (SCR0) is its own FFC, as upstream's set_script_this_ref
// sets it on every run, and its own screen's FFCs are the ones it names first.
static void zasm30_ffc_this(int i)
{
    zasm30_frame().inst[0] = zasm30_ffc_ref(currscr, i);
    zasm30_frame().ffc_screen = -1;
}

// ---- an FFC of another screen of the region ---------------------------------------------------

// FFC slot i of two screens changes places (all of it that the FFC registers read and write).
static void zasm30_swap_ffc(mapscr &a, mapscr &b, int i)
{
    std::swap(a.ffdata[i], b.ffdata[i]);
    std::swap(a.ffcset[i], b.ffcset[i]);
    std::swap(a.ffdelay[i], b.ffdelay[i]);
    std::swap(a.ffx[i], b.ffx[i]);
    std::swap(a.ffy[i], b.ffy[i]);
    std::swap(a.ffxdelta[i], b.ffxdelta[i]);
    std::swap(a.ffydelta[i], b.ffydelta[i]);
    std::swap(a.ffxdelta2[i], b.ffxdelta2[i]);
    std::swap(a.ffydelta2[i], b.ffydelta2[i]);
    std::swap(a.ffflags[i], b.ffflags[i]);
    std::swap(a.ffwidth[i], b.ffwidth[i]);
    std::swap(a.ffheight[i], b.ffheight[i]);
    std::swap(a.fflink[i], b.fflink[i]);
    std::swap(a.ffscript[i], b.ffscript[i]);
    std::swap(a.initialized[i], b.initialized[i]);

    for(int n = 0; n < 8; ++n)
        std::swap(a.initd[i][n], b.initd[i][n]);

    for(int n = 0; n < 2; ++n)
        std::swap(a.inita[i][n], b.inita[i][n]);
}

// While it lives, FFC `slot` (and its Misc[]) of region screen `screen` stands in for the current
// screen's FFC in that slot, so the register code, which reads tmpscr and ffmisc at the selected
// slot only, reaches it. Only that slot moves: a script that names every FFC of a big region in
// turn costs one slot each. For the current screen nothing moves. ok is false when the screen
// isn't one of the region any more; px, py are its top-left in region pixels.
struct Zasm30OtherFFCs
{
    mapscr *m;
    Zasm30ScreenState *st;
    bool ok;
    int px, py;
    int slot;

    Zasm30OtherFFCs(int screen, int ffc) : m(NULL), st(NULL), ok(true), px(0), py(0), slot(ffc)
    {
        q30_region_screen_pixels(screen, &px, &py);

        if(screen == currscr)
            return;

        m = q30_region_ffc_table(screen);
        st = (Zasm30ScreenState *)q30_region_screen_scripts(screen);
        ok = m && st;

        if(ok)
            swap();
    }

    ~Zasm30OtherFFCs()
    {
        if(ok && m)
            swap();
    }

    void swap()
    {
        // (a slot past the 32 names no FFC: the register code reports it missing, reading nothing)
        if(slot < 0 || slot >= MAXFFCS)
            return;

        zasm30_swap_ffc(tmpscr[0], *m, slot);

        for(int n = 0; n < 16; ++n)
            std::swap(ffmisc[slot][n], st->ffc_misc[slot][n]);
    }
};

static bool zasm30_ffc_hook_busy = false;

static bool zasm30_is_ffc_register(long arg)
{
    switch(arg)
    {
    case DATA: case FFSCRIPT: case FCSET: case DELAY: case FX: case FY: case XD: case YD: case XD2: case YD2:
    case FFFLAGSD: case FFCWIDTH: case FFCHEIGHT: case FFTWIDTH: case FFTHEIGHT: case FFLINK: case FFMISCD: case FFINITDD:
        return true;
    }

    return false;
}

// get_register: an FFC register of a 3.0 script in a scrolling region (true: done). It's read in
// the table of the selected FFC's screen; X and Y get that screen's place in the region.
static bool zasm30_region_ffc_get(const long arg, long &ret)
{
    if(zasm30_ffc_hook_busy || !zasm30_is_ffc_register(arg) || !q30_region_on())
        return false;

    const int screen = zasm30_frame().ffc_screen < 0 ? currscr : zasm30_frame().ffc_screen;
    Zasm30OtherFFCs in(screen, ri->ffcref);

    if(!in.ok)
    {
        ret = -10000;   // its screen is gone: as a missing FFC
        return true;
    }

    zasm30_ffc_hook_busy = true;
    ret = get_register(arg);
    zasm30_ffc_hook_busy = false;

    if((arg == FX || arg == FY) && ri->ffcref < MAXFFCS)
        ret += (arg == FX ? in.px : in.py) * 10000L;

    return true;
}

// set_register: the same for writes. Giving another screen's FFC a script starts it over there.
static bool zasm30_region_ffc_set(const long arg, long value)
{
    if(zasm30_ffc_hook_busy || !zasm30_is_ffc_register(arg) || !q30_region_on())
        return false;

    const int screen = zasm30_frame().ffc_screen < 0 ? currscr : zasm30_frame().ffc_screen;
    Zasm30OtherFFCs in(screen, ri->ffcref);

    if(!in.ok)
        return true;

    if(arg == FX || arg == FY)
        value -= (arg == FX ? in.px : in.py) * 10000L;

    if(arg == FFSCRIPT && in.m)
    {
        // (as 2.53's FFSCRIPT, with that screen's script state; tmpscr and ffmisc hold its FFCs)
        const int i = ri->ffcref;

        if(i < MAXFFCS)
        {
            tmpscr->ffscript[i] = vbound(value / 10000, 0, NUMSCRIPTFFC - 1);
            memset(ffmisc[i], 0, sizeof(ffmisc[i]));
            memset(tmpscr->inita[i], 0, sizeof(tmpscr->inita[i]));
            memset(tmpscr->initd[i], 0, sizeof(tmpscr->initd[i]));
            tmpscr->initialized[i] = false;
            in.st->ffc_ri[i].Clear();
            in.st->ffc_frames[i] = Zasm30Frame();
        }

        return true;
    }

    zasm30_ffc_hook_busy = true;
    set_register(arg, value);
    zasm30_ffc_hook_busy = false;
    return true;
}

// The sprite registers (X, Y, Tile, CSet, Misc[]...) of an FFC in a scrolling region, read or
// written in its screen's table, X and Y in region pixels (zasm30_sprite_get / _set).
static bool zasm30_region_ffc_sprite_get(const long arg, long &ret, const Z30Sprite &r)
{
    Zasm30OtherFFCs in(r.ffc_screen < 0 ? currscr : r.ffc_screen, r.ffc);

    if(!in.ok)
    {
        ret = 0;
        return z30_is_sprite_register(arg);
    }

    const bool done = zasm30_sprite_read(arg, ret, r);

    if(done && (arg == Z30R_SPRITE_X || arg == Z30R_SPRITE_Y))
        ret += (arg == Z30R_SPRITE_X ? in.px : in.py) * 10000L;

    return done;
}

static bool zasm30_region_ffc_sprite_set(const long arg, long value, const Z30Sprite &r)
{
    Zasm30OtherFFCs in(r.ffc_screen < 0 ? currscr : r.ffc_screen, r.ffc);

    if(!in.ok)
        return z30_is_sprite_register(arg);

    if(arg == Z30R_SPRITE_X || arg == Z30R_SPRITE_Y)
        value -= (arg == Z30R_SPRITE_X ? in.px : in.py) * 10000L;

    return zasm30_sprite_write(arg, value, r);
}
