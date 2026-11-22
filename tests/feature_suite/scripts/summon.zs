// r12sum2: the Rock Shooter (155) on 0x3A summons one Octorok (20) at script frames 130 and 230;
// every npc's position is printed every 10 frames and right after each summon.
global script Active
{
	void run()
	{
		int f = 0;
		npc sm = NULL;
		while(true)
		{
			++f;
			if(!sm || !sm->isValid())
			{
				for(int i = Screen->NumNPCs(); i > 0; --i)
				{
					npc n = Screen->LoadNPC(i);
					if(n->ID == 155)
						sm = n;
				}
			}
			if(sm && sm->isValid() && (f == 130 || f == 230))
			{
				npc m = sm->SummonMinion(20, 41);
				printf("TS: %d: summoned %d\n", f, m ? 1 : 0);
			}
			if(f % 10 == 0 || (f >= 130 && f <= 142) || (f >= 230 && f <= 242))
			{
				for(int i = 1; i <= Screen->NumNPCs(); ++i)
				{
					npc n = Screen->LoadNPC(i);
					printf("TS: %d: npc %d at %d,%d\n", f, n->ID, n->X, n->Y);
				}
			}
			Waitframe();
		}
	}
}
