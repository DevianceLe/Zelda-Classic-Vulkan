// combodata->ZHeight, ZStepHeight and GenFlags[] read and written, and Link walking onto combos
// whose height a script changed.
// frames: 700
// snap: 650
#include "std.zh"

const int FLOOR = 60000;	// combos the test makes its own
const int B1 = 60001;
const int B2 = 60002;
const int B3 = 60003;

int lastx = -1000, lasty, lastz, laststand, lastoffs;
int phase = 0;

void report(int f)
{
	int st = Hero->Standing ? 1 : 0;
	if(Hero->X == lastx && Hero->Y == lasty && Hero->Z == lastz && st == laststand && Hero->DrawYOffset == lastoffs)
		return;
	lastx = Hero->X; lasty = Hero->Y; lastz = Hero->Z; laststand = st; lastoffs = Hero->DrawYOffset;
	printf("TH: p%d f%d hero %d,%d z %d standing %d yoffs %d action %d\n", phase, f, Hero->X, Hero->Y, Hero->Z, st, Hero->DrawYOffset, Hero->Action);
}

void block(int id, int look, int zh, int zstep)
{
	combodata from = Game->LoadComboData(look);
	combodata c = Game->LoadComboData(id);
	c->OriginalTile = from->OriginalTile;
	c->Tile = from->Tile;
	c->CSet = from->CSet;
	c->Type = 0;
	c->Flag = 0;
	c->Walk = 0xFF;		// solid, with its effect on, in all four quarters
	c->GenFlags[GENFLAG_WALK_ON_TOP] = true;
	c->ZHeight = zh;
	c->ZStepHeight = zstep;
}

void show(char32[] what, combodata c, int pos)
{
	printf("TH: %s: type %d flag %d walk %d effect %d | screen T %d I %d S %d E %d solid %d\n", what, c->Type, c->Flag, c->Walk, c->Effect,
		Screen->ComboT[pos], Screen->ComboI[pos], Screen->ComboS[pos], Screen->ComboE[pos], Screen->isSolid(ComboX(pos) + 4, ComboY(pos) + 4) ? 1 : 0);
}

void walk(int dir, int frames, int f0)
{
	for(int i = 0; i < frames; ++i)
	{
		Input->Button[dir] = true;
		report(f0 + i);
		Waitframe();
	}
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		// the registers themselves
		combodata c = Game->LoadComboData(B1);
		printf("TH: fresh combo: z %d step %d dive %d walkontop %d offset %d\n", c->ZHeight, c->ZStepHeight, c->DiveUnderLevel,
			c->GenFlags[GENFLAG_WALK_ON_TOP] ? 1 : 0, c->GenFlags[GENFLAG_WALK_ON_TOP_DRAW_OFFSET] ? 1 : 0);
		c->ZHeight = 12;
		c->ZStepHeight = 4;
		c->DiveUnderLevel = 3;
		printf("TH: written: z %d step %d dive %d\n", c->ZHeight, c->ZStepHeight, c->DiveUnderLevel);
		c->ZHeight = 2.5;
		c->ZStepHeight = 0.0001;
		printf("TH: fractions: z %d step %d\n", c->ZHeight, c->ZStepHeight);
		c->ZHeight = -3;
		c->ZStepHeight = -3;
		c->DiveUnderLevel = -3;
		printf("TH: negative: z %d step %d dive %d\n", c->ZHeight, c->ZStepHeight, c->DiveUnderLevel);
		c->ZHeight = 214748.3647;
		c->ZStepHeight = 214748.3647;
		c->DiveUnderLevel = 300;
		printf("TH: largest: z %d step %d dive %d\n", c->ZHeight, c->ZStepHeight, c->DiveUnderLevel);
		c->DiveUnderLevel = 2.9;
		printf("TH: dive 2.9 -> %d\n", c->DiveUnderLevel);
		c->DiveUnderLevel = 0;
		for(int i = 0; i < GENFLAG_MAX; ++i)
		{
			c->GenFlags[i] = true;
			printf("TH: genflag %d set: %d %d %d %d\n", i, c->GenFlags[0] ? 1 : 0, c->GenFlags[1] ? 1 : 0, c->GenFlags[2] ? 1 : 0, c->GenFlags[3] ? 1 : 0);
		}
		for(int i = 0; i < GENFLAG_MAX; ++i)
			c->GenFlags[i] = false;
		printf("TH: genflags cleared: %d %d %d %d\n", c->GenFlags[0] ? 1 : 0, c->GenFlags[1] ? 1 : 0, c->GenFlags[2] ? 1 : 0, c->GenFlags[3] ? 1 : 0);
		combodata other = Game->LoadComboData(B2);
		printf("TH: another combo is untouched: z %d step %d\n", other->ZHeight, other->ZStepHeight);

		// the scene: a floor everywhere, three blocks in a row to Link's right
		int look = Screen->ComboD[ComboAt(Hero->X + 8, Hero->Y + 8)];
		combodata fl = Game->LoadComboData(FLOOR);
		combodata from = Game->LoadComboData(look);
		fl->OriginalTile = from->OriginalTile;
		fl->Tile = from->Tile;
		fl->CSet = from->CSet;
		fl->Walk = 0;
		fl->Type = 0;
		for(int l = 1; l <= 6; ++l)
		{
			mapdata m = Game->LoadTempScreen(l);
			if(Screen->LayerMap[l] > 0)
				for(int pos = 0; pos < 176; ++pos)
					m->ComboD[pos] = 0;
		}
		for(int pos = 0; pos < 176; ++pos)
		{
			Screen->ComboD[pos] = FLOOR;
			Screen->ComboF[pos] = 0;
		}
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		block(B1, look, 8, 0);
		block(B2, look, 16, 8);
		block(B3, look, 0, 0);
		Screen->ComboD[ComboAt(128, 80)] = B1;
		Screen->ComboD[ComboAt(144, 80)] = B2;
		Screen->ComboD[ComboAt(160, 80)] = B3;
		// type, flag and solid squares written through the combo's data and through the screen,
		// read back both ways (B1 has had its 3.0 fields written, the floor has not)
		int p1 = ComboAt(128, 80);
		int p0 = ComboAt(32, 32);
		combodata b1 = Game->LoadComboData(B1);
		show("block", b1, p1);
		show("floor", fl, p0);
		b1->Walk = 0; show("block walk 0", b1, p1);
		b1->Walk = 5; show("block walk 5", b1, p1);
		b1->Effect = 0; show("block effect 0", b1, p1);
		b1->Effect = 15; b1->Walk = 15; show("block walk 15 effect 15", b1, p1);
		Screen->ComboS[p1] = 3; show("block ComboS 3", b1, p1);
		Screen->ComboS[p1] = 15;
		b1->Type = CT_WATER; show("block type water", b1, p1);
		b1->Type = CT_SCRIPT6; show("block type script6", b1, p1);
		b1->Type = CT_SWITCHBLOCK; show("block type switchblock", b1, p1);
		b1->Type = -1; show("block type -1", b1, p1);
		Screen->ComboT[p1] = CT_STAIR; show("block ComboT stair", b1, p1);
		Screen->ComboT[p1] = CT_BRIDGE; show("block ComboT bridge", b1, p1);
		b1->Type = 0;
		b1->Flag = CF_SCRIPT1; show("block flag script1", b1, p1);
		b1->Flag = CF_PUSHED; show("block flag pushed", b1, p1);
		b1->Flag = CF_SCRIPT6; show("block flag script6", b1, p1);
		b1->Flag = 200; show("block flag 200", b1, p1);
		Screen->ComboI[p1] = 5; show("block ComboI 5", b1, p1);
		b1->Flag = 0;
		fl->Type = CT_SCRIPT6; show("floor type script6", fl, p0);
		fl->Type = CT_WATER; show("floor type water", fl, p0);
		fl->Type = 0;
		fl->Flag = CF_SCRIPT6; show("floor flag script6", fl, p0);
		fl->Flag = 0;
		fl->Walk = 15; show("floor walk 15", fl, p0);
		fl->Walk = 0; show("floor walk 0", fl, p0);

		Hero->X = 96;
		Hero->Y = 80;
		Hero->Dir = DIR_RIGHT;
		Waitframe();

		phase = 1;		// B1 is 8 high with no step: a wall
		walk(CB_RIGHT, 40, 0);
		phase = 2;		// a step of 8: he walks up, and on to B2 (16 high, step 8); B3 has no height: a wall from there
		Game->LoadComboData(B1)->ZStepHeight = 8;
		walk(CB_RIGHT, 60, 100);
		phase = 3;		// B3 20 high with a step of 4: reachable from 16
		Game->LoadComboData(B3)->ZHeight = 20;
		Game->LoadComboData(B3)->ZStepHeight = 4;
		walk(CB_RIGHT, 30, 200);
		phase = 4;		// off its far side to the floor, and back: B3 is a wall from the floor
		walk(CB_RIGHT, 30, 300);
		walk(CB_LEFT, 30, 330);
		phase = 5;		// a jump high enough gets on it (20 - 4 = 16)
		Hero->Jump = 4;
		walk(CB_LEFT, 60, 400);
		phase = 6;		// standing on B3 while its height changes
		Game->LoadComboData(B3)->ZHeight = 3;
		walk(CB_UP, 3, 500);
		Game->LoadComboData(B3)->ZHeight = 60;
		walk(CB_DOWN, 3, 510);
		phase = 7;		// from 60 up, everything to the left is below
		walk(CB_LEFT, 40, 520);
		phase = 8;		// the 8 pixel draw offset, on B1
		Game->LoadComboData(B1)->GenFlags[GENFLAG_WALK_ON_TOP_DRAW_OFFSET] = true;
		Hero->X = 96;
		Hero->Y = 80;
		Waitframe();
		walk(CB_RIGHT, 30, 600);
		phase = 9;		// no longer walk-on-top while he stands on it
		Game->LoadComboData(B1)->GenFlags[GENFLAG_WALK_ON_TOP] = false;
		walk(CB_UP, 3, 650);
		Game->LoadComboData(B1)->GenFlags[GENFLAG_WALK_ON_TOP] = true;
		walk(CB_UP, 3, 660);
		report(699);
		printf("TH: DONE\n");
		// hold still for the picture
		while(true)
		{
			Hero->X = 128;
			Hero->Y = 80;
			Hero->Dir = DIR_DOWN;
			Waitframe();
		}
	}
}
