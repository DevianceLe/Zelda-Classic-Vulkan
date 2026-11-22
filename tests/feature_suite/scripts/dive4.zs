// Diving under a solid combo with a "dive under" level (combodata->DiveUnderLevel): a top-down
// pool with a wall of solid water in it; swimming into it stops at it, diving goes under it while the
// flippers' level is high enough, and the dive lasts while Link is under it.
// frames: 760
#include "std.zh"

const int WATER = 60012;	// combos the test makes its own
const int ROCK = 60013;
const int GROUND = 60011;

int phase = 0;
int lx = -1000, ly, la, ls;

void report(int f)
{
	int s = Hero->Standing ? 1 : 0;
	if(Hero->X == lx && Hero->Y == ly && Hero->Action == la && s == ls)
		return;
	lx = Hero->X; ly = Hero->Y; la = Hero->Action; ls = s;
	printf("TD: p%d f%d hero %d,%d action %d standing %d\n", phase, f, Hero->X, Hero->Y, Hero->Action, s);
}

void hold(int btn, int frames, int f0)
{
	for(int i = 0; i < frames; ++i)
	{
		if(btn >= 0) Input->Button[btn] = true;
		report(f0 + i);
		Waitframe();
	}
}

void dive_and_hold(int btn, int frames, int f0)
{
	Input->Press[CB_A] = true;
	Input->Button[CB_A] = true;
	hold(btn, frames, f0);
}

void make(int id, int look, int walk, int type)
{
	combodata from = Game->LoadComboData(look);
	combodata c = Game->LoadComboData(id);
	c->OriginalTile = from->OriginalTile;
	c->Tile = from->Tile;
	c->CSet = from->CSet;
	c->Flag = 0;
	c->Type = type;
	c->Walk = walk;
}

void place(int x, int y)
{
	Hero->X = x;
	Hero->Y = y;
	Hero->Jump = 0;
	Hero->Dir = DIR_RIGHT;
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		int look = Screen->ComboD[ComboAt(Hero->X + 8, Hero->Y + 8)];
		make(WATER, look, 0, CT_WATER);
		make(ROCK, look, 15, CT_WATER);	// solid water: swum into, it stops him
		make(GROUND, look, 0, CT_NONE);
		Game->LoadComboData(ROCK)->DiveUnderLevel = 1;
		for(int l = 1; l <= 6; ++l)
		{
			mapdata m = Game->LoadTempScreen(l);
			if(Screen->LayerMap[l] > 0)
				for(int pos = 0; pos < 176; ++pos)
					m->ComboD[pos] = 0;
		}
		for(int pos = 0; pos < 176; ++pos)
		{
			Screen->ComboD[pos] = WATER;
			Screen->ComboF[pos] = 0;
		}
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		for(int col = 0; col < 3; ++col)
			for(int row = 0; row < 11; ++row)
				Screen->ComboD[row * 16 + col] = GROUND;
		for(int row = 2; row < 9; ++row)
			Screen->ComboD[row * 16 + 8] = ROCK;
		Screen->Flag[SFL_SIDEVIEW] = false;
		Hero->Item[I_FLIPPERS] = true;
		itemdata fl = Game->LoadItemData(I_FLIPPERS);
		printf("TD: flippers level %d dive %d cooldown %d button %d flags %d %d, dive under %d\n", fl->Level, fl->Attributes[0], fl->Attributes[1],
			fl->Attributes[2], fl->Flags[0] ? 1 : 0, fl->Flags[1] ? 1 : 0, Game->LoadComboData(ROCK)->DiveUnderLevel);
		// (upstream dives on the flippers' button, for their length; this engine on A, 50 + 30)
		fl->Attributes[0] = 50;
		fl->Attributes[1] = 30;
		fl->Attributes[2] = 1;
		fl->Flags[0] = false;
		fl->Flags[1] = false;
		place(16, 80);
		hold(-1, 10, 0);

		phase = 2;		// dive and stop half over the rock (its centre off it), then wait for the dive to end
		place(96, 80);
		hold(-1, 30, 100);
		dive_and_hold(CB_RIGHT, 22, 130);
		hold(-1, 110, 152);
		phase = 3;		// dive, stop with the centre over the rock, then 4 pixels back off it, and wait
		place(96, 80);
		hold(-1, 30, 300);
		dive_and_hold(CB_RIGHT, 25, 330);
		hold(-1, 60, 355);
		hold(CB_LEFT, 4, 415);
		hold(-1, 120, 419);
		report(759);
		printf("TD: DONE\n");
		while(true)
			Waitframe();
	}
}
