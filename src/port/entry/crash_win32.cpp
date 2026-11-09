// zplayer: crash report on Windows.
//
// An unhandled exception, abort(), std::terminate or a C runtime
// invalid-parameter error writes zplayer_crash.txt in the current directory
// (next to allegro.log): the exception code and address and the call stack,
// with function names and source lines when zplayer.pdb is next to the exe.
// The game's behaviour is unchanged; the process still ends as it would.
//
// Written from scratch for zcport.

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <exception>

#pragma comment(lib, "dbghelp.lib")

namespace
{

void write_frame(FILE *f, HANDLE process, DWORD64 addr)
{
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    ZeroMemory(buf, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    DWORD line_disp = 0;

    fprintf(f, "  %08llx", (unsigned long long)addr);

    if(SymFromAddr(process, addr, &disp, sym))
        fprintf(f, "  %s+0x%llx", sym->Name, (unsigned long long)disp);

    if(SymGetLineFromAddr64(process, addr, &line_disp, &line))
        fprintf(f, "  (%s:%lu)", line.FileName, (unsigned long)line.LineNumber);

    fputc('\n', f);
}

void write_report(const char *reason, EXCEPTION_RECORD *rec, CONTEXT *context)
{
    static volatile LONG once = 0;

    if(InterlockedExchange(&once, 1))
        return;

    FILE *f = fopen("zplayer_crash.txt", "w");

    if(!f)
        return;

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    if(rec)
    {
        fprintf(f, "zplayer crashed: exception 0x%08lx at %p\n",
                (unsigned long)rec->ExceptionCode, rec->ExceptionAddress);

        if(rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        {
            fprintf(f, "  %s address %p\n",
                    rec->ExceptionInformation[0] ? "writing" : "reading",
                    (void *)rec->ExceptionInformation[1]);
        }
    }
    else
        fprintf(f, "zplayer stopped: %s\n", reason);

    fprintf(f, "stack:\n");

    CONTEXT ctx = *context;
    STACKFRAME64 frame;
    ZeroMemory(&frame, sizeof(frame));
    DWORD machine;
#if defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#elif defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    for(int i = 0; i < 64; ++i)
    {
        if(!StackWalk64(machine, process, thread, &frame, &ctx, NULL,
                        SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;

        if(frame.AddrPC.Offset == 0)
            break;

        write_frame(f, process, frame.AddrPC.Offset);
    }

    fclose(f);
    SymCleanup(process);
}

LONG WINAPI on_crash(EXCEPTION_POINTERS *ep)
{
    write_report(NULL, ep->ExceptionRecord, ep->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

void report_here(const char *reason)
{
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    write_report(reason, NULL, &ctx);
}

void on_abort(int)
{
    report_here("abort()");
}

void on_terminate()
{
    static char reason[512];
    _snprintf(reason, sizeof(reason) - 1, "std::terminate, no exception in flight (joinable std::thread destroyed?)");

    if(std::exception_ptr e = std::current_exception())
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch(const std::exception &x)
        {
            _snprintf(reason, sizeof(reason) - 1, "std::terminate, uncaught exception: %s", x.what());
        }
        catch(...)
        {
            _snprintf(reason, sizeof(reason) - 1, "std::terminate, uncaught non-standard exception");
        }
    }

    report_here(reason);
    abort();
}

void on_invalid_parameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    report_here("C runtime invalid parameter");
    abort();
}

} // namespace

extern "C" void zplayer_install_crash_report(void)
{
    SetUnhandledExceptionFilter(on_crash);
    signal(SIGABRT, on_abort);
    std::set_terminate(on_terminate);
    _set_invalid_parameter_handler(on_invalid_parameter);
}

#endif // _WIN32
