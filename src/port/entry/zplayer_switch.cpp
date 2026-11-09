// zplayer on Nintendo Switch homebrew: the process entry point (libnx, instead
// of src/port/entry/zplayer_main.cpp, which needs SDL_main).
//
// The player opens zelda.dat, zc.cfg, zc.sav, soundfont.sf2 ... relative to the
// working directory, which is always sdmc:/switch/zplayer/ here. stderr goes to
// zplayer.log there.
//
// Test aid: "args.txt" in that folder is added to the command line, one
// whitespace-separated word at a time (double quotes keep spaces). The homebrew
// loader passes none, and an emulator cannot be sent input without taking the
// desktop's focus, so this is how -replay, -framehash and -quest-tour get run.
//
// Written from scratch for zcport.

#if defined(__SWITCH__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <switch.h>

#include "zcport/entry.h"

namespace
{

std::vector<std::string> read_extra_args()
{
    std::vector<std::string> out;
    FILE *f = fopen("args.txt", "r");
    if (!f)
        return out;
    std::string word;
    bool in_word = false, quoted = false;
    for (int c = fgetc(f); ; c = fgetc(f))
    {
        const bool end = c == EOF;
        if (!end && c == '"')
        {
            quoted = !quoted;
            in_word = true;
        }
        else if (end || (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')))
        {
            if (in_word)
                out.push_back(word);
            word.clear();
            in_word = false;
            if (end)
                break;
        }
        else
        {
            word += (char)c;
            in_word = true;
        }
    }
    fclose(f);
    return out;
}

// Exit is held back until the game has saved (see the applet's exit lock).
void unlock_exit()
{
    appletUnlockExit();
}

} // namespace

int main(int argc, char *argv[])
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/zplayer", 0777);
    if (chdir("sdmc:/switch/zplayer/") != 0)
        return 1;

    if (freopen("zplayer.log", "w", stderr))
        setvbuf(stderr, NULL, _IOLBF, 0);

    appletLockExit();
    atexit(unlock_exit);

    static std::vector<std::string> extra = read_extra_args();
    static std::vector<char *> args;
    for (int i = 0; i < argc; i++)
        args.push_back(argv[i]);
    if (args.empty())
        args.push_back((char *)"sdmc:/switch/zplayer/zplayer.nro");
    for (size_t i = 0; i < extra.size(); i++)
    {
        fprintf(stderr, "zplayer: argument from args.txt: %s\n", extra[i].c_str());
        args.push_back(&extra[i][0]);
    }
    const int n = (int)args.size();
    args.push_back(NULL);

    zcport_sdl_register();
    return zcport_game_main(n, &args[0]);
}

#endif // __SWITCH__
