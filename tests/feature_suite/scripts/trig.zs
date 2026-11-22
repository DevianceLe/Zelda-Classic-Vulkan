// Combo trigger fields scripts reach (r12 A.1): combodata->TrigTintR/G/B, TrigButton[], TrigFlags[],
// TriggerFlags[], Triggers[], NumTriggers, OnlyGenTriggers, GetTrigger, and combotrigger-> on the
// same triggers; then a tint set by a script, used in play: Screen->TriggerCombo on a combo whose
// trigger tints the screen (picture compared at the snap frame).
// frames: 140
// snap: 130
#include "std.zh"

const int CMB = 60020;		// combos the test writes to
const int CMB2 = 60030;

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Waitframe();

		combodata cd = Game->LoadComboData(CMB);
		printf("TT: start: triggers %d onlygen %d tint %d %d %d rule6bit %d\n", cd->NumTriggers, cd->OnlyGenTriggers ? 1 : 0,
			cd->TrigTintR, cd->TrigTintG, cd->TrigTintB, Game->FFRules[480] ? 1 : 0);
		printf("TT: a read adds none: triggers %d button0 %d flags0 %d\n", cd->NumTriggers, cd->TrigButton[0] ? 1 : 0, cd->TrigFlags[0] ? 1 : 0);
		cd->TrigTintR = 40;
		printf("TT: a write adds one: triggers %d tint %d %d %d\n", cd->NumTriggers, cd->TrigTintR, cd->TrigTintG, cd->TrigTintB);
		cd->TrigTintG = -63;
		cd->TrigTintB = 300;
		printf("TT: tint written -63 300: %d %d\n", cd->TrigTintG, cd->TrigTintB);
		cd->TrigTintR = -500;
		printf("TT: tint written -500: %d\n", cd->TrigTintR);
		cd->TrigTintR = 17;
		printf("TT: tint written 17: %d\n", cd->TrigTintR);

		// TrigButton[]
		cd->TrigButton[1] = true;
		cd->TrigButton[6] = true;
		printf("TT: buttons: %d %d %d %d %d %d %d %d\n", cd->TrigButton[0] ? 1 : 0, cd->TrigButton[1] ? 1 : 0, cd->TrigButton[2] ? 1 : 0,
			cd->TrigButton[3] ? 1 : 0, cd->TrigButton[4] ? 1 : 0, cd->TrigButton[5] ? 1 : 0, cd->TrigButton[6] ? 1 : 0, cd->TrigButton[7] ? 1 : 0);
		printf("TT: button -2 %d, 9 %d\n", cd->TrigButton[-2] ? 1 : 0, cd->TrigButton[9] ? 1 : 0);
		cd->TrigButton[6] = false;
		printf("TT: button 6 cleared: %d\n", cd->TrigButton[6] ? 1 : 0);

		// TrigFlags[] and TriggerFlags[]
		cd->TrigFlags[0] = true;		// sword
		cd->TrigFlags[5] = true;
		cd->TrigFlags[33] = true;
		cd->TrigFlags[29] = true;		// only by generic triggers
		printf("TT: flag sets: %d %d %d, onlygen %d\n", cd->TriggerFlags[0], cd->TriggerFlags[1], cd->TriggerFlags[2], cd->OnlyGenTriggers ? 1 : 0);
		cd->TriggerFlags[1] = 12;
		cd->TriggerFlags[0] = 1;
		printf("TT: flag set 1 = 12, 0 = 1: flags 0 %d 5 %d 29 %d 33 %d 34 %d 35 %d onlygen %d\n", cd->TrigFlags[0] ? 1 : 0, cd->TrigFlags[5] ? 1 : 0,
			cd->TrigFlags[29] ? 1 : 0, cd->TrigFlags[33] ? 1 : 0, cd->TrigFlags[34] ? 1 : 0, cd->TrigFlags[35] ? 1 : 0, cd->OnlyGenTriggers ? 1 : 0);
		printf("TT: flag set 6 %d, -1 %d\n", cd->TriggerFlags[6], cd->TriggerFlags[-1]);
		cd->TriggerFlags[2] = 500000;
		printf("TT: flag set 2 written 500000: %d\n", cd->TriggerFlags[2]);

		// Triggers[], NumTriggers, combotrigger->
		combotrigger t = cd->Triggers[0];
		printf("TT: trigger 0 ref ok %d, tint %d %d %d, button1 %d flag0 %d\n", t ? 1 : 0, t->TintR, t->TintG, t->TintB, t->Button[1] ? 1 : 0,
			t->Flags[0] ? 1 : 0);
		cd->NumTriggers = 3;
		combotrigger t2 = cd->Triggers[2];
		t2->TintB = 9;
		t2->Timer = 70000;
		t2->CSetChange = -100;
		t2->ReqLevelState[3] = true;
		t2->ReqGlobalState[200] = true;
		t2->ReqHeroDir[2] = true;
		t2->DestHeroX = 12.5;
		t2->ChanceDenominator = 0;
		printf("TT: trigger 2: tint %d timer %d cset %d lstate3 %d gstate200 %d dir2 %d x %d den %d; trigger 0 tint b %d\n", t2->TintB, t2->Timer,
			t2->CSetChange, t2->ReqLevelState[3] ? 1 : 0, t2->ReqGlobalState[200] ? 1 : 0, t2->ReqHeroDir[2] ? 1 : 0, t2->DestHeroX * 10,
			t2->ChanceDenominator, t->TintB);
		cd->TrigCSetChange = -100;
		printf("TT: combodata cset change -100: %d\n", cd->TrigCSetChange);
		t2->SetLabel("second");
		combotrigger t3 = cd->GetTrigger("second");
		combotrigger t4 = cd->GetTrigger("none");
		char32 buf[32];
		t3->GetLabel(buf);
		printf("TT: by label: found %d, the same %d, missing %d, label %s\n", t3 ? 1 : 0, t3 == t2 ? 1 : 0, t4 ? 1 : 0, buf);
		t2->SetReqExDoor(1, 4, true);
		printf("TT: ex door: %d %d\n", t2->GetReqExDoor(1, 4), t2->GetReqExDoor(1, 3));
		cd->NumTriggers = 1;
		printf("TT: triggers %d, onlygen written false\n", cd->NumTriggers);
		cd->OnlyGenTriggers = false;
		printf("TT: flag 29 %d\n", cd->TrigFlags[29] ? 1 : 0);

		// In play: the combo under position 0 of layer 0 tints the screen when triggered by script.
		combodata ce = Game->LoadComboData(CMB2);
		ce->Tile = Game->LoadComboData(Screen->ComboD[0])->Tile;
		ce->TrigTintR = 0;
		ce->TrigTintG = -40;
		ce->TrigTintB = 60;
		Screen->ComboD[0] = CMB2;
		Waitframe();
		printf("TT: triggered %d\n", Screen->TriggerCombo(0, 0) ? 1 : 0);
		printf("TT: DONE\n");
		while(true)
			Waitframe();
	}
}
