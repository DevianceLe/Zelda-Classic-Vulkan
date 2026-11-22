// Script values that were stubs: Hero->LiftedWeapon (an lweapon held up over Link's head),
// sprite->Gravity (the "obeys gravity" move flag), sprite->SwitchHooked, and the first combo
// trigger's TrigGlobalState and TrigChange.
// frames: 120
#include "std.zh"

const int CMB = 60020;		// a combo the test writes to

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		for(int i = Screen->NumNPCs(); i > 0; --i)
			Screen->LoadNPC(i)->Remove();
		Waitframe();

		// Gravity
		npc n = Screen->CreateNPC(NPC_STALFOS1);
		n->X = 64;
		n->Y = 64;
		printf("TR: gravity: hero %d npc %d\n", Hero->Gravity ? 1 : 0, n->Gravity ? 1 : 0);
		n->Gravity = false;
		printf("TR: npc gravity written false reads %d, move flag %d\n", n->Gravity ? 1 : 0, n->MoveFlags[NPCMV_OBEYS_GRAVITY] ? 1 : 0);
		n->MoveFlags[NPCMV_OBEYS_GRAVITY] = true;
		printf("TR: move flag written true: gravity %d\n", n->Gravity ? 1 : 0);
		npc m = Screen->CreateNPC(NPC_STALFOS1);
		m->Gravity = false;
		printf("TR: a new npc's gravity written first reads %d, can fall %d\n", m->Gravity ? 1 : 0, m->MoveFlags[NPCMV_CAN_PITFALL] ? 1 : 0);
		Hero->Gravity = false;
		printf("TR: hero gravity written false reads %d\n", Hero->Gravity ? 1 : 0);
		Hero->Gravity = true;

		// SwitchHooked
		printf("TR: switch hooked: hero %d npc %d\n", Hero->SwitchHooked ? 1 : 0, n->SwitchHooked ? 1 : 0);
		n->Remove();
		m->Remove();

		// LiftedWeapon
		printf("TR: lifting at the start %d\n", Hero->LiftedWeapon ? 1 : 0);
		lweapon lw = Screen->CreateLWeapon(LW_SCRIPT1);
		lw->X = 100;
		lw->Y = 100;
		int before = Screen->NumLWeapons();
		Hero->LiftedWeapon = lw;
		printf("TR: lifted %d, the same %d, valid %d, lweapons %d -> %d\n", Hero->LiftedWeapon ? 1 : 0, Hero->LiftedWeapon == lw ? 1 : 0,
			lw->isValid() ? 1 : 0, before, Screen->NumLWeapons());
		for(int i = 0; i < 4; ++i)
		{
			Waitframe();
			printf("TR: frame %d: held at %d,%d z %d (hero %d,%d), valid %d\n", i, lw->X, lw->Y, lw->Z, Hero->X, Hero->Y, lw->isValid() ? 1 : 0);
		}
		lweapon lw2 = Screen->CreateLWeapon(LW_SCRIPT2);
		Hero->LiftedWeapon = lw2;
		printf("TR: another lifted: the first valid %d, the second held %d, lweapons %d\n", lw->isValid() ? 1 : 0, Hero->LiftedWeapon == lw2 ? 1 : 0,
			Screen->NumLWeapons());
		Hero->LiftedWeapon = NULL;
		Waitframe();
		printf("TR: none written: lifting %d, the second valid %d, lweapons %d\n", Hero->LiftedWeapon ? 1 : 0, lw2->isValid() ? 1 : 0,
			Screen->NumLWeapons());

		// TrigGlobalState, TrigChange
		combodata cd = Game->LoadComboData(CMB);
		printf("TR: trigger: global state %d change %d\n", cd->TrigGlobalState, cd->TrigChange);
		cd->TrigGlobalState = 7;
		cd->TrigChange = -3;
		printf("TR: written 7 and -3: %d %d\n", cd->TrigGlobalState, cd->TrigChange);
		cd->TrigGlobalState = 300;
		cd->TrigChange = 70000;
		printf("TR: written 300 and 70000: %d %d\n", cd->TrigGlobalState, cd->TrigChange);
		cd->TrigGlobalState = -5;
		cd->TrigChange = -70000;
		printf("TR: written -5 and -70000: %d %d\n", cd->TrigGlobalState, cd->TrigChange);
		combodata ce = Game->LoadComboData(CMB + 1);
		printf("TR: a fresh trigger: exstate %d door dir %d level %d cooldown %d sfx %d\n", ce->TrigExState, ce->TrigExDoorDir, ce->TrigLevel,
			ce->TrigCooldown, ce->TrigSFX);
		ce->TrigExState = 40;
		ce->TrigCooldown = 300;
		ce->TrigSpawnItemPickup = 65535;
		ce->TrigLItems = 131071;
		ce->TrigSFX = 2000;
		ce->TrigCSetChange = -20;
		ce->TrigBunny = -9;
		ce->TrigCounter = -30;
		ce->TrigSpawnEnemy = 600;
		ce->TrigItem = 5000;
		printf("TR: written: exstate %d cooldown %d pickup %d litems %d sfx %d cset %d bunny %d counter %d enemy %d item %d\n", ce->TrigExState,
			ce->TrigCooldown, ce->TrigSpawnItemPickup, ce->TrigLItems, ce->TrigSFX, ce->TrigCSetChange, ce->TrigBunny, ce->TrigCounter,
			ce->TrigSpawnEnemy, ce->TrigItem);
		printf("TR: DONE\n");
		while(true)
			Waitframe();
	}
}
