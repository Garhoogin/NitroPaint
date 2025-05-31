#pragma once
#include <Windows.h>

#include "color.h"

//
// Image resize modes
//
typedef enum ImgScaleSetting_ {
	IMG_SCALE_FILL,                // stretch to fill whole output, destryoing aspect ratio
	IMG_SCALE_COVER,               // stretch to cover the whole output, preserving the aspect ratio
	IMG_SCALE_FIT                  // stretch to maximize the size while keeping full visibility and aspect ratio
} ImgScaleSetting;


int ImgIsValidTGA(const unsigned char *buffer, unsigned int dwSize);

COLOR32 *ImgReadEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize);

//
// Read an image and return an array of pixels and size.
//
COLOR32 *ImgRead(LPCWSTR path, int *pWidth, int *pHeight);

//
// Read an image from a memory block.
//
COLOR32 *ImgReadMemEx(const unsigned char *buffer, unsigned int size, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize);

//
// Read an image from a memory block.
// 
COLOR32 *ImgReadMem(const unsigned char *buffer, unsigned int size, int *pWidth, int *pHeight);

//
// Write an array of pixels to an image file.
//
HRESULT ImgWrite(COLOR32 *px, int width, int height, LPCWSTR path);

//
// Write an indexed image to a file from a given color palette and index data.
// Index data is 8 bits per pixel with the stride equal to the image's width.
//
HRESULT ImgWriteIndexed(unsigned char *bits, int width, int height, COLOR32 *palette, int paletteSize, LPCWSTR path);

//
// Write an animated GIF to a file.
//
HRESULT ImgWriteAnimatedGif(LPCWSTR path, const COLOR32 *const *pFrames, int width, int height, const int *pDurations, int nFrames);

//
// Flip an image horizontally and/or vertically.
//
void ImgFlip(COLOR32 *px, int width, int height, int hFlip, int vFlip);

//
// Swap red and blue color channels in an image.
//
void ImgSwapRedBlue(COLOR32 *px, int width, int height);

//
// Count the number of unique colors in an image (counting transparent as a color), and otherwise ignoring the alpha channel.
//
int ImgCountColors(COLOR32 *px, int nPx);

//
// Crop an image with a source bounding box.
//
COLOR32 *ImgCrop(COLOR32 *px, int width, int height, int srcX, int srcY, int srcWidth, int srcHeight);

//
// Crop an image with a source bounding box and write pixel data to a buffer.
//
void ImgCropInPlace(COLOR32 *px, int width, int height, COLOR32 *out, int srcX, int srcY, int srcWidth, int srcHeight);

//
// Composite two translucent images.
//
COLOR32 *ImgComposite(COLOR32 *back, int backWidth, int backHeight, COLOR32 *front, int frontWidth, int frontHeight, int *outWidth, int *outHeight);

//
// Create an alpha mask for rendering an image with a transparent region.
//
unsigned char *ImgCreateAlphaMask(COLOR32 *px, int width, int height, unsigned int threshold, int *pRows, int *pStride);

//
// Create a color mask for a bitmap.
//
unsigned char *ImgCreateColorMask(COLOR32 *px, int width, int height, int *pRows, int *pStride);

//
// Resize an image. When downscaling, the pixels are resampled to lose as little image information
// as possible. When upscaling, pixels are preserved.
//
COLOR32 *ImgScale(COLOR32 *px, int width, int height, int outWidth, int outHeight);

//
// Scales an image, with additional options to specify how the aspect ratio should be handled.
//
COLOR32 *ImgScaleEx(COLOR32 *px, int width, int height, int outWidth, int outHeight, ImgScaleSetting setting);
