// Saving and continuing (r22save): Game->SaveAndContinue() with a continue point set on another screen of
// the DMap (Game->ContinueScreen), then Game->Continue(); what the game continues with is printed each time.
#include "std.zh"

int sc_phase;   // a global variable: kept across continues

global script SaveContinue
{
	void TellState(int when)
	{
		printf("TC: %d dmap %d screen %d hero %d,%d dir %d; continue %d,%d\n", when, Game->GetCurDMap(), Game->GetCurScreen(),
			Hero->X, Hero->Y, Hero->Dir, Game->ContinueDMap, Game->ContinueScreen);
		printf("TC: %d life %d of %d, magic %d of %d; HP per heart %d, continue hearts %d percent %d\n", when, Hero->HP, Hero->MaxHP,
			Hero->MP, Hero->MaxMP, Game->Generic[GEN_HP_PER_HEART], Game->Generic[GEN_CONTINUEHEARTS], Game->Generic[GEN_CONTINUEISPERCENT]);
		printf("TC: %d rupees %d, c40 %d, B %d A %d, exstate5 %d\n", when, Game->Counter[CR_RUPEES], Game->Counter[CR_CUSTOM40],
			Hero->ItemB, Hero->ItemA, Screen->ExState[5]);
	}

	void run()
	{
		Waitframes(10);
		if(sc_phase == 0)
		{
			sc_phase = 1;
			Hero->MaxHP = 80;
			Hero->HP = 12;
			Game->Counter[CR_RUPEES] = 33;
			Game->MCounter[CR_CUSTOM40] = 400;
			Game->Counter[CR_CUSTOM40] = 44;
			Hero->Item[I_BRANG1] = true;
			Hero->SetItemSlot(I_BRANG1, ITM_SLOT_B, ITM_REQUIRE_NONE);
			Screen->ExState[5] = true;
			Game->ContinueScreen = 0x13;
			TellState(0);
			Game->SaveAndContinue();
		}
		else if(sc_phase == 1)
		{
			sc_phase = 2;
			TellState(1);
			Hero->HP = 3;
			Game->Counter[CR_RUPEES] = 9;
			Waitframes(20);
			TellState(2);
			Game->Continue();
		}
		else
			TellState(3);
		while(true)
			Waitframe();
	}
}
