// Arrays as script objects for 3.0 quests compiled at ZScript version 28 on (upstream's
// ZScriptVersion::gc_arrays(), zc/scripting/script_object.cpp and array_manager.cpp).
// Included once, from ffscript.cpp, after the 3.0 heap code.
//
// From version 28 a script's array pointer is the array's object id from the shared pool
// (zasm30_object_id_new: 60001 up, a freed id is the next one given out), not slot * 10000. The
// arrays stay in this engine's slots (heap slots Z30_HEAP_FIRST up, global arrays at
// MAX_ZCARRAY_SIZE + n); zasm30_aptr turns a pointer into its slot at every array site, and
// zasm30_aval a slot into the pointer a script sees. Older quests (and every 2.53 quest) keep
// slot * 10000, exactly as before.
//
// Heap arrays are reference counted as upstream: a new one starts with one reference, held by the
// autorelease pool, which lets go at the end of each script run (run_script); STORE_OBJECT,
// SET_OBJECT, REF_INC / REF_DEC / REF_REMOVE / REF_AUTORELEASE and MARK_TYPE_STACK move the
// counts as upstream's handlers do. An array whose count reaches 0 is freed (its id handed out
// again) once a mark from the scripts' variables (not their registers) finds nothing else that
// refers to it: this engine does not count the references array elements and class fields hold,
// so that check stands in for them. The mark-and-sweep collector (zasm30_collect) still frees
// what nothing reaches, as upstream's collector does for cycles.

static inline bool zasm30_gc_arrays() { return zasm30_ffscript_version >= 28; }

static std::map<long, long> zasm30_aid_slot;    // array id -> slot
static std::vector<long> zasm30_slot_aid;       // slot -> array id (0: none)
static std::vector<long> zasm30_slot_refs;      // slot -> reference count (heap arrays)
static std::vector<long> zasm30_pool;           // upstream's autorelease pool (ids, in order)
static std::set<long> zasm30_pool_set;
static std::vector<long> zasm30_dead;           // heap slots whose count reached 0, to be checked
static bool zasm30_mark_no_regs = false;        // zasm30_mark_script: leave the registers out

static void zasm30_arrays_size()
{
    if(zasm30_slot_aid.empty())
    {
        zasm30_slot_aid.assign(Z30_LOCALRAM_SIZE, 0);
        zasm30_slot_refs.assign(Z30_LOCALRAM_SIZE, 0);
    }
}

// Forget every array id (a new game or quest; the ids themselves start again elsewhere).
static void zasm30_arrays_forget()
{
    zasm30_aid_slot.clear();
    if(!zasm30_slot_aid.empty())
    {
        std::fill(zasm30_slot_aid.begin(), zasm30_slot_aid.end(), 0);
        std::fill(zasm30_slot_refs.begin(), zasm30_slot_refs.end(), 0);
    }
    zasm30_pool.clear();
    zasm30_pool_set.clear();
    zasm30_dead.clear();
}

static void zasm30_arr_bind(long slot, long id)
{
    zasm30_arrays_size();
    if(slot <= 0 || slot >= Z30_LOCALRAM_SIZE || !id)
        return;
    zasm30_slot_aid[slot] = id;
    zasm30_aid_slot[id] = slot;
}

// The slot an array pointer names (what ArrayH takes); 0 if none.
long zasm30_aptr(long v)
{
    if(!zasm30_gc_arrays())
        return v / 10000;

    std::map<long, long>::const_iterator it = zasm30_aid_slot.find(v);

    if(it != zasm30_aid_slot.end())
        return it->second;

    // a pointer this engine made before it had ids (none should be left): its slot has no id
    if(v > 0 && v % 10000 == 0)
    {
        const long p = v / 10000;

        if(p < Z30_LOCALRAM_SIZE && (zasm30_slot_aid.empty() || !zasm30_slot_aid[p]))
            return p;
    }

    return 0;
}

// The pointer a script sees for an array's slot.
long zasm30_aval(long slot)
{
    if(!zasm30_gc_arrays() || slot <= 0)
        return slot * 10000;

    zasm30_arrays_size();

    if(slot < Z30_LOCALRAM_SIZE && zasm30_slot_aid[slot])
        return zasm30_slot_aid[slot];

    return slot * 10000;
}

// A heap array is gone (zasm30_free_array): its id is free again.
static void zasm30_arr_unbind(long slot)
{
    if(zasm30_slot_aid.empty() || slot <= 0 || slot >= Z30_LOCALRAM_SIZE || !zasm30_slot_aid[slot])
        return;

    const long id = zasm30_slot_aid[slot];
    zasm30_slot_aid[slot] = 0;
    zasm30_slot_refs[slot] = 0;
    zasm30_aid_slot.erase(id);

    if(zasm30_pool_set.erase(id))
        zasm30_pool.erase(std::find(zasm30_pool.begin(), zasm30_pool.end(), id));

    zasm30_object_id_free(id);
}

// A new heap array (zasm30_new_array): an id, one reference, in the autorelease pool.
static long zasm30_arr_new(long slot)
{
    const long id = zasm30_object_id_new();

    if(!id)
        return slot * 10000;

    zasm30_arr_bind(slot, id);
    zasm30_slot_refs[slot] = 1;
    zasm30_pool.push_back(id);
    zasm30_pool_set.insert(id);
    return id;
}

// The heap slot a counted reference names (0: not a counted array; global arrays never go).
static long zasm30_counted_slot(long id)
{
    if(!zasm30_gc_arrays() || !id)
        return 0;

    std::map<long, long>::const_iterator it = zasm30_aid_slot.find(id);

    if(it == zasm30_aid_slot.end() || !zasm30_heap_slot(it->second))
        return 0;

    return it->second;
}

static void zasm30_ref_dec(long id)
{
    const long p = zasm30_counted_slot(id);

    if(!p || zasm30_slot_refs[p] <= 0)
        return;

    if(--zasm30_slot_refs[p] == 0)
        zasm30_dead.push_back(p);
}

static void zasm30_ref_inc(long id)
{
    const long p = zasm30_counted_slot(id);

    if(!p)
        return;

    // the first reference that keeps it takes it out of the autorelease pool
    if(++zasm30_slot_refs[p] == 2 && zasm30_pool_set.erase(id))
    {
        zasm30_pool.erase(std::find(zasm30_pool.begin(), zasm30_pool.end(), id));
        zasm30_ref_dec(id);
    }
}

static void zasm30_autorelease(long id)
{
    const long p = zasm30_counted_slot(id);

    if(p && !zasm30_pool_set.count(id))
    {
        zasm30_pool.push_back(id);
        zasm30_pool_set.insert(id);
        ++zasm30_slot_refs[p];
    }
}

// ---- the script's typed stack slots (upstream's ri->stack_pos_is_object) ---------------------
static void zasm30_store_object(long offset, long id)
{
    std::set<long> &objs = zasm30_frame().objpos;
    const long old = SH::read_stack(offset);
    zasm30_ref_inc(id);

    if(objs.count(offset))
        zasm30_ref_dec(old);
    else
        objs.insert(offset);

    SH::write_stack(offset, id);

    if(zasm30_pool_set.erase(id))
    {
        zasm30_pool.erase(std::find(zasm30_pool.begin(), zasm30_pool.end(), id));
        zasm30_ref_dec(id);
    }
}

static void zasm30_ref_change(long arg, bool inc)
{
    long id;

    if(arg == -1)
        id = zasm30_frame().thiskey;
    else
    {
        const long offset = get_register(D(4)) + arg;

        if(!zasm30_frame().objpos.count(offset))
            return;

        id = SH::read_stack(offset);
    }

    if(inc)
        zasm30_ref_inc(id);
    else
        zasm30_ref_dec(id);
}

static void zasm30_ref_remove(long arg)
{
    const long offset = get_register(D(4)) + arg;
    std::set<long> &objs = zasm30_frame().objpos;

    if(!objs.count(offset))
        return;

    const long id = SH::read_stack(offset);
    objs.erase(offset);
    zasm30_ref_dec(id);
}

static void zasm30_mark_type_stack(long type, long arg)
{
    const long offset = get_register(D(4)) + arg;

    if(type)
        zasm30_frame().objpos.insert(offset);
    else
        zasm30_frame().objpos.erase(offset);
}

// ---- freeing what reached 0 ------------------------------------------------------------------
static void zasm30_collect_mark();      // the mark half of zasm30_collect

// Free the arrays whose count reached 0 and that nothing else refers to, in the order they
// reached 0 (so the last one freed is the next id given out, as upstream's).
static void zasm30_free_dead()
{
    if(zasm30_dead.empty())
        return;

    // (a mark costs about as much as a collection: at most 16 a frame; the rest wait their turn,
    // and the collector frees whatever is left once 1000 more arrays are made)
    static int marks_frame = -1, marks = 0;

    if(frame != marks_frame)
    {
        marks_frame = frame;
        marks = 0;
    }

    if(++marks > 16)
    {
        if(zasm30_dead.size() > 4096)
            zasm30_dead.erase(zasm30_dead.begin(), zasm30_dead.end() - 4096);
        return;
    }

    std::vector<long> dead;
    dead.swap(zasm30_dead);

    bool any = false;

    for(size_t n = 0; n < dead.size(); ++n)
        if(zasm30_slot_refs[dead[n]] == 0 && zasm30_slot_aid[dead[n]] && zasm30_array_live(dead[n]))
            any = true;

    if(!any)
        return;

    zasm30_mark_no_regs = true;
    zasm30_collect_mark();
    zasm30_mark_no_regs = false;

    for(size_t n = 0; n < dead.size(); ++n)
    {
        const long p = dead[n];

        if(zasm30_slot_refs[p] == 0 && zasm30_slot_aid[p] && zasm30_array_live(p) && !zasm30_gc_marked[p])
            zasm30_free_array(p);
    }
}

// The end of a script run: the autorelease pool lets go (upstream's run_script).
static void zasm30_drain_pool()
{
    if(!zasm30_gc_arrays())
        return;

    if(!zasm30_pool.empty())
    {
        std::vector<long> ids;
        ids.swap(zasm30_pool);
        zasm30_pool_set.clear();

        for(size_t n = 0; n < ids.size(); ++n)
            zasm30_ref_dec(ids[n]);
    }

    zasm30_free_dead();
}

// A saved game is loaded: the quest's global arrays take the ids they had (60001 up, in order,
// as when the global script first made them).
void zasm30_arrays_load_globals(long count)
{
    if(!zasm30_gc_arrays())
        return;

    zasm30_object_ids_reset();
    zasm30_arrays_forget();

    for(long n = 0; n < count; ++n)
        zasm30_arr_bind(MAX_ZCARRAY_SIZE + n, zasm30_object_id_new());
}
