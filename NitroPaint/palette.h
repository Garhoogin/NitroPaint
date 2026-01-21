#pragma once

#include "color.h"

//use of intrinsics under x86
#if defined(_M_IX86) || defined(_M_X64)
#define RX_SIMD
#ifdef _MSC_VER
#include <intrin.h>
#else // _MSC_VER
#include <x86intrin.h>
#endif
#endif

#define BALANCE_DEFAULT      20 // Balance/Color Balance default setting
#define BALANCE_MIN           1 // Balance/Color Balance minimum setting
#define BALANCE_MAX          39 // Balance/Color Balance maximum setting

#define RECLUSTER_DEFAULT     8 // Default number of reclusters applied to the color palette

#define RX_PALETTE_MAX_SIZE 256 // Maximum created color palette size

#define RX_GAMMA 1.27

// -----------------------------------------------------------------------------------------------
// Name: enum RxFlag
//
// Represents the current operational status of a color reduction context or operation. The status
// values are returned from functions operating on a color reduction context, and stored in the
// context until cleared. When a status other than RX_STATUS_OK is set in a context, it is in an
// error state, and all subsequent operations except for those resetting the context or releasing
// resources become no-operations. No-op'ed function calls return the last set status.
//
// Status values:
//   RX_STATUS_OK                The operation was completed successfully.
//   RX_STATUS_NOMEM             The operation could not be completed because of insufficient
//                               memory.
//   RX_STATUS_INVALID           One or more of the parameters were invalid. This status code is
//                               not retained and only returned by the API that generated the
//                               error.
// -----------------------------------------------------------------------------------------------
typedef enum RxStatus_ {
	RX_STATUS_OK,                             // The operation was successful
	RX_STATUS_NOMEM,                          // The operation failed due to insufficient memory
	RX_STATUS_INVALID                         // The parameters were invalid
} RxStatus;

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
//
// Color reduction flags:
//   RX_FLAG_PRESERVE_ALPHA      During color reduction, the alpha channel is not modified.
//   RX_FLAG_NO_PRESERVE_ALPHA   During color reduction, the alpha channel is modified.
//   RX_FLAG_NO_WRITEBACK        Color reduction will not write back the RGB pixel data.
//   RX_FLAG_NO_ALPHA_DITHER     The alpha channel will not be dithered (when using pixel or
//                               palette alpha mode).
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

	RX_FLAG_PRESERVE_ALPHA      = (0x00<< 4), // leaves the alpha channel unaffected in a color reduction operation.
	RX_FLAG_NO_PRESERVE_ALPHA   = (0x01<< 4), // modifies the alpha channel in a color reduction operation.
	RX_FLAG_NO_WRITEBACK        = (0x01<< 5), // suppresses writeback of RGB pixel data in color reduction
	RX_FLAG_NO_ALPHA_DITHER     = (0x01<< 6), // the alpha channel will not be dithered
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


#if defined(RX_SIMD) && !defined(_M_X64)
void *RxMemAlloc(size_t size);
void *RxMemCalloc(size_t nMemb, size_t size);
void RxMemFree(void *p);
#else // RX_SIMD
#define RxMemAlloc  malloc
#define RxMemCalloc calloc
#define RxMemFree   free
#endif

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
//   The completed operation status.
// -----------------------------------------------------------------------------------------------
RxStatus RxCreatePalette(
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
//   pOutCols      Pointer to output number of colors (may be NULL).
//
// Returns:
//   The completed operation status.
// -----------------------------------------------------------------------------------------------
RxStatus RxCreatePaletteEx(
	const COLOR32 *px,             // the image pixels
	unsigned int   width,          // the image width
	unsigned int   height,         // the image height
	COLOR32       *pal,            // the output palette
	unsigned int   nColors,        // the number of palette colors to create
	int            balance,        // the balance setting
	int            colorBalance,   // the color balance setting
	int            enhanceColors,  // enhance largely used colors
	RxFlag         flag,           // color reduction flags
	unsigned int  *pOutCols        // number of output colors
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
//   c0xp          Set to 1 to indicate that color index 0 is reserved for transparency.
//   diffuse       The error diffusion amount, from 0 to 1. Set to 0 to disable dithering.
// -----------------------------------------------------------------------------------------------
RxStatus RxReduceImage(
	COLOR32       *px,           // the image pixels
	unsigned int   width,        // the image width
	unsigned int   height,       // the image height
	const COLOR32 *palette,      // the color palette
	unsigned int   nColors,      // number of colors in the palette
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
//   flag          Color reduction flag.
//   diffuse       The error diffusion amount, from 0 to 1. Set to 0 to disable dithering.
//   balance       The balance setting.
//   colorBalance  The color balance setting.
//   enhanceColors Enhance largely used colors.
// -----------------------------------------------------------------------------------------------
RxStatus RxReduceImageEx(
	COLOR32       *px,            // the image pixels
	int           *indices,       // the output palette index data (optional)
	unsigned int   width,         // the image width
	unsigned int   height,        // the image height
	const COLOR32 *palette,       // the color palette
	unsigned int   nColors,       // the color palette size
	RxFlag         flag,          // color reduction flags
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
//   useColor0       Enables using color 0 of the palette as an opaque color.
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
	int            useColor0,        // use color 0 of the palette for reduction
	int            balance,          // the balance setting
	int            colorBalance,     // the color balance setting
	int            enhanceColors,    // enhance largely used colors
	int           *progress          // pointer to current progress
);


//----------structures used by palette generator

typedef struct RxReduction_ RxReduction;

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

typedef union RxYiqColor_ {
	struct {
		float y;
		float i;
		float q;
		float a;
	};
#ifdef RX_SIMD
	__m128 yiq;
#endif
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
	double totalWeight;
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
	unsigned int count;
} RxTotalBuffer;

typedef struct RxPaletteMapEntry_ {
	RxYiqColor color;
	unsigned int index;
	double sortVal;
} RxPaletteMapEntry;

typedef struct RxPaletteAccelNode_ {
	struct RxPaletteAccelNode_ *pLeft;    // left pointer
	struct RxPaletteAccelNode_ *pRight;   // right pointer
	struct RxPaletteAccelNode_ *parent;   // parent node
	RxPaletteMapEntry *mid;               // mid color
	double splitVal;                      // value of split
	unsigned int nCol;                    // number of colors this node
	unsigned int start;                   // start index of color
	unsigned int splitDir;                // split direction (Y,I,Q,A)
} RxPaletteAccelNode;

typedef struct RxPaletteAccelerator_ {
	int initialized;                      // marks that a palette has been loaded
	int useAccelerator;                   // marks that the loaded palette is using the accelerator
	RxPaletteAccelNode root;              // the root node of the accelerator
	RxPaletteMapEntry *pltt;              // palette mapping entries used by the accelerator
	RxPaletteAccelNode *nodebuf;          // accelerator working memory

	RxYiqColor plttSmall[16];             // palette buffer used for small palettes
	RxYiqColor *plttLarge;                // pointer to palette buffer (heap allocated or pointer to small)
	unsigned int nPltt;                   // number of palette colors loaded
} RxPaletteAccelerator;

//progress update callback function
typedef void (*RxProgressCallback) (RxReduction *reduction, unsigned int progress, unsigned int progressMax, void *data);

//reduction workspace structure
struct RxReduction_ {
	double yWeight;
	double iWeight;
	double qWeight;
	double aWeight;
	union {
		struct {
			double yWeight2;
			double iWeight2;
			double qWeight2;
			double aWeight2;
		};
#ifdef RX_SIMD
		struct {
			__m128d yiWeight2;
			__m128d qaWeight2;
			__m128 yiqaWeight2; // YIQA weights packed into singles
		};
#endif
	};
	union {
		struct {
			double interactionY;
			double interactionI;
			double interactionQ;
			double interactionA;
		};
#ifdef RX_SIMD
		struct {
			__m128d interactionYI;
			__m128d interactionQA;
			__m128 interactionYIQA;
		};
#endif
	};
	RxStatus status;
	int nPaletteColors;
	int nUsedColors;
	int enhanceColors;
	int nReclusters;
	int reclusterIteration;
	int nPinnedClusters;
	double lastSSE;
	COLOR32 (*maskColors) (COLOR32 col);
	RxAlphaMode alphaMode;
	float fAlphaThreshold;
	RxHistogram *histogram;
	RxHistEntry **histogramFlat;
	RxPaletteAccelerator accel;
	unsigned int newCentroids[RX_PALETTE_MAX_SIZE];
	RxTotalBuffer blockTotals[RX_PALETTE_MAX_SIZE];
	RxColorNode *colorTreeHead;
	RxColorNode *colorBlocks[RX_PALETTE_MAX_SIZE];
	COLOR32 paletteRgb[RX_PALETTE_MAX_SIZE];
	COLOR32 paletteRgbCopy[RX_PALETTE_MAX_SIZE];
	RxYiqColor paletteYiq[RX_PALETTE_MAX_SIZE];
	RxYiqColor paletteYiqCopy[RX_PALETTE_MAX_SIZE];
	RxProgressCallback progressCallback;
	void *progressCallbackData;
};

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
//   yiq           The input YIQ color.
//
// Returns:
//   The input YIQ color converted to RGB.
// -----------------------------------------------------------------------------------------------
COLOR32 RxConvertYiqToRgb(
	const RxYiqColor *yiq  // the input YIQ color
);

// -----------------------------------------------------------------------------------------------
// Name: RxInit
//
// Initialize a RxReduction structure with palette parameters. Release the resources held by this
// context using the RxDestroy function.
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
	int          enhanceColors   // assign more weight to frequently occurring colors
);

// -----------------------------------------------------------------------------------------------
// Name: RxNew
//
// Allocates and initializes a RxReduction structure with palette parameters. Free the returned
// context using the RxFree function.
//
// Parameters:
//   balance       The balance setting.
//   colorBalance  The color balance setting.
//   enhanceColors Enhance largely used colors.
//   nColors       The number of colors to generate in a palette.
//
// Returns:
//   A pointer to the allocated color reduction context, if successful, or NULL on failure.
// -----------------------------------------------------------------------------------------------
RxReduction *RxNew(
	int          balance,        // the balance setting
	int          colorBalance,   // the color balance setting
	int          enhanceColors   // assign more weight to frequently occurring colors
);

// -----------------------------------------------------------------------------------------------
// Name: RxSetBalance
//
// Sets the color balance parameters for a color reduction context.
//
// Parameters:
//   reduction     The color reduction context
//   balance       The balance setting.
//   colorBalance  The color balance setting.
//   enhanceColors Enhance largely used colors.
// -----------------------------------------------------------------------------------------------
void RxSetBalance(
	RxReduction *reduction,     // the color reduction context
	int          balance,       // the balance setting
	int          colorBalance,  // the colr balance setting
	int          enhanceColors  // assign more weight to frequently occurring colors
);

// -----------------------------------------------------------------------------------------------
// Name: RxApplyFlags
//
// Sets the flags for a color reduction context's operations.
//
// Parameters:
//   reduction     The color reduction context
//   flag          The new flags
// -----------------------------------------------------------------------------------------------
void RxApplyFlags(
	RxReduction *reduction,
	RxFlag       flag
);

// -----------------------------------------------------------------------------------------------
// Name: RxSetProgressCallback
//
// Sets the progress update callback for the color reduction context. This callback is called
// periodically during a color reduction operation for a callee wanting progress updates. You
// may specify a pointer to user data to use for progress update logic. You may clear the
// callback by passing NULL for the callback parameter.
//
// Parameters:
//   reduction     The color reduction context
//   callback      The new progress update callback
//   userData      A user pointer passed to the callback function
// -----------------------------------------------------------------------------------------------
void RxSetProgressCallback(
	RxReduction       *reduction,  // the color reduction context
	RxProgressCallback callback,   // the progress callback
	void              *userData    // user data passed to the callback function
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistAddColor
//
// Adds a single color to the histogram with the specified weight. If the color already exists in
// the histogram, then its weight will be increased by the specified weight.
//
// Parameters:
//   reduction     The color reduction context
//   col           The color to add to the histogram
//   weight        The weight given to the color
// -----------------------------------------------------------------------------------------------
void RxHistAddColor(
	RxReduction      *reduction,  // the color reduction context
	const RxYiqColor *col,        // the color to add
	double            weight      // the color's weight
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
RxStatus RxHistAdd(
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
// Name: RxHistGetTopN
//
// Gets the n most highly-weighted colors in the histogram. This internally sorts the histogram,
// so do not rely on the sorted order of the histogram after calling this function. If there are
// less than n colors in the histogram, only the number of entries in the histogram will be
// written.
//
// Parameters:
//   reduction     The color reduction context.
//   n             The maximum number of entries to return from the histogram
//   cols          Buffer to receive the top n colors, in descending weight
//   weights       Corresponding weights for each color, optional.
//
// Returns:
//   The number of colors returned. This will be equal to n if the histogram has n or more
//   entries. If the number of entries in the histogram is less than n, the return value is the
//   number of entries in the histogram.
// -----------------------------------------------------------------------------------------------
unsigned int RxHistGetTopN(
	RxReduction *reduction,  // the color reduction context
	unsigned int n,          // number of colors to return
	RxYiqColor  *cols,       // buffer for returned colors
	double      *weights     // buffer for returned weights
);

// -----------------------------------------------------------------------------------------------
// Name: RxHistClear
//
// Clears out a color reduction context's histogram. Can be used to create multiple palettes.
//
// Parameters:
//   reduction     The color reduction context.
// -----------------------------------------------------------------------------------------------
RxStatus RxHistClear(
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
RxStatus RxHistFinalize(
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
//   nColors       The number of palette colors.
// -----------------------------------------------------------------------------------------------
RxStatus RxComputePalette(
	RxReduction *reduction,  // the color reduction context
	unsigned int nColors     // the number of palette colors
);

// -----------------------------------------------------------------------------------------------
// Name: RxCreatePaletteEx
//
// Creates a color palette for an image without reserving any color slots for transparency on a
// given color reduction context.
//
// Parameters:
//   reduction     The color reduction context.
//   px            The image pixels.
//   width         The image width.
//   height        The image height.
//   pal           The output palette buffer.
//   nColors       The size of the color palette to create.
//   flag          Color reduction flags for palette sorting (see enum RxFlag).
//   pOutCols      Pointer to output number of colors (may be NULL).
//
// Returns:
//   The completed operation status.
// -----------------------------------------------------------------------------------------------
RxStatus RxCreatePaletteWithContext(
	RxReduction   *reduction,  // the color reduction context
	const COLOR32 *px,         // the input image
	unsigned int   width,      // the image width
	unsigned int   height,     // the image height
	COLOR32       *pal,        // a buffer receiving the created palette
	unsigned int   nColors,    // the maximum number of colors to generate
	RxFlag         flag,       // the flags for palette sorting
	unsigned int  *pOutCols    // a pointer receiving the number of generated colors (optional)
);

// -----------------------------------------------------------------------------------------------
// Name: RxComputeColorDifference
//
// Compute a color palette using the histogram held by the color reduction context. Before calling
// this function, the histogram must be finalized by a call to RxHistFinalize. 
//
// Parameters:
//   reduction     The color reduction context.
//   yiq1          The first color.
//   yiq2          The second color.
//
// Returns:
//   A measure of squared difference between the two colors. Difference measures from different
//   reduction contexts should not be compared directly. When two colors are identical, this
//   function will return 0.0. A pair of colors with a greater distance should appear more
//   dissimilar than a pair with a lower distance.
// -----------------------------------------------------------------------------------------------
double RxComputeColorDifference(
	RxReduction *reduction,
	const RxYiqColor *yiq1,
	const RxYiqColor *yiq2
);

// -----------------------------------------------------------------------------------------------
// Name: RxComputePaletteError
//
// Compute palette error on a bitmap given a specified reduction context.
//
// Parameters:
//   reduction      The color reduction context.
//   px             The image pixels.
//   width          The input width
//   height         The input height
//   palette        The color palette, as RGB colors.
//   nColors        The number of palette colors.
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
	unsigned int   width,           // the input width
	unsigned int   height,          // the input height
	const COLOR32 *palette,         // the color palette
	unsigned int   nColors,         // the number of colors in the palette
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
// Name: RxReduceImageEx
//
// Reduce the colors of image according to a given color palette. This function optionally writes
// the indexed color values to an array with the same dimension as the input image.
//
// Parameters:
//   reduction     The color reduction context.
//   px            The image pixels.
//   indices       The output indexed buffer (optional). This may be set to NULL.
//   width         The image width.
//   height        The image height.
//   palette       The color palette with which to reduce the image.
//   nColors       The number of colors in the color palette.
//   flag          Color reduction flag.
//   diffuse       The error diffusion amount, from 0 to 1. Set to 0 to disable dithering.
// -----------------------------------------------------------------------------------------------
RxStatus RxReduceImageWithContext(
	RxReduction   *reduction,     // the color reduction context
	COLOR32       *px,            // the image pixels
	int           *indices,       // the output palette index data (optional)
	unsigned int   width,         // the image width
	unsigned int   height,        // the image height
	const COLOR32 *palette,       // the color palette
	unsigned int   nColors,       // the color palette size
	RxFlag         flag,          // color reduction flags
	float          diffuse       // the error diffusion amount (from 0 to 1)
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteLoad
//
// Loads a color palette into the color reduction context. The loaded palette is used by
// functions like RxPaletteFindClosestColor. When the palette is no longer used, free it using
// the RxPaletteFree function.
//
// Parameters:
//   reduction     The color reduction context
//   pltt          The color palette to load, as RGBA colors.
//   nColors       The number of colors in the color palette to load.
// -----------------------------------------------------------------------------------------------
RxStatus RxPaletteLoad(
	RxReduction          *reduction,
	const COLOR32        *pltt,
	unsigned int          nColors
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteFindClosestColor
//
// Finds the closest color in the loaded palette to the specified color.
//
// Parameters:
//   reduction     The color reduction context
//   color         The color to search for
//   outDiff       A pointer that receives the distance to the most similar color. This may be
//                 NULL.
//
// Returns:
//   The index of the most similar color in the color palette.
// -----------------------------------------------------------------------------------------------
unsigned int RxPaletteFindClosestColor(
	RxReduction *reduction,
	COLOR32      color,
	double      *outDiff
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteFindClosestColorYiq
//
// Finds the closest color in the loaded palette to the specified color.
//
// Parameters:
//   reduction     The color reduction context
//   color         The color to search for
//   outDiff       A pointer that receives the distance to the most similar color. This may be
//                 NULL.
//
// Returns:
//   The index of the most similar color in the color palette.
// -----------------------------------------------------------------------------------------------
unsigned int RxPaletteFindClosestColorYiq(
	RxReduction      *reduction,
	const RxYiqColor *color,
	double           *outDiff
);

// -----------------------------------------------------------------------------------------------
// Name: RxPaletteFree
//
// Frees the palette loaded in the current reduction context.
//
// Parameters:
//   reduction     The color reduction context.
// -----------------------------------------------------------------------------------------------
void RxPaletteFree(
	RxReduction *reduction
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

// -----------------------------------------------------------------------------------------------
// Name: RxFree
//
// Frees all resources held by a color reduction context. Only call this function on the return
// value of RxNew.
//
// Parameters:
//   reduction     The color reduction context to be freed
// -----------------------------------------------------------------------------------------------
void RxFree(
	RxReduction *reduction // the color reduction context
);
