// r19view: Viewport-> on a lone screen (not in a scrolling region; upstream's 1x1 region): Mode 1
// keeps the view at 0, 0 there; Mode 2 moves it right/down and back past where it began, then
// places it more than a screen away (all but nothing in view) and left/below; Mode 0 back to 0, 0.
#include "std.zh"

global script ViewportLone
{
	void run()
	{
		for(int f = 0; f < 60; ++f) Waitframe();
		printf("TV: start %d %d %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Width, Viewport->Height, Viewport->Mode);
		Viewport->Mode = VIEW_MODE_CENTER;
		Waitframe();
		printf("TV: centred %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		Viewport->Mode = VIEW_MODE_SCRIPT;
		for(int i = 0; i < 40; ++i)
		{
			Viewport->X += 3;
			Viewport->Y += 2;
			Waitframe();
		}
		printf("TV: moved %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		for(int i = 0; i < 40; ++i)
		{
			Viewport->X -= 6;
			Viewport->Y -= 4;
			Waitframe();
		}
		printf("TV: moved back %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 20; ++i) Waitframe();
		Viewport->X = 300;
		Viewport->Y = 200;
		Waitframe();
		printf("TV: far %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 30; ++i) Waitframe();
		Viewport->X = -100;
		Viewport->Y = 50;
		Waitframe();
		printf("TV: left %d %d\n", Viewport->X, Viewport->Y);
		for(int i = 0; i < 30; ++i) Waitframe();
		Viewport->Mode = VIEW_MODE_CENTER_AND_BOUND;
		Waitframe();
		printf("TV: bound %d %d mode %d\n", Viewport->X, Viewport->Y, Viewport->Mode);
		while(true) Waitframe();
	}
}
