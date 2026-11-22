// An enemy knocked back onto a pit: a sword hit pushes a stalfos next to a pit into it; it falls
// (70 frames, the fall sprite) and is gone. Also one hit along the pit's edge, and one that can't
// fall (move flag cleared), which the pit stops. Each faces along the way it is hit (a hit
// across the way it faces doesn't knock it back), and is stunned so it stays that way.
// frames: 600
// snap: 590
#include "std.zh"

const int GROUND = 60011;	// combos the test makes its own
const int PIT = 60014;

int phase = 0;
int lx = -1000, ly, lv, lc, lhp;

void report(npc n, int f)
{
	int v = n->isValid() ? 1 : 0;
	int x = v ? n->X : -1, y = v ? n->Y : -1, hp = v ? n->HP : -1;
	if(x == lx && y == ly && v == lv && Screen->NumNPCs() == lc && hp == lhp)
		return;
	lx = x; ly = y; lv = v; lc = Screen->NumNPCs(); lhp = hp;
	printf("TK: p%d f%d npc valid %d at %d,%d hp %d, npcs %d items %d\n", phase, f, v, x, y, hp, Screen->NumNPCs(), Screen->NumItems());
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

npc spawn(int x, int y, bool can_fall, int dir)
{
	npc n = Screen->CreateNPC(NPC_STALFOS1);
	n->X = x;
	n->Y = y;
	n->Dir = dir;
	n->HP = 100;
	n->Stun = 1000;
	n->ItemSet = 0;
	n->MoveFlags[NPCMV_CAN_PITFALL] = can_fall;
	return n;
}

void slash(npc n, int dir, int frames, int f0)
{
	Hero->Dir = dir;
	Input->Press[CB_A] = true;
	Input->Button[CB_A] = true;
	for(int i = 0; i < frames; ++i)
	{
		report(n, f0 + i);
		Waitframe();
	}
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		int look = Screen->ComboD[ComboAt(Hero->X + 8, Hero->Y + 8)];
		make(GROUND, look, 0, CT_NONE);
		make(PIT, look, 0, CT_PITFALL);
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
		for(int row = 1; row < 10; ++row)
			for(int col = 9; col < 11; ++col)
				Screen->ComboD[row * 16 + col] = PIT;
		Screen->Flag[SFL_SIDEVIEW] = false;
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Hero->Item[I_SWORD1] = true;
		Hero->ItemA = I_SWORD1;
		Waitframe();

		phase = 1;		// knocked right into the pit
		Hero->X = 104;
		Hero->Y = 80;
		npc n = spawn(124, 80, true, DIR_LEFT);
		printf("TK: move flags: can fall %d, pit walk %d\n", n->MoveFlags[NPCMV_CAN_PITFALL] ? 1 : 0, n->MoveFlags[NPCMV_CAN_PIT_WALK] ? 1 : 0);
		for(int i = 0; i < 10; ++i) { report(n, i); Waitframe(); }
		slash(n, DIR_RIGHT, 140, 10);

		phase = 2;		// one that can't fall: the pit stops it
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Hero->X = 104;
		Hero->Y = 80;
		n = spawn(124, 80, false, DIR_LEFT);
		printf("TK: can fall written false reads %d\n", n->MoveFlags[NPCMV_CAN_PITFALL] ? 1 : 0);
		for(int i = 0; i < 10; ++i) { report(n, 200 + i); Waitframe(); }
		slash(n, DIR_RIGHT, 80, 210);

		phase = 3;		// knocked up along the pit's edge, half over it
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Hero->X = 136;
		Hero->Y = 120;
		n = spawn(136, 100, true, DIR_DOWN);
		for(int i = 0; i < 10; ++i) { report(n, 300 + i); Waitframe(); }
		slash(n, DIR_UP, 140, 310);
		printf("TK: DONE\n");
		while(true)
			Waitframe();
	}
}
