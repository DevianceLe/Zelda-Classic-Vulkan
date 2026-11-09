// zcport platform module: timer interrupts, retrace_count, rest().
//
// One timer thread runs every installed handler at its due time. Times are
// kept in Allegro timer ticks (TIMERS_PER_SECOND = 1193181 per second) on a
// monotonic clock, so periods do not drift: each handler's next due time is
// its previous due time plus its speed, and a handler that fell behind is
// called once per missed tick (as the reference build does: a 1-tick timer
// really runs about 1.19 million times a second).
//
// Handlers run on the timer thread with the timer lock held, so after
// remove_int() returns the handler is not running and will not run again.
//
// rest() sleeps the way the reference build does on Windows (a plain
// millisecond sleep, so its granularity follows the system timer
// resolution: about 15.6 ms unless the program raised it, as the game does
// around its frame throttle with timeBeginPeriod). The timer thread itself
// waits on a high-resolution waitable timer, so handler periods are
// accurate whatever the system timer resolution is.
//
// Written from scratch for zcport (no Allegro code).

#include "platform_internal.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#ifdef _WIN32
   #include <winalleg.h>
#endif

// ---------------------------------------------------------------------------
// globals from timer.h
// ---------------------------------------------------------------------------

volatile int retrace_count = 0;

static TIMER_DRIVER timerdrv_zcport =
{
   AL_ID('Z','C','P','T'), "zcport timer", "zcport timer thread", "zcport timer",
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

TIMER_DRIVER *timer_driver = NULL;

namespace
{
   const int kMaxSlots = 16;
   const int64_t kTps = TIMERS_PER_SECOND;

   struct Slot
   {
      bool used;
      void (*proc)(void);
      void (*pproc)(void *);
      void *param;
      int64_t speed;     // ticks between calls
      int64_t next;      // due time, ticks since g_t0
   };

   std::recursive_mutex g_lock;
   Slot g_slots[kMaxSlots];
   std::thread g_thread;
   bool g_installed = false;
   bool g_run = false;
   std::chrono::steady_clock::time_point g_t0;

   int64_t g_retrace_next = 0;
   int64_t g_retrace_speed = kTps / 70;
   std::atomic<int> g_retrace_hz(70);
   std::atomic<int> g_retrace_hz_applied(70);

   // ---- clock ---------------------------------------------------------------

   int64_t now_ticks()
   {
      int64_t ns = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
         std::chrono::steady_clock::now() - g_t0).count();
      if (ns < 0)
         ns = 0;
      return (ns / 1000000000) * kTps + ((ns % 1000000000) * kTps) / 1000000000;
   }

   int64_t ticks_to_ns(int64_t t)
   {
      return (t / kTps) * 1000000000 + ((t % kTps) * 1000000000) / kTps;
   }

   // ---- waiting ---------------------------------------------------------------

#ifdef _WIN32
   HANDLE g_wake = NULL;
   HANDLE g_waitable = NULL;

   void waiter_open()
   {
      g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
      // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+): precise
      // without raising the system timer resolution.
      g_waitable = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
      if (!g_waitable)
         g_waitable = CreateWaitableTimerW(NULL, FALSE, NULL);
   }

   void waiter_close()
   {
      if (g_wake)
         CloseHandle(g_wake);
      if (g_waitable)
         CloseHandle(g_waitable);
      g_wake = g_waitable = NULL;
   }

   void waiter_wake()
   {
      if (g_wake)
         SetEvent(g_wake);
   }

   // Waits until 'until' (ticks) or a wake-up.
   void waiter_wait(int64_t until)
   {
      int64_t ns = ticks_to_ns(until) - ticks_to_ns(now_ticks());
      if (ns <= 0)
         return;
      if (ns > 1000000000)
         ns = 1000000000;
      if (g_waitable) {
         LARGE_INTEGER due;
         due.QuadPart = -(LONGLONG)((ns + 99) / 100);
         if (SetWaitableTimer(g_waitable, &due, 0, NULL, NULL, FALSE)) {
            HANDLE hs[2] = { g_wake, g_waitable };
            WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            return;
         }
      }
      WaitForSingleObject(g_wake, (DWORD)((ns + 999999) / 1000000));
   }
#else
   std::mutex g_wait_mutex;
   std::condition_variable g_wait_cv;
   bool g_wake_flag = false;

   void waiter_open() { g_wake_flag = false; }
   void waiter_close() {}

   void waiter_wake()
   {
      std::lock_guard<std::mutex> guard(g_wait_mutex);
      g_wake_flag = true;
      g_wait_cv.notify_one();
   }

   void waiter_wait(int64_t until)
   {
      std::unique_lock<std::mutex> lock(g_wait_mutex);
      std::chrono::steady_clock::time_point deadline =
         g_t0 + std::chrono::nanoseconds(ticks_to_ns(until));
      g_wait_cv.wait_until(lock, deadline, [] { return g_wake_flag; });
      g_wake_flag = false;
   }
#endif

   // ---- the timer thread -------------------------------------------------------

   void apply_retrace_rate(int64_t now)
   {
      int hz = g_retrace_hz.load();
      if (hz != g_retrace_hz_applied.load()) {
         g_retrace_hz_applied = hz;
         g_retrace_speed = kTps / (hz > 0 ? hz : 70);
         g_retrace_next = now + g_retrace_speed;
      }
   }

   void timer_thread_main()
   {
#ifdef _WIN32
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
      for (;;) {
         int64_t wake_at;
         {
            std::lock_guard<std::recursive_mutex> guard(g_lock);
            if (!g_run)
               break;

            int64_t now = now_ticks();
            for (int i = 0; i < kMaxSlots; i++) {
               // A handler that fell far behind (the process was suspended)
               // catches up at most ten seconds of ticks.
               if (g_slots[i].used && now - g_slots[i].next > 10 * kTps)
                  g_slots[i].next = now - 10 * kTps;
               while (g_slots[i].used && g_slots[i].next <= now) {
                  Slot &s = g_slots[i];
                  s.next += s.speed;
                  if (s.pproc)
                     s.pproc(s.param);
                  else if (s.proc)
                     s.proc();
               }
            }

            apply_retrace_rate(now);
            if (now - g_retrace_next > kTps)
               g_retrace_next = now;
            while (g_retrace_next <= now) {
               retrace_count = retrace_count + 1;
               g_retrace_next += g_retrace_speed;
            }

            wake_at = g_retrace_next;
            for (int i = 0; i < kMaxSlots; i++)
               if (g_slots[i].used && g_slots[i].next < wake_at)
                  wake_at = g_slots[i].next;
         }
         waiter_wait(wake_at);
      }
   }

   int find_slot(void (*proc)(void), void (*pproc)(void *), void *param)
   {
      for (int i = 0; i < kMaxSlots; i++) {
         const Slot &s = g_slots[i];
         if (!s.used)
            continue;
         if (proc && s.proc == proc && !s.pproc)
            return i;
         if (pproc && s.pproc == pproc && s.param == param)
            return i;
      }
      return -1;
   }

   int add_timer(void (*proc)(void), void (*pproc)(void *), void *param, long speed)
   {
      if (!timer_driver && install_timer() != 0)
         return -1;
      int64_t sp = speed > 0 ? (int64_t)speed : 1;

      std::lock_guard<std::recursive_mutex> guard(g_lock);
      int i = find_slot(proc, pproc, param);
      if (i >= 0) {
         // As though the speed changed exactly at the last tick.
         Slot &s = g_slots[i];
         int64_t last = s.next - s.speed;
         s.speed = sp;
         s.next = last + sp;
      }
      else {
         for (i = 0; i < kMaxSlots; i++)
            if (!g_slots[i].used)
               break;
         if (i >= kMaxSlots)
            return -1;
         Slot &s = g_slots[i];
         s.used = true;
         s.proc = proc;
         s.pproc = pproc;
         s.param = param;
         s.speed = sp;
         s.next = now_ticks() + sp;
      }
      waiter_wake();
      return 0;
   }

   void del_timer(void (*proc)(void), void (*pproc)(void *), void *param)
   {
      std::lock_guard<std::recursive_mutex> guard(g_lock);
      int i = find_slot(proc, pproc, param);
      if (i >= 0)
         g_slots[i].used = false;
   }
}

void zcport_platform::timer_set_retrace_rate(int hz)
{
   if (hz <= 0)
      hz = 70;
   g_retrace_hz = hz;
   waiter_wake();
}

// ---------------------------------------------------------------------------
// timer.h API
// ---------------------------------------------------------------------------

extern "C" int install_timer(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (g_installed)
      return 0;

   static bool t0_set = false;
   if (!t0_set) {
      g_t0 = std::chrono::steady_clock::now();
      t0_set = true;
   }
   for (int i = 0; i < kMaxSlots; i++)
      g_slots[i].used = false;

   waiter_open();
   int64_t now = now_ticks();
   g_retrace_hz_applied = g_retrace_hz.load();
   g_retrace_speed = kTps / g_retrace_hz_applied.load();
   g_retrace_next = now + g_retrace_speed;

   g_run = true;
   g_thread = std::thread(timer_thread_main);
   g_installed = true;
   timer_driver = &timerdrv_zcport;
   _add_exit_func(remove_timer, "remove_timer");
   return 0;
}

extern "C" void remove_timer(void)
{
   {
      std::lock_guard<std::recursive_mutex> guard(g_lock);
      if (!g_installed)
         return;
      g_run = false;
      for (int i = 0; i < kMaxSlots; i++)
         g_slots[i].used = false;
      waiter_wake();
   }
   if (g_thread.joinable()) {
      if (g_thread.get_id() == std::this_thread::get_id())
         g_thread.detach();
      else
         g_thread.join();
   }
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   waiter_close();
   g_installed = false;
   timer_driver = NULL;
   _remove_exit_func(remove_timer);
}

extern "C" int install_int_ex(void (*proc)(void), long speed)
{
   if (!proc)
      return -1;
   return add_timer(proc, NULL, NULL, speed);
}

extern "C" int install_int(void (*proc)(void), long speed)
{
   return install_int_ex(proc, MSEC_TO_TIMER(speed));
}

extern "C" void remove_int(void (*proc)(void))
{
   if (proc)
      del_timer(proc, NULL, NULL);
}

extern "C" int install_param_int_ex(void (*proc)(void *param), void *param, long speed)
{
   if (!proc)
      return -1;
   return add_timer(NULL, proc, param, speed);
}

extern "C" int install_param_int(void (*proc)(void *param), void *param, long speed)
{
   return install_param_int_ex(proc, param, MSEC_TO_TIMER(speed));
}

extern "C" void remove_param_int(void (*proc)(void *param), void *param)
{
   if (proc)
      del_timer(NULL, proc, param);
}

// ---------------------------------------------------------------------------
// rest
// ---------------------------------------------------------------------------

static void rest_sleep(unsigned int ms)
{
#ifdef _WIN32
   // Same primitive as the reference build: granularity follows the system
   // timer resolution.
   Sleep(ms);
#else
   zcport_sleep_ms(ms);
#endif
}

extern "C" void rest_callback(unsigned int tyme, void (*callback)(void))
{
   // Lets the platform show the screen and handle the window (see
   // zcport/platform.h pump_events).
   zcport_pump_events();

   if (callback) {
      // The reference build calls the callback continuously until the time
      // (in whole milliseconds) has passed; nothing for 0.
      if (tyme == 0)
         return;
      uint32_t start = zcport_ticks_ms();
      while ((uint32_t)(zcport_ticks_ms() - start) < tyme)
         callback();
      return;
   }

   if (tyme == 0)
      zcport_sleep_ms(0);
   else
      rest_sleep(tyme);
}

extern "C" void rest(unsigned int tyme)
{
   rest_callback(tyme, NULL);
}
