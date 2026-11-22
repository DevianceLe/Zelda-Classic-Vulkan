// Hold-up with a script tile (r12sub, Mystical Harps' global script): while Link holds an item up,
// the script sets Hero->ScriptTile to a tile plus Hero->TileMod, the ring's tile modifier. Link walks
// into a heart container whose pickup holds it up; Hero->Action, TileMod, Tile and ScriptTile are
// printed whenever one changes.
// frames: 260
// snap: 150
#include "std.zh"

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		for(int i = 0; i < 70; ++i)
			Waitframe();
		itemdata ring = Game->LoadItemData(I_RING1);
		ring->TileMod = 20;
		Hero->Item[I_RING1] = true;
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Hero->X = 64;
		Hero->Y = 80;
		Hero->Dir = DIR_RIGHT;
		itemsprite it = Screen->CreateItem(I_HEARTCONTAINER);
		it->X = 96;
		it->Y = 80;
		it->Pickup |= IP_HOLDUP;
		int la = -1, lt = -1, lm = -1;
		for(int f = 0; f < 180; ++f)
		{
			if(f < 40)
				Input->Button[CB_RIGHT] = true;
			if(Hero->Action == LA_HOLD1LAND || Hero->Action == LA_HOLD2LAND)
				Hero->ScriptTile = 1000 + Hero->TileMod;
			else
				Hero->ScriptTile = -1;
			if(Hero->Action != la || Hero->TileMod != lm || Hero->Tile != lt)
				printf("TU: f%d action %d tilemod %d tile %d scripttile %d x %d\n", f, Hero->Action, Hero->TileMod, Hero->Tile, Hero->ScriptTile, Hero->X);
			la = Hero->Action;
			lm = Hero->TileMod;
			lt = Hero->Tile;
			Waitframe();
		}
		printf("TU: DONE\n");
		while(true)
			Waitframe();
	}
}
