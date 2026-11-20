//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  aglogo.h
//
//  Armageddon games logo with fire field generator.
//  Adapted from code written by Midnight and included in
//  SPHINX C-- examples.
//  (Tweaked by Jeremy Craner.)
//
//--------------------------------------------------------

#ifndef _AGLOGO_H_
#define _AGLOGO_H_

extern DATAFILE* data;

int aglogo(BITMAP *frame, BITMAP *firebuf, int resx, int resy);

// The ZQuest Classic logo of the 3.0 intro and title screen (assets/zc/intro_logo.dat): the
// whole logo, the logo without its sword, and the sword alone. A pixel's value is its colour in
// pal (1 to colours); 0 is see-through.
struct Zc3Logo
{
    int w, h, colours;
    RGB pal[256];
    BITMAP *layer[3];
};

bool Zc3LogoLoad(Zc3Logo &logo, int height, int max_colours);
void Zc3LogoFree(Zc3Logo &logo);
#endif
 
