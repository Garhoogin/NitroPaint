#pragma once
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nanr.h"
#include "texture.h"

//
// Initialize preview target
//
int PreviewInit(void);

//
// End preview target
//
void PreviewEnd(void);

//
// Load a palette for preview
//
int PreviewLoadBgPalette(NCLR *nclr);

//
// Load an OBJ palette for preview
//
int PreviewLoadObjPalette(NCLR *nclr);

//
// Load a character for preview
//
int PreviewLoadBgCharacter(NCGR *ncgr);

//
// Load a character for OBJ preview
//
int PreviewLoadObjCharacter(NCGR *ncgr);

//
// Load a BG screen for preview
//
int PreviewLoadBgScreen(NSCR *nscr);

//
// Load a cell for preview with an animation. If nanr is NULL, then the cell is
// shown static, and cellno selects the cell to preview. If nanr is not NULL, 
// then cellno selects the animation index to show.
//
int PreviewLoadObjCell(NCER *ncer, NANR *nanr, int cellno);

//
// Preview a BG screen
//
int PreviewScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr);
