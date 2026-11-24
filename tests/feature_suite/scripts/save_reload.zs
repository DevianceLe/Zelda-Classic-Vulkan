// Saving and loading the save again in one run (r22save): the script sets state a save keeps, saves
// (Game->Save()) and loads the save again (Game->Reload()); after the reload it prints what came back.
// Our player reads the save back from the save file on a reload in test runs (ZC_TEST_SAVE_RELOAD),
// so what it prints went through the save format.
#include "std.zh"

int sr_phase;   // a global variable: the save keeps it

global script SaveReload
{
	void TellState(int when)
	{
		printf("TL: %d dmap %d screen %d hero %d,%d dir %d; continue %d,%d\n", when, Game->GetCurDMap(), Game->GetCurScreen(),
			Hero->X, Hero->Y, Hero->Dir, Game->ContinueDMap, Game->ContinueScreen);
		printf("TL: %d life %d of %d, magic %d of %d; HP per heart %d, continue hearts %d percent %d\n", when, Hero->HP, Hero->MaxHP,
			Hero->MP, Hero->MaxMP, Game->Generic[GEN_HP_PER_HEART], Game->Generic[GEN_CONTINUEHEARTS], Game->Generic[GEN_CONTINUEISPERCENT]);
		printf("TL: %d counters rupees %d of %d, bombs %d of %d, c5 %d of %d, c40 %d of %d, c100 %d of %d\n", when,
			Game->Counter[CR_RUPEES], Game->MCounter[CR_RUPEES], Game->Counter[CR_BOMBS], Game->MCounter[CR_BOMBS],
			Game->Counter[CR_CUSTOM5], Game->MCounter[CR_CUSTOM5], Game->Counter[CR_CUSTOM40], Game->MCounter[CR_CUSTOM40],
			Game->Counter[CR_CUSTOM100], Game->MCounter[CR_CUSTOM100]);
		printf("TL: %d items bomb %d brang %d candle %d; B %d A %d; heart pieces %d\n", when, Hero->Item[I_BOMB],
			Hero->Item[I_BRANG1], Hero->Item[I_CANDLE1], Hero->ItemB, Hero->ItemA, Game->Generic[GEN_HEARTPIECES]);
		mapdata md = Game->LoadMapData(20, 0x15);
		printf("TL: %d states secret %d item %d; 0x15 exstate3 %d exstate9 %d; here exstate5 %d; screen D %d %d\n", when,
			Game->GetScreenState(20, 0x15, ST_SECRET), Game->GetScreenState(20, 0x16, ST_ITEM), md->ExState[3], md->ExState[9],
			Screen->ExState[5], Game->GetScreenD(0x15, 2), Game->GetScreenD(0x14, 7));
		printf("TL: %d switches level 3 %d level 0 %d, global 7 %d\n", when, Game->LSwitches[3], Game->LSwitches[0], Game->GSwitch[7]);
		printf("TL: %d saved portals %d\n", when, Game->NumSavedPortals);
		for(int i = 0; i < Game->NumSavedPortals; ++i)
		{
			savedportal sp = Game->SavedPortals[i];
			printf("TL: %d portal %d from %d,%d to %d,%d at %d,%d sfx %d effect %d sprite %d\n", when, i, sp->SrcDMap, sp->SrcScreen,
				sp->DestDMap, sp->DestScreen, sp->X, sp->Y, sp->WarpSFX, sp->WarpEffect, sp->Sprite);
		}
	}

	void run()
	{
		Waitframes(10);
		if(sr_phase == 0)
		{
			sr_phase = 1;
			Hero->MaxHP = 64;
			Hero->HP = 20;
			Hero->MaxMP = 96;
			Hero->MP = 40;
			Game->Counter[CR_RUPEES] = 123;
			Game->MCounter[CR_BOMBS] = 20;
			Game->Counter[CR_BOMBS] = 7;
			Game->MCounter[CR_CUSTOM5] = 50;
			Game->Counter[CR_CUSTOM5] = 5;
			Game->MCounter[CR_CUSTOM40] = 400;
			Game->Counter[CR_CUSTOM40] = 41;
			Game->MCounter[CR_CUSTOM100] = 1000;
			Game->Counter[CR_CUSTOM100] = 101;
			Game->Generic[GEN_HEARTPIECES] = 2;
			Hero->Item[I_BOMB] = true;
			Hero->Item[I_BRANG1] = true;
			Hero->Item[I_CANDLE1] = true;
			Hero->SetItemSlot(I_BRANG1, ITM_SLOT_B, ITM_REQUIRE_NONE);
			Hero->SetItemSlot(I_CANDLE1, ITM_SLOT_A, ITM_REQUIRE_NONE);
			Game->SetScreenState(20, 0x15, ST_SECRET, true);
			Game->SetScreenState(20, 0x16, ST_ITEM, true);
			mapdata md = Game->LoadMapData(20, 0x15);
			md->ExState[3] = true;
			md->ExState[9] = true;
			Screen->ExState[5] = true;
			Game->SetScreenD(0x15, 2, 777);
			Game->SetScreenD(0x14, 7, -55);
			Game->LSwitches[3] = 0x1234;
			Game->GSwitch[7] = -1;
			savedportal sp = Game->CreateSavedPortal();
			sp->SrcDMap = Game->GetCurDMap();
			sp->SrcScreen = 0x15;
			sp->DestDMap = Game->GetCurDMap();
			sp->DestScreen = 0x16;
			sp->X = 64;
			sp->Y = 80;
			sp->WarpSFX = 5;
			sp->WarpEffect = 1;
			sp->Sprite = 3;
			Hero->X = 96;
			Hero->Y = 112;
			Waitframe();
			TellState(0);
			Game->Save();
			Waitframes(2);
			Hero->HP = 4;           // after the save: the reload forgets it
			Game->Counter[CR_RUPEES] = 5;
			Waitframe();
			Game->Reload();
		}
		else
		{
			TellState(1);
			Waitframes(10);
			TellState(2);
		}
		while(true)
			Waitframe();
	}
}
