// zcport GUI: popup_dialog(), do_dialog() with what was underneath the
// dialog saved and put back afterwards.
//
// Written from scratch for zcport (no Allegro code); checked against the
// reference in test_gui_blit_procs.cpp. Needs the blit module; uses only the
// public Allegro API.

#include <allegro.h>

#include <errno.h>

extern "C" int popup_dialog(DIALOG *dialog, int focus_obj)
{
   // The area under the first object is saved (the other objects should
   // lie inside it).
   BITMAP *gui_bmp = gui_get_screen();
   BITMAP *saved = create_bitmap_ex(bitmap_color_depth(gui_bmp), dialog->w, dialog->h);

   if (saved) {
      scare_mouse_area(dialog->x, dialog->y, dialog->w, dialog->h);
      blit(gui_bmp, saved, dialog->x, dialog->y, 0, 0, dialog->w, dialog->h);
      unscare_mouse();
   }
   else {
      *allegro_errno = ENOMEM;
   }

   int ret = do_dialog(dialog, focus_obj);

   if (saved) {
      scare_mouse_area(dialog->x, dialog->y, dialog->w, dialog->h);
      blit(saved, gui_bmp, 0, 0, dialog->x, dialog->y, dialog->w, dialog->h);
      unscare_mouse();
      destroy_bitmap(saved);
   }

   return ret;
}
