// 2.55/3.0: upstream's reworked Patra (ePatra / esPatra in upstream's guys.cpp), for every Patra of
// a 2.55/3.0 quest but the big one under qr_HARDCODED_BS_PATRA. The big one (data attribute 10 = 1)
// without that rule: 2x2 tiles, a
// 32 wide hit box at -8, drawn at -8,-8, plain 12x12 orbiters, rings of the size 90 (84 for the
// small one), and the attack attributes 18-32 (fire chance and cooldown, eye attack patterns,
// loop limits, speed-ups, ring radii, the core's shot patterns). Included once from guys.cpp,
// after esPatra; never used for a 2.53 quest. (2.53's ePatraBS stays for the hardcoded rule.)

extern dword q255_compat_rule_version;      // q255_core.cpp

// upstream's compat rules: quests saved before compat rule version 9 have qr_HARDCODED_BS_PATRA,
// qr_PATRAS_USE_HARDCODED_OFFSETS, qr_BROKEN_BIG_ENEMY_ANIMATION and qr_BROKEN_ATTRIBUTE_31_32 on
static bool q30_patra_rule(int qr)
{
    return q30_quest_rule(qr) || q255_compat_rule_version < 9;
}

static bool q30_patra_reworked(int id)
{
    // (upstream makes every Patra this one, but the big one under qr_HARDCODED_BS_PATRA)
    return q30_active() && !(guysbuf[id & 0xFFF].misc10 == 1 && q30_patra_rule(288));    // qr_HARDCODED_BS_PATRA
}

enum { q30patrat1SHOT, q30patrat1SHOTFAST, q30patrat3SHOT, q30patrat3SHOTFAST, q30patrat5SHOT, q30patrat5SHOTFAST,
       q30patrat4SHOTCARD, q30patrat4SHOTDIAG, q30patrat4SHOTRAND, q30patrat8SHOT, q30patratBREATH, q30patratSTREAM };

#define Q30_EFLAG3  0x004
#define Q30_EFLAG5  0x010
#define Q30_EFLAG6  0x020
#define Q30_EFLAG7  0x040
#define Q30_EFLAG8  0x080
#define Q30_EFLAG9  0x100
#define Q30_EFLAG10 0x200

// upstream's weapon::setAngle (without the auto-rotation)
static void q30_set_angle(weapon *w, double a)
{
    if(!w)
        return;

    w->angular = true;
    w->angle = a;

    if(a==-PI || a==PI) w->dir=left;
    else if(a==-PI/2) w->dir=up;
    else if(a==PI/2) w->dir=down;
    else if(a==0) w->dir=right;
    else if(a<-PI/2) w->dir=l_up;
    else if(a<0) w->dir=r_up;
    else if(a>(PI/2)) w->dir=l_down;
    else w->dir=r_down;
}

// a double given to upstream's zfix: cut toward zero at 1/10000 (2.53's fix rounds to 1/65536)
static inline fix q30_zf(double v)
{
    return fix((double)(long)(v * 10000) / 10000.0);
}

static inline long q30_safemod(long a, long b)
{
    return b == 0 ? 0 : a % b;
}

// an orbiter of the reworked Patra (upstream's esPatra: its own fire clocks)
class esPatra30 : public esPatra
{
public:
    int clk4, clk5;
    esPatra30(fix X, fix Y, int Id, int Clk) : esPatra(X, Y, Id, Clk), clk4(0), clk5(0) {}
};

class ePatra30 : public ePatra
{
public:
    long dmisc16, dmisc17, dmisc18, dmisc19, dmisc20, dmisc21, dmisc22, dmisc23, dmisc24, dmisc25, dmisc26,
         dmisc27, dmisc28, dmisc29, dmisc30, dmisc31, dmisc32;
    dword editorflags;
    int clk4, clk5, clk6, clk7;
    int firesfx;

    ePatra30(fix X, fix Y, int Id, int Clk);
    virtual bool animate(int index);
    virtual void draw(BITMAP *dest);
    void FirePatraWeapon();
    bool IsBigAnim() const { return anim == a4FRM8DIRB || anim == a2FRMB; }
    weapon *last_ewpn() { return Ewpns.Count() > 0 ? (weapon *)Ewpns.spr(Ewpns.Count() - 1) : NULL; }
    bool add_ewpn(int ax, int ay, int az, int adir)
    {
        const int before = Ewpns.Count();
        addEwpn(ax, ay, az, wpn, 3, wdp, adir, getUID());
        return Ewpns.Count() > before;
    }
};

ePatra30::ePatra30(fix X, fix Y, int Id, int Clk) : ePatra(X, Y, Id, Clk)
{
    const Q30GuyData *g = q30_guy_data(id & 0xFFF);
    long *const dm[17] = { &dmisc16, &dmisc17, &dmisc18, &dmisc19, &dmisc20, &dmisc21, &dmisc22, &dmisc23, &dmisc24,
                           &dmisc25, &dmisc26, &dmisc27, &dmisc28, &dmisc29, &dmisc30, &dmisc31, &dmisc32 };

    for(int q = 0; q < 17; ++q)
        *dm[q] = g ? g->attributes[15 + q] : 0;

    if(q30_patra_rule(291))     // qr_BROKEN_ATTRIBUTE_31_32
    {
        dmisc31 = dmisc32;
        dmisc32 = 0;
    }

    editorflags = g ? g->editorflags : 0;
    firesfx = g ? g->firesfx : wpnsfx(wpn);

    if(editorflags & Q30_EFLAG5)    // (upstream: placed where the screen puts it, not at 128,48)
    {
        x = X;
        y = Y;
    }

    clk4 = clk5 = clk6 = clk7 = 0;

    if(dmisc29 == 0)
        dmisc29 = !dmisc4 ? (dmisc10 ? 90 / 3 : 84 / 3) : (dmisc10 ? 90 / 2 : 84 / 2);

    if(dmisc30 == 0)
        dmisc30 = !dmisc4 ? (long)((dmisc10 ? 90 / 3 : 84 / 3) * 0.5) : (long)((dmisc10 ? 90 / 2 : 84 / 2) * 0.5);

    if(dmisc31 == 0)
        dmisc31 = !dmisc4 ? (dmisc10 ? 90 / 3 : 84 / 3) * 2 : (long)((dmisc10 ? 90 / 2 : 84 / 2) * 0.5);

    if(dmisc32 == 0)
        dmisc32 = !dmisc4 ? (dmisc10 ? 90 / 3 : 84 / 3) : (long)((dmisc10 ? 90 / 2 : 84 / 2) * 0.25);

    // upstream's ePatra::init_size_flags over the enemy's own (which the enemy constructor ran;
    // ePatra's constructor gave a size tab draw Y offset the playing field's offset once more)
    const long sf = q30_sizeflags;
    const int pfo = q30_quest_rule(319) ? playing_field_offset : original_playing_field_offset;     // qr_OLD_DRAWOFFSET

    if(!(sf & 0x001) && dmisc10 == 1) { txsz = 2; extend = 3; }
    if(!(sf & 0x002) && dmisc10 == 1) { tysz = 2; extend = 3; }
    if(!(sf & 0x004) && dmisc10 == 1) hxsz = 32;
    if(!(sf & 0x020) && dmisc10 == 1) hxofs = -8;
    if(!(sf & 0x080) && dmisc10 == 1) xofs = -8;
    if(!(sf & 0x100) && dmisc10 == 1) yofs = pfo - 8;

    if(editorflags & Q30_EFLAG8)
        misc = 1;
}

static int q30_patra_ring_dir(double ddir)
{
    if((ddir<=(((-5)*PI)/8))&&(ddir>(((-7)*PI)/8))) return l_down;
    if((ddir<=(((-3)*PI)/8))&&(ddir>(((-5)*PI)/8))) return left;
    if((ddir<=(((-1)*PI)/8))&&(ddir>(((-3)*PI)/8))) return l_up;
    if((ddir<=(((1)*PI)/8))&&(ddir>(((-1)*PI)/8))) return up;
    if((ddir<=(((3)*PI)/8))&&(ddir>(((1)*PI)/8))) return r_up;
    if((ddir<=(((5)*PI)/8))&&(ddir>(((3)*PI)/8))) return right;
    if((ddir<=(((7)*PI)/8))&&(ddir>(((5)*PI)/8))) return r_down;
    return down;
}

bool ePatra30::animate(int index)
{
    if(dying)
    {
        for(int i=index+1; i<index+flycnt+flycnt2+1; i++)
            ((enemy*)guys.spr(i))->hp = -1000;

        return Dead(index);
    }

    const bool newtiles = get_bit(quest_rules,qr_NEWENEMYTILES) != 0;
    const bool flag3 = (editorflags & Q30_EFLAG3) != 0;
    double basesize = 84;
    if(dmisc10) basesize = 90;
    double halfsize = basesize / 2;

    if(clk==0)
        removearmos(x,y);

    if((clk4 <= 0 || clk4%2) && (clk7 <= 0 || clk6 <= -16))
    {
        if(!dmisc22 || loopcnt == 0 || (dmisc22 == 1 && loopcnt < 0)) variable_walk_8(rate,homing,hrate,spw_floater);
        if(loopcnt < 0) ++clk2;
        if(++clk2>basesize)
        {
            clk2=0;
            if((!dmisc26 || (dmisc26 == 1 && flycnt) || (dmisc26 == 2 && !flycnt)) && (!(editorflags & Q30_EFLAG10) || flycnt || flycnt2))
            {
                if(loopcnt > 0)
                    --loopcnt;
                else if(loopcnt == 0)
                {
                    if(q30_safemod(misc, dmisc6) == 0)
                    {
                        if(dmisc21 > 0) loopcnt=-dmisc21;
                        else loopcnt=dmisc7;
                    }
                }
                else if(loopcnt == -1) loopcnt=dmisc7;
                else ++loopcnt;

                if(!(editorflags & Q30_EFLAG9) || loopcnt == 0) ++misc;
            }
            else
            {
                loopcnt = 0;
                misc = 1;
            }
        }
    }
    if(clk4 > 0) --clk4;

    if(clk6 < 0)
    {
        if(dmisc5 == 1 || dmisc5 == 3)
        {
            if(newtiles)
            {
                if(clk7 <= 0 || clk6 != -16) ++clk6;
                if(clk6 == 0) o_tile=d->e_tile;
                else
                {
                    if(clk6 >= -16) o_tile=d->e_tile + (IsBigAnim() ? 320 : 80);
                    else o_tile=d->e_tile + (IsBigAnim() ? 160 : 40);
                }
            }
            else clk6 = 0;
        }
    }
    else if(dmisc19) ++clk6;
    if(clk5 < 0) ++clk5;
    else if(dmisc19) ++clk5;

    if(clk7 > 0 && clk6 >= -16) --clk7;
    if(clk6 > 0) clk7 = 0;

    for(int i=index+1; i<index+flycnt+1; i++)
    {
        //outside ring
        if(!adjusted)
        {
            ((enemy*)guys.spr(i))->o_tile = newtiles ? d->e_tile+dmisc8 : o_tile+1;
            ((enemy*)guys.spr(i))->cs=dmisc9;
            ((enemy*)guys.spr(i))->hp=dmisc3;
        }

        if(((enemy*)guys.spr(i))->hp <= 0)
        {
            for(int j=i; j<index+flycnt+flycnt2; j++)
                guys.swap(j,j+1);

            if(--flycnt == 0 && dmisc23 != 0) step += fix(dmisc23 / 100.0);
        }
        else
        {
            int pos2 = ((enemy*)guys.spr(i))->misc;
            // (upstream pins each operation's rounding with volatile temps)
            volatile double a2_frac = pos2*(double)basesize/(dmisc1 == 0 ? 1 : dmisc1);
            volatile double a2_diff = clk2 - a2_frac;
            volatile double a2_scale = PI/halfsize;
            double a2 = a2_diff*a2_scale;

            if(!dmisc4) //Big Ring
            {
                if(loopcnt>0)
                {
                    volatile double base_x =  cos(a2+PI/2)*labs(dmisc31);
                    volatile double base_y = -sin(a2+PI/2)*labs(dmisc31);
                    volatile double ring_x = sin(pos2*PI*2/(dmisc1 == 0 ? 1 : dmisc1))*(double)(labs(dmisc31)-labs(dmisc29));
                    volatile double ring_y = cos(pos2*PI*2/(dmisc1 == 0 ? 1 : dmisc1))*(double)(labs(dmisc31)-labs(dmisc29));
                    guys.spr(i)->x = q30_zf(base_x - ring_x);
                    guys.spr(i)->y = q30_zf(base_y + ring_y);
                }
                else
                {
                    guys.spr(i)->x = q30_zf( cos(a2+PI/2)*labs(dmisc29));
                    guys.spr(i)->y = q30_zf(-sin(a2+PI/2)*labs(dmisc29));
                }

                temp_x=guys.spr(i)->x;
                temp_y=guys.spr(i)->y;
            }
            else //Oval
            {
                circle_x =  cos(a2+PI/2)*labs(dmisc29);
                circle_y = -sin(a2+PI/2)*labs(dmisc29);

                if(loopcnt>0)
                {
                    guys.spr(i)->x = q30_zf( cos(a2+PI/2)*labs(dmisc29));
                    guys.spr(i)->y = q30_zf((-sin(a2+PI/2)-cos(pos2*PI*2/(dmisc1 == 0 ? 1 : dmisc1)))*labs(dmisc31));
                }
                else
                {
                    guys.spr(i)->x = q30_zf(circle_x);
                    guys.spr(i)->y = q30_zf(circle_y);
                }

                temp_x=circle_x;
                temp_y=circle_y;
            }

            guys.spr(i)->dir = q30_patra_ring_dir(atan2(double(temp_y),double(temp_x)));
            guys.spr(i)->x += x;
            guys.spr(i)->y += y;
        }
    }

    if((wpn>wEnemyWeapons || (wpn >= wScript1 && wpn <= wScript10)) && (dmisc5==1 || dmisc5== 3) && (!dmisc25 || (dmisc25 == 1 && !flycnt && !flycnt2) || (dmisc25 == 2 && (flycnt || flycnt2)) || (dmisc25 == 3 && flycnt2 && !flycnt)))
    {
        int timeneeded = 48;
        int patbreath = (rand()%50+50);
        if((patbreath % 4) == 0) ++patbreath;
        if(dmisc28 == q30patratBREATH)
            timeneeded = 48 + patbreath;
        if(dmisc28 == q30patratSTREAM)
            timeneeded = 48 + 96;
        if(((((dmisc18 > 0 || ((editorflags & Q30_EFLAG10) && !flycnt && !flycnt2)) && !(rand() % zc_max(dmisc18, 1L))) || //New 1/N chance
            (dmisc18 == 0 && !(rand()&127)) //Old hardcoded firing chance
            || (dmisc18 == -1 && loopcnt > 0 && ((clk2 == (int)floor(halfsize + 0.5) && (!flag3 || !newtiles))
            || (clk4 == 10 && flag3 && newtiles))))
            && (clk6 >= 0) //if not in the middle of firing...
            && clk6 >= dmisc19) //if over the set cooldown between shots...
            && ((!(editorflags & Q30_EFLAG7) || (loopcnt == 0 && (basesize*((double)dmisc6 - q30_safemod(misc, dmisc6))) > timeneeded)) || dmisc18 == -1))
        {
            switch(dmisc28)
            {
            case q30patratSTREAM:
                clk7 = 97;
                clk6 = (flag3 && newtiles) ? -48 : 0;
                break;

            case q30patratBREATH:
                clk7 = patbreath;
                clk6 = (flag3 && newtiles) ? -48 : 0;
                break;

            default:
                if(flag3 && newtiles)
                {
                    clk6 = -48;
                    if(editorflags & Q30_EFLAG6) clk4 = abs(clk6) + 16;
                }
                else
                {
                    clk6 = 0;
                    if(editorflags & Q30_EFLAG6) clk4 = 16;
                    FirePatraWeapon();
                }
                break;
            }
        }
        if(clk6 < 0)
        {
            switch(dmisc28)
            {
            case q30patratSTREAM:
                if(clk7 > 0 && (clk7 % 12) == 0) FirePatraWeapon();
                if(editorflags & Q30_EFLAG6) clk4 = abs(clk6) + 16;
                break;

            case q30patratBREATH:
                if(clk7 > 0 && (clk7 % 4) == 0) FirePatraWeapon();
                if(editorflags & Q30_EFLAG6) clk4 = abs(clk6) + 16;
                break;

            default:
                if(flag3 && newtiles && clk6 == -16)
                {
                    FirePatraWeapon();
                    if(editorflags & Q30_EFLAG6) clk4 = abs(clk6) + 16;
                }
                break;
            }
        }
    }

    int randattempts = 0;
    int randeye = 0;
    if(flycnt2 > 0)
    {
        do
        {
            randeye = ((flycnt2 > 0) ? (rand() % zc_max(1, flycnt2)) : 0);
            randeye += (index + flycnt + 1);
            ++randattempts;
        } while(((esPatra30*)guys.spr(randeye))->clk5 < 0 && randattempts < 10);
    }
    bool dofire = false;
    if(dmisc20)
    {
        const long remain = dmisc6 - q30_safemod(misc, dmisc6);
        if((dmisc18 > 0 && !(rand() % zc_max(dmisc18, 1L))) ||
           (dmisc18 == 0 && !(rand()&127)) ||
           (dmisc18 == -1 && (loopcnt > 0 || dmisc20 == 4) && ((clk2 == (int)floor(halfsize + 0.5) && (!flag3 || !newtiles) && dmisc20 != 2 && dmisc20 != 4)
           || (clk2 == 10 && dmisc20 != 4 && ((flag3 && newtiles) || dmisc20 == 2))
           || (((((q30_safemod(misc, dmisc6) == 0 && (loopcnt == 0 && !dmisc21)) || loopcnt > 1 || loopcnt == -1) && clk2 <= 53 && clk2 >= 51 && flag3) || (!flag3 && loopcnt > 0 && clk2 == 1)) && dmisc20 == 4))))
        {
            if(clk5 >= 0 || !flag3 || !newtiles)
            {
                if(clk5 >= dmisc19)
                {
                    if((!(editorflags & Q30_EFLAG7) || (loopcnt == 0 &&
                        ((dmisc20 == 2 && (basesize*(double)remain) > (48.0 + 12.0*flycnt2)) ||
                         (dmisc20 == 4 && (basesize*(double)remain) > (48.0 + 96)) ||
                         (dmisc20 != 2 && dmisc20 != 4 && (basesize*(double)remain) > 48))))
                       || dmisc18 == -1)
                        dofire = true;
                }
            }
        }
    }
    if(flycnt2)
    {
        for(int i=index+flycnt+1; i<index+flycnt+flycnt2+1; i++)//inner ring
        {
            esPatra30 *s = (esPatra30*)guys.spr(i);

            if(!adjusted)
            {
                s->hp=12*zc_hero_dmgmult();

                if(newtiles)
                {
                    if(q30_patra_rule(289))     // qr_PATRAS_USE_HARDCODED_OFFSETS
                        s->o_tile = (dmisc5 == 1 || dmisc5 == 3) ? d->e_tile+120 : d->e_tile+40;
                    else s->o_tile = d->s_tile;
                }
                else
                    s->o_tile=o_tile+1;

                s->cs=dmisc9;
                if(dmisc27) s->hp=dmisc27;
            }

            s->superman = flycnt>0;

            if(s->hp <= 0)
            {
                for(int j=i; j<index+flycnt+flycnt2; j++)
                    guys.swap(j,j+1);

                if(--flycnt2 == 0 && dmisc24 != 0) step += fix(dmisc24 / 100.0);
            }
            else
            {
                int pos2 = s->misc;
                volatile double a2_frac = pos2*basesize/(dmisc2==0 ? 1 : dmisc2);
                volatile double a2_diff = clk2 - a2_frac;
                volatile double a2_scale = PI/halfsize;
                double a2 = a2_diff*a2_scale;

                if(dmisc4==0)
                {
                    if(loopcnt>0)
                    {
                        volatile double base_x =  cos(a2+PI/2)*labs(dmisc32);
                        volatile double base_y = -sin(a2+PI/2)*labs(dmisc32);
                        volatile double ring_x = sin(pos2*PI*2/(dmisc2==0?1:dmisc2))*(double)(labs(dmisc32)-labs(dmisc30));
                        volatile double ring_y = cos(pos2*PI*2/(dmisc2==0?1:dmisc2))*(double)(labs(dmisc32)-labs(dmisc30));
                        s->x = q30_zf(base_x - ring_x);
                        s->y = q30_zf(base_y + ring_y);
                    }
                    else
                    {
                        s->x = q30_zf( cos(a2+PI/2)*labs(dmisc30));
                        s->y = q30_zf(-sin(a2+PI/2)*labs(dmisc30));
                    }

                    temp_x=s->x;
                    temp_y=s->y;
                }
                else
                {
                    circle_x =  cos(a2+PI/2)*labs(dmisc30);
                    circle_y = -sin(a2+PI/2)*labs(dmisc30);

                    if(loopcnt>0)
                    {
                        s->x = q30_zf( cos(a2+PI/2)*labs(dmisc30));
                        s->y = q30_zf((-sin(a2+PI/2)-cos(pos2*PI*2/(dmisc2 == 0 ? 1 : dmisc2)))*labs(dmisc32));
                    }
                    else
                    {
                        s->x = q30_zf(circle_x);
                        s->y = q30_zf(circle_y);
                    }

                    temp_x=circle_x;
                    temp_y=circle_y;
                }

                s->dir = q30_patra_ring_dir(atan2(double(temp_y),double(temp_x)));
                s->x += x;
                s->y = y-s->y;

                if((wpn>wEnemyWeapons || (wpn >= wScript1 && wpn <= wScript10)) && (dmisc5==2 || dmisc5== 3))
                {
                    if(s->clk5 < 0 && flag3)
                    {
                        if(s->clk4 <= 0 || s->clk5 != -16) ++s->clk5;
                        if(q30_patra_rule(289))
                        {
                            const int b = dmisc5 == 3 ? 120 : 40;
                            if(s->clk5 >= 0) s->o_tile=d->e_tile+b;
                            else if(s->clk5 >= -16) s->o_tile=d->e_tile+b+80;
                            else if(s->clk5 >= -48) s->o_tile=d->e_tile+b+40;
                            else s->o_tile=d->e_tile+b;
                        }
                        else
                        {
                            if(s->clk5 >= 0) s->o_tile=d->s_tile;
                            else if(s->clk5 >= -16) s->o_tile=d->s_tile+80;
                            else if(s->clk5 >= -48) s->o_tile=d->s_tile+40;
                            else s->o_tile=d->s_tile;
                        }
                    }
                    else if((dmisc19 || s->clk5) && (s->clk4 <= 0 || s->clk5 != -16)) ++s->clk5;
                    if(s->clk4 > 0) --s->clk4;
                    if(!dmisc25 || (dmisc25 == 1 && !s->superman) || ((dmisc25 == 2 || dmisc25 == 3) && s->superman))
                    {
                        const int sx = int(s->x), sy = int(s->y), sz = int(s->z);

                        switch(dmisc20) //Patra Attack Patterns
                        {
                        case 4: //Single one rapidfires
                            if(dofire && i == randeye)
                            {
                                s->clk5 = -16;
                                if(flag3 && newtiles) s->clk5 = -48;
                                s->clk4 = 96;
                                clk5 = -3;
                                if(editorflags & Q30_EFLAG6) clk4 = abs(clk5) + 16;
                            }
                            if(s->clk5 == -16 && (s->clk4 % 12) == 0)
                            {
                                add_ewpn(sx,sy,sz,dir);
                                sfx(firesfx,pan(int(x)));
                            }
                            break;

                        case 3: //Ring
                            if(dofire)
                            {
                                if(flag3 && newtiles)
                                {
                                    s->clk5 = -48;
                                    clk5 = -48;
                                    if(editorflags & Q30_EFLAG6) clk4 = 64;
                                }
                                else
                                {
                                    if(add_ewpn(sx,sy,sz,dir))
                                        q30_set_angle(last_ewpn(), atan2(double(LinkY()-y),double(LinkX()-x)));
                                    sfx(firesfx,pan(int(x)));
                                    s->clk5 = 0;
                                    clk5 = 0;
                                    if(editorflags & Q30_EFLAG6) clk4 = 16;
                                }
                            }
                            if(s->clk5 == -16)
                            {
                                if(add_ewpn(sx,sy,sz,dir))
                                    q30_set_angle(last_ewpn(), atan2(double(LinkY()-y),double(LinkX()-x)));
                                sfx(firesfx,pan(int(x)));
                            }
                            break;

                        case 2: //one after another
                            if(dofire)
                            {
                                s->clk5 = -48 - (12*(i-(index+flycnt+1)));
                                clk5 = -48 - (12*flycnt2);
                                if(editorflags & Q30_EFLAG6) clk4 = abs(clk5) + 16;
                            }
                            if(s->clk5 == -16)
                            {
                                add_ewpn(sx,sy,sz,dir);
                                sfx(firesfx,pan(int(x)));
                            }
                            break;

                        case 1: //random one eye
                            if(dofire && i == randeye)
                            {
                                if(flag3 && newtiles)
                                {
                                    s->clk5 = -48;
                                    clk5 = -48;
                                    if(editorflags & Q30_EFLAG6) clk4 = 64;
                                }
                                else
                                {
                                    add_ewpn(sx,sy,sz,dir);
                                    sfx(firesfx,pan(int(x)));
                                    s->clk5 = 0;
                                    clk5 = 0;
                                    if(editorflags & Q30_EFLAG6) clk4 = 16;
                                }
                            }
                            if(flag3 && newtiles && s->clk5 == -16)
                            {
                                add_ewpn(sx,sy,sz,dir);
                                sfx(firesfx,pan(int(x)));
                            }
                            break;

                        default: //old behavior, all eyes can fire any time
                            if((((dmisc18 && !(rand() % zc_max(dmisc18, 1L))) ||
                                (!dmisc18 && !(rand()&127))) && (s->clk5 >= 0 || !flag3 || !newtiles)
                                && s->clk5 >= dmisc19) && (!(editorflags & Q30_EFLAG7) || (loopcnt == 0 &&
                                (dmisc20 != 2 && (basesize*((double)dmisc6 - q30_safemod(misc, dmisc6))) > 48))))
                            {
                                if(flag3 && newtiles)
                                {
                                    s->clk5 = -48;
                                    if(editorflags & Q30_EFLAG6) clk4 = 64;
                                }
                                else
                                {
                                    add_ewpn(sx,sy,sz,dir);
                                    sfx(firesfx,pan(int(x)));
                                    s->clk5 = 0;
                                    if(editorflags & Q30_EFLAG6) clk4 = 16;
                                }
                            }
                            if(flag3 && newtiles && s->clk5 == -16)
                            {
                                add_ewpn(sx,sy,sz,dir);
                                sfx(firesfx,pan(int(x)));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    adjusted=true;
    return enemy::animate(index);
}

void ePatra30::FirePatraWeapon()
{
    int xoff = 0;
    int yoff = 0;
    const Q30GuyData *g = q30_guy_data(id & 0xFFF);
    const long wov = g ? g->weap.override_flags : 0;

    if(q30_sizeflags & 0x004)
        xoff += hxofs + (hxsz / 2) - ((wov & 0x004) ? (g->weap.tilew * 8) : 8);

    if(q30_sizeflags & 0x008)
        yoff += hyofs + (hysz / 2) - ((wov & 0x008) ? (g->weap.tileh * 8) : 8);

    sfx(firesfx,pan(int(x)));
    const int wx = int(x) + xoff, wy = int(y) + yoff, wz = int(z);

    switch(dmisc28)
    {
    case q30patrat8SHOT:
    case q30patrat4SHOTDIAG:
    case q30patrat4SHOTRAND:
        if(dmisc28 != q30patrat4SHOTRAND || (rand()%2))
        {
            const int dd[4] = { l_up, l_down, r_up, r_down };

            for(int q = 0; q < 4; ++q)
            {
                Ewpns.add(new weapon((fix)wx,(fix)wy,(fix)wz,wpn,1,wdp,dd[q],-1,getUID()));
                if(wpn != ewFlame && wpn != ewFlame2) last_ewpn()->step *= .707;
            }

            if(dmisc28 == q30patrat4SHOTDIAG || dmisc28 == q30patrat4SHOTRAND) break;
        }
        // fall through
    case q30patrat4SHOTCARD:
    {
        const int dd[4] = { up, down, left, right };

        for(int q = 0; q < 4; ++q)
            Ewpns.add(new weapon((fix)wx,(fix)wy,(fix)wz,wpn,1,wdp,dd[q],-1,getUID()));

        break;
    }

    default:
    {
        if(!add_ewpn(int(x),int(y),int(z),dir))
            break;

        weapon *w = last_ewpn();
        if(dmisc28 == q30patratBREATH) w->angle += ((rand()%41 - 20)/100.0)*PI;
        double anglestore = w->angle;
        const bool fast = dmisc28 == q30patrat1SHOTFAST || dmisc28 == q30patrat3SHOTFAST || dmisc28 == q30patrat5SHOTFAST;
        if(fast) w->step *= 2;

        if(dmisc28 == q30patrat3SHOT || dmisc28 == q30patrat3SHOTFAST || dmisc28 == q30patrat5SHOT || dmisc28 == q30patrat5SHOTFAST)
        {
            const bool fast2 = dmisc28 == q30patrat3SHOTFAST || dmisc28 == q30patrat5SHOTFAST;
            const double spread[2] = { 0.46364761, 0.78539816 }, add[2] = { 0.1180, 0.4142 };
            const int pairs = (dmisc28 == q30patrat5SHOT || dmisc28 == q30patrat5SHOTFAST) ? 2 : 1;

            for(int p = 0; p < pairs; ++p)
                for(int sgn = 1; sgn >= -1; sgn -= 2)
                {
                    if(!add_ewpn(int(x),int(y),int(z),dir))
                        continue;

                    weapon *e = last_ewpn();
                    e->angle = anglestore + sgn * spread[p];
                    e->step += add[p];
                    if(p == 0 ? fast2 : dmisc28 == q30patrat5SHOTFAST) e->step *= 2;
                }
        }
        break;
    }
    }

    sfx(firesfx,pan(int(x)));
}

void ePatra30::draw(BITMAP *dest)
{
    tile=o_tile;
    update_enemy_frame();
    // (upstream: the body drawn as if facing up, so it stays centered in its ring)
    int real_dir = dir;
    dir = up;
    enemy::draw(dest);
    dir = real_dir;
}
