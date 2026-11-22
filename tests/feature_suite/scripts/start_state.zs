// What a new game starts with in the z3 quest's init data, as scripts see it: life and magic (3.0's
// HP per heart), the counters. (The hero tests set life to 48 themselves, so this shows any
// difference once.)
// frames: 20
#include "std.zh"

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		printf("TI: life %d of %d, magic %d of %d\n", Hero->HP, Hero->MaxHP, Hero->MP, Hero->MaxMP);
		printf("TI: rupees %d bombs %d arrows %d keys %d\n", Game->Counter[CR_RUPEES], Game->Counter[CR_BOMBS], Game->Counter[CR_ARROWS], Game->Counter[CR_KEYS]);
		printf("TI: max rupees %d bombs %d arrows %d\n", Game->MCounter[CR_RUPEES], Game->MCounter[CR_BOMBS], Game->MCounter[CR_ARROWS]);
		while(true)
			Waitframe();
	}
}
