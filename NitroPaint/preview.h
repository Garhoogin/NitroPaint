#pragma once
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
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
// Load a character for preview
//
int PreviewLoadBgCharacter(NCGR *ncgr);

//
// Load a BG screen for preview
//
int PreviewLoadBgScreen(NSCR *nscr, int depth, int useExtPalette);

//
// Preview a BG screen
//
int PreviewScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr);
