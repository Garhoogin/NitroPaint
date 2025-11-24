#pragma once

#include "nftr.h"
#include "editor.h"
#include "tilededitor.h"
#include "framebuffer.h"

typedef struct NFTRVIEWERDATA_ {
	EDITOR_BASIC_MEMBERS;
	NFTR *nftr;
	TedData ted;
	UiCtlManager mgr;            // UI manager

	int dblClickElement;
	int dblClickTimer;

	int glyphCacheInit;
	StList glyphCacheFree;
	StList glyphCache;

	LOGFONT lastFont;
	int lastFontSet;

	int renderTransparent;
	int curGlyph;
	int selectedColor;
	FrameBuffer fbPreview;       // text render preview framebuffer
	wchar_t *previewText;        // preview text

	int spaceX;                  // text preview space X
	int spaceY;                  // text preview space Y
	COLOR palette[256];          // color palette to render with
	COLORREF dlgCustomColors[16];

	HWND hWndMargin;
	HWND hWndPreview;
	HWND hWndDepthLabel;
	HWND hWndDepthList;
	HWND hWndGlyphList;
	HWND hWndImportCMap;         // import code map
	HWND hWndExportCMap;         // export code map

	HWND hWndLabelGlyphProp;     // glyph properties group label
	HWND hWndLabelGlyphWidth;    // glyph width label
	HWND hWndLabelGlyphLeading;  // glyph width input
	HWND hWndLabelGlyphTrailing; // glyph leading label
	HWND hWndInputGlyphWidth;    // glyph leading input
	HWND hWndInputGlyphLeading;  // glyph trailing label
	HWND hWndInputGlyphTrailing; // glyph trailing input

	HWND hWndLabelFontProp;      // label for font properties group
	HWND hWndLabelCellSize;
	HWND hWndInputCellWidth;
	HWND hWndInputCellHeight;
	HWND hWndLabelAscent;
	HWND hWndInputAscent;

	HWND hWndLabelPrevProp;      // label for preview properties group
	HWND hWndLabelPrevSpace;     // label for preview spacing
	HWND hWndInputPrevSpaceX;    // input for preview spacing X
	HWND hWndInputPrevSpaceY;    // input for preview spacing Y

	HWND hWndPreviewInput;

} NFTRVIEWERDATA;

void RegisterNftrViewerClass(void);

HWND CreateNftrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);
HWND CreateNftrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NFTR *nftr);
