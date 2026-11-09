// zplayer on Android: the game data and the working directory.
//
// The player opens zelda.dat, zc.cfg, zc.sav, quests ... relative to its
// working directory, like on Windows. On Android the data ships inside the
// APK as assets/data/... (android/app/build.gradle copies it there and
// writes assets/data/files.txt), so before the game starts it is copied to
// the app's internal storage, which then becomes the working directory.
//
// - Game data (.dat, .qst, .nsf) is copied when missing or when the APK's
//   copy has a different size (a newer APK).
// - zc.cfg is copied only when missing: the player's own settings stay.
// - Saves (zc.sav ...) are never touched.
//
// Written from scratch for zcport.

#if defined(__ANDROID__)

#include <SDL3/SDL.h>

#include <errno.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

bool file_size(const std::string &path, long long *size)
{
    struct stat st;
    if(stat(path.c_str(), &st) != 0)
        return false;
    *size = (long long)st.st_size;
    return true;
}

bool copy_asset(const std::string &asset, const std::string &dest)
{
    size_t len = 0;
    void *data = SDL_LoadFile(asset.c_str(), &len);   // relative paths read the APK's assets
    if(!data)
    {
        SDL_Log("zplayer: cannot read asset %s: %s", asset.c_str(), SDL_GetError());
        return false;
    }
    std::string tmp = dest + ".part";
    bool ok = SDL_SaveFile(tmp.c_str(), data, len);
    SDL_free(data);
    if(ok)
        ok = rename(tmp.c_str(), dest.c_str()) == 0;
    if(!ok)
        SDL_Log("zplayer: cannot write %s", dest.c_str());
    return ok;
}

} // namespace

extern "C" int zplayer_android_prepare_data(void)
{
    const char *base = SDL_GetAndroidInternalStoragePath();
    if(!base)
    {
        SDL_Log("zplayer: no internal storage: %s", SDL_GetError());
        return -1;
    }
    std::string dir = std::string(base) + "/game";
    if(mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
    {
        SDL_Log("zplayer: cannot create %s", dir.c_str());
        return -1;
    }

    size_t list_len = 0;
    char *list = (char *)SDL_LoadFile("data/files.txt", &list_len);
    if(!list)
        SDL_Log("zplayer: the APK has no game data (data/files.txt): %s", SDL_GetError());
    else
    {
        std::string names(list, list_len);
        SDL_free(list);
        size_t pos = 0;
        while(pos < names.size())
        {
            size_t end = names.find('\n', pos);
            if(end == std::string::npos)
                end = names.size();
            std::string name = names.substr(pos, end - pos);
            pos = end + 1;
            if(!name.empty() && name[name.size() - 1] == '\r')
                name.erase(name.size() - 1);
            if(name.empty() || name.find('/') != std::string::npos || name.find("..") != std::string::npos)
                continue;

            std::string asset = "data/" + name;
            std::string dest = dir + "/" + name;
            long long have = 0;
            bool exists = file_size(dest, &have);
            bool settings = (name == "zc.cfg");
            if(exists && settings)
                continue;
            if(exists)
            {
                SDL_IOStream *io = SDL_IOFromFile(asset.c_str(), "rb");
                long long want = io ? (long long)SDL_GetIOSize(io) : -1;
                if(io)
                    SDL_CloseIO(io);
                if(want == have)
                    continue;
            }
            SDL_Log("zplayer: installing %s", name.c_str());
            copy_asset(asset, dest);
        }
    }

    if(chdir(dir.c_str()) != 0)
    {
        SDL_Log("zplayer: cannot enter %s", dir.c_str());
        return -1;
    }
    return 0;
}

#endif // __ANDROID__
