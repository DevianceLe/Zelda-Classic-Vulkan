// What scripts see of Link in a sideview room: swimming (Hero->Action, the Game->Generic[] swim
// rates), leaving the water, drowning, sideview ladders.
// frames: 1700
// snap: 1650
#include "std.zh"

const int AIR = 60010;		// combos the test makes its own
const int GROUND = 60011;
const int WATER = 60012;

int phase = 0;
int lx = -1000, ly, la, ld, lc, ls, lj, lhp;

void report(int f)
{
	int c = Hero->Climbing ? 1 : 0;
	int s = Hero->Standing ? 1 : 0;
	if(Hero->X == lx && Hero->Y == ly && Hero->Action == la && Hero->Dir == ld && c == lc && s == ls && Hero->Jump == lj && Hero->HP == lhp)
		return;
	lx = Hero->X; ly = Hero->Y; la = Hero->Action; ld = Hero->Dir; lc = c; ls = s; lj = Hero->Jump; lhp = Hero->HP;
	printf("TW: p%d f%d hero %d,%d action %d dir %d climbing %d standing %d jump %d hp %d\n", phase, f, Hero->X, Hero->Y, Hero->Action, Hero->Dir, c, s, Hero->Jump, Hero->HP);
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

void put(int col, int row, int id)
{
	Screen->ComboD[row * 16 + col] = id;
}

void rates(char32[] what)
{
	printf("TW: %s: water gravity %d, step up %d side %d down %d, exit jump %d\n", what, Game->Generic[GEN_WATERGRAVITY], Game->Generic[GEN_SIDESWIM_STEP_UP],
		Game->Generic[GEN_SIDESWIM_STEP_SIDE], Game->Generic[GEN_SIDESWIM_STEP_DOWN], Game->Generic[GEN_SIDESWIM_EXITJUMP]);
}

void place(int x, int y)
{
	Hero->X = x;
	Hero->Y = y;
	Hero->Jump = 0;
	Hero->Action = LA_NONE;
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		// the room: air, a floor, a ledge on the left, a pool, a ladder up to a ledge on the right
		int look = Screen->ComboD[ComboAt(Hero->X + 8, Hero->Y + 8)];
		make(AIR, look, 0, CT_NONE);
		make(GROUND, look, 15, CT_NONE);
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
			Screen->ComboD[pos] = AIR;
			Screen->ComboF[pos] = 0;
		}
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		for(int col = 0; col < 16; ++col)
			put(col, 10, GROUND);
		for(int col = 0; col < 4; ++col)
			put(col, 4, GROUND);
		for(int row = 4; row < 10; ++row)
		{
			put(4, row, GROUND);
			put(10, row, GROUND);
			for(int col = 5; col < 10; ++col)
				put(col, row, WATER);
		}
		for(int row = 4; row < 10; ++row)
			Screen->ComboF[row * 16 + 12] = CF_SIDEVIEW_LADDER;
		put(13, 4, GROUND);
		put(14, 4, GROUND);
		Game->FFRules[qr_SIDESWIM] = true;
		Game->FFRules[qr_DROWN] = true;
		Screen->Flag[SFL_SIDEVIEW] = true;
		Hero->Item[I_FLIPPERS] = true;
		printf("TW: rules: sideswim %d drown %d, sideview %d, flippers %d\n", Game->FFRules[qr_SIDESWIM] ? 1 : 0, Game->FFRules[qr_DROWN] ? 1 : 0,
			Screen->Flag[SFL_SIDEVIEW] ? 1 : 0, Hero->Item[I_FLIPPERS] ? 1 : 0);
		rates("as the quest sets them");
		Hero->HP = Hero->MaxHP;
		place(32, 48);
		Hero->Dir = DIR_RIGHT;
		Waitframe();

		phase = 1;		// along the ledge, over the pool's wall and into the water
		hold(CB_RIGHT, 60, 0);
		phase = 2;		// in the water with nothing held
		hold(-1, 40, 100);
		phase = 3;		// each way
		hold(CB_DOWN, 20, 200);
		hold(CB_LEFT, 20, 220);
		hold(CB_RIGHT, 20, 240);
		hold(CB_UP, 12, 260);
		hold(-1, 10, 272);
		phase = 4;		// other rates, written by the script
		Game->Generic[GEN_WATERGRAVITY] = 1;
		Game->Generic[GEN_SIDESWIM_STEP_UP] = 0.5;
		Game->Generic[GEN_SIDESWIM_STEP_SIDE] = 2;
		Game->Generic[GEN_SIDESWIM_STEP_DOWN] = 3;
		rates("written");
		place(112, 80);
		hold(-1, 20, 300);
		hold(CB_UP, 20, 320);
		hold(CB_LEFT, 12, 340);
		hold(CB_RIGHT, 12, 352);
		hold(CB_DOWN, 12, 364);
		phase = 5;		// a negative water gravity floats him up
		Game->Generic[GEN_WATERGRAVITY] = -1;
		rates("gravity -1");
		hold(-1, 60, 400);
		phase = 6;		// out of the top: with no exit jump only a ladder gets him out
		Game->Generic[GEN_WATERGRAVITY] = 0;
		Game->Generic[GEN_SIDESWIM_STEP_UP] = 1;
		Game->Generic[GEN_SIDESWIM_STEP_SIDE] = 1;
		Game->Generic[GEN_SIDESWIM_STEP_DOWN] = 1;
		Game->Generic[GEN_SIDESWIM_EXITJUMP] = 0;
		place(112, 80);
		hold(CB_UP, 50, 500);
		phase = 7;		// with an exit jump of 3
		Game->Generic[GEN_SIDESWIM_EXITJUMP] = 3;
		rates("exit jump 3");
		place(112, 80);
		hold(CB_UP, 80, 600);
		phase = 8;		// attacking and being frozen in the water
		place(112, 96);
		hold(-1, 5, 700);
		Hero->Action = LA_FROZEN;
		hold(-1, 5, 710);
		printf("TW: frozen in water reads %d\n", Hero->Action);
		Hero->Action = LA_NONE;
		hold(-1, 5, 720);
		printf("TW: none in water reads %d\n", Hero->Action);
		Hero->Action = LA_SWIMMING;
		hold(-1, 3, 730);
		printf("TW: swimming written in sideview water reads %d\n", Hero->Action);
		Hero->Action = LA_SIDESWIM;
		hold(-1, 3, 740);
		printf("TW: sideswim written reads %d\n", Hero->Action);
		phase = 9;		// no flippers: drowning
		Hero->Item[I_FLIPPERS] = false;
		place(112, 96);
		hold(-1, 150, 800);
		printf("TW: after drowning: hero %d,%d action %d hp %d\n", Hero->X, Hero->Y, Hero->Action, Hero->HP);
		phase = 10;		// drowning written by the script, on dry ground and in the water
		Hero->Item[I_FLIPPERS] = true;
		place(32, 48);
		hold(-1, 10, 1000);
		Hero->Action = LA_SIDEDROWN;
		hold(-1, 5, 1010);
		printf("TW: sidedrown written on the ledge reads %d\n", Hero->Action);
		hold(-1, 120, 1020);
		phase = 11;		// the ladder: up from the floor, off the top onto the ledge, and back down
		place(192, 144);
		hold(-1, 10, 1100);
		hold(CB_UP, 80, 1110);
		hold(CB_RIGHT, 16, 1190);
		hold(CB_LEFT, 16, 1206);
		hold(CB_DOWN, 30, 1222);
		hold(-1, 10, 1252);
		hold(CB_LEFT, 10, 1262);		// off the side of it: he falls
		hold(-1, 40, 1272);
		phase = 12;		// put on it by the script, facing up there by the rule, then let go
		Game->FFRules[qr_SIDEVIEWLADDER_FACEUP] = true;
		place(192, 96);
		Hero->Climbing = true;
		Hero->Dir = DIR_LEFT;
		printf("TW: climbing written reads %d\n", Hero->Climbing ? 1 : 0);
		hold(-1, 10, 1320);
		hold(CB_RIGHT, 6, 1330);
		hold(CB_DOWN, 6, 1336);
		Hero->Climbing = false;
		hold(-1, 30, 1342);
		Game->FFRules[qr_SIDEVIEWLADDER_FACEUP] = false;
		phase = 13;		// down in the air beside it grabs it, unless qr_DOWN_DOESNT_GRAB_LADDERS
		place(192, 96);
		Hero->Jump = 1;
		hold(-1, 4, 1400);
		hold(CB_DOWN, 20, 1404);
		Game->FFRules[qr_DOWN_DOESNT_GRAB_LADDERS] = true;
		place(192, 96);
		Hero->Jump = 1;
		hold(-1, 4, 1430);
		hold(CB_DOWN, 40, 1434);
		Game->FFRules[qr_DOWN_DOESNT_GRAB_LADDERS] = false;
		report(1599);
		printf("TW: DONE\n");
		while(true)
		{
			place(32, 48);
			Hero->Dir = DIR_DOWN;
			Waitframe();
		}
	}
}
