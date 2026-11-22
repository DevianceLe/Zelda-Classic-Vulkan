// Enemies that fall into a pit leave no item (upstream enemy::do_falling: no leave_item), while the
// same enemies killed on the ground drop theirs. 8 stalfos with drop sets 1-8 are killed on the
// ground (control), then 8 more with the same drop sets are put on pit cells, stunned, and fall.
// frames: 400
#include "std.zh"

const int GROUND = 60011;	// combos the test makes its own
const int PIT = 60014;

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

npc spawn(int x, int y, int set)
{
	npc n = Screen->CreateNPC(NPC_STALFOS1);
	n->X = x;
	n->Y = y;
	n->HP = 100;
	n->Stun = 1000;
	n->ItemSet = set;
	n->MoveFlags[NPCMV_CAN_PITFALL] = true;
	return n;
}

void wait(int frames, int f0, int phase)
{
	int lc = -1, li = -1;
	for(int i = 0; i < frames; ++i)
	{
		if(Screen->NumNPCs() != lc || Screen->NumItems() != li)
		{
			lc = Screen->NumNPCs(); li = Screen->NumItems();
			printf("TP: p%d f%d npcs %d items %d\n", phase, f0 + i, lc, li);
		}
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
		for(int col = 2; col < 14; ++col)	// the pit: row 7, columns 2-13
			Screen->ComboD[7 * 16 + col] = PIT;
		Screen->Flag[SFL_SIDEVIEW] = false;
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		for(int i = Screen->NumItems(); i > 0; --i)
			Screen->LoadItem(i)->Remove();
		Hero->X = 120;
		Hero->Y = 144;
		Waitframe();

		npc n[8];
		for(int i = 0; i < 8; ++i)	// control: killed on the ground (row 3)
			n[i] = spawn(32 + i * 24, 48, i + 1);
		wait(2, 0, 1);
		for(int i = 0; i < 8; ++i)
			n[i]->HP = 0;
		wait(100, 2, 1);
		for(int i = Screen->NumNPCs(); i > 0; --i)
		{
			npc e = Screen->LoadNPC(i);
			printf("TP: left at f102: npc %d hp %d at %d,%d\n", e->ID, e->HP, e->X, e->Y);
		}
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		for(int i = Screen->NumItems(); i > 0; --i)
			Screen->LoadItem(i)->Remove();

		for(int i = 0; i < 8; ++i)	// on the pit (row 7)
			n[i] = spawn(32 + i * 24, 112, i + 1);
		wait(150, 102, 2);
		printf("TP: DONE\n");
		while(true)
			Waitframe();
	}
}
