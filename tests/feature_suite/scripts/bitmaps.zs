// bitmap->Write() and ->Read() (image files), combodata->Script / InitD and dmapdata->InitD
// written by a script, Game->Generic[] for the values 2.53 has no room for.
// frames: 200
// snap: 150
#include "std.zh"

void px(char32[] what, bitmap b)
{
	printf("TB: %s: valid %d size %dx%d px %d %d %d %d %d\n", what, b->isValid() ? 1 : 0, b->Width, b->Height,
		b->GetPixel(0, 0), b->GetPixel(5, 5), b->GetPixel(20, 10), b->GetPixel(40, 30), b->GetPixel(63, 47));
}

void exists(char32[] path)
{
	printf("TB: exists '%s' %d\n", path, FileSystem->FileExists(path) ? 1 : 0);
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		printf("TB: rule %d\n", Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] ? 1 : 0);
		Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] = false;
		bitmap b = Game->CreateBitmap(64, 48);
		b->Clear(0);
		b->Rectangle(0, 0, 0, 63, 47, 0x21, 1, 0, 0, 0, true, OP_OPAQUE);
		b->Rectangle(0, 4, 4, 30, 20, 0x45, 1, 0, 0, 0, true, OP_OPAQUE);
		b->Rectangle(0, 32, 24, 60, 44, 0x73, 1, 0, 0, 0, true, OP_OPAQUE);
		b->PutPixel(0, 63, 47, 0x0F, 0, 0, 0, OP_OPAQUE);
		Waitframe();
		px("drawn", b);

		b->Write(0, "Files/t/b1.png");
		b->Write(0, "Files/t/b1.bmp", true);
		b->Write(0, "Files/t/b1.txt", true);
		Waitframe();
		exists("Files/t/b1.png");
		exists("Files/t/b1.bmp");
		exists("Files/t/b1.txt");

		// an existing file is kept without allow_overwrite, replaced with it
		b->Rectangle(0, 0, 0, 10, 10, 0x05, 1, 0, 0, 0, true, OP_OPAQUE);
		b->Write(0, "Files/t/b1.png");
		Waitframe();
		bitmap c;
		c->Read(0, "Files/t/b1.png");
		Waitframe();
		px("read back, not overwritten", c);
		b->Write(0, "Files/t/b1.png", true);
		Waitframe();
		c->Read(0, "Files/t/b1.png");
		Waitframe();
		px("read back, overwritten", c);

		bitmap d = Game->CreateBitmap(16, 16);
		d->Read(0, "Files/t/b1.bmp");
		Waitframe();
		px("bmp read into a made bitmap", d);

		bitmap e = Game->CreateBitmap(16, 16);
		e->Read(0, "Files/t/missing.png");
		Waitframe();
		px("missing file", e);

		// combodata->Script / InitD, dmapdata->InitD
		combodata cd = Game->LoadComboData(60020);
		printf("TB: combo script %d initd %d\n", cd->Script, cd->InitD[2]);
		cd->Script = 3;
		cd->InitD[2] = 7;
		printf("TB: combo script %d initd %d\n", cd->Script, cd->InitD[2]);
		cd->Script = 0;
		printf("TB: combo script %d\n", cd->Script);

		// Game->Generic[]: written, read back
		printf("TB: generic light rad %d dark perc %d bunny %d wave %d %d\n", Game->Generic[GEN_DEFAULT_LIGHT_RAD], Game->Generic[GEN_TRANS_PERCENT],
			Game->Generic[GEN_BUNNY_TILEMOD], Game->Generic[GEN_LIGHT_WAVE_RATE], Game->Generic[GEN_LIGHT_WAVE_SIZE]);
		Game->Generic[GEN_DEFAULT_LIGHT_RAD] = 40;
		Game->Generic[GEN_BUNNY_TILEMOD] = 3;
		printf("TB: generic light rad %d bunny %d\n", Game->Generic[GEN_DEFAULT_LIGHT_RAD], Game->Generic[GEN_BUNNY_TILEMOD]);

		// the image read back, on screen
		for(int i = 0; i < 40; ++i)
		{
			Screen->Rectangle(6, 0, 0, 255, 175, 0x01, 1, 0, 0, 0, true, OP_OPAQUE);
			c->Blit(6, RT_SCREEN, 0, 0, 64, 48, 16, 16, 64, 48, 0, 0, 0, 0, 0, true);
			d->Blit(6, RT_SCREEN, 0, 0, 64, 48, 96, 16, 64, 48, 0, 0, 0, 0, 0, true);
			e->Blit(6, RT_SCREEN, 0, 0, 64, 48, 176, 16, 64, 48, 0, 0, 0, 0, 0, true);
			Waitframe();
		}
		printf("TB: DONE\n");
		while(true)
		{
			Screen->Rectangle(6, 0, 0, 255, 175, 0x01, 1, 0, 0, 0, true, OP_OPAQUE);
			c->Blit(6, RT_SCREEN, 0, 0, 64, 48, 16, 16, 64, 48, 0, 0, 0, 0, 0, true);
			d->Blit(6, RT_SCREEN, 0, 0, 64, 48, 96, 16, 64, 48, 0, 0, 0, 0, 0, true);
			e->Blit(6, RT_SCREEN, 0, 0, 64, 48, 176, 16, 64, 48, 0, 0, 0, 0, 0, true);
			Waitframe();
		}
	}
}
