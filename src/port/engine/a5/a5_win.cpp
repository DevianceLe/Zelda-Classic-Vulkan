// The Windows-only pieces, kept apart so windows.h's BITMAP doesn't meet zcport's.
#include <allegro5/allegro_windows.h>

#include "image_wic.h"

HWND al_get_win_window_handle(ALLEGRO_DISPLAY*) { return nullptr; }

// PNG, JPEG and BMP through the Windows Imaging Component, premultiplied.
ALLEGRO_BITMAP* al_load_bitmap(const char* path)
{
	ZqwicImage image;
	if (zqwic_decode(path, &image) != 0)
		return nullptr;

	ALLEGRO_BITMAP* b = nullptr;
	if (!image.indexed)
	{
		b = al_create_bitmap((int)image.w, (int)image.h);
		ALLEGRO_LOCKED_REGION* lr = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ANY, ALLEGRO_LOCK_WRITEONLY);
		const unsigned char* p = image.pixels;
		for (unsigned y = 0; y < image.h; y++)
		{
			uint32_t* out = (uint32_t*)((unsigned char*)lr->data + (size_t)y * lr->pitch);
			for (unsigned x = 0; x < image.w; x++, p += 4)
			{
				uint32_t a = p[3];
				uint32_t r = p[2] * a / 255, g = p[1] * a / 255, bl = p[0] * a / 255;
				out[x] = r | (g << 8) | (bl << 16) | (a << 24);
			}
		}
		al_unlock_bitmap(b);
	}
	zqwic_free(&image);
	return b;
}
