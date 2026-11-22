// Plain fields the pack's scripts use that were unhandled (r12 A.2): mapdata StairsX/Y, ExitDir,
// WarpArrival, ItemX/Y, MazePath[], SecretCombo/CSet/Flags[]; Screen->ItemX/Y, Catchall,
// TileWarpType/DMap/Screen[]; shopdata Item/HasItem/Price[], Type; itemdata TileMod, AFrames,
// ASpeed, Delay; lweapon Parent, UID; combodata UserFlags.
// frames: 120
#include "std.zh"

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		Waitframe();
		mapdata md = Game->LoadMapData(1, 0x10);
		printf("TP: mapdata: stairs %d,%d exit %d arrival %d,%d item %d,%d path %d %d %d %d\n", md->StairsX, md->StairsY, md->ExitDir,
			md->WarpArrivalX, md->WarpArrivalY, md->ItemX, md->ItemY, md->MazePath[0], md->MazePath[1], md->MazePath[2], md->MazePath[3]);
		printf("TP: secrets: %d %d %d / %d %d %d / %d %d\n", md->SecretCombo[0], md->SecretCombo[5], md->SecretCombo[127], md->SecretCSet[0],
			md->SecretCSet[5], md->SecretCSet[-1], md->SecretFlags[0], md->SecretFlags[130]);
		md->StairsX = 300;
		md->ItemY = -4;
		md->MazePath[2] = 3;
		md->SecretCombo[5] = 4464;	// (70000: ad25219 refuses it, ef321f8 keeps 4464)
		md->SecretCombo[6] = 1234;
		md->SecretCSet[5] = 300;
		md->SecretFlags[-1] = 77;
		printf("TP: written: stairs x %d item y %d path2 %d secret5 %d secret6 %d cset5 %d flag127 %d\n", md->StairsX, md->ItemY, md->MazePath[2],
			md->SecretCombo[5], md->SecretCombo[6], md->SecretCSet[5], md->SecretFlags[127]);

		printf("TP: screen: item %d,%d catchall %d warps %d %d %d / %d %d\n", Screen->ItemX, Screen->ItemY, Screen->Catchall,
			Screen->TileWarpType[0], Screen->TileWarpDMap[0], Screen->TileWarpScreen[0], Screen->TileWarpDMap[3], Screen->TileWarpScreen[4]);
		Screen->Catchall = 300000;
		Screen->ItemX = 77;
		Screen->TileWarpDMap[1] = 5;
		Screen->TileWarpDMap[2] = 511;	// (9999: ad25219 refuses it, ef321f8 keeps it)
		Screen->TileWarpScreen[1] = 0x45;
		printf("TP: screen written: catchall %d item x %d dmap1 %d dmap2 %d screen1 %d\n", Screen->Catchall, Screen->ItemX,
			Screen->TileWarpDMap[1], Screen->TileWarpDMap[2], Screen->TileWarpScreen[1]);

		shopdata sd = Game->LoadShopData(0);
		printf("TP: shop 0: type %d items %d %d %d has %d %d %d prices %d %d %d\n", sd->Type, sd->Item[0], sd->Item[1], sd->Item[2],
			sd->HasItem[0] ? 1 : 0, sd->HasItem[1] ? 1 : 0, sd->HasItem[2] ? 1 : 0, sd->Price[0], sd->Price[1], sd->Price[2]);
		sd->Price[1] = 70000;
		sd->Item[2] = 33;
		sd->HasItem[2] = true;
		printf("TP: shop written: price1 %d item2 %d has2 %d bad index %d\n", sd->Price[1], sd->Item[2], sd->HasItem[2] ? 1 : 0, sd->Price[3]);
		shopdata si = Game->LoadInfoShopData(1);
		printf("TP: info shop 1: type %d prices %d %d %d item %d\n", si->Type, si->Price[0], si->Price[1], si->Price[2], si->Item[0]);

		itemdata id = Game->LoadItemData(5);
		printf("TP: item 5: tilemod %d frames %d speed %d delay %d\n", id->TileMod, id->AFrames, id->ASpeed, id->Delay);
		id->TileMod = -260;
		id->AFrames = 3;
		id->ASpeed = 300;
		id->Delay = 2;
		printf("TP: item 5 written: tilemod %d frames %d speed %d delay %d\n", id->TileMod, id->AFrames, id->ASpeed, id->Delay);

		lweapon lw = Screen->CreateLWeapon(LW_SCRIPT1);
		printf("TP: lweapon: parent %d uid ok %d\n", lw->Parent, lw->UID > 0 ? 1 : 0);
		lw->Parent = 5;
		int p1 = lw->Parent;
		lw->Parent = 5000;
		printf("TP: lweapon parent written 5: %d, 5000: %d\n", p1, lw->Parent);
		lw->Remove();

		combodata cd = Game->LoadComboData(60020);
		printf("TP: user flags %d\n", cd->UserFlags);
		cd->UserFlags = 37;
		cd->UserFlags = cd->UserFlags | 1024;
		printf("TP: user flags written: %d flag 2 %d flag 10 %d\n", cd->UserFlags, cd->Flags[2] ? 1 : 0, cd->Flags[10] ? 1 : 0);
		printf("TP: DONE\n");
		while(true)
			Waitframe();
	}
}
