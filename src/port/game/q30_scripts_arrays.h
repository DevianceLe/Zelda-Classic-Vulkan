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
//
// Class objects, files, directories and stacks are counted the same way (zasm30_obj_refs), and a
// class field the compiler types as an object (ZCLASS_MARK_TYPE) holds a reference, as do an
// object's member arrays. Whatever reaches 0 goes before the next object id is given out (or at
// the end of the run), in the order it reached 0, so ids are handed out again as upstream's are.
// The check that nothing else refers to it marks from the scripts' typed stack slots only: the id
// a variable that left scope leaves behind in its slot keeps nothing alive.

static inline bool zasm30_gc_arrays() { return zasm30_ffscript_version >= 28; }

struct Zasm30Internal;
static const Zasm30Internal *zasm30_internal(long id);
static void zasm30_internals_forget();
static long zasm30_iarr_proxy(long id);

static std::map<long, long> zasm30_aid_slot;    // array id -> slot
static std::vector<long> zasm30_slot_aid;       // slot -> array id (0: none)
static std::vector<long> zasm30_slot_refs;      // slot -> reference count (heap arrays)
static std::vector<long> zasm30_pool;           // upstream's autorelease pool (ids, in order)
static std::set<long> zasm30_pool_set;
static std::vector<long> zasm30_dead;           // ids whose count reached 0, to be checked (in order)
static std::map<long, long> zasm30_obj_refs;    // class objects, files, directories, stacks: counts
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
    zasm30_obj_refs.clear();
    zasm30_internals_forget();
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

    if(zasm30_internal(v))      // an internal array (below): a proxy slot ArrayH reads through
        return zasm30_iarr_proxy(v);

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

static long *zasm30_obj_count(long id)
{
    if(!id || zasm30_obj_refs.empty())
        return NULL;

    std::map<long, long>::iterator it = zasm30_obj_refs.find(id);
    return it == zasm30_obj_refs.end() ? NULL : &it->second;
}

static void zasm30_pool_remove(long id)
{
    if(zasm30_pool_set.erase(id))
        zasm30_pool.erase(std::find(zasm30_pool.begin(), zasm30_pool.end(), id));
}

static void zasm30_ref_dec(long id)
{
    const long p = zasm30_counted_slot(id);

    if(p)
    {
        if(zasm30_slot_refs[p] > 0 && --zasm30_slot_refs[p] == 0)
            zasm30_dead.push_back(id);
        return;
    }

    long *c = zasm30_obj_count(id);

    if(c && *c > 0 && --*c == 0)
        zasm30_dead.push_back(id);
}

static void zasm30_ref_inc(long id)
{
    const long p = zasm30_counted_slot(id);
    long *c = p ? &zasm30_slot_refs[p] : zasm30_obj_count(id);

    if(!c)
        return;

    // the first reference that keeps it takes it out of the autorelease pool
    if(++*c == 2 && zasm30_pool_set.count(id))
    {
        zasm30_pool_remove(id);
        zasm30_ref_dec(id);
    }
}

static void zasm30_autorelease(long id)
{
    const long p = zasm30_counted_slot(id);
    long *c = p ? &zasm30_slot_refs[p] : zasm30_obj_count(id);

    if(c && !zasm30_pool_set.count(id))
    {
        zasm30_pool.push_back(id);
        zasm30_pool_set.insert(id);
        ++*c;
    }
}

// A new class object, file, directory or stack (version 28 on): one reference, in the pool.
static void zasm30_obj_new(long id)
{
    if(!zasm30_gc_arrays() || !id)
        return;

    zasm30_obj_refs[id] = 1;
    zasm30_pool.push_back(id);
    zasm30_pool_set.insert(id);
}

// An object is gone, however it went (zasm30_object_id_free): forget its count.
static void zasm30_obj_forget(long id)
{
    if(zasm30_obj_refs.erase(id))
        zasm30_pool_remove(id);
}

static long zasm30_obj_refcount(long id)
{
    const long p = zasm30_counted_slot(id);

    if(p)
        return zasm30_slot_refs[p];

    long *c = zasm30_obj_count(id);
    return c ? *c : -1;
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

    if(zasm30_pool_set.count(id))
    {
        zasm30_pool_remove(id);
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
static bool zasm30_reached(long id);    // ffscript.cpp: did the last mark reach this object?
static bool zasm30_free_handle(long id);    // q30_scripts_cmds.h: a file, directory or stack goes

// A class object goes: the objects its typed fields hold and its member arrays let go first
// (those that reach 0 and nothing reached go before it, as upstream's delete_script_object).
static void zasm30_release_now(long id);

static void zasm30_object_drop(long id)
{
    const long n = zasm30_object_index(id);

    if(n < 0 || n >= (long)zasm30_objects.size() || !zasm30_objects[n].live)
        return;

    std::vector<long> held;
    {
        Zasm30Object &o = zasm30_objects[n];

        for(size_t k = 0; k < o.data.size(); ++k)
            if(k >= (size_t)o.nvars || (k < o.types.size() && o.types[k]))
                held.push_back(o.data[k]);

        o.live = false;     // (nothing reaches it while what it held goes)
    }

    const size_t first = zasm30_dead.size();

    for(size_t k = 0; k < held.size(); ++k)
        zasm30_ref_dec(held[k]);

    std::vector<long> now(zasm30_dead.begin() + first, zasm30_dead.end());
    zasm30_dead.resize(first);

    for(size_t k = 0; k < now.size(); ++k)
        if(zasm30_obj_refcount(now[k]) == 0 && !zasm30_reached(now[k]))
            zasm30_release_now(now[k]);

    Zasm30Object &o = zasm30_objects[n];
    o.own = Z30_OWN_NONE;
    o.owner = 0;
    std::vector<long>().swap(o.data);
    std::vector<byte>().swap(o.types);
    zasm30_free_objects.push_back(n);
    zasm30_object_ids.erase(id);
    if(id != Z30_OBJECT_BASE + n)
        zasm30_object_id_free(id);
    o.id = 0;
}

static void zasm30_release_now(long id)
{
    if(const long p = zasm30_counted_slot(id))
    {
        if(zasm30_slot_aid[p] && zasm30_array_live(p))
            zasm30_free_array(p);
        return;
    }

    if(Zasm30Object *o = zasm30_object(id))
    {
        if(o->own == Z30_OWN_NONE)      // (an owned or global object lives on)
            zasm30_object_drop(id);
        return;
    }

    zasm30_free_handle(id);
}

// Free what reached 0 and that nothing else refers to, in the order it reached 0 (so the last one
// freed is the next id given out, as upstream's). Runs before an object id is given out and at the
// end of a script run.
static void zasm30_free_dead()
{
    if(zasm30_dead.empty())
        return;

    static bool busy = false;

    if(busy)
        return;

    // (a mark costs about as much as a collection: at most 16 a frame; the rest wait their turn,
    // and the collector frees whatever is left once 1000 more objects are made)
    static int marks_frame = -1, marks = 0;

    if(frame != marks_frame)
    {
        marks_frame = frame;
        marks = 0;
    }

    bool any = false;

    for(size_t n = 0; n < zasm30_dead.size() && !any; ++n)
        if(zasm30_obj_refcount(zasm30_dead[n]) == 0)
            any = true;

    if(!any)
    {
        zasm30_dead.clear();
        return;
    }

    if(++marks > 16)
    {
        if(zasm30_dead.size() > 4096)
            zasm30_dead.erase(zasm30_dead.begin(), zasm30_dead.end() - 4096);
        return;
    }

    busy = true;
    std::vector<long> dead;
    dead.swap(zasm30_dead);

    zasm30_mark_no_regs = true;
    zasm30_collect_mark();
    zasm30_mark_no_regs = false;

    for(size_t n = 0; n < dead.size(); ++n)
    {
        const long id = dead[n];

        if(zasm30_obj_refcount(id) == 0 && !zasm30_reached(id))
            zasm30_release_now(id);
    }

    // (what reached 0 while those went, and nothing reached, goes too)
    while(!zasm30_dead.empty())
    {
        std::vector<long> more;
        more.swap(zasm30_dead);

        for(size_t n = 0; n < more.size(); ++n)
            if(zasm30_obj_refcount(more[n]) == 0 && !zasm30_reached(more[n]))
                zasm30_release_now(more[n]);
    }

    busy = false;
}

// A script's frame ends (it quits, or starts again): the objects its typed stack slots hold
// let go, as upstream's take_script_object_refs.
static void zasm30_release_frame(Zasm30Frame &f)
{
    if(f.objpos.empty())
        return;

    if(!zasm30_gc_arrays() || f.wstack.empty())
    {
        f.objpos.clear();
        return;
    }

    std::set<long> objs;
    objs.swap(f.objpos);

    for(std::set<long>::const_iterator it = objs.begin(); it != objs.end(); ++it)
        if(*it >= 0 && *it < (long)f.wstack.size())
            zasm30_ref_dec(f.wstack[*it]);
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

// ---- internal arrays (LOAD_INTERNAL_ARRAY / LOAD_INTERNAL_ARRAY_REF, version 28 on) ----------
// An engine array a script holds as an array (SizeOfArray(Screen->D), an npc's Misc[] passed to
// a function, a range for over one): upstream's find_or_create_internal_script_array. Each
// (register, ref) pair gets one pool id, kept for good; its elements are read and written through
// the register itself (with the ref and the index in place), so every bound and side effect is
// the register's own. Its size comes from the register's own index check (z30_array_index), or
// from the table below for the registers 2.53 already had.
void set_register(const long arg, const long value);

struct Zasm30Internal { long var, refreg, ref; };
static std::map<long, Zasm30Internal> zasm30_internals;     // id -> which engine array
enum { Z30_IARR_PROXY = -1000, Z30_IARR_RING = 8 };
static long zasm30_iarr_ring[Z30_IARR_RING];                // proxy slot -> id
static int zasm30_iarr_next = 0;
bool zasm30_size_probe = false;     // z30_array_index: record the size, log nothing
long zasm30_probed_size = -1;

static long zasm30_iarr_access(const Zasm30Internal &ia, long index, bool write, long value)
{
    long oldref = 0;

    if(ia.refreg)
    {
        oldref = get_register(ia.refreg);
        set_register(ia.refreg, ia.ref);
    }

    const long oldidx = ri->d[0];
    ri->d[0] = index * 10000;
    long ret = 0;

    if(write)
        set_register(ia.var, value);
    else
        ret = get_register(ia.var);

    ri->d[0] = oldidx;

    if(ia.refreg)
        set_register(ia.refreg, oldref);

    return ret;
}

static long zasm30_iarr_size(const Zasm30Internal &ia)
{
    zasm30_size_probe = true;
    zasm30_probed_size = -1;
    zasm30_iarr_access(ia, 214747, false, 0);
    zasm30_size_probe = false;

    if(zasm30_probed_size >= 0)
        return zasm30_probed_size;

    switch(ia.var)
    {
    case NPCMISCD: case LWPNMISCD: case EWPNMISCD: case ITEMMISCD: case LINKMISCD: case NPCDD:
        return 32;
    case FFMISCD:
        return 16;
    case FFINITDD: case Z30R_NPCINITD: case SDD:
        return 8;
    case COMBODD: case COMBODDM:
        return 176;
    }

    return 0;
}

static const Zasm30Internal *zasm30_internal(long id)
{
    if(zasm30_internals.empty())
        return NULL;

    std::map<long, Zasm30Internal>::const_iterator it = zasm30_internals.find(id);
    return it == zasm30_internals.end() ? NULL : &it->second;
}

// The proxy slot ArrayH takes for an internal array's id (see zasm30_aptr).
static long zasm30_iarr_proxy(long id)
{
    const int k = zasm30_iarr_next;
    zasm30_iarr_next = (zasm30_iarr_next + 1) % Z30_IARR_RING;
    zasm30_iarr_ring[k] = id;
    return Z30_IARR_PROXY - k;
}

static bool zasm30_is_iarr_proxy(long ptr)
{
    return ptr <= Z30_IARR_PROXY && ptr > Z30_IARR_PROXY - Z30_IARR_RING;
}

static const Zasm30Internal *zasm30_iarr_of(long ptr)
{
    return zasm30_is_iarr_proxy(ptr) ? zasm30_internal(zasm30_iarr_ring[Z30_IARR_PROXY - ptr]) : NULL;
}

// The array as a whole (what ArrayH::getArray hands commands that read it all): a copy.
static ZScriptArray &zasm30_iarr_copy(long ptr)
{
    static ZScriptArray copies[Z30_IARR_RING];
    ZScriptArray &a = copies[Z30_IARR_PROXY - ptr];
    const Zasm30Internal *ia = zasm30_iarr_of(ptr);
    const long size = ia ? zasm30_iarr_size(*ia) : 0;
    a.Resize(size);

    for(long n = 0; n < size; ++n)
        a[n] = zasm30_iarr_access(*ia, n, false, 0);

    return a;
}

static long zasm30_iarr_get(long ptr, long index)
{
    const Zasm30Internal *ia = zasm30_iarr_of(ptr);
    return ia ? zasm30_iarr_access(*ia, index, false, 0) : 0;
}

static void zasm30_iarr_set(long ptr, long index, long value)
{
    if(const Zasm30Internal *ia = zasm30_iarr_of(ptr))
        zasm30_iarr_access(*ia, index, true, value);
}

static long zasm30_iarr_len(long ptr)
{
    const Zasm30Internal *ia = zasm30_iarr_of(ptr);
    return ia ? zasm30_iarr_size(*ia) : -1;
}

static void zasm30_load_internal_array(bool with_ref, const ffscript *cmd)
{
    if(!zasm30_gc_arrays())
    {
        set_register(sarg1, 0);
        return;
    }

    long refreg = 0;

    if(with_ref)
    {
        std::map<const ffscript *, Zasm30Literal>::const_iterator it = zasm30_literals.find(cmd);

        if(it != zasm30_literals.end() && !it->second.vec.empty())
            refreg = it->second.vec[0];
    }

    const long ref = refreg ? get_register(refreg) : 0;

    for(std::map<long, Zasm30Internal>::const_iterator it = zasm30_internals.begin(); it != zasm30_internals.end(); ++it)
    {
        if(it->second.var == sarg2 && it->second.ref == ref && it->second.refreg == refreg)
        {
            set_register(sarg1, it->first);
            return;
        }
    }

    const long id = zasm30_object_id_new();

    if(id)
    {
        Zasm30Internal ia;
        ia.var = sarg2;
        ia.refreg = refreg;
        ia.ref = ref;
        zasm30_internals[id] = ia;
    }

    set_register(sarg1, id);
}

static void zasm30_internals_forget()
{
    zasm30_internals.clear();
}
