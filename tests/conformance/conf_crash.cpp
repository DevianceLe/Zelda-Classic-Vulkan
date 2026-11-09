/*
 * conf_crash.cpp - make crashes fail fast and visibly instead of opening
 * Windows error dialogs (which would hang the runner).
 *
 * Deliberately does not include allegro.h: windows.h and Allegro's BITMAP
 * clash, and the port headers may not want windows.h either.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#endif

extern "C" void conf_crash_note(const char *what, unsigned long code);

namespace conf
{
namespace detail
{
void install_crash_handlers();
}
}

#ifdef _WIN32

static LONG WINAPI conf_exception_filter(EXCEPTION_POINTERS *ep)
{
    unsigned long code = 0;

    if(ep && ep->ExceptionRecord)
        code = (unsigned long)ep->ExceptionRecord->ExceptionCode;

    conf_crash_note("unhandled exception", code);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void __cdecl conf_invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *,
                                           unsigned int, uintptr_t)
{
    conf_crash_note("CRT invalid parameter", 0xC000000DUL);
    TerminateProcess(GetCurrentProcess(), 0xC000000DU);
}

static void __cdecl conf_purecall()
{
    conf_crash_note("pure virtual call", 0xC0000025UL);
    TerminateProcess(GetCurrentProcess(), 0xC0000025U);
}

#endif

static void conf_on_abort(int)
{
    conf_crash_note("abort()", 3);
    _exit(3);
}

void conf::detail::install_crash_handlers()
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(conf_exception_filter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(conf_invalid_parameter);
    _set_purecall_handler(conf_purecall);
    /* Debug CRT asserts go to stderr instead of a dialog (no-ops in Release). */
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    signal(SIGABRT, conf_on_abort);
}
