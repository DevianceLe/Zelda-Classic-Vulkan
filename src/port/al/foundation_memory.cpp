// zcport foundation: memory helpers, emulated C library functions and the
// internal random number generator.
//
// Written from scratch for zcport (no Allegro code).

#include "foundation_internal.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

extern "C" void *_al_malloc(size_t size)
{
   return malloc(size);
}

extern "C" void _al_free(void *mem)
{
   free(mem);
}

extern "C" void *_al_realloc(void *mem, size_t size)
{
   return realloc(mem, size);
}

extern "C" char *_al_strdup(const char *string)
{
   if (!string)
      return NULL;
   size_t n = strlen(string) + 1;
   char *s = (char *)malloc(n);
   if (s)
      memcpy(s, string, n);
   return s;
}

extern "C" void *_al_sane_realloc(void *ptr, size_t size)
{
   if (size == 0) {
      free(ptr);
      return NULL;
   }
   if (!ptr)
      return malloc(size);
   void *p = realloc(ptr, size);
   if (!p)
      free(ptr);
   return p;
}

extern "C" char *_al_sane_strncpy(char *dest, const char *src, size_t n)
{
   if (n == 0)
      return dest;
   size_t i = 0;
   for (; i + 1 < n && src[i]; i++)
      dest[i] = src[i];
   dest[i] = 0;
   return dest;
}

extern "C" int _alemu_stricmp(const char *s1, const char *s2)
{
   for (;;) {
      int c1 = tolower((unsigned char)*s1++);
      int c2 = tolower((unsigned char)*s2++);
      if (c1 != c2)
         return c1 - c2;
      if (!c1)
         return 0;
   }
}

extern "C" char *_alemu_strlwr(char *string)
{
   for (char *p = string; *p; p++)
      *p = (char)tolower((unsigned char)*p);
   return string;
}

extern "C" char *_alemu_strupr(char *string)
{
   for (char *p = string; *p; p++)
      *p = (char)toupper((unsigned char)*p);
   return string;
}

// A small linear congruential generator for library internals (never the
// game's rand()).
static unsigned int g_al_rand_state = 1;

extern "C" void _al_srand(int seed)
{
   g_al_rand_state = (unsigned int)seed;
}

extern "C" int _al_rand(void)
{
   g_al_rand_state = g_al_rand_state * 1103515245u + 12345u;
   return (int)((g_al_rand_state >> 16) & _AL_RAND_MAX);
}
