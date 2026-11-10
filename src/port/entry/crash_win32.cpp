// zplayer: crash report on Windows.
//
// An unhandled exception, abort(), std::terminate or a C runtime
// invalid-parameter error writes zplayer_crash.txt in the current directory
// (next to allegro.log): the exception code and address, the registers and
// the call stack, with function names and source lines when zplayer.pdb is
// next to the exe. The game's behaviour is unchanged; the process still ends
// as it would.
//
// The report uses Win32 calls and static storage only. It writes the exception
// and the thread first and asks dbghelp for names last (dbghelp allocates and
// takes locks): a crash can come with the heap damaged, or after exit() has
// taken the C runtime apart. Module names are read without the loader lock,
// which the main thread holds while the process ends.
//
// Two ways in:
//   - the unhandled-exception filter, for everything during play;
//   - a vectored handler, which sees an exception before anything handles it.
//     Code in other modules often handles its own (a driver probing memory,
//     say), so it reports only faults in zplayer's own code, which never
//     handles them, and faults after the exit functions have run.
// A thread that faults while ExitProcess is ending the other threads is cut
// off at its next system call, so its report can be an empty file.
//
// Written from scratch for zcport.

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdlib.h>
#include <exception>

#pragma comment(lib, "dbghelp.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{

// ---- output -----------------------------------------------------------------

HANDLE g_file = INVALID_HANDLE_VALUE;
char g_buf[4096];
DWORD g_used = 0;

void flush_out()
{
    DWORD written = 0;

    if(g_used && g_file != INVALID_HANDLE_VALUE)
        WriteFile(g_file, g_buf, g_used, &written, NULL);

    g_used = 0;
}

void put_ch(char c)
{
    if(g_used == sizeof(g_buf))
        flush_out();

    g_buf[g_used++] = c;
}

void put(const char *s)
{
    while(*s)
        put_ch(*s++);
}

// At least `digits` digits, more if the value needs them.
void put_hex(ULONG_PTR v, int digits)
{
    static const char hex[] = "0123456789abcdef";

    while(digits < (int)sizeof(ULONG_PTR) * 2 && (v >> (digits * 4)) != 0)
        ++digits;

    for(int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        put_ch(hex[(v >> shift) & 15]);
}

void put_addr(ULONG_PTR v)
{
    put_hex(v, sizeof(ULONG_PTR) * 2);
}

void put_dec(DWORD v)
{
    char tmp[12];
    int n = 0;

    do
    {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while(v);

    while(n)
        put_ch(tmp[--n]);
}

// The module list, read straight from the loader's data. GetModuleFileName
// would take the loader lock, which the main thread holds while the process
// ends: a crash on another thread then would wait for it and be cut off.
char g_mod_name[128];

bool find_module_name(ULONG_PTR base)
{
    g_mod_name[0] = 0;

    __try
    {
        PEB *peb = ((TEB *)NtCurrentTeb())->ProcessEnvironmentBlock;
        LIST_ENTRY *head = &peb->Ldr->InMemoryOrderModuleList;
        int guard = 0;

        for(LIST_ENTRY *e = head->Flink; e && e != head && guard < 1024; e = e->Flink, ++guard)
        {
            LDR_DATA_TABLE_ENTRY *m = CONTAINING_RECORD(e, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

            if((ULONG_PTR)m->DllBase != base)
                continue;

            const WCHAR *s = m->FullDllName.Buffer;
            USHORT len = m->FullDllName.Length / sizeof(WCHAR), start = 0, n = 0;

            for(USHORT i = 0; i < len; ++i)
                if(s[i] == L'\\' || s[i] == L'/')
                    start = i + 1;

            for(USHORT i = start; i < len && n + 1 < sizeof(g_mod_name); ++i)
                g_mod_name[n++] = (s[i] > 32 && s[i] < 127) ? (char)s[i] : '?';

            g_mod_name[n] = 0;
            return n != 0;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_mod_name[0] = 0;
    }

    return false;
}

// "name.dll+0x1234" when the address is inside a loaded image.
bool put_module(ULONG_PTR addr)
{
    MEMORY_BASIC_INFORMATION mi;

    if(!VirtualQuery((const void *)addr, &mi, sizeof(mi)) || mi.Type != MEM_IMAGE || !mi.AllocationBase)
        return false;

    if(find_module_name((ULONG_PTR)mi.AllocationBase))
        put(g_mod_name);
    else
    {
        put("image@");
        put_addr((ULONG_PTR)mi.AllocationBase);
    }

    put("+0x");
    put_hex(addr - (ULONG_PTR)mi.AllocationBase, sizeof(ULONG_PTR) * 2);
    return true;
}

// ---- the stack --------------------------------------------------------------

ULONG_PTR context_sp(const CONTEXT *c)
{
#if defined(_M_IX86)
    return c->Esp;
#elif defined(_M_X64)
    return c->Rsp;
#else
    return c->Sp;
#endif
}

ULONG_PTR context_pc(const CONTEXT *c)
{
#if defined(_M_IX86)
    return c->Eip;
#elif defined(_M_X64)
    return c->Rip;
#else
    return c->Pc;
#endif
}

// Whatever on the stack looks like an address in a loaded image's code. Needs
// no symbols and no heap, so it is there when the walk below can't run.
void put_stack_scan(ULONG_PTR sp)
{
    ULONG_PTR limit = (ULONG_PTR)((NT_TIB *)NtCurrentTeb())->StackBase;

    if(sp < (ULONG_PTR)((NT_TIB *)NtCurrentTeb())->StackLimit || sp >= limit)
    {
        put("stack pointer is outside this thread's stack\n");
        return;
    }

    put("code addresses on the stack:\n");
    sp &= ~(ULONG_PTR)(sizeof(ULONG_PTR) - 1);
    int shown = 0;

    for(ULONG_PTR p = sp; p < limit && shown < 64; p += sizeof(ULONG_PTR))
    {
        ULONG_PTR v = *(const ULONG_PTR *)p;
        MEMORY_BASIC_INFORMATION mi;

        if(!VirtualQuery((const void *)v, &mi, sizeof(mi)) || mi.Type != MEM_IMAGE || mi.State != MEM_COMMIT)
            continue;

        if(!(mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            continue;

        put("  sp+0x");
        put_hex(p - sp, 4);
        put("  ");
        put_addr(v);
        put("  ");
        put_module(v);
        put_ch('\n');
        ++shown;
    }
}

void put_frame(HANDLE process, DWORD64 addr)
{
    static char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    ZeroMemory(buf, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    DWORD line_disp = 0;

    put("  ");
    put_addr((ULONG_PTR)addr);

    if(SymFromAddr(process, addr, &disp, sym))
    {
        put("  ");
        put(sym->Name);
        put("+0x");
        put_hex((ULONG_PTR)disp, 4);
    }
    else
    {
        put("  ");
        put_module((ULONG_PTR)addr);
    }

    if(SymGetLineFromAddr64(process, addr, &line_disp, &line))
    {
        put("  (");
        put(line.FileName);
        put_ch(':');
        put_dec(line.LineNumber);
        put_ch(')');
    }

    put_ch('\n');
}

void walk_stack(const CONTEXT *context)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    put("stack:\n");

    static CONTEXT ctx;
    ctx = *context;
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

        put_frame(process, frame.AddrPC.Offset);
        flush_out();
    }

    SymCleanup(process);
}

// dbghelp allocates, so a damaged heap can fault inside it.
void walk_stack_guarded(const CONTEXT *context)
{
    __try
    {
        walk_stack(context);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        put("(the stack walk itself faulted)\n");
    }
}

// ---- the report -------------------------------------------------------------

volatile LONG g_claimed = 0;
volatile LONG g_finished = 0;
volatile DWORD g_writer = 0;
volatile LONG g_exiting = 0;
DWORD g_main_thread = 0;

// Where the crashing thread started: tells a game thread from SDL's, a driver's
// or the system's. Looked up at install time (GetProcAddress can take the
// loader lock).
typedef LONG (NTAPI *QueryThreadFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
QueryThreadFn g_query_thread = NULL;

void put_thread()
{
    put("thread ");
    put_dec(GetCurrentThreadId());

    if(GetCurrentThreadId() == g_main_thread)
        put(" (the game's)");

    ULONG_PTR start = 0;

    if(g_query_thread && g_query_thread(GetCurrentThread(), 9 /* ThreadQuerySetWin32StartAddress */,
                                        &start, sizeof(start), NULL) >= 0 && start)
    {
        put(", started at ");
        put_addr(start);
        put(" ");
        put_module(start);
    }

    put_ch('\n');
}

void write_report(const char *reason, EXCEPTION_RECORD *rec, CONTEXT *context)
{
    if(InterlockedExchange(&g_claimed, 1))
    {
        // Another thread is writing: don't let this one end the process first.
        if(g_writer != GetCurrentThreadId())
            for(int i = 0; i < 300 && !g_finished; ++i)
                Sleep(10);

        return;
    }

    g_writer = GetCurrentThreadId();
    g_file = CreateFileA("zplayer_crash.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if(g_file == INVALID_HANDLE_VALUE)
    {
        g_finished = 1;
        return;
    }

    if(rec)
    {
        put("zplayer crashed: exception 0x");
        put_hex(rec->ExceptionCode, 8);
        put(" at ");
        put_addr((ULONG_PTR)rec->ExceptionAddress);

        if(rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        {
            put("\n  ");
            put(rec->ExceptionInformation[0] == 8 ? "executing" : rec->ExceptionInformation[0] ? "writing" : "reading");
            put(" address ");
            put_addr(rec->ExceptionInformation[1]);
        }

        put_ch('\n');
    }
    else
    {
        put("zplayer stopped: ");
        put(reason);
        put_ch('\n');
    }

    if(g_exiting)
        put("after the exit functions ran\n");

    flush_out();

    put("in ");

    if(!put_module(context_pc(context)))
        put("no loaded module");

    put_ch('\n');
    put_thread();
    flush_out();
#if defined(_M_IX86)
    put("eax ");
    put_addr(context->Eax);
    put(" ebx ");
    put_addr(context->Ebx);
    put(" ecx ");
    put_addr(context->Ecx);
    put(" edx ");
    put_addr(context->Edx);
    put(" esi ");
    put_addr(context->Esi);
    put(" edi ");
    put_addr(context->Edi);
    put(" ebp ");
    put_addr(context->Ebp);
    put(" esp ");
    put_addr(context->Esp);
    put_ch('\n');
#endif
    flush_out();

    put_stack_scan(context_sp(context));
    flush_out();

    walk_stack_guarded(context);
    flush_out();

    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
    g_finished = 1;
}

LONG WINAPI on_crash(EXCEPTION_POINTERS *ep)
{
    write_report(NULL, ep->ExceptionRecord, ep->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
}

bool is_fatal_code(DWORD code)
{
    switch(code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374: // heap corruption
    case 0xC0000409: // stack buffer overrun
        return true;
    }

    return false;
}

bool in_zplayer_image(ULONG_PTR addr)
{
    const ULONG_PTR base = (ULONG_PTR)&__ImageBase;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + __ImageBase.e_lfanew);
    return addr >= base && addr < base + nt->OptionalHeader.SizeOfImage;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep)
{
    if(!is_fatal_code(ep->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    if(g_writer == GetCurrentThreadId())
        return EXCEPTION_CONTINUE_SEARCH;   // a fault inside the report itself

    if(g_exiting || in_zplayer_image((ULONG_PTR)ep->ExceptionRecord->ExceptionAddress))
        write_report(NULL, ep->ExceptionRecord, ep->ContextRecord);

    return EXCEPTION_CONTINUE_SEARCH;
}

void mark_exiting()
{
    g_exiting = 1;
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

void copy_reason(char *to, size_t size, const char *a, const char *b)
{
    size_t n = 0;

    for(; *a && n + 1 < size; ++a)
        to[n++] = *a;

    for(; b && *b && n + 1 < size; ++b)
        to[n++] = *b;

    to[n] = 0;
}

void on_terminate()
{
    static char reason[512];
    copy_reason(reason, sizeof(reason), "std::terminate, no exception in flight (joinable std::thread destroyed?)", NULL);

    if(std::exception_ptr e = std::current_exception())
    {
        try
        {
            std::rethrow_exception(e);
        }
        catch(const std::exception &x)
        {
            copy_reason(reason, sizeof(reason), "std::terminate, uncaught exception: ", x.what());
        }
        catch(...)
        {
            copy_reason(reason, sizeof(reason), "std::terminate, uncaught non-standard exception", NULL);
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
    g_main_thread = GetCurrentThreadId();

    if(HMODULE ntdll = GetModuleHandleA("ntdll.dll"))
        g_query_thread = (QueryThreadFn)GetProcAddress(ntdll, "NtQueryInformationThread");

    SetUnhandledExceptionFilter(on_crash);
    AddVectoredExceptionHandler(1, on_exception);
    signal(SIGABRT, on_abort);
    std::set_terminate(on_terminate);
    _set_invalid_parameter_handler(on_invalid_parameter);
    // First registered, so the last exit function to run.
    atexit(mark_exiting);
}

#endif // _WIN32
