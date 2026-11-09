/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/text.h - text output.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_TEXT_H
#define ALLEGRO_TEXT_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;
struct FONT;

AL_VAR(struct FONT *, font);
AL_VAR(int, allegro_404_char);   /* color module, default '^' */

AL_FUNC(void, textout_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, AL_CONST char *str, int x, int y, int color, int bg));
AL_FUNC(void, textout_centre_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, AL_CONST char *str, int x, int y, int color, int bg));
AL_FUNC(void, textout_right_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, AL_CONST char *str, int x, int y, int color, int bg));
AL_FUNC(void, textout_justify_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, AL_CONST char *str, int x1, int x2, int y, int diff, int color, int bg));
AL_PRINTFUNC(void, textprintf_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...), 7, 8);
AL_PRINTFUNC(void, textprintf_centre_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...), 7, 8);
AL_PRINTFUNC(void, textprintf_right_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...), 7, 8);
AL_PRINTFUNC(void, textprintf_justify_ex, (struct BITMAP *bmp, AL_CONST struct FONT *f, int x1, int x2, int y, int diff, int color, int bg, AL_CONST char *format, ...), 9, 10);

AL_FUNC(int, text_length, (AL_CONST struct FONT *f, AL_CONST char *str));
AL_FUNC(int, text_height, (AL_CONST struct FONT *f));
AL_FUNC(void, destroy_font, (struct FONT *f));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_TEXT_H */
