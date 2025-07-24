#pragma once

#include "editor.h"
#include "jlyt.h"
#include "color.h"
#include "framebuffer.h"

#include "nftrviewer.h"
#include "ncerviewer.h"

#define LYT_EDITOR_MAX_FONTS 16

typedef struct LYTEDITOR_ {
	HWND hWnd;               // editor window
	void *data;              // editor data
	int type;                // file type
	HWND hWndPreview;
	HWND hWndElementLabel;
	HWND hWndElementDropdown;
	HWND hWndAddElement;
	HWND hWndRemoveElement;
	HWND hWndOriginLabel;
	HWND hWndOriginXDropdown;
	HWND hWndOriginYDropdown;
	HWND hWndPositionLabel;
	HWND hWndPositionX;
	HWND hWndPositionY;

	COLOR32 bgColor;
	FrameBuffer fb;
	UiCtlManager mgr;
	COLOR32 cellbuf[256 * 512];
	int cellcov[256 * 512];

	//mouse state
	int mouseDown;       // indicates if the mouse is down
	int mouseX;          // current mouse X (or -1 if not in client area)
	int mouseY;          // current mouse Y (or -1 if not in client area)
	int mouseDownX;      // X position of mouse at mouse down
	int mouseDownY;      // Y position of mouse at mouse down
	int mouseDragged;    // whether the mouse was dragged while down
	int hit;             // the hit test result of the current mouse position
	int mouseDownHit;    // the hit test result when the mouse was down
	int dragStartX;      // X position of dragged element at start of drag
	int dragStartY;      // Y position of dragged element at start of drag

	int curElem;         // currently selected layout element
	int updating;        // updating UI controls (suppress UI response)

	//associated data
	NFTRVIEWERDATA *registeredFontEditors[LYT_EDITOR_MAX_FONTS]; // registered font editors
	NCERVIEWERDATA *registeredCellEditor;                        // registered cell data bank editor
} LYTEDITOR;

typedef struct BNLLEDITORDATA_ {
	EDITOR_BASIC_MEMBERS;
	BNLL bnll;
	LYTEDITOR editor;

	HWND hWndAlignmentLabel;
	HWND hWndAlignX;
	HWND hWndAlignY;
	HWND hWndSpacingLabel;
	HWND hWndSpacingX;
	HWND hWndSpacingY;
	HWND hWndFontLabel;
	HWND hWndFontInput;
	HWND hWndPaletteLabel;
	HWND hWndPaletteInput;
	HWND hWndColorLabel;
	HWND hWndColorInput;
	HWND hWndMessageLabel;
	HWND hWndMessageInput;
	HWND hWndEditFonts;
} BNLLEDITORDATA;

typedef struct BNCLEDITORDATA_ {
	EDITOR_BASIC_MEMBERS;
	BNCL bncl;
	LYTEDITOR editor;

	HWND hWndCellLabel;
	HWND hWndCellInput;
} BNCLEDITORDATA;

typedef struct BNBLEDITORDATA_ {
	EDITOR_BASIC_MEMBERS;
	BNBL bnbl;
	LYTEDITOR editor;

	HWND hWndWidthLabel;
	HWND hWndWidthInput;
	HWND hWndHeightLabel;
	HWND hWndHeightInput;
} BNBLEDITORDATA;

HWND CreateBnllViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);
HWND CreateBnclViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);
HWND CreateBnblViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

void RegisterLytEditor(void);
