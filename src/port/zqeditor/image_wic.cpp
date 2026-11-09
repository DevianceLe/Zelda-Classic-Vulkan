// zqeditor: decodes PNG, JPEG and BMP files with the Windows Imaging Component.
// 8-bit indexed images keep their pixels and palette order, which tile
// grabbing relies on; everything else is delivered as 32-bit BGRA.
//
// Written from scratch for zcport.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <stdlib.h>
#include <string.h>

#include <string>

#include "image_wic.h"

namespace
{

template<class T> void release(T *&p)
{
    if(p)
    {
        p->Release();
        p = NULL;
    }
}

std::wstring widen(const char *path)
{
    UINT page = CP_UTF8;
    int n = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);

    if(n <= 0)
    {
        page = CP_ACP;
        n = MultiByteToWideChar(page, 0, path, -1, NULL, 0);
    }

    if(n <= 0)
        return std::wstring();

    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(page, 0, path, -1, &out[0], n);
    return out;
}

bool copy_indexed(IWICImagingFactory *factory, IWICBitmapFrameDecode *frame, ZqwicImage *out)
{
    IWICPalette *palette = NULL;
    bool ok = false;

    if(SUCCEEDED(factory->CreatePalette(&palette)) && SUCCEEDED(frame->CopyPalette(palette)))
    {
        UINT count = 0;
        memset(out->palette, 0, sizeof(out->palette));

        if(SUCCEEDED(palette->GetColors(256, (WICColor *)out->palette, &count)))
        {
            size_t size = (size_t)out->w * out->h;
            out->pixels = (unsigned char *)malloc(size ? size : 1);
            ok = out->pixels &&
                 SUCCEEDED(frame->CopyPixels(NULL, out->w, (UINT)size, out->pixels));

            if(ok)
                out->indexed = 1;
            else
            {
                free(out->pixels);
                out->pixels = NULL;
            }
        }
    }

    release(palette);
    return ok;
}

bool copy_bgra(IWICImagingFactory *factory, IWICBitmapFrameDecode *frame, ZqwicImage *out)
{
    IWICFormatConverter *converter = NULL;
    bool ok = false;

    if(SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
       SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                       NULL, 0.0, WICBitmapPaletteTypeCustom)))
    {
        size_t size = (size_t)out->w * out->h * 4;
        out->pixels = (unsigned char *)malloc(size ? size : 1);
        ok = out->pixels &&
             SUCCEEDED(converter->CopyPixels(NULL, out->w * 4, (UINT)size, out->pixels));

        if(ok)
            out->indexed = 0;
        else
        {
            free(out->pixels);
            out->pixels = NULL;
        }
    }

    release(converter);
    return ok;
}

} // namespace

int zqwic_decode(const char *path, ZqwicImage *out)
{
    memset(out, 0, sizeof(*out));

    std::wstring wide = widen(path);

    if(wide.empty())
        return -1;

    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    bool ok = false;

    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory))) &&
       SUCCEEDED(factory->CreateDecoderFromFilename(wide.c_str(), NULL, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnDemand, &decoder)) &&
       SUCCEEDED(decoder->GetFrame(0, &frame)) &&
       SUCCEEDED(frame->GetSize(&out->w, &out->h)))
    {
        WICPixelFormatGUID format;

        if(SUCCEEDED(frame->GetPixelFormat(&format)) &&
           IsEqualGUID(format, GUID_WICPixelFormat8bppIndexed))
            ok = copy_indexed(factory, frame, out);

        if(!ok)
            ok = copy_bgra(factory, frame, out);
    }

    release(frame);
    release(decoder);
    release(factory);

    if(SUCCEEDED(init))
        CoUninitialize();

    return ok ? 0 : -1;
}

void zqwic_free(ZqwicImage *image)
{
    free(image->pixels);
    image->pixels = NULL;
}
