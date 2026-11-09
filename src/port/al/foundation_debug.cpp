// zcport foundation: al_trace, al_assert and their handlers.
//
// al_trace() formats the message (printf rules), offers it to the handler
// installed with register_trace_handler(); if there is none, or it returns
// 0, the message is appended to the trace file: $ALLEGRO_TRACE if set,
// else "allegro.log" in the current directory, created (truncated) on the
// first message and kept open, flushed after every message.
//
// Written from scratch for zcport (no Allegro code).

#include "foundation_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
   #include <winalleg.h>
#endif

static int (*g_trace_handler)(const char *msg) = NULL;
static int (*g_assert_handler)(const char *msg) = NULL;

static FILE *g_trace_file = NULL;
static bool g_trace_opened = false;
static FILE *g_assert_file = NULL;
static bool g_assert_opened = false;

// Never destroyed: al_trace() can run from exit-time code.
static std::mutex &trace_lock()
{
   static std::mutex *m = new std::mutex;
   return *m;
}

static void close_debug_files(void)
{
   if (g_trace_file) {
      fclose(g_trace_file);
      g_trace_file = NULL;
   }
   if (g_assert_file) {
      fclose(g_assert_file);
      g_assert_file = NULL;
   }
}

static void write_trace_file(const char *msg)
{
   if (!g_trace_opened) {
      g_trace_opened = true;
      const char *name = getenv("ALLEGRO_TRACE");
      g_trace_file = fopen((name && name[0]) ? name : "allegro.log", "w");
      atexit(close_debug_files);
   }
   if (g_trace_file) {
      fputs(msg, g_trace_file);
      fflush(g_trace_file);
   }
}

extern "C" void zcport_trace_raw(const char *msg)
{
   if (!msg)
      return;
   std::lock_guard<std::mutex> guard(trace_lock());
   if (g_trace_handler && g_trace_handler(msg))
      return;
   write_trace_file(msg);
}

extern "C" void al_trace(const char *msg, ...)
{
   /* (not named "small": windows.h defines that as a macro for char) */
   char buf[1024];
   std::string big;
   const char *text = buf;

   va_list ap;
   va_start(ap, msg);
   int n = vsnprintf(buf, sizeof(buf), msg, ap);
   va_end(ap);

   if (n < 0) {
      buf[0] = 0;
   }
   else if (n >= (int)sizeof(buf)) {
      big.resize((size_t)n + 1);
      va_start(ap, msg);
      vsnprintf(&big[0], big.size(), msg, ap);
      va_end(ap);
      text = big.c_str();
   }

   zcport_trace_raw(text);
}

extern "C" void register_trace_handler(int (*handler)(const char *msg))
{
   std::lock_guard<std::mutex> guard(trace_lock());
   g_trace_handler = handler;
}

extern "C" void register_assert_handler(int (*handler)(const char *msg))
{
   g_assert_handler = handler;
}

extern "C" void al_assert(const char *file, int linenr)
{
   char buf[256];
   snprintf(buf, sizeof(buf), "Assert failed at line %d of %s", linenr, file ? file : "?");
   buf[sizeof(buf) - 1] = 0;

   if (g_assert_handler && g_assert_handler(buf))
      return;

   const char *name = getenv("ALLEGRO_ASSERT");
   if (name && name[0]) {
      if (!g_assert_opened) {
         g_assert_opened = true;
         g_assert_file = fopen(name, "w");
      }
      if (g_assert_file) {
         fprintf(g_assert_file, "%s\n", buf);
         fflush(g_assert_file);
      }
      return;
   }

   const ZCPORT_PLATFORM *p = zcport_get_platform();
   allegro_exit();
   if (p && p->message)
      p->message(buf);
   fprintf(stderr, "%s\n", buf);
   fflush(stderr);
   abort();
}
