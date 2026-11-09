/*
 * zcport: Microsoft's rand()/srand() for the game on other systems.
 *
 * The Windows build uses the C runtime's rand(): a linear congruential
 * generator (state * 214013 + 2531011, bits 16..30 returned, RAND_MAX
 * 32767), seeded with 1. On Linux/Android the game sources are compiled
 * with zcport/lp64.h, which maps rand/srand here, so the game gets the same
 * sequence of random numbers as on Windows.
 *
 * Written from scratch for zcport.
 */

#include <stdint.h>

namespace
{
uint32_t g_state = 1;
}

extern "C" {

int zcport_msvc_rand(void)
{
    g_state = g_state * 214013u + 2531011u;
    return (int)((g_state >> 16) & 0x7FFFu);
}

void zcport_msvc_srand(unsigned int seed)
{
    g_state = seed;
}

} // extern "C"
