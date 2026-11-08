#pragma once
#include "object/NitroCell.h"


// ----- render cell

void CellRender(
	COLOR32   *px,             // output 512x256 pixel buffer
	int       *covbuf,         // output coverage buffer (optional)
	NCER      *ncer,           // cell data bank
	NCGR      *ncgr,           // character graphics
	NCLR      *nclr,           // color palette
	NCER_CELL *cell,           // cell to render
	int        xOffs,          // horizontal displacement of render
	int        yOffs,          // vertical displacement of render
	double     a,              // affine parameter A
	double     b,              // affine parameter B
	double     c,              // affine parameter C
	double     d,              // affine parameter D
	int        forceAffine,    // forces all OBJ to be in affine mode
	int        forceDoubleSize // forces all affine OBJ to be in double size mode
);
