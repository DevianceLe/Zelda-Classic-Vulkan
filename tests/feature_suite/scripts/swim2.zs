// Top-down swimming and wading under the quest's movement rules: Link walks into a pool (walkable
// water), swims across it, turns back and swims to the shore he came from; then he walks through
// a strip of shallow water. Every change of his position or action is printed.
// frames: 400
#include "std.zh"

const int WATER = 60012;	// combos the test makes its own
const int GROUND = 60011;
const int SHALLOW = 60015;

int phase = 0;
int lx = -1000, ly, la;

void report(int f)
{
	if(Hero->X == lx && Hero->Y == ly && Hero->Action == la)
		return;
	lx = Hero->X; ly = Hero->Y; la = Hero->Action;
	printf("TS: p%d f%d hero %d,%d action %d\n", phase, f, Hero->X, Hero->Y, Hero->Action);
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

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		int look = Screen->ComboD[ComboAt(Hero->X + 8, Hero->Y + 8)];
		make(WATER, look, 0, CT_WATER);
		make(GROUND, look, 0, CT_NONE);
		make(SHALLOW, look, 0, CT_SHALLOWWATER);
		for(int l = 1; l <= 6; ++l)
		{
			mapdata m = Game->LoadTempScreen(l);
			if(Screen->LayerMap[l] > 0)
				for(int pos = 0; pos < 176; ++pos)
					m->ComboD[pos] = 0;
		}
		for(int pos = 0; pos < 176; ++pos)
		{
			Screen->ComboD[pos] = GROUND;
			Screen->ComboF[pos] = 0;
		}
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		for(int col = 3; col < 12; ++col)	// the pool: rows 1-6, columns 3-11
			for(int row = 1; row < 7; ++row)
				Screen->ComboD[row * 16 + col] = WATER;
		for(int col = 5; col < 12; ++col)	// the shallow strip: row 9, columns 5-11
			Screen->ComboD[9 * 16 + col] = SHALLOW;
		Screen->Flag[SFL_SIDEVIEW] = false;
		Hero->Item[I_FLIPPERS] = true;
		printf("TS: step %d, rules drown %d no hopping %d newer movement %d\n", Hero->Step, Game->FFRules[qr_DROWN] ? 1 : 0,
			Game->FFRules[qr_NO_HOPPING] ? 1 : 0, Game->FFRules[qr_NEW_HERO_MOVEMENT2] ? 1 : 0);
		Hero->X = 16;
		Hero->Y = 48;
		Hero->Jump = 0;
		Hero->Dir = DIR_RIGHT;
		hold(-1, 10, 0);

		phase = 1;		// into the pool and across it
		hold(CB_RIGHT, 90, 10);
		phase = 2;		// back to the shore
		hold(CB_LEFT, 90, 100);
		hold(-1, 30, 190);
		phase = 3;		// through the shallow strip
		Hero->X = 32;
		Hero->Y = 144;
		Hero->Dir = DIR_RIGHT;
		hold(-1, 20, 220);
		hold(CB_RIGHT, 150, 240);
		report(399);
		printf("TS: DONE\n");
		while(true)
			Waitframe();
	}
}
