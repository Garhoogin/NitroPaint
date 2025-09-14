#pragma once

#include "color.h"

#define BALANCE_DEFAULT      20 // Balance/Color Balance default setting
#define BALANCE_MIN           1 // Balance/Color Balance minimum setting
#define BALANCE_MAX          39 // Balance/Color Balance maximum setting

#define RECLUSTER_DEFAULT     8 // Default number of reclusters applied to the color palette

#define RX_PALETTE_MAX_SIZE 256 // Maximum created color palette size

// -----------------------------------------------------------------------------------------------
// Name: enum RxFlag
//
// Flags passed to color reduction APIs.
//
// Color palette sort flags:
//   RX_FLAG_SORT_ONLY_USED      When the color palette is produced, there is the possibility that
//                               not all of the allotted space is used. These unused slots are
//                               filled with black. This parameter controls whether to include
//                               these unused slots in the final palette sorting process.
//   RX_FLAG_SORT_ALL            The whole palette is sorted.
//
// Alpha operation flags:
//   RX_FLAG_ALPHA_MODE_NONE     Alpha processing does not take place.
//   RX_FLAG_ALPHA_MODE_RESRVE   The alpha channel is considered to be binary (fully opaque or
//                               fully transparent). This assumes transparency is encoded by using
//                               a color palette entry reserved for transparent pixels.
//   RX_FLAG_ALPHA_MODE_PIXEL    The alpha channel is encoded uniquely per-pixel and is not stored
//                               in the color palette. Produced color palette entries will have
//                               their entries fully opaque.
//   RX_FLAG_ALPHA_MODE_PALETTE  The alpha channel is encoded in the color palette. 
//
// Color masking flags:
//   RX_FLAG_MASK_BITS           The created color palette data entries are converted to the
//                               nearest representable color in RGBA5551. 
//   RX_FLAG_NO_MASK_BITS        The created color palette data is not converted.
// -----------------------------------------------------------------------------------------------
typedef enum RxFlag_ {
	RX_FLAG_SORT_ALL            = (0x00<< 0), // sort the entire output palette
	RX_FLAG_SORT_ONLY_USED      = (0x01<< 0), // only sorts the used portion of the palette

	RX_FLAG_ALPHA_MODE_MASK     = (0x03<< 1), // mask for alpha modes
	RX_FLAG_ALPHA_MODE_NONE     = (0x00<< 1), // no alpha awareness
	RX_FLAG_ALPHA_MODE_RESERVE  = (0x01<< 1), // alpha is binary, and transparency is represented with a palette entry
	RX_FLAG_ALPHA_MODE_PIXEL    = (0x02<< 1), // alpha is encoded per-pixel and discarded from the palette
	RX_FLAG_ALPHA_MODE_PALETTE  = (0x03<< 1), // alpha is part of the color palette

	RX_FLAG_MASK_BITS           = (0x00<< 3), // color palette colors are masked to RGBA5551.
	RX_FLAG_NO_MASK_BITS        = (0x01<< 3), // color palette colors are not masked
} RxFlag;

typedef struct RxBalanceSetting_ {
	int balance;          // relative priority of lightness over color information (1-39)
	int colorBalance;     // relative priority of reds over greens                 (1-39)
	int enhanceColors;    // give more weight to gradient colors                   (0 or 1)
} RxBalanceSetting;

typedef struct RxDitherSetting_ {
	int dither;           // enable dithering (0 or 1)
	float diffuse;        // dithering amount (0-1)
} RxDitherSetting;

//
// Comparator for use with qsort, sortrs an array of colors by lightness.
//
int RxColorLightnessComparator(
	const void *d1,   // pointer to a COLOR32
	const void *d2    // pointer to a COLOR32
);

// -----------------------------------------------------------------------------------------------
// Name: RxCreatePalette
//
// Creates a color palette for an image.
//
// Parameters:
//   px            The image pixels.
//   width         The image width.
//   height        The image height.
//   pal           The output palette buffer.
//   nColors       The size of the color palette to create.
//
// Returns:
//   The number of colors that were created by palette generation. This number does not include
//   the black-filled placeholder colors, if they are created.
// -----------------------------------------------------------------------------------------------
int RxCreatePalette(
	const COLOR32 *px,      // the image pixels
	unsigned int   width,   // the image width
	unsigned int   height,  // the image height
	COLOR32       *pal,     // the output palette
	unsigned int   nColors  // the number of palette colors to create
);

// -----------------------------------------------------------------------------------------------
// Name: RxCreatePaletteEx
//
// Creates a color palette for an image without reserving any color slots for transparency, with
// color balance parameters.
//
// Parameters:
//   px            The image pixels.
//   width         The image width.
//   height        The image height.
//   pal           The output palette buffer.
//   nColors       The size of the color palette to create.
//   balance       Balance setting.
//   colorBalance  Color balance setting.
//   enhanceColors Enhance largely used colors.
//   flag          Color reduction flags (see enum RxFlag).
//
// Returns:
//   The number of colors that were created by palette generation. This number does not include
//   the black-filled placeholder colors, if they are created, and is not affected by the setting
//   of the RX_FLAG_SORT_ONLY_USED flag.
// -----------------------------------------------------------------------------------------------
int RxCreatePaletteEx(
	const COLOR32 *px,             // the image pixels
	unsigned int   width,          // the image width
	unsigned int   height,         // the image height
	COLOR32       *pal,            // the output palette
	unsigned int   nColors,        // the number of palette colors to create
	int            balance,        // the balance setting
	int            colorBalance,   // the color balance setting
	int            enhanceColors,  // enhance largely used colors
	RxFlag         flag            // color reduction flags
);

// -----------------------------------------------------------------------------------------------
// Name: RxCreatePaletteTransparentReserve
//
// Creates a color palette for an image, reserving the first color slot for transparency, whether
// or not it contains any transparent pixels.
//
// Parameters:
//   px            The image pixels.
//   width         The image width.
//   height        The image height.
//   pal           The output palette buffer.
//   nColors       The size of the color palette to create.
// -----------------------------------------------------------------------------------------------
void RxCreatePaletteTransparentReserve(
	const COLOR32 *px,      // the image pixels
	unsigned int   width,   // the image width
	unsigned int   height,  // the image height
	COLOR32       *pal,     // the output palette
	unsigned int   nColors  // the number of palette colors to create
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteFindClosestColorSimple
//
// Finds the closest color in a palette to the specified color. If the specified color appears
// in the color palette, its index is returned. Otherwise, the nearest color in terms of a
// weighted YUV difference is used to estimate the nearest color.
//
// Parameters:
//   rgb           The color to match.
//   palette       The color palette to search.
//   nColors       The size of the color palette to search.
//
// Returns:
//   The index of the nearest color to rgb in the color palette.
// -----------------------------------------------------------------------------------------------
int RxPaletteFindClosestColorSimple(
	COLOR32        rgb,     // the color to match
	const COLOR32 *palette, // the input color palette
	unsigned int   nColors  // the input color palette size
);

// -----------------------------------------------------------------------------------------------
// Name: doDiffuse
//
// Apply Floyd-Steinberg dithering to a pixel's surroundings. Deprecated.
// -----------------------------------------------------------------------------------------------
void doDiffuse(
	int           i,
	int           width,
	int           height,
	unsigned int *pixels,
	int           errorRed,
	int           errorGreen,
	int           errorBlue,
	int           errorAlpha,
	float         amt
);

// -----------------------------------------------------------------------------------------------
// Name: RxReduceImage
//
// Reduce the colors of image according to a given color palette.
//
// Parameters:
//   px            The image pixels.
//   width         The image width.
//   height        The image height.
//   palette       The color palette with which to reduce the image.
//   nColors       The number of colors in the color palette.
//   touchAlpha    Set to 1 to modify the pixel alpha values.
//   binaryAlpha   Set to 1 to indicate that alpha values are binary, or 0 otherwise.
//   c0xp          Set to 1 to indicate that color index 0 is reserved for transparency.
//   diffuse       The error diffusion amount, from 0 to 1. Set to 0 to disable dithering.
// -----------------------------------------------------------------------------------------------
void RxReduceImage(
	COLOR32       *px,           // the image pixels
	unsigned int   width,        // the image width
	unsigned int   height,       // the image height
	const COLOR32 *palette,      // the color palette
	unsigned int   nColors,      // number of colors in the palette
	int            touchAlpha,   // modifies the alpha channel of the image
	int            binaryAlpha,  // alpha values are binary
	int            c0xp,         // color 0 is transparent
	float          diffuse       // the error diffusion amount (from 0 to 1)
);

// -----------------------------------------------------------------------------------------------
// Name: RxReduceImageEx
//
// Reduce the colors of image according to a given color palette. This function optionally writes
// the indexed color values to an array with the same dimension as the input image.
//
// Parameters:
//   px            The image pixels.
//   indices       The output indexed buffer (optional). This may be set to NULL.
//   width         The image width.
//   height        The image height.
//   palette       The color palette with which to reduce the image.
//   nColors       The number of colors in the color palette.
//   touchAlpha    Set to 1 to modify the pixel alpha values.
//   binaryAlpha   Set to 1 to indicate that alpha values are binary, or 0 otherwise.
//   c0xp          Set to 1 to indicate that color index 0 is reserved for transparency.
//   diffuse       The error diffusion amount, from 0 to 1. Set to 0 to disable dithering.
//   balance       The balance setting.
//   colorBalance  The color balance setting.
//   enhanceColors Enhance largely used colors.
// -----------------------------------------------------------------------------------------------
void RxReduceImageEx(
	COLOR32       *px,            // the image pixels
	int           *indices,       // the output palette index data (optional)
	unsigned int   width,         // the image width
	unsigned int   height,        // the image height
	const COLOR32 *palette,       // the color palette
	unsigned int   nColors,       // the color palette size
	int            touchAlpha,    // modifies the alpha channel of the image
	int            binaryAlpha,   // alpha values are binary
	int            c0xp,          // color 0 is transparent
	float          diffuse,       // the error diffusion amount (from 0 to 1)
	int            balance,       // the balance setting
	int            colorBalance,  // the color balance setting
	int            enhanceColors  // enhance largely used colors
);

// -----------------------------------------------------------------------------------------------
// Name: RxCreateMultiplePalettes
//
// Creates multiple palettes for an image for character map color reduction.
//
// Parameters:
//   px              The image pixels.
//   tilesX          The image width, in 8-pixel units.
//   tilesY          The image height, in 8-pixel units.
//   dest            The output color palette buffer.
//   paletteBase     The index of the first palette to generate.
//   nPalettes       The number of palettes to generate. This must be between 1 and 16.
//   paletteSize     The size of a color palette in the output.
//   nColsPerPalette The number of colors to generate for each palette.
//   paletteOffset   The index into a color palette of the first usable color.
//   progress        The output progress.
// -----------------------------------------------------------------------------------------------
void RxCreateMultiplePalettes(
	const COLOR32 *px,               // the image pixels
	unsigned int   tilesX,           // the image width in 8-pixel blocks
	unsigned int   tilesY,           // the image height in 8-pixel blocks
	COLOR32       *dest,             // the palette destination
	int            paletteBase,      // the base palette index
	int            nPalettes,        // the number of palettes
	int            paletteSize,      // the full size of one palette entry
	int            nColsPerPalette,  // the number of colors to create per palette
	int            paletteOffset,    // the offset into the palette to write colors
	int           *progress          // pointer to current progress
);


// -----------------------------------------------------------------------------------------------
// Name: RxCreateMultiplePalettesEx
//
// Creates multiple palettes for an image for character map color reduction with user-provided
// balance, color balance, and color enhancement settings.
//
// Parameters:
//   px              The image pixels.
//   tilesX          The image width, in 8-pixel units.
//   tilesY          The image height, in 8-pixel units.
//   dest            The output color palette buffer.
//   paletteBase     The index of the first palette to generate.
//   nPalettes       The number of palettes to generate. This must be between 1 and 16.
//   paletteSize     The size of a color palette in the output.
//   nColsPerPalette The number of colors to generate for each palette.
//   paletteOffset   The index into a color palette of the first usable color.
//   balance         The balance setting.
//   colorBalance    The color balance setting.
//   enhanceColors   Enhance largely used colors.
//   progress        The output progress.
// -----------------------------------------------------------------------------------------------
void RxCreateMultiplePalettesEx(
	const COLOR32 *px,               // the image pixels
	unsigned int   tilesX,           // the image width in 8-pixel units
	unsigned int   tilesY,           // the image height in 8-pixel units
	COLOR32       *dest,             // the palette destination
	int            paletteBase,      // the base palette index
	int            nPalettes,        // the number of palettes
	int            paletteSize,      // the full size of one palette entry
	int            nColsPerPalette,  // the number of colors to create per palette
	int            paletteOffset,    // the offset into the palette to write colors
	int            balance,          // the balance setting
	int            colorBalance,     // the color balance setting
	int            enhanceColors,    // enhance largely used colors
	int           *progress          // pointer to current progress
);

// -----------------------------------------------------------------------------------------------
// Name: RxConvertRgbToYuv
//
// Convert an RGB color to YUV space.
//
// Parameters:
//   r,g,b         The input RGB color, with components from 0-255.
//   y,u,v         The output YUV color.
// -----------------------------------------------------------------------------------------------
void RxConvertRgbToYuv(
	int  r,  // input color R
	int  g,  // input color G
	int  b,  // input color B
	int *y,  // output color Y
	int *u,  // output color U
	int *v   // output color V
);

// -----------------------------------------------------------------------------------------------
// Name: RxConvertYuvToRgb
//
// Convert a YUV color to RGB space.
//
// Parameters:
//   y,u,v         The input YUV color.
//   r,g,b         The output RGB color.
// -----------------------------------------------------------------------------------------------
void RxConvertYuvToRgb(
	int  y,  // input color Y
	int  u,  // input color U
	int  v,  // input color V
	int *r,  // output color R
	int *g,  // output color G
	int *b   // output color B
);


//----------structures used by palette generator

typedef enum RxAlphaMode_ {
	RX_ALPHA_NONE,        // alpha processing is not done.
	RX_ALPHA_RESERVE,     // alpha values are binary, and represented by a reserved color.
	RX_ALPHA_PIXEL,       // alpha values are part of the bitmap data.
	RX_ALPHA_PALETTE      // alpha values are part of the color palette.
} RxAlphaMode;

typedef struct RxRgbColor_ {
	int r;
	int g;
	int b;
	int a;
} RxRgbColor;

typedef struct RxYiqColor_ {
	int y;
	int i;
	int q;
	int a;
} RxYiqColor;

//histogram linked list entry as secondary sorting
typedef struct RxHistEntry_ {
	RxYiqColor color;
	struct RxHistEntry_ *next;
	int entry;
	double weight;
	double value;
} RxHistEntry;

//structure for a node in the color tree
typedef struct RxColorNode_ {
	RxYiqColor color;
	double weight;
	double priority;
	int canSplit;
	int pivotIndex;
	int startIndex;
	int endIndex;
	struct RxColorNode_ *left;
	struct RxColorNode_ *right;
} RxColorNode;

//allocator for allocating the linked lists
typedef struct RxSlab_ {
	void *allocation;
	unsigned int pos;
	struct RxSlab_ *next;
} RxSlab;

//histogram structure
typedef struct RxHistogram_ {
	RxSlab allocator;
	RxHistEntry *entries[0x20000];
	int nEntries;
	int firstSlot;
} RxHistogram;

//struct for totaling a bucket in reclustering
typedef struct {
	double y;
	double i;
	double q;
	double a;
	double weight;
	double error;
} RxTotalBuffer;

//reduction workspace structure
typedef struct RxReduction_ {
	double yWeight;
	double iWeight;
	double qWeight;
	double aWeight;
	double yWeight2;
	double iWeight2;
	double qWeight2;
	double aWeight2;
	int nPaletteColors;
	int nUsedColors;
	int enhanceColors;
	int nReclusters;
	COLOR32 (*maskColors) (COLOR32 col);
	RxAlphaMode alphaMode;
	unsigned int alphaThreshold;
	RxHistogram *histogram;
	RxHistEntry **histogramFlat;
	RxTotalBuffer blockTotals[RX_PALETTE_MAX_SIZE];
	RxColorNode *colorTreeHead;
	RxColorNode *colorBlocks[RX_PALETTE_MAX_SIZE];
	COLOR32 paletteRgb[RX_PALETTE_MAX_SIZE];
	COLOR32 paletteRgbCopy[RX_PALETTE_MAX_SIZE];
	RxYiqColor paletteYiq[RX_PALETTE_MAX_SIZE];
	RxYiqColor paletteYiqCopy[RX_PALETTE_MAX_SIZE];
	double lumaTable[512];
	double gamma;
} RxReduction;

// -----------------------------------------------------------------------------------------------
// Name: RxConvertRgbToYiq
//
// Encode an RGBA color to a YIQA color.
//
// Parameters:
//   rgb           The input RGB color.
//   yiq           The output YIQ color.
// -----------------------------------------------------------------------------------------------
void RxConvertRgbToYiq(
	COLOR32 rgb,     // the input RGB color
	RxYiqColor *yiq  // the output YIQ color
);

// -----------------------------------------------------------------------------------------------
// Name: RxConvertYiqToRgb
//
// Decode a YIQ color to RGB.
//
// Parameters:
//   rgb           The output RGB color.
//   yiq           The input YIQ color.
// -----------------------------------------------------------------------------------------------
void RxConvertYiqToRgb(
	RxRgbColor *rgb,       // the output RGB color
	const RxYiqColor *yiq  // the input YIQ color
);

// -----------------------------------------------------------------------------------------------
// Name: RxInit
//
// Initialize a RxReduction structure with palette parameters.
//
// Parameters:
//   reduction     The color reduction context.
//   balance       The balance setting.
//   colorBalance  The color balance setting.
//   enhanceColors Enhance largely used colors.
//   nColors       The number of colors to generate in a palette.
// -----------------------------------------------------------------------------------------------
void RxInit(
	RxReduction *reduction,      // the color reduction context
	int          balance,        // the balance setting
	int          colorBalance,   // the color balance setting
	int          enhanceColors,  // assign more weight to frequently occurring colors
	unsigned int nColors         // the number of colors to set the reduction up to calculate
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistAdd
//
// Add an image's color data to the histogram of a color reduction context.
//
// Parameters:
//   reduction     The color reduction context.
//   px            The input image pixels.
//   width         The image width.
//   height        The image height.
// -----------------------------------------------------------------------------------------------
void RxHistAdd(
	RxReduction   *reduction,  // the color reduction context
	const COLOR32 *px,         // the image pixels
	unsigned int   width,      // the image width
	unsigned int   height      // the image height
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistSort
//
// Runs principal component analysis on a range of colors in the histogram and sorts them by
// their position along the axis.
//
// Parameters:
//   reduction     The color reduction context.
//   startIndex    The index of the first histogram entry to sort.
//   endIndex      The index of the last (non-inclusive) entry in the histogram to sort.
// -----------------------------------------------------------------------------------------------
void RxHistSort(
	RxReduction *reduction,   // the color reduction context
	int          startIndex,  // the first index in the histogram to sort
	int          endIndex     // the last (non-inclusive) index in the histogram to sort
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistClear
//
// Clears out a color reduction context's histogram. Can be used to create multiple palettes.
//
// Parameters:
//   reduction     The color reduction context.
// -----------------------------------------------------------------------------------------------
void RxHistClear(
	RxReduction *reduction  // the color reduction context
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistFinalize
//
// Finalizes the histogram of a color reduction context. Call this function once all of the colors
// to be added to the histogram have been added to the histogram. Colors may not be added after
// calling this function.
//
// Parameters:
//   reduction     The color reduction context.
// -----------------------------------------------------------------------------------------------
void RxHistFinalize(
	RxReduction *reduction  // the color reduction context
);

// -----------------------------------------------------------------------------------------------
// Name: RxComputePalette
//
// Compute a color palette using the histogram held by the color reduction context. Before calling
// this function, the histogram must be finalized by a call to RxHistFinalize. 
//
// Parameters:
//   reduction     The color reduction context.
// -----------------------------------------------------------------------------------------------
void RxComputePalette(
	RxReduction *reduction  // the color reduction context
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteFindCloestColorYiq
//
// Find the closest YIQA color to a specified YIQA color with a provided reduction context.
//
// Parameters:
//   reduction     The color reduction context.
//   yiqColor      The color to search the palette for.
//   palette       The color palette to search.
//   nColors       The size of the color palette.
//
// Returns:
//   The index of the closest color to the input color in the supplied color palette.
// -----------------------------------------------------------------------------------------------
int RxPaletteFindCloestColorYiq(
	RxReduction      *reduction,  // the color reduction context
	const RxYiqColor *yiqColor,   // the color to match
	const RxYiqColor *palette,    // the color palette
	unsigned int      nColors     // the color palette size
);

// -----------------------------------------------------------------------------------------------
// Name: RxComputePaletteError
//
// Compute palette error on a bitmap given a specified reduction context.
//
// Parameters:
//   reduction      The color reduction context.
//   px             The image pixels.
//   nPx            The number of image pixels.
//   palette        The color palette, as RGB colors.
//   nColors        The number of palette colors.
//   alphaThreshold The threshold with which translucent pixels are translated into binary levels.
//                  A source alpha less than the threshold is converted to an alpha value of 0,
//                  and an alpha greater than or equal to the threshold is converted to 1.
//   maxError       The maximum error. When the error would be above maxError, it is truncated to
//                  maxError.
//
// Returns:
//   The total palette error for the color palette applied to the input image, or maxError,
//   whichever is lesser.
// -----------------------------------------------------------------------------------------------
double RxComputePaletteError(
	RxReduction   *reduction,       // the color reduction context
	const COLOR32 *px,              // the input pixels
	unsigned int   nPx,             // the number of pixels
	const COLOR32 *palette,         // the color palette
	unsigned int   nColors,         // the number of colors in the palette
	int            alphaThreshold,  // the minimum alpha value to be converted to opaque
	double         maxError         // the maximum error value to return
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistComputePaletteError
//
// Computes the total error for a color palette on the reduction context's current histogram.
//
// Parameters:
//   reduction     The color reduction context.
//   palette       The color palette, as RGB colors.
//   nColors       The number of palette colors.
//   maxError      The maximum error. When the error would be above maxError, it is truncated to
//                 maxError.
//
// Returns:
//   The total palette error for the color palette applied to the input image, or maxError,
//   whichever is lesser.
// -----------------------------------------------------------------------------------------------
double RxHistComputePaletteError(
	RxReduction   *reduction,  // the color reduction context
	const COLOR32 *palette,    // the color palette
	unsigned int   nColors,    // the number of colors in the palette
	double         maxError    // the maximum error value to return
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistComputePaletteErrorYiq
//
// Compute palette error on a histogram for a YIQ palette.
//
// Parameters:
//   reduction     The color reduction context.
//   yiqPalette    The color palette, as YIQ colors.
//   nColors       The number of palette colors.
//   maxError      The maximum error. When the error would be above maxError, it is truncated to
//                 maxError.
//
// Returns:
//   The total palette error for the color palette applied to the reduction context's histogram,
//   or maxError, whichever is lesser.
// -----------------------------------------------------------------------------------------------
double RxHistComputePaletteErrorYiq(
	RxReduction      *reduction,   // the color reduction context
	const RxYiqColor *yiqPalette,  // the color palette
	unsigned int      nColors,     // the number of colors in the palette
	double            maxError     // the maximum error value to return
);

// -----------------------------------------------------------------------------------------------
// Name: RxDestroy
//
// Frees all resources held by a color reduction context.
//
// Parameters:
//   reduction     The color reduction context to be freed
// -----------------------------------------------------------------------------------------------
void RxDestroy(
	RxReduction *reduction // the color reduction context
);
