// r16view / r19view: Viewport-> from a script inside the z3 3x3 region (Link on its middle screen 0x39):
// Width/Height, Mode 2 moving the view right/down then back left/up past where
// it began, Mode 1, Mode 0, a dead zone override and ResetFollowSettings, then
// the view placed more than a screen past Link's screen (beyond the region's
// bottom-right, then beyond its top-left).
#include "std.zh"

global script ViewportMove
{
	void run()
	{
		for(int f = 0; f < 60; ++f) Waitframe();
		printf("TV: start %d %d %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Width, Viewport->Height, Viewport->Mode);
		Waitframe();
		Viewport->Mode = VIEW_MODE_SCRIPT;
		for(int i = 0; i < 60; ++i)
		{
			Viewport->X += 2;
			Viewport->Y += 1;
			Waitframe();
		}
		printf("TV: moved %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		for(int i = 0; i < 60; ++i)
		{
			Viewport->X -= 4;
			Viewport->Y -= 2;
			Waitframe();
		}
		printf("TV: moved back %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 20; ++i) Waitframe();
		Viewport->Mode = VIEW_MODE_CENTER;
		Waitframe();
		printf("TV: centred %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		Viewport->Mode = VIEW_MODE_CENTER_AND_BOUND;
		Waitframe();
		printf("TV: bound %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		Viewport->DeadzoneWidth = 64;
		Viewport->LookaheadX = 20;
		printf("TV: overrides %d %d\n", Viewport->DeadzoneWidth, Viewport->LookaheadX);
		Viewport->ResetFollowSettings();
		printf("TV: reset %d %d\n", Viewport->DeadzoneWidth, Viewport->LookaheadX);
		Viewport->Mode = VIEW_MODE_SCRIPT;
		Viewport->X = 300;
		Viewport->Y = 200;
		Waitframe();
		printf("TV: placed %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 40; ++i) Waitframe();
		Viewport->X = 650;
		Viewport->Y = 420;
		Waitframe();
		printf("TV: far %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 40; ++i) Waitframe();
		Viewport->X = -280;
		Viewport->Y = -200;
		Waitframe();
		printf("TV: before %d %d\n", Viewport->X, Viewport->Y);
		while(true) Waitframe();
	}
}
