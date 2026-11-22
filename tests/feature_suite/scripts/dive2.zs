// Which button starts a dive and how long it lasts: the flippers' attributes (1 dive length,
// 2 the wait before the next, 3 the button bits) and flag 2 (the button ends a dive early), as
// upstream's tick_diving. Only Link's action is reported (24 diving), not where he swims.
// frames: 760
#include "std.zh"

const int WATER = 60012;	// a combo the test makes its own

int phase = 0;
int la = -1;

void report(int f)
{
	if(Hero->Action == la)
		return;
	la = Hero->Action;
	printf("TV: p%d f%d action %d\n", phase, f, la);
}

void hold(int frames, int f0)
{
	for(int i = 0; i < frames; ++i)
	{
		report(f0 + i);
		Waitframe();
	}
}

void press(int btn, int f0)
{
	Input->Press[btn] = true;
	Input->Button[btn] = true;
	report(f0);
	Waitframe();
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
		Screen->Flag[SFL_SIDEVIEW] = false;
		Hero->Item[I_FLIPPERS] = true;
		itemdata fl = Game->LoadItemData(I_FLIPPERS);
		printf("TV: as the quest has them: dive %d wait %d button %d flags %d %d\n", fl->Attributes[0], fl->Attributes[1], fl->Attributes[2],
			fl->Flags[0] ? 1 : 0, fl->Flags[1] ? 1 : 0);
		fl->Attributes[0] = 20;
		fl->Attributes[1] = 45;
		fl->Attributes[2] = 16;		// Ex1 only
		fl->Flags[0] = false;
		fl->Flags[1] = false;
		Hero->X = 96;
		Hero->Y = 80;
		hold(40, 0);

		phase = 1;		// A doesn't dive now
		press(CB_A, 40);
		hold(40, 41);
		phase = 2;		// Ex1 does: 20 frames under, then 45 before the next
		press(CB_EX1, 100);
		hold(30, 101);
		phase = 3;		// Ex1 again while waiting: nothing
		press(CB_EX1, 131);
		hold(60, 132);
		phase = 4;		// flag 2: Ex1 ends a dive early
		fl->Flags[1] = true;
		fl->Attributes[0] = 60;
		press(CB_EX1, 200);
		hold(10, 201);
		press(CB_EX1, 211);
		hold(80, 212);
		phase = 5;		// a long dive and a long wait, on A or Ex2
		fl->Flags[1] = false;
		fl->Attributes[0] = 70;
		fl->Attributes[1] = 90;
		fl->Attributes[2] = 33;
		press(CB_EX2, 300);
		hold(180, 301);
		phase = 6;		// no dive length: no dive
		fl->Attributes[0] = 0;
		press(CB_A, 500);
		hold(30, 501);
		phase = 7;		// diving written by the script: the flippers' length
		fl->Attributes[0] = 33;
		fl->Attributes[1] = 10;
		Hero->Action = LA_DIVING;
		hold(80, 540);
		report(759);
		printf("TV: DONE\n");
		while(true)
			Waitframe();
	}
}
