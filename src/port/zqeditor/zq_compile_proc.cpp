// Runs a ZScript compiler program for zq_compile.cpp, talking upstream's pipe protocol over the
// child's stdin and stdout (as upstream's ZQuest does with its "zscript", launch_piped_process).
// Kept apart from the rest of ZQuest because it needs the system headers, whose names clash
// with ZQuest's own.
//
// The protocol (both compilers, binary, native byte order): the child writes an int32 when it
// starts; for each message an int32 code (-9998 info, -9997 error, -9996 warning, -9995 debug),
// after which it waits for an int32 back (the message text is in its -console file); while
// busy an int32 -9993 and a uint32 of seconds it needs (also answered); at the end an int32
// -9994 and the int32 result.
//
// Written for the port.

#include "zq_compile_proc.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

const int CODE_TERM = -9994;
const int CODE_IDLE = -9993;

#ifdef _WIN32

std::wstring widen(const std::string &s)
{
    // ZQuest's narrow strings are in the process code page (UTF-8 under a UTF-8 manifest).
    if(s.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// One argument quoted the way the C runtime splits a command line.
void append_arg(std::wstring &cmd, const std::wstring &a)
{
    if(!cmd.empty())
        cmd += L' ';

    if(!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
    {
        cmd += a;
        return;
    }

    cmd += L'"';
    size_t slashes = 0;

    for(size_t i = 0; i < a.size(); ++i)
    {
        if(a[i] == L'\\')
        {
            ++slashes;
            continue;
        }

        if(a[i] == L'"')
            cmd.append(slashes * 2 + 1, L'\\');
        else
            cmd.append(slashes, L'\\');

        slashes = 0;
        cmd += a[i];
    }

    cmd.append(slashes * 2, L'\\');
    cmd += L'"';
}

struct Child
{
    HANDLE process, in_w, out_r;

    Child() : process(NULL), in_w(NULL), out_r(NULL) {}
    ~Child() { close(); }

    void close()
    {
        if(in_w) CloseHandle(in_w);
        if(out_r) CloseHandle(out_r);
        if(process) CloseHandle(process);
        in_w = out_r = process = NULL;
    }

    bool start(const std::string &exe, const std::vector<std::string> &args, const std::string &dir, std::string &why)
    {
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE in_r = NULL, out_w = NULL;

        if(!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0))
        {
            why = "the pipes to the compiler couldn't be made";
            if(in_r) CloseHandle(in_r);
            return false;
        }

        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

        std::wstring cmd;
        append_arg(cmd, widen(exe));
        for(size_t i = 0; i < args.size(); ++i)
            append_arg(cmd, widen(args[i]));

        STARTUPINFOW si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_r;
        si.hStdOutput = out_w;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        std::wstring wdir = widen(dir);

        BOOL ok = CreateProcessW(NULL, &buf[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                                 wdir.empty() ? NULL : wdir.c_str(), &si, &pi);
        CloseHandle(in_r);
        CloseHandle(out_w);

        if(!ok)
        {
            why = "the compiler couldn't be started (" + exe + ")";
            return false;
        }

        CloseHandle(pi.hThread);
        process = pi.hProcess;
        return true;
    }

    // Reads exactly n bytes. 0: done; 1: the child ended or the pipe broke; 2: timed out or
    // cancelled.
    int read(void *buf, size_t n, std::chrono::steady_clock::time_point deadline, volatile bool *cancel)
    {
        size_t got = 0;

        while(got < n)
        {
            DWORD avail = 0;

            if(!PeekNamedPipe(out_r, NULL, 0, NULL, &avail, NULL))
                return 1;

            if(avail == 0)
            {
                if((cancel && *cancel) || std::chrono::steady_clock::now() > deadline)
                    return 2;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            DWORD want = (DWORD)(n - got), r = 0;
            if(want > avail)
                want = avail;

            if(!ReadFile(out_r, (char *)buf + got, want, &r, NULL) || r == 0)
                return 1;
            got += r;
        }

        return 0;
    }

    bool write(const void *buf, size_t n)
    {
        DWORD w = 0;
        return WriteFile(in_w, buf, (DWORD)n, &w, NULL) && w == n;
    }

    void kill()
    {
        if(process)
            TerminateProcess(process, 1);
    }

    int wait_exit()
    {
        if(!process)
            return -1;
        WaitForSingleObject(process, 5000);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        return (int)code;
    }
};

#else

struct Child
{
    pid_t pid;
    int in_w, out_r;

    Child() : pid(-1), in_w(-1), out_r(-1) {}
    ~Child() { close(); }

    void close()
    {
        if(in_w >= 0) ::close(in_w);
        if(out_r >= 0) ::close(out_r);
        in_w = out_r = -1;
    }

    bool start(const std::string &exe, const std::vector<std::string> &args, const std::string &dir, std::string &why)
    {
        int in_p[2], out_p[2];

        if(pipe(in_p) != 0)
        {
            why = "the pipes to the compiler couldn't be made";
            return false;
        }

        if(pipe(out_p) != 0)
        {
            ::close(in_p[0]);
            ::close(in_p[1]);
            why = "the pipes to the compiler couldn't be made";
            return false;
        }

        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(exe.c_str()));
        for(size_t i = 0; i < args.size(); ++i)
            argv.push_back(const_cast<char *>(args[i].c_str()));
        argv.push_back(NULL);

        pid = fork();

        if(pid == 0)
        {
            dup2(in_p[0], 0);
            dup2(out_p[1], 1);
            ::close(in_p[0]);
            ::close(in_p[1]);
            ::close(out_p[0]);
            ::close(out_p[1]);
            if(!dir.empty() && chdir(dir.c_str()) != 0)
                _exit(127);
            execv(exe.c_str(), &argv[0]);
            _exit(127);
        }

        ::close(in_p[0]);
        ::close(out_p[1]);

        if(pid < 0)
        {
            ::close(in_p[1]);
            ::close(out_p[0]);
            why = "the compiler couldn't be started (" + exe + ")";
            return false;
        }

        in_w = in_p[1];
        out_r = out_p[0];
        signal(SIGPIPE, SIG_IGN);
        return true;
    }

    int read(void *buf, size_t n, std::chrono::steady_clock::time_point deadline, volatile bool *cancel)
    {
        size_t got = 0;

        while(got < n)
        {
            if((cancel && *cancel) || std::chrono::steady_clock::now() > deadline)
                return 2;

            struct pollfd p;
            p.fd = out_r;
            p.events = POLLIN;
            p.revents = 0;
            int r = poll(&p, 1, 20);

            if(r < 0 && errno == EINTR)
                continue;
            if(r <= 0)
                continue;

            ssize_t k = ::read(out_r, (char *)buf + got, n - got);

            if(k < 0 && errno == EINTR)
                continue;
            if(k <= 0)
                return 1;
            got += (size_t)k;
        }

        return 0;
    }

    bool write(const void *buf, size_t n)
    {
        size_t done = 0;

        while(done < n)
        {
            ssize_t k = ::write(in_w, (const char *)buf + done, n - done);
            if(k < 0 && errno == EINTR)
                continue;
            if(k <= 0)
                return false;
            done += (size_t)k;
        }

        return true;
    }

    void kill()
    {
        if(pid > 0)
            ::kill(pid, SIGKILL);
    }

    int wait_exit()
    {
        if(pid <= 0)
            return -1;
        int st = 0;
        for(int i = 0; i < 500; ++i)
        {
            pid_t r = waitpid(pid, &st, WNOHANG);
            if(r == pid)
                return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            if(r < 0)
                return -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return -1;
    }
};

#endif

} // namespace

int zq_run_linked_compiler(const std::string &exe, const std::vector<std::string> &args, const std::string &dir,
                           int timeout_s, volatile bool *cancel, void (*on_message)(int code, void *user), void *user,
                           std::string &why)
{
    Child c;

    if(!c.start(exe, args, dir, why))
        return ZQ_PROC_NOT_RUN;

    // "timeout_s" for each step, plus whatever the compiler says it needs (its idle message).
    long extra = 0;
    std::chrono::steady_clock::time_point deadline;
    auto rearm = [&]()
    {
        deadline = timeout_s > 0 ? std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s + extra)
                                 : std::chrono::steady_clock::time_point::max();
        extra = 0;
    };

    int32_t code = 0;
    rearm();
    int r = c.read(&code, 4, deadline, cancel);

    while(r == 0)
    {
        rearm();
        r = c.read(&code, 4, deadline, cancel);

        if(r)
            break;

        if(code == CODE_IDLE)
        {
            uint32_t secs = 0;
            r = c.read(&secs, 4, deadline, cancel);
            if(r)
                break;
            extra = (long)secs;
            if(!c.write(&code, 4))
            {
                r = 1;
                break;
            }
            continue;
        }

        if(code == CODE_TERM)
        {
            int32_t result = 0;
            r = c.read(&result, 4, deadline, cancel);
            if(r)
                break;
            c.wait_exit();
            return result;
        }

        if(code >= -9998 && code <= -9995)
        {
            if(on_message)
                on_message(code, user);
            if(!c.write(&code, 4))
            {
                r = 1;
                break;
            }
            continue;
        }

        // anything else: not the protocol
        r = 1;
        break;
    }

    if(r == 2)
    {
        c.kill();
        c.wait_exit();
        why = (cancel && *cancel) ? "the compile was cancelled" : "the compiler timed out";
        return ZQ_PROC_TIMEOUT;
    }

    c.kill();
    int exit_code = c.wait_exit();
    why = "the compiler stopped unexpectedly (exit code " + std::to_string(exit_code) + ")";
    return ZQ_PROC_DIED;
}

std::string zq_program_dir()
{
    const char *base = SDL_GetBasePath();
    return base ? std::string(base) : std::string();
}

std::string zq_temp_dir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 2];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    if(n == 0 || n > MAX_PATH)
        return std::string();
    int len = WideCharToMultiByte(CP_ACP, 0, buf, (int)n, NULL, 0, NULL, NULL);
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_ACP, 0, buf, (int)n, &out[0], len, NULL, NULL);
    return out;
#else
    const char *t = getenv("TMPDIR");
    std::string d = (t && *t) ? t : "/tmp";
    if(d[d.size() - 1] != '/')
        d += '/';
    return d;
#endif
}

long zq_process_id()
{
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}
