// ConsoleLogger.h: declaration of the CConsoleLogger / CConsoleLoggerEx classes.
//
// The 2.53.0 Release 3 source tarball ships the implementation of these classes
// inline in ffscript.cpp, but not this header. This declaration was rebuilt from
// that implementation and from the call sites in ffscript.cpp and zelda.cpp.
//
// The classes send text over a named pipe to an external helper process
// (ZConsole.exe / ZConsole_OnTop.exe, shipped with the binary package, not with
// the source). They are only used when the ZScript debugger is enabled.
//
// Windows only: on other platforms this header declares nothing, and every
// call site is already wrapped in #ifdef _WIN32.

#ifndef ZC_CONSOLELOGGER_H
#define ZC_CONSOLELOGGER_H

#ifdef _WIN32

#include "zc_alleg.h" // pulls in <winalleg.h>, i.e. windows.h without the BITMAP clash
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

class CConsoleLogger
{
public:
    CConsoleLogger();
    virtual ~CConsoleLogger();

    // Create a new console window backed by a helper process.
    // All parameters are optional; see ffscript.cpp for details.
    long Create(const char *lpszWindowTitle = NULL,
                int buffer_size_x = -1, int buffer_size_y = -1,
                const char *logger_name = NULL,
                const char *helper_executable = NULL);

    // Disconnect the pipe from the helper process.
    long Close(void);

    // Print an unformatted string; iSize == -1 means strlen(lpszText).
    // Defined inline in ffscript.cpp, so it is only callable from there.
    int print(const char *lpszText, int iSize = -1);

    // Print a formatted string (at most 1023 characters).
    int printf(const char *format, ...);

    // Redirect / restore the CRT stdout.
    int SetAsDefaultOutput(void);
    int ResetDefaultOutput(void);

protected:
    char             m_name[MAX_PATH]; // "\\.\pipe\" + logger name
    HANDLE           m_hPipe;
    CRITICAL_SECTION m_cs;

    // Thin wrappers so the implementation can call these without arguments.
    void InitializeCriticalSection(void) { ::InitializeCriticalSection(&m_cs); }
    void DeleteCriticalSection(void)     { ::DeleteCriticalSection(&m_cs); }
    void EnterCriticalSection(void)      { ::EnterCriticalSection(&m_cs); }
    void LeaveCriticalSection(void)      { ::LeaveCriticalSection(&m_cs); }

    // WriteFile() guarded by the object's critical section.
    BOOL SafeWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                       LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
    {
        EnterCriticalSection();
        BOOL ret = ::WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite,
                               lpNumberOfBytesWritten, lpOverlapped);
        LeaveCriticalSection();
        return ret;
    }

    // Hook for subclasses to send extra "NAME: value\r\n" headers during Create().
    // Return non-zero to abort Create().
    virtual int AddHeaders(void) { return 0; }

    // Low-level output used by print()/printf().
    virtual int _print(const char *lpszText, int iSize);
};

class CConsoleLoggerEx : public CConsoleLogger
{
public:
    // Text attributes, matching the Win32 console attribute bits
    // (FOREGROUND_* / BACKGROUND_*), which is what the helper process applies.
    enum enumColors
    {
        COLOR_BLACK                = 0x0000,
        COLOR_BLUE                 = 0x0001,
        COLOR_GREEN                = 0x0002,
        COLOR_RED                  = 0x0004,
        COLOR_WHITE                = COLOR_RED | COLOR_GREEN | COLOR_BLUE,
        COLOR_INTENSITY            = 0x0008,
        COLOR_BACKGROUND_BLACK     = 0x0000,
        COLOR_BACKGROUND_BLUE      = 0x0010,
        COLOR_BACKGROUND_GREEN     = 0x0020,
        COLOR_BACKGROUND_RED       = 0x0040,
        COLOR_BACKGROUND_WHITE     = COLOR_BACKGROUND_RED | COLOR_BACKGROUND_GREEN | COLOR_BACKGROUND_BLUE,
        COLOR_BACKGROUND_INTENSITY = 0x0080
    };

    // Pipe protocol commands, sent in the top byte of a DWORD.
    // Reconstructed: the order must match what the external helper expects.
    enum enumCommands
    {
        COMMAND_PRINT,
        COMMAND_CPRINT,
        COMMAND_CLEAR_EOL,
        COMMAND_COLORED_CLEAR_EOL,
        COMMAND_GOTOXY,
        COMMAND_CLEAR_SCREEN,
        COMMAND_COLORED_CLEAR_SCREEN
    };

    CConsoleLoggerEx();

    // Formatted output with explicit attributes, or with the current attributes.
    int cprintf(int attributes, const char *format, ...);
    int cprintf(const char *format, ...);

    void cls(void);
    void cls(DWORD color);
    void clear_eol(void);
    void clear_eol(DWORD color);
    void gotoxy(int x, int y);

protected:
    DWORD m_dwCurrentAttributes;

    virtual int _print(const char *lpszText, int iSize);
    int _cprint(int attributes, const char *lpszText, int iSize);
};

#else // !_WIN32

// Other systems (zplayer on Linux/Android): there is no console helper
// process. The game only writes to the ZScript debug console under _WIN32,
// but declares the object everywhere, so a do-nothing class stands in.
class CConsoleLoggerEx
{
public:
    enum enumColors
    {
        COLOR_BLACK                = 0x0000,
        COLOR_BLUE                 = 0x0001,
        COLOR_GREEN                = 0x0002,
        COLOR_RED                  = 0x0004,
        COLOR_WHITE                = COLOR_RED | COLOR_GREEN | COLOR_BLUE,
        COLOR_INTENSITY            = 0x0008,
        COLOR_BACKGROUND_BLACK     = 0x0000,
        COLOR_BACKGROUND_BLUE      = 0x0010,
        COLOR_BACKGROUND_GREEN     = 0x0020,
        COLOR_BACKGROUND_RED       = 0x0040,
        COLOR_BACKGROUND_WHITE     = COLOR_BACKGROUND_RED | COLOR_BACKGROUND_GREEN | COLOR_BACKGROUND_BLUE,
        COLOR_BACKGROUND_INTENSITY = 0x0080
    };

    int Create(const char *, int = -1, int = -1, const char * = 0, const char * = 0) { return -1; }
    int Close(void) { return 0; }
    int printf(const char *, ...) { return 0; }
    int cprintf(int, const char *, ...) { return 0; }
    int cprintf(const char *, ...) { return 0; }
    void cls(void) {}
    void cls(unsigned long) {}
    void clear_eol(void) {}
    void clear_eol(unsigned long) {}
    void gotoxy(int, int) {}
};

#endif // _WIN32

#endif // ZC_CONSOLELOGGER_H
