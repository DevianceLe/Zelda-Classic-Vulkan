// 2.55 quest readers for the combo sections: CMBO (versions 7-32 in the old flat
// format, 33-73 with per-combo "has" flags) and CMBA (versions 2-5). See q255.h.
//
// CMBO: fields with no room in newcombo go to the table in q30_triggers.h, through
// q30_combo_store. Attributes, usrflags, genflags, label, combo script, triggers, lift
// data, general data and misc weapon data all live there, with upstream's load fixes
// applied (v<19 attributes*10000, v<20 genflags, v<23 CMBTYPEFX, v<25 lock SFX, v<26
// armos, v<27 next/prev to trigchange, v<28 chests, v<40 ripples and grass, v<49 splash,
// v<52 only_gentrig, v<67 CMBTYPEFX on cuttables).
//   cur_frame, aclk v15-32: dropped (2.53 restarts animations at load)
//   o_tile v15-32: used only to pick the tile below
//
// CMBO, stored but limited by 2.53:
//   tile: the player holds NEWMAXTILES tiles; anything higher becomes tile 0
//   type: 2.53 numbers its types as upstream does up to cSCRIPT5. Upstream's later types
//       become cNONE in combobuf, keeping only their solidity; the true number stays in
//       the table and q30_triggers.cpp gives them their behaviour
//   flag: numbered as 2.53 up to mfRAFT_BOUNCE (103); upstream's mfPUSHED is 104. Any
//       other flag above 103 becomes mfNONE in combobuf; the table keeps the true number
//   animflags: kept whole, 2.53 tests only AF_FRESH and AF_CYCLE
//   nexttimer: stored for v<=32, not in the file from v33, so it stays 0
//
// CMBA, read and dropped:
//   aliases past the first 2048 (v3+ files hold 8192)
//   combo pools: v4+
//   autocombos: v5+

#include "precompiled.h" //always first

#include <string.h>

#include "q255.h"
#include "q30.h"
#include "q30_triggers.h"
#include "sfx.h"
#include "tiles.h"

extern combo_alias combo_aliases[MAXCOMBOALIASES];
int init_combo_classes(); // in qst.cpp, not in a header

// What a v33+ combo has in the file (upstream's CHAS_ flags).
enum
{
    Q255_CHAS_ATTRIB = 0x0001,
    Q255_CHAS_FLAG = 0x0002,
    Q255_CHAS_TRIG = 0x0004,
    Q255_CHAS_ANIM = 0x0008,
    Q255_CHAS_SCRIPT = 0x0010,
    Q255_CHAS_BASIC = 0x0020,
    Q255_CHAS_LIFT = 0x0040,
    Q255_CHAS_GENERAL = 0x0080,
    Q255_CHAS_MISC_WEAP_DATA = 0x0100
};

// Upstream's limits, for the same rejects as its reader.
const int Q255_NUM_COMBO_ATTRIBUTES = 24;
const int Q255_MAXCOMBOPOOLS = 8192;
const int Q255_WPNSPR_MAX = 5;

// A byte count, then that many chars (p_getcstr).
static void skip_cstr(Q255Section &s)
{
    byte n = s.getc();
    s.skip(n);
}

// A bitstring (p_getbitstr, p_getbvec on a word-sized, byte-valued vector): the
// size; if not 0, a pairs marker and an entry count; then the entries, either
// word index + byte value or just bytes. Upstream's size is usually 65535 whatever is
// in use, so the vector only grows to the highest byte that is set.
static void read_bitstr(Q255Section &s, Q30Bits &out)
{
    out.v.clear();
    word sz = s.igetw();

    if(!sz)
        return;

    byte pairs = s.getc();
    word count = s.igetw();

    for(word q = 0; q < count && !s.failed; ++q)
    {
        word idx = pairs ? s.igetw() : q;
        byte val = s.getc();

        if(val && idx < sz)
        {
            if(idx >= out.v.size())
                out.v.resize((size_t)idx + 1, 0);

            out.v[idx] = val;
        }
    }
}

// A script_config (p_getvar): the script, the run_args array, and the inst_init
// map. The array has a word count and p_getarr reads no more than its 8 ints.
static void read_script_config(Q255Section &s, word &script, long *initd)
{
    script = s.igetw();
    word args = s.igetw();

    for(word q = 0; q < args; ++q)
    {
        long v = s.igetl_s();

        if(q < 8)
            initd[q] = v;
    }

    word inst = s.igetw();
    s.skip((size_t)inst * (2 + 4));
}

// A weapon_data (read_weap_data), which has a format version of its own.
static void read_weapon_data(Q255Section &s, Q30WeaponData &d)
{
    word v = s.igetw();
    d.flags = s.igetw();
    d.moveflags = s.igetl();
    d.wflags = s.igetw();

    for(int q = 0; q < Q255_WPNSPR_MAX; ++q)
    {
        d.burnsprs[q] = (v < 1) ? (word)s.getc() : s.igetw();
        d.light_rads[q] = s.getc();

        if(v >= 3)
            d.light_offsets[q] = s.igetl_s();
    }

    d.glow_shape = s.getc();
    d.override_flags = s.igetl_s();
    d.tilew = s.igetl_s();
    d.tileh = s.igetl_s();
    d.hxsz = s.igetl_s();
    d.hysz = s.igetl_s();
    d.hzsz = s.igetl_s();
    d.hxofs = s.igetl_s();
    d.hyofs = s.igetl_s();
    d.xofs = s.igetl_s();
    d.yofs = s.igetl_s();
    d.step = s.igetl_s();
    d.unblockable = s.getc();
    d.timeout = s.igetl_s();
    d.imitate_weapon = s.getc();
    d.default_defense = s.getc();
    d.lift_level = s.getc();
    d.lift_time = s.getc();
    d.lift_height = s.igetl_s();

    if(v < 5)
    {
        d.script = s.igetw();

        for(int q = 0; q < 8; ++q)
            d.initd[q] = s.igetl_s();
    }
    else
        read_script_config(s, d.script, d.initd);

    d.pierce_count = s.igetw_s();

    if(v >= 2)
    {
        d.bounce_mult = s.igetl_s();
        d.bounce_add = s.igetl_s();
    }

    if(v >= 4)
    {
        d.viewport_suspend_range = s.igetl_s();
        d.viewport_despawn_range = s.igetl_s();
    }
}

// One combo_trigger of a v33+ combo (readcombo_triggers_loop).
static void read_trigger(Q255Section &s, word v, Q30Trigger &t)
{
    if(v >= 52)
        t.label = s.getcstr();

    if(v < 57)
    {
        int words = (v < 36) ? 3 : 6;

        for(int q = 0; q < words; ++q)
        {
            dword val = s.igetl();

            for(int b = 0; b < 32; ++b)
                if(val & (1u << b))
                    t.flags.set((size_t)q * 32 + b);
        }
    }
    else
        read_bitstr(s, t.flags);

    t.triggerlevel = s.igetl_s();
    t.triggerbtn = s.getc();
    t.triggeritem = (v >= 65) ? s.igetw() : (word)s.getc();
    t.trigtimer = (v >= 53) ? s.igetw() : (word)s.getc();
    t.trigsfx = (v >= 64) ? s.igetw() : (word)s.getc();
    t.trigchange = s.igetl_s();
    t.trigprox = s.igetw();
    t.trigctr = (v >= 53) ? s.igetw_s() : (short)s.getc();
    t.trigctramnt = s.igetl_s();
    t.triglbeam = s.getc();
    t.trigcschange = (signed char)s.getc();
    t.spawnitem = (v >= 65) ? s.igetl_s() : (long)s.igetw_s();
    t.spawnenemy = s.igetw_s();
    t.exstate = (signed char)s.getc();
    t.spawnip = s.igetl_s();
    t.trigcopycat = s.getc();
    t.trigcooldown = s.getc();

    if(v >= 35)
    {
        t.prompt_cid = s.igetw();
        t.prompt_cs = s.getc();
        t.prompt_x = s.igetw_s();
        t.prompt_y = s.igetw_s();
    }

    if(v >= 36)
    {
        t.trig_lstate = s.getc();
        t.trig_gstate = s.getc();
        t.trig_statetime = s.igetl_s();
    }

    if(v >= 37)
        t.trig_genscr = s.igetw();

    if(v >= 38)
    {
        t.trig_group = s.getc();
        t.trig_group_val = s.igetw();
    }

    if(v >= 45)
    {
        t.exdoor_dir = (signed char)s.getc();
        t.exdoor_ind = s.getc();
    }

    if(v >= 46)
    {
        t.trig_levelitems = (v >= 59) ? s.igetw() : (word)s.getc();
        t.trigdmlevel = s.igetw_s();

        if(t.trigdmlevel < -1)
            t.trigdmlevel = -1;

        if(t.trigdmlevel >= MAXLEVELS)
            t.trigdmlevel = MAXLEVELS - 1;

        if(v >= 48)
        {
            for(int q = 0; q < 3; ++q)
                t.trigtint[q] = s.igetw_s();
        }
        else
        {
            // six-bit values, as a scaled-up rgb
            for(int q = 0; q < 3; ++q)
            {
                int val = (signed char)s.getc();
                int a = val < 0 ? -val : val;
                int scaled = (a * 255) / 63;
                t.trigtint[q] = (short)(val < 0 ? -scaled : scaled);
            }
        }

        t.triglvlpalette = s.igetw_s();
        t.trigbosspalette = s.igetw_s();
        t.trigquaketime = s.igetw_s();
        t.trigwavytime = s.igetw_s();
        t.trig_swjinxtime = s.igetw_s();
        t.trig_itmjinxtime = s.igetw_s();
        t.trig_stuntime = s.igetw_s();
        t.trig_bunnytime = s.igetw_s();
        t.trig_pushtime = s.getc();
    }

    if(v >= 47)
        t.trig_shieldjinxtime = s.igetw_s();

    if(v >= 53)
    {
        t.req_level_state = s.igetl();
        t.unreq_level_state = s.igetl();
        read_bitstr(s, t.req_global_state);
        read_bitstr(s, t.unreq_global_state);
        t.fail_prompt_cid = s.igetw();
        t.fail_prompt_cs = s.getc();
        t.trig_msgstr = s.igetl_s();
        t.fail_msgstr = s.igetl_s();
        t.player_bounce = s.igetl_s();
        t.req_player_z = s.igetl_s();
    }
    else
    {
        t.fail_prompt_cid = t.prompt_cid;
        t.fail_prompt_cs = t.prompt_cs;
    }

    if(v >= 54)
    {
        t.req_player_dir = s.getc();
        t.dest_player_x = s.igetl_s();
        t.dest_player_y = s.igetl_s();
        t.dest_player_z = s.igetl_s();
        t.req_player_jump = s.igetl_s();
        t.req_player_x = s.igetl_s();
        t.req_player_y = s.igetl_s();
    }

    if(v >= 56)
    {
        t.dest_player_dir = (signed char)s.getc();
        t.force_ice_combo = s.igetl_s();
        t.force_ice_vx = s.igetl_s();
        t.force_ice_vy = s.igetl_s();
    }

    if(v >= 58)
    {
        t.trig_gravity = s.igetl_s();
        t.trig_terminal_v = s.igetl_s();
    }

    if(v >= 61)
    {
        read_bitstr(s, t.req_screen_state);
        read_bitstr(s, t.unreq_screen_state);
        read_bitstr(s, t.req_screen_ex_state);
        read_bitstr(s, t.unreq_screen_ex_state);
        t.trigstatemap = s.getc();
        t.trigstatescreen = s.getc();
    }

    if(v >= 62)
    {
        t.play_music = s.igetl_s();
        t.set_music_refresh = (signed char)s.getc();
    }

    if(v >= 68)
    {
        t.viewport_cond_range = s.igetw_s();
        t.chance_numerator = s.igetl_s();
        t.chance_denominator = s.igetl_s();
        t.sfx_pan = s.igetw_s();
        t.sfx_volume = s.getc();
        t.sfx_frequency = s.igetl_s();
    }

    if(v >= 69)
    {
        for(int dir = 0; dir < 4; ++dir)
        {
            read_bitstr(s, t.req_screen_ex_door[dir]);
            read_bitstr(s, t.unreq_screen_ex_door[dir]);
        }
    }

    if(v >= 70)
        t.combopos_state = s.getc();

    if(v >= 71)
        t.large_combo_copycat = s.getc();
}

static bool trigger_is_blank(const Q30Trigger &t)
{
    static const Q30Trigger blank;
    return t.label == blank.label && t.flags.empty() && !t.triggerlevel && !t.triggerbtn && !t.triggeritem
        && !t.trigtimer && !t.trigsfx && !t.trigchange && !t.trigprox && !t.trigctr && !t.trigctramnt
        && !t.triglbeam && !t.trigcschange && !t.spawnitem && !t.spawnenemy && t.exstate == blank.exstate
        && !t.spawnip && !t.trigcopycat && !t.trigcooldown;
}

// One combo of the old flat format, v7-32 (readcombos_old).
static void read_combo_old(Q255Section &s, word v, newcombo &c, Q30Combo &x, dword &tile)
{
    tile = (v >= 11) ? s.igetl() : (dword)s.igetw();
    c.flip = s.getc();
    x.walk = s.getc();
    c.walk = (byte)(x.walk & 0x0F);
    x.type = c.type = s.getc();
    c.csets = s.getc();
    c.frames = s.getc();
    c.speed = s.getc();
    c.nextcombo = s.igetw();
    c.nextcset = s.getc();
    x.flag = c.flag = s.getc();
    c.skipanim = s.getc();
    c.nexttimer = s.igetw();
    c.skipanimy = s.getc();
    c.animflags = s.getc();

    if(v >= 8)
    {
        for(int q = 0; q < 4; ++q)
            x.attributes[q] = s.igetl_s();

        x.usrflags = s.igetl();

        if(v >= 20)
            x.genflags = s.igetw();
    }

    Q30Trigger t;
    dword trigflags[6];
    memset(trigflags, 0, sizeof(trigflags));

    if(v >= 10)
    {
        for(int q = 0; q < 3; ++q)
            trigflags[q] = s.igetl();
    }
    else if(v == 9)
    {
        for(int q = 0; q < 2; ++q)
            trigflags[q] = s.igetl();
    }

    if(v >= 9)
        t.triggerlevel = s.igetl_s();

    if(v >= 22)
        t.triggerbtn = s.getc();

    if(v >= 24)
    {
        t.triggeritem = s.getc();
        t.trigtimer = s.getc();
    }

    if(v >= 25)
        t.trigsfx = s.getc();

    if(v >= 27)
        t.trigchange = s.igetl_s();

    if(v >= 29)
    {
        t.trigprox = s.igetw();
        t.trigctr = s.getc();
        t.trigctramnt = s.igetl_s();
    }

    if(v >= 30)
        t.triglbeam = s.getc();

    if(v >= 31)
    {
        t.trigcschange = (signed char)s.getc();
        t.spawnitem = s.igetw_s();
        t.spawnenemy = s.igetw_s();
        t.exstate = (signed char)s.getc();
        t.spawnip = s.igetl_s();
        t.trigcopycat = s.getc();
    }

    if(v >= 32)
        t.trigcooldown = s.getc();

    if(v >= 12)
    {
        char label[12];
        s.read(label, 11);
        label[11] = 0;
        x.label = label;
    }

    if(v >= 13)
    {
        for(int q = 0; q < 4; ++q)
            x.attributes[8 + q] = (long)s.getc() * 10000;
    }

    if(v >= 14)
    {
        word script = s.igetw();
        long initd[8];
        memset(initd, 0, sizeof(initd));

        for(int q = 0; q < 2; ++q)
            initd[q] = s.igetl_s();

        x.script = script;
        memcpy(x.initd, initd, sizeof(x.initd));
    }

    if(v >= 15)
    {
        dword o_tile = s.igetl();
        s.skip(2);              // cur_frame, aclk

        // tile may be a frame saved mid-animation; 2.55 restarts animated
        // combos from o_tile when it loads them.
        if(o_tile && (c.frames > 1 || c.nextcombo != 0))
            tile = o_tile;
    }

    if(v >= 17)
    {
        for(int q = 4; q < 8; ++q)
            x.attributes[8 + q] = (long)s.getc() * 10000;

        for(int q = 0; q < 8; ++q)
            x.attributes[16 + q] = (long)s.igetw_s() * 10000;
    }

    // upstream's load fixes for the old format
    if(v < 18)
        x.walk |= 0xF0;

    if(v < 19)
        for(int q = 0; q < 4; ++q)
            x.attributes[q] *= 10000;

    if(v < 23)
    {
        switch(x.type)
        {
        case cSCRIPT1: case cSCRIPT2: case cSCRIPT3: case cSCRIPT4: case cSCRIPT5:
        case Q30C_SCRIPT6: case Q30C_SCRIPT7: case Q30C_SCRIPT8: case Q30C_SCRIPT9: case Q30C_SCRIPT10:
        case Q30C_TRIGGERGENERIC: case Q30C_CSWITCH:
            trigflags[Q30TF_CMBTYPEFX / 32] |= 1u << (Q30TF_CMBTYPEFX % 32);
            break;
        }
    }

    if(v < 25)
    {
        switch(x.type)
        {
        case cLOCKBLOCK: case cBOSSLOCKBLOCK:
            if(!(x.usrflags & 0x4))
                x.attributes[8 + 3] = (long)WAV_DOOR * 10000;
            x.usrflags &= ~0x4;
            break;
        }
    }

    if(v < 26 && x.type == cARMOS && (x.usrflags & 0x1))
        x.usrflags |= 0x4;

    if(v < 27)
    {
        if(trigflags[0] & 0x00040000)
            t.trigchange = 1;
        else if(trigflags[0] & 0x00080000)
            t.trigchange = -1;
        else
            t.trigchange = 0;

        trigflags[0] &= ~(0x00040000u | 0x00080000u);
    }

    if(v < 28)
    {
        switch(x.type)
        {
        case cLOCKBLOCK: case cLOCKEDCHEST:
            if(x.usrflags & 0x40)
                x.usrflags |= 0x80;
            else
                x.usrflags &= ~0x80;
            x.usrflags &= ~0x40;
            break;
        }

        switch(x.type)
        {
        case cCHEST: case cLOCKEDCHEST: case cBOSSCHEST:
            x.attributes[16 + 2] = -10000;
            x.usrflags |= 0x40;
            break;
        }
    }

    if(v < 20)
    {
        x.genflags = 0;

        switch(x.type)
        {
        case cPUSH_WAIT: case cPUSH_HEAVY: case cPUSH_HW: case cL_STATUE: case cR_STATUE:
        case cPUSH_HEAVY2: case cPUSH_HW2: case cPOUND: case cC_STATUE: case cMIRROR:
        case cMIRRORSLASH: case cMIRRORBACKSLASH: case cMAGICPRISM: case cMAGICPRISM4:
        case cMAGICSPONGE: case cEYEBALL_A: case cEYEBALL_B: case cEYEBALL_4:
        case cBUSH: case cFLOWERS: case cLOCKBLOCK: case cLOCKBLOCK2:
        case cBOSSLOCKBLOCK: case cBOSSLOCKBLOCK2: case cCHEST: case cCHEST2:
        case cLOCKEDCHEST: case cLOCKEDCHEST2: case cBOSSCHEST: case cBOSSCHEST2:
        case cBUSHNEXT: case cBUSHTOUCHY: case cFLOWERSTOUCHY: case cBUSHNEXTTOUCHY:
        case Q30C_SIGNPOST: case Q30C_CSWITCHBLOCK: case Q30C_TORCH: case Q30C_TRIGGERGENERIC:
            if(x.usrflags & 0x8000)
            {
                x.genflags |= 0x0001;
                x.usrflags &= ~0x8000;
            }
            break;
        }
    }

    for(int q = 0; q < 32 * 6; ++q)
        if(trigflags[q / 32] & (1u << (q % 32)))
            t.flags.set(q);

    if(!trigger_is_blank(t))
    {
        x.triggers.push_back(t);
    }
}

// One combo of the v33+ format (readcombo_loop): which groups of fields it has,
// then those groups in this fixed order. Fields of a missing group stay 0.
static void read_combo_new(Q255Section &s, word v, newcombo &c, Q30Combo &x, dword &tile)
{
    word has = (v < 55) ? (word)s.getc() : s.igetw();

    if(has & Q255_CHAS_BASIC)
    {
        tile = s.igetl();
        c.flip = s.getc();
        x.walk = s.getc();
        c.walk = (byte)(x.walk & 0x0F);
        x.type = c.type = s.getc();
        x.flag = c.flag = s.getc();
        c.csets = s.getc();
    }

    if(has & Q255_CHAS_SCRIPT)
    {
        if(v >= 41)
            x.label = s.getcstr();
        else
        {
            char label[12];
            s.read(label, 11);
            label[11] = 0;
            x.label = label;
        }

        word script = 0;
        long initd[8];
        memset(initd, 0, sizeof(initd));

        if(v >= 72)
            read_script_config(s, script, initd);
        else
        {
            script = s.igetw();
            int n = (v >= 43) ? 8 : 2;

            for(int q = 0; q < n; ++q)
                initd[q] = s.igetl_s();
        }

        x.script = script;
        memcpy(x.initd, initd, sizeof(x.initd));
    }

    if(has & Q255_CHAS_ANIM)
    {
        c.frames = s.getc();
        c.speed = s.getc();
        c.nextcombo = s.igetw();
        c.nextcset = s.getc();
        c.skipanim = s.getc();
        c.skipanimy = s.getc();
        c.animflags = s.getc();

        if(v >= 73)
        {
            x.delay = s.igetw();
            x.cs2_flash_duration = s.getc();
        }
    }

    if(has & Q255_CHAS_ATTRIB)
    {
        if(v < 63)
        {
            for(int q = 0; q < 4; ++q)
                x.attributes[q] = s.igetl_s();

            for(int q = 0; q < 8; ++q)
                x.attributes[8 + q] = (long)s.getc() * 10000;

            for(int q = 0; q < 8; ++q)
                x.attributes[16 + q] = (long)s.igetw_s() * 10000;
        }
        else
        {
            word num_attributes = s.igetw();

            if(num_attributes > Q255_NUM_COMBO_ATTRIBUTES)
            {
                s.failed = true;
                return;
            }

            for(word q = 0; q < num_attributes; ++q)
                x.attributes[q] = s.igetl_s();
        }
    }

    if(has & Q255_CHAS_FLAG)
    {
        x.usrflags = s.igetl();
        x.genflags = s.igetw();
    }

    if(has & Q255_CHAS_TRIG)
    {
        int count = 1;

        if(v >= 52)
            count = s.getc();

        for(int q = 0; q < count && !s.failed; ++q)
        {
            Q30Trigger t;
            read_trigger(s, v, t);
            x.triggers.push_back(t);
        }

        if(v >= 52)
            x.only_gentrig = s.getc();
        else if(!x.triggers.empty())
            x.only_gentrig = x.triggers[0].flags.get(Q30TF_ONLYGENTRIG) ? 1 : 0;
    }

    if(has & Q255_CHAS_LIFT)
    {
        x.liftcmb = s.igetw();
        x.liftcs = s.getc();
        x.liftundercmb = s.igetw();
        x.liftundercs = s.getc();
        x.liftdmg = s.getc();
        x.liftlvl = s.getc();
        x.liftitm = s.getc();
        x.liftflags = s.getc();
        x.liftgfx = s.getc();
        x.liftsprite = (v < 66) ? (word)s.getc() : s.igetw();
        x.liftsfx = (v < 64) ? (word)s.getc() : s.igetw();
        x.liftbreaksprite = s.igetw_s();
        x.liftbreaksfx = s.getc();

        if(v >= 34)
        {
            x.lifthei = s.getc();
            x.lifttime = s.getc();
        }

        if(v >= 65)
            x.lift_parent_item = s.igetw();
        else if(v >= 39)
            x.lift_parent_item = s.getc();

        if(v >= 51 && v < 55)
        {
            x.lift_weap.light_rads[0] = s.getc();
            x.lift_weap.glow_shape = s.getc();
        }

        if(v >= 55)
            read_weapon_data(s, x.lift_weap);
    }

    if(has & Q255_CHAS_GENERAL)
    {
        x.speed_mult = s.getc();
        x.speed_div = s.getc();
        x.speed_add = s.igetl_s();

        if(v >= 42)
        {
            if(v < 64)
            {
                x.sfx_appear = s.getc();
                x.sfx_disappear = s.getc();
                x.sfx_loop = s.getc();
                x.sfx_walking = s.getc();
                x.sfx_standing = s.getc();
            }
            else
            {
                x.sfx_appear = s.igetw();
                x.sfx_disappear = s.igetw();
                x.sfx_loop = s.igetw();
                x.sfx_walking = s.igetw();
                x.sfx_standing = s.igetw();
            }

            if(v < 66)
            {
                x.spr_appear = s.getc();
                x.spr_disappear = s.getc();
                x.spr_walking = s.getc();
                x.spr_standing = s.getc();
            }
            else
            {
                x.spr_appear = s.igetw();
                x.spr_disappear = s.igetw();
                x.spr_walking = s.igetw();
                x.spr_standing = s.igetw();
            }
        }

        if(v < 64)
        {
            if(v >= 44)
                x.sfx_tap = s.getc();

            if(v >= 49)
                x.sfx_landing = s.getc();
        }
        else
        {
            x.sfx_tap = s.igetw();
            x.sfx_landing = s.igetw();
        }

        if(v >= 50)
        {
            if(v < 66)
            {
                x.spr_falling = s.getc();
                x.spr_drowning = s.getc();
                x.spr_lava_drowning = s.getc();
            }
            else
            {
                x.spr_falling = s.igetw();
                x.spr_drowning = s.igetw();
                x.spr_lava_drowning = s.igetw();
            }

            if(v < 64)
            {
                x.sfx_falling = s.getc();
                x.sfx_drowning = s.getc();
                x.sfx_lava_drowning = s.getc();
            }
            else
            {
                x.sfx_falling = s.igetw();
                x.sfx_drowning = s.igetw();
                x.sfx_lava_drowning = s.igetw();
            }
        }

        if(v >= 56)
        {
            x.z_height = s.igetl_s();
            x.z_step_height = s.igetl_s();
        }

        if(v >= 60)
            x.dive_under_level = s.getc();

        if(v >= 71)
            x.large_combo_dirs = s.getc();
    }

    if(has & Q255_CHAS_MISC_WEAP_DATA)
        read_weapon_data(s, x.misc_weap);
}

// Upstream's flag number to this engine's.
static byte map_combo_flag(byte flag)
{
    if(flag == 104)             // 2.55's mfPUSHED
        return mfPUSHED;

    if(flag >= mfMAX)
        return mfNONE;

    return flag;
}

static bool is_cuttable_type(int type)
{
    switch(type)
    {
    case cSLASH: case cSLASHITEM: case cBUSH: case cFLOWERS: case cTALLGRASS: case cTALLGRASSNEXT:
    case cSLASHNEXT: case cSLASHNEXTITEM: case cBUSHNEXT: case cSLASHTOUCHY: case cSLASHITEMTOUCHY:
    case cBUSHTOUCHY: case cFLOWERSTOUCHY: case cTALLGRASSTOUCHY: case cSLASHNEXTTOUCHY:
    case cSLASHNEXTITEMTOUCHY: case cBUSHNEXTTOUCHY:
        return true;
    }

    return false;
}

// upstream's update_combo and its v<67 cuttables fix
static void apply_load_fixes(word v, Q30Combo &x, bool post_v14_patch)
{
    if(v < 40)
    {
        switch(x.type)
        {
        case cWATER: case cSHALLOWWATER:
            x.attributes[8 + 6] = 56L * 10000   /* iwRipples */;
            break;

        case cTALLGRASS: case cTALLGRASSNEXT: case cTALLGRASSTOUCHY:
            x.attributes[8 + 6] = 55L * 10000   /* iwTallGrass */;
            break;
        }
    }

    if(v < 49 && (x.type == cWATER || x.type == cSHALLOWWATER))
        x.sfx_landing = WAV_ZN1SPLASH;

    if(v < 67 && !post_v14_patch && is_cuttable_type(x.type) && !x.triggers.empty())
        x.triggers.front().flags.set(Q30TF_CMBTYPEFX);
}

// Whether a combo has anything the table should hold.
static bool combo_needs_entry(const newcombo &c, const Q30Combo &x)
{
    if(c.tile || c.type || c.flag || c.frames || x.type || x.flag)
        return true;

    if(x.usrflags || x.genflags || x.only_gentrig || !x.triggers.empty() || !x.label.empty())
        return true;

    for(int q = 0; q < Q255_NUM_COMBO_ATTRIBUTES; ++q)
        if(x.attributes[q])
            return true;

    if(x.liftcmb || x.liftundercmb || x.liftflags || x.liftgfx || x.liftsprite || x.liftsfx || x.liftdmg)
        return true;

    if(x.speed_mult != 1 || x.speed_div != 1 || x.speed_add)
        return true;

    if(x.sfx_appear || x.sfx_disappear || x.sfx_loop || x.sfx_walking || x.sfx_standing || x.sfx_tap || x.sfx_landing)
        return true;

    if(x.spr_appear || x.spr_disappear || x.spr_walking || x.spr_standing)
        return true;

    if(x.z_height || x.z_step_height || x.dive_under_level || x.large_combo_dirs || x.delay || x.cs2_flash_duration)
        return true;

    return false;
}

int q255_read_combos(Q255Section &s)
{
    const word v = s.version;

    if(v < 7 || v > 73)
        return Q255_NOT_HANDLED;

    if(s.keep)
    {
        reset_combo_animations();
        reset_combo_animations2();
        init_combo_classes();
        memset(combobuf, 0, sizeof(newcombo) * MAXCOMBOS);
        q30_combos_clear();
    }

    const int combos_used = s.igetw();

    // Upstream's v<67 fix is skipped for quests saved by 2.55.14 and later, but the
    // header's patch number isn't kept. Those quests are v67+ from that version on anyway.
    const bool post_v14_patch = false;

    for(int i = 0; i < combos_used && !s.failed; ++i)
    {
        newcombo c;
        memset(&c, 0, sizeof(c));
        Q30Combo *x = new Q30Combo();
        dword tile = 0;

        if(v <= 32)
            read_combo_old(s, v, c, *x, tile);
        else
            read_combo_new(s, v, c, *x, tile);

        if(s.failed)
        {
            delete x;
            break;
        }

        q255_note_tile("combo", tile);
        c.tile = (tile < (dword)NEWMAXTILES) ? (tile_id)tile : (tile_id)0;

        if(c.type >= cMAX)
            c.type = cNONE;

        c.flag = map_combo_flag(c.flag);
        apply_load_fixes(v, *x, post_v14_patch);

        if(s.keep && i < MAXCOMBOS)
        {
            combobuf[i] = c;

            if(combo_needs_entry(c, *x))
                q30_combo_store(i, x);
            else
                delete x;
        }
        else
            delete x;
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        setup_combo_animations();
        setup_combo_animations2();
        q30_trigger_stats();
    }

    return 0;
}

// Combo pools, v4+: for each pool a count, then entries of combo, cset, quantity.
static void skip_combo_pools(Q255Section &s)
{
    word num_pools = s.igetw();

    if(num_pools > Q255_MAXCOMBOPOOLS)
    {
        s.failed = true;
        return;
    }

    for(word p = 0; p < num_pools && !s.failed; ++p)
    {
        int entries = (int)s.igetl();

        if(entries < 1)
            continue;

        // Compared by division so a huge count can't overflow.
        if((size_t)entries > s.left() / 7)
        {
            s.failed = true;
            return;
        }

        s.skip((size_t)entries * 7);
    }
}

// Autocombos, v5+: type, display combo, erase combo, flags, arg, a count, then
// entries of a type byte and a combo.
static void skip_combo_autos(Q255Section &s)
{
    word num_autos = s.igetw();

    if(num_autos > Q255_MAXCOMBOPOOLS)
    {
        s.failed = true;
        return;
    }

    for(word a = 0; a < num_autos && !s.failed; ++a)
    {
        s.skip(1 + 4 + 4 + 1 + 1);
        int entries = (int)s.igetl();

        if(entries < 1)
            continue;

        if((size_t)entries > s.left() / 5)
        {
            s.failed = true;
            return;
        }

        s.skip((size_t)entries * 5);
    }
}

int q255_read_comboaliases(Q255Section &s)
{
    if(s.version < 2 || s.version > 5)
        return Q255_NOT_HANDLED;

    // Every alias slot is stored, used or not. Version 3 raised the count.
    const int file_aliases = (s.version >= 3) ? 8192 : 2048;

    for(int j = 0; j < file_aliases && !s.failed; ++j)
    {
        word combo = s.igetw();
        byte cset = s.getc();
        byte width = s.getc();
        byte height = s.getc();
        byte mask = s.getc();
        int count = (width + 1) * (height + 1) * (comboa_lmasktotal(mask) + 1);
        size_t cells = (size_t)count * 3;   // a word and a byte each

        // Checked first so a bad entry can't leave an alias half built.
        if(!s.has(cells))
        {
            s.failed = true;
            break;
        }

        if(!s.keep || j >= MAXCOMBOALIASES)
        {
            s.skip(cells);
            continue;
        }

        combo_alias &a = combo_aliases[j];
        delete[] a.combos;
        delete[] a.csets;

        a.combo = combo;
        a.cset = cset;
        a.width = width;
        a.height = height;
        a.layermask = mask;
        a.combos = new word[count];
        a.csets = new byte[count];

        for(int k = 0; k < count; ++k)
            a.combos[k] = s.igetw();

        for(int k = 0; k < count; ++k)
            a.csets[k] = s.getc();
    }

    if(s.version >= 4 && !s.failed)
        skip_combo_pools(s);

    if(s.version >= 5 && !s.failed)
        skip_combo_autos(s);

    return s.failed ? qe_invalid : 0;
}
