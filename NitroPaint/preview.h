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
// Preview a BG screen
//
int PreviewScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr);
