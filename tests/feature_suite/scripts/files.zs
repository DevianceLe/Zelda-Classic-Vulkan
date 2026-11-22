// File, directory and FileSystem commands.
// frames: 200
#include "std.zh"

void tryOpen(file f, char32[] path)
{
	bool ok = f->Open(path);
	printf("TF: open '%s' -> %d valid %d\n", path, ok ? 1 : 0, f->isValid() ? 1 : 0);
	if(ok) f->Close();
}

void tryCreate(file f, char32[] path)
{
	bool ok = f->Create(path);
	printf("TF: create '%s' -> %d exists %d\n", path, ok ? 1 : 0, FileSystem->FileExists(path) ? 1 : 0);
	if(ok)
	{
		f->Close();
		FileSystem->Remove(path);
	}
}

void showDir(char32[] path)
{
	directory d = FileSystem->LoadDirectory(path);
	printf("TF: dir '%s' size %d\n", path, d->Size);
	char32 name[64];
	int n = d->Size;
	for(int i = 0; i < n; ++i)
	{
		d->GetFilename(i, name);
		printf("TF:   %d '%s'\n", i, name);
	}
	strcpy(name, "untouched");
	printf("TF: dir get past end %d '%s'\n", d->GetFilename(n, name) ? 1 : 0, name);
	strcpy(name, "untouched");
	printf("TF: dir get -1 %d '%s'\n", d->GetFilename(-1, name) ? 1 : 0, name);
}

void exists(char32[] path)
{
	printf("TF: '%s' file %d dir %d\n", path, FileSystem->FileExists(path) ? 1 : 0, FileSystem->DirExists(path) ? 1 : 0);
}

void existChecks()
{
	exists("t1/a.txt");
	exists("t1");
	exists("t1/");
	exists("/t1");
	exists("t2/deep/er/b.txt");
	exists("t2\\deep");
	exists("t1/nope.txt");
	exists("");
	exists(".");
	exists("../zc.cfg");
	exists("zc.cfg");
	exists("Files");
	exists("Files/t/t1/a.txt");
	exists("Files/t/t1");
	exists("prog.exe");
	exists("t1/sub/../a.txt");
}

global script LREx1Ex2ItemSwitch
{
	void run()
	{
		char32 buf[64];
		int ints[8];

		// --- create, write, read back ---
		file f = new file("t1/a.txt", "w");
		printf("TF: new: valid %d allocated %d pos %l eof %d err %d\n", f->isValid() ? 1 : 0, f->isAllocated() ? 1 : 0, f->Pos, f->EOF, f->Error);
		printf("TF: WriteString %d\n", f->WriteString("hello world\n"));
		printf("TF: WriteChars all %d\n", f->WriteChars("second line\n"));
		printf("TF: WriteChars 3 from 2 %d\n", f->WriteChars("abcdefgh", 3, 2));
		printf("TF: PutChar %d\n", f->PutChar('!'));
		printf("TF: PutChar nl %d\n", f->PutChar('\n'));
		printf("TF: pos after writes %l\n", f->Pos);
		printf("TF: Flush %d\n", f->Flush() ? 1 : 0);
		f->Close();
		printf("TF: closed: valid %d allocated %d pos %l eof %d err %d\n", f->isValid() ? 1 : 0, f->isAllocated() ? 1 : 0, f->Pos, f->EOF, f->Error);
		printf("TF: write after close %d\n", f->WriteString("x"));

		printf("TF: FileExists %d DirExists(file) %d DirExists(t1) %d FileExists(dir) %d\n", FileSystem->FileExists("t1/a.txt") ? 1 : 0,
			FileSystem->DirExists("t1/a.txt") ? 1 : 0, FileSystem->DirExists("t1") ? 1 : 0, FileSystem->FileExists("t1") ? 1 : 0);
		printf("TF: missing: file %d dir %d\n", FileSystem->FileExists("t1/nope.txt") ? 1 : 0, FileSystem->DirExists("nope") ? 1 : 0);

		printf("TF: Open %d\n", f->Open("t1/a.txt") ? 1 : 0);
		printf("TF: ReadString %d '%s' pos %l\n", f->ReadString(buf), buf, f->Pos);
		printf("TF: GetChar %d %d\n", f->GetChar(), f->GetChar());
		printf("TF: UngetChar %d then GetChar %d\n", f->UngetChar('Z'), f->GetChar());
		memset(buf, 0, 64);
		printf("TF: ReadChars 4 %d '%s'\n", f->ReadChars(buf, 4), buf);
		memset(buf, 0, 64);
		printf("TF: ReadChars 3 at 5 %d [%d %d %d %d %d %d %d %d %d]\n", f->ReadChars(buf, 3, 5), buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
		memset(buf, 0, 64);
		printf("TF: ReadString %d '%s' eof %d\n", f->ReadString(buf), buf, f->EOF);
		memset(buf, 0, 64);
		printf("TF: ReadString %d '%s' eof %d\n", f->ReadString(buf), buf, f->EOF);
		memset(buf, 0, 64);
		printf("TF: ReadString at end %d '%s' eof %d err %d\n", f->ReadString(buf), buf, f->EOF, f->Error);
		printf("TF: GetChar at end %d eof %d\n", f->GetChar(), f->EOF);
		f->Rewind();
		printf("TF: Rewind pos %l eof %d\n", f->Pos, f->EOF);
		printf("TF: Seek 6 %d pos %l\n", f->Seek(6L) ? 1 : 0, f->Pos);
		printf("TF: Seek +3 from here %d pos %l\n", f->Seek(3L, true) ? 1 : 0, f->Pos);
		memset(buf, 0, 64);
		printf("TF: ReadChars rest %d '%s'\n", f->ReadChars(buf), buf);
		printf("TF: Seek -1 %d pos %l\n", f->Seek(-1L) ? 1 : 0, f->Pos);
		printf("TF: write to a file opened for reading %d err %d\n", f->WriteString("x"), f->Error != 0 ? 1 : 0);
		f->ClearError();
		printf("TF: after ClearError err %d eof %d\n", f->Error, f->EOF);
		f->GetError(buf);
		printf("TF: GetError text length %d\n", strlen(buf) > 0 ? 1 : 0);
		f->Close();

		// --- numbers ---
		ints[0] = 1; ints[1] = -2; ints[2] = 300; ints[3] = 70000; ints[4] = 1.5; ints[5] = 214747.9999;
		printf("TF: Create %d\n", f->Create("t1/n.bin") ? 1 : 0);
		printf("TF: WriteInts %d\n", f->WriteInts(ints, 6));
		printf("TF: WriteBytes 4 from 1 %d\n", f->WriteBytes(ints, 4, 1));
		printf("TF: pos %l\n", f->Pos);
		f->Close();
		printf("TF: OpenMode rb %d\n", f->OpenMode("t1/n.bin", "rb") ? 1 : 0);
		int back[8];
		printf("TF: ReadInts %d [%d %d %d %d %d %d %d %d]\n", f->ReadInts(back, 6), back[0], back[1], back[2], back[3], back[4], back[5], back[6], back[7]);
		int bytes[8];
		printf("TF: ReadBytes %d [%d %d %d %d %d %d]\n", f->ReadBytes(bytes, 6), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
		printf("TF: eof %d pos %l\n", f->EOF, f->Pos);
		f->Close();

		// --- modes ---
		printf("TF: OpenMode a %d\n", f->OpenMode("t1/a.txt", "a") ? 1 : 0);
		printf("TF: append %d pos %l\n", f->WriteString("tail\n"), f->Pos);
		f->Close();
		printf("TF: OpenMode r+ %d\n", f->OpenMode("t1/a.txt", "r+") ? 1 : 0);
		f->Seek(0L);
		printf("TF: overwrite %d\n", f->WriteChars("HELLO", 5));
		f->Rewind();
		memset(buf, 0, 64);
		printf("TF: ReadString %d '%s'\n", f->ReadString(buf), buf);
		f->Close();
		printf("TF: Open missing %d valid %d allocated %d\n", f->Open("t1/missing.txt") ? 1 : 0, f->isValid() ? 1 : 0, f->isAllocated() ? 1 : 0);
		printf("TF: Create in new folders %d\n", f->Create("t2/deep/er/b.txt") ? 1 : 0);
		f->WriteString("b");
		f->Close();
		printf("TF: Create truncates %d\n", f->Create("t1/a.txt") ? 1 : 0);
		f->Close();
		printf("TF: Open after truncate %d\n", f->Open("t1/a.txt") ? 1 : 0);
		memset(buf, 0, 64);
		printf("TF: ReadString %d '%s' eof %d\n", f->ReadString(buf), buf, f->EOF);
		f->Close();

		// --- the paths a script may use ---
		tryCreate(f, "plain.txt");
		tryCreate(f, "noext");
		tryCreate(f, "/leading/slash.txt");
		tryCreate(f, "\\back\\slash.txt");
		tryCreate(f, "a/./b/../c.txt");
		tryCreate(f, "../up.txt");
		tryCreate(f, "a/../../up2.txt");
		tryCreate(f, "..");
		tryCreate(f, ".");
		tryCreate(f, "");
		tryCreate(f, "sub/");
		tryCreate(f, "C:/abs.txt");
		tryCreate(f, "a:b.txt");
		tryCreate(f, "a<b.txt");
		tryCreate(f, "a>b.txt");
		tryCreate(f, "a|b.txt");
		tryCreate(f, "a?b.txt");
		tryCreate(f, "a*b.txt");
		tryCreate(f, "a&b.txt");
		tryCreate(f, "a^b.txt");
		tryCreate(f, "a$b.txt");
		tryCreate(f, "a#b.txt");
		tryCreate(f, "a\"b.txt");
		tryCreate(f, "tab\tname.txt");
		tryCreate(f, "prog.exe");
		tryCreate(f, "prog.bat");
		tryCreate(f, "prog.cmd");
		tryCreate(f, "prog.com");
		tryCreate(f, "prog.js");
		tryCreate(f, "prog.vbs");
		tryCreate(f, "prog.jar");
		tryCreate(f, "prog.ps1");
		tryCreate(f, "prog.exe.txt");
		tryCreate(f, "CON");
		tryCreate(f, "NUL.txt");
		tryCreate(f, "COM1");
		tryCreate(f, "LPT9.dat");
		tryCreate(f, "AUX");
		tryCreate(f, "PRN");
		tryCreate(f, "console.txt");
		tryCreate(f, "COM10.txt");
		tryCreate(f, "dot.");
		tryCreate(f, "space ");
		tryCreate(f, "with space.txt");
		tryCreate(f, "UPPER.TXT");
		tryOpen(f, "../up.txt");
		tryOpen(f, "prog.exe");
		tryOpen(f, "t2/deep/er/b.txt");

		// --- directories ---
		f->Create("t1/z.txt"); f->Close();
		f->Create("t1/m.txt"); f->Close();
		f->Create("t1/sub/inner.txt"); f->Close();
		// where FileExists and DirExists look depends on a quest rule
		printf("TF: rule %d\n", Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] ? 1 : 0);
		existChecks();
		Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] = true;
		printf("TF: rule %d\n", Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] ? 1 : 0);
		existChecks();
		Game->FFRules[qr_BITMAP_AND_FILESYSTEM_PATHS_ALWAYS_RELATIVE] = false;

		// the order of a listing
		f->Create("t3/b.txt"); f->Close();
		f->Create("t3/B2.txt"); f->Close();
		f->Create("t3/a10.txt"); f->Close();
		f->Create("t3/a9.txt"); f->Close();
		f->Create("t3/A1.txt"); f->Close();
		f->Create("t3/.hidden"); f->Close();
		f->Create("t3/_under.txt"); f->Close();
		f->Create("t3/zdir/x.txt"); f->Close();
		f->Create("t3/Adir/x.txt"); f->Close();
		f->Create("t3/dir10/x.txt"); f->Close();
		f->Create("t3/dir9/x.txt"); f->Close();
		f->Create("t3/noext"); f->Close();
		showDir("t3");
		showDir("t1");
		showDir("t1/");
		showDir("t2");
		showDir("");
		showDir("nope");
		showDir("../");
		showDir("t1/a.txt");
		directory d = FileSystem->LoadDirectory("t1");
		int before = d->Size;
		f->Create("t1/new.txt"); f->Close();
		printf("TF: listing is a snapshot: %d -> %d\n", before, d->Size);
		d->Reload();
		printf("TF: after Reload %d\n", d->Size);
		d->Free();
		printf("TF: freed directory size %d\n", d->Size);

		// --- removing ---
		printf("TF: FileSystem->Remove file %d then exists %d\n", FileSystem->Remove("t1/new.txt") ? 1 : 0, FileSystem->FileExists("t1/new.txt") ? 1 : 0);
		printf("TF: Remove missing %d\n", FileSystem->Remove("t1/new.txt") ? 1 : 0);
		printf("TF: Remove a folder with files %d still there %d\n", FileSystem->Remove("t1") ? 1 : 0, FileSystem->DirExists("t1") ? 1 : 0);
		f->Create("t1/gone.txt");
		f->WriteString("x");
		printf("TF: file->Remove %d valid %d exists %d\n", f->Remove() ? 1 : 0, f->isValid() ? 1 : 0, FileSystem->FileExists("t1/gone.txt") ? 1 : 0);
		printf("TF: file->Remove with nothing open %d\n", f->Remove() ? 1 : 0);

		// --- LoadFile, Free, a pointer never set ---
		file g = FileSystem->LoadFile("t1/m.txt", "w");
		printf("TF: LoadFile valid %d\n", g->isValid() ? 1 : 0);
		g->WriteString("m");
		g->Free();
		printf("TF: after Free valid %d allocated %d\n", g->isValid() ? 1 : 0, g->isAllocated() ? 1 : 0);
		file h = FileSystem->LoadFile("../x.txt", "w");
		printf("TF: LoadFile outside valid %d allocated %d\n", h->isValid() ? 1 : 0, h->isAllocated() ? 1 : 0);
		file h2 = FileSystem->LoadFile("t1/none.txt", "r");
		printf("TF: LoadFile missing valid %d allocated %d\n", h2->isValid() ? 1 : 0, h2->isAllocated() ? 1 : 0);
		file k;
		printf("TF: null file: valid %d allocated %d pos %l eof %d err %d\n", k->isValid() ? 1 : 0, k->isAllocated() ? 1 : 0, k->Pos, k->EOF, k->Error);
		printf("TF: null file Allocate %d allocated %d\n", k->Allocate() ? 1 : 0, k->isAllocated() ? 1 : 0);
		file k2;
		printf("TF: null file Open %d allocated %d valid %d\n", k2->Open("t1/m.txt") ? 1 : 0, k2->isAllocated() ? 1 : 0, k2->isValid() ? 1 : 0);
		memset(buf, 0, 64);
		printf("TF: read %d '%s'\n", k2->ReadString(buf), buf);
		k2->Close();

		// --- how many files can be open ---
		file many[300];
		int opened = 0;
		for(int i = 0; i < 300; ++i)
		{
			many[i] = new file("t1/m.txt", "r");
			if(many[i]->isValid()) ++opened;
		}
		printf("TF: files open at once %d\n", opened);
		for(int i = 0; i < 300; ++i)
			many[i]->Free();
		file again = new file("t1/m.txt", "r");
		printf("TF: after freeing them, one more: valid %d\n", again->isValid() ? 1 : 0);
		again->Free();

		printf("TF: DONE\n");
		while(true) Waitframe();
	}
}
