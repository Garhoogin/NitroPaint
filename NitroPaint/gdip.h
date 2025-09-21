#pragma once
#include <Windows.h>

#include "color.h"


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
HRESULT ImgWrite(const COLOR32 *px, int width, int height, LPCWSTR path);

//
// Write an indexed image to a file from a given color palette and index data.
// Index data is 8 bits per pixel with the stride equal to the image's width.
//
HRESULT ImgWriteIndexed(const unsigned char *bits, int width, int height, const COLOR32 *palette, int paletteSize, LPCWSTR path);

//
// Write an indexed image to a memory buffer.
//
HRESULT ImgWriteMemIndexed(const unsigned char *bits, int width, int height, const COLOR32 *palette, int paletteSize, void **pBuffer, unsigned int *pSize);

//
// Write an animated GIF to a file.
//
HRESULT ImgWriteAnimatedGif(LPCWSTR path, const COLOR32 *const *pFrames, int width, int height, const int *pDurations, int nFrames);


// ----- image operations


// -----------------------------------------------------------------------------------------------
// Name: enum ImgScaleSetting
//
// Image resize modes
// -----------------------------------------------------------------------------------------------
typedef enum ImgScaleSetting_ {
	IMG_SCALE_FILL,                // stretch to fill whole output, destryoing aspect ratio
	IMG_SCALE_COVER,               // stretch to cover the whole output, preserving the aspect ratio
	IMG_SCALE_FIT                  // stretch to maximize the size while keeping full visibility and aspect ratio
} ImgScaleSetting;


// -----------------------------------------------------------------------------------------------
// Name: ImgFlip
//
// Flip an image horizontally and/or vertically.
//
// Parameters:
//   px            The input pixel buffer.
//   width         The image width.
//   height        The image height.
//   hFlip         Set to 1 to flip the image horizontally.
//   vFlip         Set to 1 to flip the image vertically.
// -----------------------------------------------------------------------------------------------
void ImgFlip(
	COLOR32     *px,
	unsigned int width,
	unsigned int height,
	int          hFlip,
	int          vFlip
);

// -----------------------------------------------------------------------------------------------
// Name: ImgSwapRedBlue
//
// Swap red and blue color channels in an image.
//
// Parameters:
//   px            The input pixel buffer.
//   width         The image width.
//   height        The image height.
// -----------------------------------------------------------------------------------------------
void ImgSwapRedBlue(
	COLOR32     *px,
	unsigned int width,
	unsigned int height
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCountColors
//
// Count the number of unique colors in an image. This function treats full transparency as one
// color, regardless of what the values of RGB are for those pixels. Pixels with nonzero alpha
// values are treated as nondistinct when their RGB values are equal in value.
//
// Parameters:
//   px            The input pixel buffer.
//   nPx           The number of pixels (width*height).
//
// Returns:
//   The number of disctinct RGB colors in the input image.
// -----------------------------------------------------------------------------------------------
unsigned int ImgCountColors(
	const COLOR32 *px,
	unsigned int   nPx
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCrop
//
// Crop an image with a source bounding box. The sampling region is specified as a rectangle whose
// starting coordinates may be negative. Points sampled from out of the bounds of the input image
// are rendered as transparent pixels in the output buffer.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   srcX          The source X to begin sampling (may be negative)
//   srcY          The source Y to begin sampling (may be negative)
//   srcWidth      The source rectangle X
//   srcHeight     The source rectangle Y
//
// Returns:
//   The cropped pixel buffer. The output pixel buffer will have a size of srcWidth x srcHeight.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgCrop(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	int            srcX,
	int            srcY,
	unsigned int   srcWidth,
	unsigned int   srcHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCropInPlace
//
// Crop an image with a source bounding box and write pixel data to a buffer. The sampling region
// is specified as a rectangle whose starting coordinates may be negative. Points sampled from
// out of the bounds of the input image are rendered as transparent pixels in the output buffer.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   out           The output pixel buffer (sized srcWidth x srcHeight)
//   srcX          The source X to begin sampling (may be negative)
//   srcY          The source Y to begin sampling (may be negative)
//   srcWidth      The source rectangle X
//   srcHeight     The source rectangle Y
// -----------------------------------------------------------------------------------------------
void ImgCropInPlace(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	COLOR32       *out,
	int            srcX,
	int            srcY,
	unsigned int   srcWidth,
	unsigned int   srcHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgComposite
//
// Composite two translucent images.
//
// Parameters:
//   back          The back layer pixels
//   backWidth     The back layer width
//   backHeight    The back layer height
//   front         The front layer pixels
//   frontWidth    The front layer width
//   frontHeight   The front layer height
//   outWidth      The composited width
//   outHeight     The composited height
//
// Returns:
//   The compositde pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgComposite(
	const COLOR32 *back,
	unsigned int   backWidth,
	unsigned int   backHeight,
	const COLOR32 *front,
	unsigned int   frontWidth,
	unsigned int   frontHeight,
	unsigned int  *outWidth,
	unsigned int  *outHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCreateAlphaMask
//
// Create an alpha mask for rendering an image with a transparent region.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   pRows         The output mask number of rows
//   pStride       The output mask stride
//
// Returns:
//   The created alpha mask.
// -----------------------------------------------------------------------------------------------
unsigned char *ImgCreateAlphaMask(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	unsigned int   threshold,
	unsigned int  *pRows,
	unsigned int  *pStride
);

// -----------------------------------------------------------------------------------------------
// Name: ImgCreateColorMask
//
// This function takes an RGBA image as input and produces a bitmap color mask as a result. The
// created color mask is in 24-bit per pixel format.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   pRows         The output mask number of rows
//   pStride       The output mask stride
//
// Returns:
//   The created color mask.
// -----------------------------------------------------------------------------------------------
unsigned char *ImgCreateColorMask(
	const COLOR32 *px, 
	unsigned int   width,
	unsigned int   height,
	unsigned int  *pRows,
	unsigned int  *pStride
);

// -----------------------------------------------------------------------------------------------
// Name: ImgScale
//
// Resize an image. When downscaling, the pixels are resampled to lose as little image information
// as possible. When upscaling, pixels are preserved.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   outWidth      Width after scaling
//   outHeight     Height after scaling
//
// Returns:
//   The resulting scaleed image pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgScale(
	const COLOR32 *px,
	unsigned int   width,
	unsigned int   height,
	unsigned int   outWidth,
	unsigned int   outHeight
);

// -----------------------------------------------------------------------------------------------
// Name: ImgScaleEx
//
// Scales an image, with additional options to specify how the aspect ratio should be handled.
//
// Parameters:
//   px            Input image pixels
//   width         Input image width
//   height        Input image height
//   outWidth      Width after scaling
//   outHeight     Height after scaling
//   mode          Image scaling mode (see enum ImgScaleSetting).
//
// Returns:
//   The resulting scaleed image pixels.
// -----------------------------------------------------------------------------------------------
COLOR32 *ImgScaleEx(
	const COLOR32  *px,
	unsigned int    width,
	unsigned int    height,
	unsigned int    outWidth,
	unsigned int    outHeight,
	ImgScaleSetting mode
);
