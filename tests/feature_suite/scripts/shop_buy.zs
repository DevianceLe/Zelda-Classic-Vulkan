// r12shopA: shop 0 gets three wares before the merchant sets the shop up (shopdata->Item/HasItem/Price),
// then the rupees, Link's screen and position and the items out are printed every 10 frames.
global script Active
{
	void run()
	{
		shopdata sd = Game->LoadShopData(0);
		for(int i = 0; i < 3; ++i)
		{
			sd->Item[i] = 30 + i;
			sd->HasItem[i] = true;
			sd->Price[i] = 10 * (i + 1);
		}
		Game->Counter[CR_RUPEES] = 100;
		int f = 0;
		while(true)
		{
			++f;
			if(f % 10 == 0)
			{
				printf("TB: %d: rupees %d link %d,%d items %d have30-32 %d %d %d\n", f, Game->Counter[CR_RUPEES], Hero->X, Hero->Y,
					Screen->NumItems(), Hero->Item[30] ? 1 : 0, Hero->Item[31] ? 1 : 0, Hero->Item[32] ? 1 : 0);
				for(int i = 1; i <= Screen->NumItems(); ++i)
				{
					item it = Screen->LoadItem(i);
					printf("TB: %d: item %d at %d,%d\n", f, it->ID, it->X, it->Y);
				}
			}
			Waitframe();
		}
	}
}
