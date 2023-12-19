#pragma once
#include "ncer.h"

typedef struct OBJ_BOUNDS_ {
	int x;
	int y;
	int width;
	int height;
} OBJ_BOUNDS;

typedef struct OBJ_IMAGE_SLICE_ {
	OBJ_BOUNDS bounds;
	COLOR32 px[64 * 64];
} OBJ_IMAGE_SLICE;

/******************************************************************************\
*
* Divide an image into a set of OBJ. This function outputs OBJ with valid 
* hardware OBJ sizes. Only the opaque region of the image is guaranteed to be
* covered when aggressiveness > 0. With aggressiveness=0, the whole image is
* covered with OBJ. Only the bounding box of opaque pixels is covered wen the
* full parameter is zero, or the whole image area otherwise.
*
* Parameters:
*	px                      the image pixels
*   width                   the image width
*   height                  the image height
*	aggressiveness          the level of aggressiveness when dividing the OBJ
*                           (0-100)
*	full                    controls whether the whole or only opaque region is
*                           used.
*   affine                  makes a cell for affine use. If nonzero, the cell
*                           is generated using 8x8, 16x16, 32x32, and 64x64 OBJ
*                           only.
*   pnObj                   pointer to the output number of OBJ.
*
* Returns:
*	A list of OBJ covering the image with coordinates in the image space.
*
\******************************************************************************/
OBJ_BOUNDS *CellgenMakeCell(COLOR32 *px, int width, int height, int aggressiveness, int full, int affine, int *pnObj);

/******************************************************************************\
*
* Processes a set of OBJ and divides those that have a greater width:height or
* height:width ratio (depending on which is larger) to ensure that no OBJ is
* too elongated. This can help to ensure that an affine cell can be rotated
* properly. The input array is discarded after calling this function and the
* returned array should be used instead.
*
* Parameters:
*   obj                     the input OBJ array
*   nObj                    the number of input OBJ
*   maxRatio                the largest acceptable size ratio
*   pnOutObj                the output number of OBJ
*
* Returns:
*	A list of OBJ satisfying the ratio criteria.
*
\******************************************************************************/
OBJ_BOUNDS *CellgenEnsureRatio(OBJ_BOUNDS *obj, int nObj, int maxRatio, int *pnOutObj);

/******************************************************************************\
*
* Slices an image into the rectangular regions specified by the OBJ bounds
* array. For OBJ bounds that overlap, only one slice will contain those pixels.
* The other slices occupying this pixel will have transparency there.
*
* Parameters:
*   px                      the pixels of input image
*   width                   the width of the input image
*   height                  the height of the input image
*   bounds                  the input OBJ bounds array
*   nObj                    the number of input OBJ bounds
*   cut                     if nonzero, cuts used parts of slices out of others
*
* Returns:
*	A list of OBJ slices containing the cut up input image.
*
\******************************************************************************/
OBJ_IMAGE_SLICE *CellgenSliceImage(COLOR32 *px, int width, int height, OBJ_BOUNDS *bounds, int nObj, int cut);

/******************************************************************************\
*
* Gets the bounding box of the image's opaque pixels.
*
* Parameters:
*   px                      the pixels of input image
*   width                   the width of the input image
*   height                  the height of the input image
*   pxMin                   pointer to the output minimum X
*   pxMax                   pointer to the output maximum X
*   pyMin                   pointer to the output minimum Y
*   pyMax                   pointer to the output maximum Y
*
* Returns:
*	Nothing
*
\******************************************************************************/
void CellgenGetBounds(COLOR32 *px, int width, int height, int *pxMin, int *pxMax, int *pyMin, int *pyMax);
