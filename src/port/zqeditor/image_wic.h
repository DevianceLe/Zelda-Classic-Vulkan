// zqeditor: image decoding through the Windows Imaging Component. Kept apart
// from image_loaders.cpp because <windows.h> and <allegro.h> both define
// BITMAP and RGB.
//
// Written from scratch for zcport.

#ifndef ZQEDITOR_IMAGE_WIC_H
#define ZQEDITOR_IMAGE_WIC_H

struct ZqwicImage
{
    unsigned w, h;
    int indexed;              // 1: one byte per pixel, 0: four bytes (B G R A)
    unsigned char *pixels;    // release with zqwic_free()
    unsigned palette[256];    // 0xAARRGGBB, when indexed
};

// 0 on success.
int zqwic_decode(const char *path, ZqwicImage *out);
void zqwic_free(ZqwicImage *image);

#endif
