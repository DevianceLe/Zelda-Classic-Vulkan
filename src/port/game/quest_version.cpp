// See quest_version.h. Built into both the player and ZQuest.

#include "precompiled.h" //always first

#include <stdio.h>

#include "quest_version.h"

extern char q255_quest_built_with[];    // q255_basic.cpp

std::string zc_quest_built_with(int v, int b)
{
    char buf[16] = "";

    if(v >= 0x255)
        return q255_quest_built_with[0] ? q255_quest_built_with : "2.55";

    if(v == 0x250)
    {
        return b <= 23 ? "2.50 beta" : b == 24 ? "2.50.0" : b <= 28 ? "2.50.1" : b == 29 ? "2.50.2"
             : b == 30 ? "2.50.3" : b <= 32 ? "2.53.0" : b == 33 ? "2.53.1" : "";
    }

    if(v >= 0x100 && v < 0x250)
        sprintf(buf, "%x.%02x", v>>8, v&0xFF);

    return buf;
}
