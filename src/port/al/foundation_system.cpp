// zcport foundation: library start-up and shutdown, OS information,
// messages, the platform hook table, exit functions, the close button and
// display switch callbacks.
//
// Written from scratch for zcport (no Allegro code).

#include "foundation_internal.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
   #include <winalleg.h>
   #include <intrin.h>
#else
   #include <unistd.h>
   #if defined(__i386__) || defined(__x86_64__)
      #include <cpuid.h>
   #endif
#endif

// ---------------------------------------------------------------------------
// globals from base.h / system.h
// ---------------------------------------------------------------------------

static int zcport_dummy_errno = 0;
int *allegro_errno = &zcport_dummy_errno;

char allegro_id[256] = "Allegro 4.4.2, zcport";
char allegro_error[ALLEGRO_ERROR_SIZE] = "";

int os_type = OSTYPE_UNKNOWN;
int os_version = -1;
int os_revision = -1;
int os_multitasking = FALSE;

char cpu_vendor[32] = "";
int cpu_family = 0;
int cpu_model = 0;
int cpu_capabilities = 0;

SYSTEM_DRIVER system_none;
static SYSTEM_DRIVER system_zcport;
SYSTEM_DRIVER *system_driver = NULL;

// ---------------------------------------------------------------------------
// platform hook table
// ---------------------------------------------------------------------------

static const ZCPORT_PLATFORM *g_platform = NULL;

extern "C" void zcport_set_platform(const ZCPORT_PLATFORM *platform)
{
   g_platform = platform;
}

extern "C" const ZCPORT_PLATFORM *zcport_get_platform(void)
{
   return g_platform;
}

extern "C" uint64_t zcport_ticks_us(void)
{
   if (g_platform && g_platform->get_ticks_us)
      return g_platform->get_ticks_us();

   static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
   return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
}

extern "C" uint32_t zcport_ticks_ms(void)
{
   return (uint32_t)(zcport_ticks_us() / 1000u);
}

extern "C" void zcport_sleep_ms(unsigned int ms)
{
   if (g_platform && g_platform->sleep_ms) {
      g_platform->sleep_ms(ms);
      return;
   }
   if (ms == 0)
      std::this_thread::yield();
   else
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

extern "C" void zcport_pump_events(void)
{
   if (g_platform && g_platform->pump_events)
      g_platform->pump_events();
   zcport_foundation::keyboard_update_repeat();
}

// ---------------------------------------------------------------------------
// exit functions
// ---------------------------------------------------------------------------

namespace
{
   struct ExitFunc
   {
      void (*func)(void);
      const char *desc;
   };

   // Never destroyed: allegro_exit() runs from atexit(), and a function-local
   // static created after that registration would already be destroyed by
   // then (the C runtime runs exit-time destructors in reverse order), so the
   // subsystems would never be removed (a still-running timer thread then
   // ends the process with std::terminate).
   std::vector<ExitFunc> &exit_funcs()
   {
      static std::vector<ExitFunc> *list = new std::vector<ExitFunc>;
      return *list;
   }

   bool g_installed = false;
   bool g_atexit_registered = false;
}

extern "C" void _add_exit_func(void (*func)(void), const char *desc)
{
   std::vector<ExitFunc> &list = exit_funcs();
   for (size_t i = 0; i < list.size(); i++)
      if (list[i].func == func)
         return;
   ExitFunc e;
   e.func = func;
   e.desc = desc;
   list.push_back(e);
}

extern "C" void _remove_exit_func(void (*func)(void))
{
   std::vector<ExitFunc> &list = exit_funcs();
   for (size_t i = 0; i < list.size(); i++) {
      if (list[i].func == func) {
         list.erase(list.begin() + i);
         return;
      }
   }
}

// ---------------------------------------------------------------------------
// OS and CPU detection
// ---------------------------------------------------------------------------

static void detect_os(void)
{
#ifdef _WIN32
   // Same classification Allegro 4.4 makes; Windows reports 6.2 to
   // programs without a compatibility manifest, as it does for the
   // reference build.
   OSVERSIONINFOA info;
   memset(&info, 0, sizeof(info));
   info.dwOSVersionInfoSize = sizeof(info);
#pragma warning(push)
#pragma warning(disable: 4996)
   GetVersionExA(&info);
#pragma warning(pop)
   os_version = (int)info.dwMajorVersion;
   os_revision = (int)info.dwMinorVersion;
   os_multitasking = TRUE;
   if (info.dwPlatformId == VER_PLATFORM_WIN32_NT) {
      if (os_version == 6 && os_revision == 1)
         os_type = OSTYPE_WIN7;
      else if (os_version == 6 && os_revision == 0)
         os_type = OSTYPE_WINVISTA;
      else if (os_version == 5 && os_revision == 2)
         os_type = OSTYPE_WIN2003;
      else if (os_version == 5 && os_revision == 1)
         os_type = OSTYPE_WINXP;
      else if (os_version == 5 && os_revision == 0)
         os_type = OSTYPE_WIN2000;
      else
         os_type = OSTYPE_WINNT;
   }
   else {
      os_type = OSTYPE_WIN95;
   }
#elif defined(__APPLE__)
   os_type = OSTYPE_MACOSX;
   os_multitasking = TRUE;
#elif defined(__linux__)
   os_type = OSTYPE_LINUX;
   os_multitasking = TRUE;
#else
   os_type = OSTYPE_UNIX;
   os_multitasking = TRUE;
#endif
}

static void zcport_cpuid(unsigned int leaf, unsigned int regs[4])
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
   int r[4];
   __cpuid(r, (int)leaf);
   for (int i = 0; i < 4; i++)
      regs[i] = (unsigned int)r[i];
#elif (defined(__i386__) || defined(__x86_64__))
   __cpuid(leaf, regs[0], regs[1], regs[2], regs[3]);
#else
   (void)leaf;
   regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

extern "C" void check_cpu(void)
{
   cpu_capabilities = 0;
   cpu_family = 0;
   cpu_model = 0;
   cpu_vendor[0] = 0;

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
   unsigned int regs[4];
   zcport_cpuid(0, regs);
   unsigned int max_leaf = regs[0];
   memcpy(cpu_vendor + 0, &regs[1], 4);
   memcpy(cpu_vendor + 4, &regs[3], 4);
   memcpy(cpu_vendor + 8, &regs[2], 4);
   cpu_vendor[12] = 0;
   cpu_capabilities |= CPU_ID | CPU_FPU;

   if (max_leaf >= 1) {
      zcport_cpuid(1, regs);
      cpu_family = (int)((regs[0] >> 8) & 0xF);
      cpu_model = (int)((regs[0] >> 4) & 0xF);
      if (regs[3] & (1u << 15)) cpu_capabilities |= CPU_CMOV;
      if (regs[3] & (1u << 23)) cpu_capabilities |= CPU_MMX;
      if (regs[3] & (1u << 25)) cpu_capabilities |= CPU_SSE | CPU_MMXPLUS;
      if (regs[3] & (1u << 26)) cpu_capabilities |= CPU_SSE2;
      if (regs[2] & (1u << 0))  cpu_capabilities |= CPU_SSE3;
      if (regs[2] & (1u << 9))  cpu_capabilities |= CPU_SSSE3;
      if (regs[2] & (1u << 19)) cpu_capabilities |= CPU_SSE41;
      if (regs[2] & (1u << 20)) cpu_capabilities |= CPU_SSE42;
   }

   zcport_cpuid(0x80000000u, regs);
   if (regs[0] >= 0x80000001u) {
      zcport_cpuid(0x80000001u, regs);
      if (regs[3] & (1u << 29)) cpu_capabilities |= CPU_AMD64;
      if (regs[3] & (1u << 31)) cpu_capabilities |= CPU_3DNOW;
      if (regs[3] & (1u << 30)) cpu_capabilities |= CPU_ENH3DNOW;
   }
#endif
}

// ---------------------------------------------------------------------------
// install / exit
// ---------------------------------------------------------------------------

static void zcport_get_executable_name_os(char *output, int size);

static void sysdrv_get_executable_name(char *output, int size)
{
   zcport_get_executable_name_os(output, size);
}

static void init_system_drivers(void)
{
   memset(&system_none, 0, sizeof(system_none));
   system_none.id = SYSTEM_NONE;
   system_none.name = "No system driver";
   system_none.desc = "No system driver";
   system_none.ascii_name = "No system driver";
   system_none.get_executable_name = sysdrv_get_executable_name;

   memset(&system_zcport, 0, sizeof(system_zcport));
   system_zcport.id = SYSTEM_ZCPORT;
   system_zcport.name = "zcport";
   system_zcport.desc = "zcport (SDL3 + Vulkan)";
   system_zcport.ascii_name = "zcport";
   system_zcport.get_executable_name = sysdrv_get_executable_name;
}

static void allegro_exit_stub(void)
{
   allegro_exit();
}

extern "C" int install_allegro(int system_id, int *errno_ptr, int (*atexit_ptr)(void (*func)(void)))
{
   if (g_installed)
      return 0;

   allegro_errno = errno_ptr ? errno_ptr : &zcport_dummy_errno;
   allegro_error[0] = 0;

   init_system_drivers();

   if (system_id == SYSTEM_NONE) {
      system_driver = &system_none;
   }
   else {
      system_driver = &system_zcport;
      if (g_platform && g_platform->init) {
         if (g_platform->init() != 0) {
            if (!allegro_error[0])
               zcport_set_error("Unable to initialise the platform layer");
            system_driver = NULL;
            return -1;
         }
      }
   }

   detect_os();
   check_cpu();

   set_color_depth(8);

   if (atexit_ptr && !g_atexit_registered) {
      atexit_ptr(allegro_exit_stub);
      g_atexit_registered = true;
   }

   g_installed = true;

   /* no close-button callback yet: the reference greys the close button */
   if (g_platform && g_platform->close_button_enable)
      g_platform->close_button_enable(0);

   return 0;
}

extern "C" int _install_allegro_version_check(int system_id, int *errno_ptr,
   int (*atexit_ptr)(void (*func)(void)), int version)
{
   int major = (version >> 16) & 0xFF;
   int minor = (version >> 8) & 0xFF;
   if (major != ALLEGRO_VERSION || minor != ALLEGRO_SUB_VERSION) {
      char buf[ALLEGRO_ERROR_SIZE];
      snprintf(buf, sizeof(buf),
               "The detected dynamic Allegro library (%d.%d.%d) is not compatible with this program (%d.%d.%d).",
               ALLEGRO_VERSION, ALLEGRO_SUB_VERSION, ALLEGRO_WIP_VERSION,
               major, minor, version & 0xFF);
      zcport_set_error(buf);
      return -1;
   }
   return install_allegro(system_id, errno_ptr, atexit_ptr);
}

extern "C" void allegro_exit(void)
{
   if (!g_installed)
      return;

   // Most recently installed subsystem first; each exit function normally
   // removes itself, but remove it here in case it does not.
   std::vector<ExitFunc> &list = exit_funcs();
   while (!list.empty()) {
      void (*func)(void) = list.back().func;
      list.pop_back();
      func();
   }

   if (system_driver == &system_zcport && g_platform && g_platform->exit)
      g_platform->exit();

   system_driver = NULL;
   g_installed = false;
}

extern "C" int zcport_system_installed(void)
{
   return g_installed ? 1 : 0;
}

extern "C" void zcport_set_error(const char *msg)
{
   if (!msg)
      msg = "";
   size_t n = strlen(msg);
   if (n > ALLEGRO_ERROR_SIZE - 1)
      n = ALLEGRO_ERROR_SIZE - 1;
   memcpy(allegro_error, msg, n);
   allegro_error[n] = 0;
}

// ---------------------------------------------------------------------------
// messages, names, desktop
// ---------------------------------------------------------------------------

#define ZCPORT_MESSAGE_SIZE 4096

extern "C" void allegro_message(const char *msg, ...)
{
   char buf[ZCPORT_MESSAGE_SIZE];
   va_list ap;
   va_start(ap, msg);
   vsnprintf(buf, sizeof(buf), msg, ap);
   va_end(ap);
   buf[sizeof(buf) - 1] = 0;

   if (g_platform && g_platform->message) {
      g_platform->message(buf);
      return;
   }
   fputs(buf, stderr);
   fflush(stderr);
}

static void zcport_get_executable_name_os(char *output, int size)
{
   if (!output || size <= 0)
      return;
   output[0] = 0;
#ifdef _WIN32
   DWORD n = GetModuleFileNameA(NULL, output, (DWORD)size);
   if (n == 0 || n >= (DWORD)size)
      output[size - 1] = 0;
#elif defined(__linux__)
   ssize_t n = readlink("/proc/self/exe", output, (size_t)size - 1);
   if (n < 0)
      n = 0;
   output[n] = 0;
#endif
   if (!output[0]) {
      strncpy(output, "./", (size_t)size);
      output[size - 1] = 0;
   }
}

extern "C" void get_executable_name(char *output, int size)
{
   if (!output || size <= 0)
      return;
   if (g_platform && g_platform->get_executable_name &&
       g_platform->get_executable_name(output, size) == 0)
      return;
   zcport_get_executable_name_os(output, size);
}

static void (*g_close_button_proc)(void) = NULL;

extern "C" int set_close_button_callback(void (*proc)(void))
{
   g_close_button_proc = proc;

   /* the close button is greyed while no callback is set (as the reference) */
   if (g_platform && g_platform->close_button_enable)
      g_platform->close_button_enable(proc != NULL);

   return 0;
}

extern "C" int zcport_close_button_pressed(void)
{
   void (*proc)(void) = g_close_button_proc;
   if (proc) {
      proc();
      return 1;
   }
   return 0;
}

static char g_window_title[256] = "";

extern "C" void set_window_title(const char *name)
{
   if (!name)
      name = "";
   strncpy(g_window_title, name, sizeof(g_window_title) - 1);
   g_window_title[sizeof(g_window_title) - 1] = 0;
   if (g_platform && g_platform->set_window_title)
      g_platform->set_window_title(g_window_title);
}

extern "C" int desktop_color_depth(void)
{
   if (g_platform && g_platform->desktop_color_depth)
      return g_platform->desktop_color_depth();
#ifdef _WIN32
   HDC dc = GetDC(NULL);
   int bpp = GetDeviceCaps(dc, BITSPIXEL);
   ReleaseDC(NULL, dc);
   return bpp;
#else
   return 0;
#endif
}

extern "C" int get_desktop_resolution(int *width, int *height)
{
   if (g_platform && g_platform->get_desktop_resolution)
      return g_platform->get_desktop_resolution(width, height);
#ifdef _WIN32
   if (width)
      *width = GetSystemMetrics(SM_CXSCREEN);
   if (height)
      *height = GetSystemMetrics(SM_CYSCREEN);
   return 0;
#else
   return -1;
#endif
}

extern "C" void yield_timeslice(void)
{
   zcport_sleep_ms(0);
}

// ---------------------------------------------------------------------------
// display switching (the platform reports focus changes)
// ---------------------------------------------------------------------------

#define ZCPORT_MAX_SWITCH_CALLBACKS 8

static int g_switch_mode = SWITCH_BACKGROUND;
static void (*g_switch_in[ZCPORT_MAX_SWITCH_CALLBACKS])(void);
static void (*g_switch_out[ZCPORT_MAX_SWITCH_CALLBACKS])(void);

extern "C" int set_display_switch_mode(int mode)
{
   if (mode < SWITCH_NONE || mode > SWITCH_BACKAMNESIA)
      return -1;
   /* in a windowed mode the reference only accepts PAUSE and BACKGROUND */
   if (gfx_driver && gfx_driver->windowed &&
       (mode == SWITCH_NONE || mode == SWITCH_AMNESIA || mode == SWITCH_BACKAMNESIA))
      return -1;
   g_switch_mode = mode;
   for (int i = 0; i < ZCPORT_MAX_SWITCH_CALLBACKS; i++) {
      g_switch_in[i] = NULL;
      g_switch_out[i] = NULL;
   }
   return 0;
}

extern "C" int get_display_switch_mode(void)
{
   return g_switch_mode;
}

extern "C" int set_display_switch_callback(int dir, void (*cb)(void))
{
   if (dir != SWITCH_IN && dir != SWITCH_OUT)
      return -1;
   if (!cb)
      return 0;   /* accepted and ignored, like the reference */
   void (**list)(void) = (dir == SWITCH_IN) ? g_switch_in : g_switch_out;
   for (int i = 0; i < ZCPORT_MAX_SWITCH_CALLBACKS; i++) {
      if (list[i] == cb)
         return 0;
   }
   for (int i = 0; i < ZCPORT_MAX_SWITCH_CALLBACKS; i++) {
      if (!list[i]) {
         list[i] = cb;
         return 0;
      }
   }
   return -1;
}

extern "C" void remove_display_switch_callback(void (*cb)(void))
{
   for (int i = 0; i < ZCPORT_MAX_SWITCH_CALLBACKS; i++) {
      if (g_switch_in[i] == cb)
         g_switch_in[i] = NULL;
      if (g_switch_out[i] == cb)
         g_switch_out[i] = NULL;
   }
}

extern "C" void zcport_display_switch_notify(int dir)
{
   void (**list)(void) = (dir == SWITCH_IN) ? g_switch_in : g_switch_out;
   for (int i = 0; i < ZCPORT_MAX_SWITCH_CALLBACKS; i++) {
      if (list[i])
         list[i]();
   }
}
