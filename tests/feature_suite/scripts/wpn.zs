// Enemy weapons from their first frame on: an Octorok fires rocks; for each rock the script prints
// where it is against the Octorok on the first three frames it sees it, then how many frames it
// lived and where it was last. (Upstream's script sees a new rock where it was fired; it moves
// from the next frame.)
// frames: 900
#include "std.zh"

const int GROUND = 60011;

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
		make(GROUND, look, 0, CT_NONE);
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
		Screen->Flag[SFL_SIDEVIEW] = false;
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Hero->X = 0;
		Hero->Y = 0;
		Waitframe();
		printf("TW: extra death frame %d\n", Game->FFRules[qr_WEAPONS_EXTRA_DEATH_FRAME] ? 1 : 0);
		Game->FFRules[qr_WEAPONS_EXTRA_DEATH_FRAME] = true;	// (the rule this test is about)

		npc o = Screen->CreateNPC(NPC_OCTOROCK1S);
		o->X = 120;
		o->Y = 80;
		o->HP = 1000;
		int uids[64];
		int age[64];
		int lx[64];
		int ly[64];
		int n = 0;
		int shown = 0;
		for(int f = 0; f < 880 && shown < 12; ++f)
		{
			// every rock alive: its age, and where it is
			for(int k = 0; k < n; ++k)
				if(uids[k] && age[k] >= 0)
				{
					bool alive = false;
					for(int i = Screen->NumEWeapons(); i > 0; --i)
					{
						eweapon e = Screen->LoadEWeapon(i);
						if(e->UID == uids[k])
						{
							alive = true;
							++age[k];
							if(age[k] < 3)
								printf("TW: rock %d age %d at %d,%d from the octorok\n", k, age[k], e->X - o->X, e->Y - o->Y);
							lx[k] = e->X; ly[k] = e->Y;
						}
					}
					if(!alive)
					{
						printf("TW: rock %d seen %d frames, last at %d,%d\n", k, age[k] + 1, lx[k], ly[k]);
						age[k] = -1;
						++shown;
					}
				}
			for(int i = Screen->NumEWeapons(); i > 0; --i)
			{
				eweapon e = Screen->LoadEWeapon(i);
				bool known = false;
				for(int k = 0; k < n; ++k)
					if(uids[k] == e->UID) known = true;
				if(!known && n < 64)
				{
					uids[n] = e->UID; age[n] = 0; lx[n] = e->X; ly[n] = e->Y;
					printf("TW: rock %d age 0 at %d,%d from the octorok (%d,%d), dir %d step %d\n", n, e->X - o->X, e->Y - o->Y, e->X, e->Y, e->Dir, e->Step);
					++n;
				}
			}
			Waitframe();
		}
		printf("TW: DONE\n");
		while(true)
			Waitframe();
	}
}
