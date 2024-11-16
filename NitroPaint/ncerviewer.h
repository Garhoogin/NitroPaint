#pragma once
#include "editor.h"
#include "framebuffer.h"
#include "childwindow.h"
#include "ncer.h"
#include "undo.h"

typedef struct NCERVIEWERDATA_ {
	EDITOR_BASIC_MEMBERS;
	NCER ncer;
	int hoverX;
	int hoverY;
	int cell;
	int oamStartX;
	int oamStartY;
	int showCellBounds;
	int showGuidelines;
	int autoCalcBounds;                  // automatically recalculate cell bounds
	
	int foreignDataUpdate;               // external data we depend on is modified
	int cellListRedrawCount;             // count of current redraw suppressions
	int cellListDragging;                // cell list item drag state
	int cellListDraggingItem;            // cell list index dragging

	int mouseDown;
	int mouseDownHit;
	int dragStartX;
	int dragStartY;
	int dragEndX;
	int dragEndY;
	int selMoved;                        // has the selection moved in this gesture?
	int *selectedOBJ;                    // array of selected OBJ indices
	int nSelectedOBJ;                    // number of selected OBJ indices

	COLOR32 frameBuffer[256 * 512];      // buffer where the current cell is rendered
	int covBuffer[256 * 512];            // coverage buffer for current cell render
	FrameBuffer fb;                      // frame buffer the viewer renders
	HWND hWndViewer;

	HWND hWndCellList;
	HWND hWndCreateCell;
	HWND hWndMappingModeLabel;
	HWND hWndMappingMode;
	HWND hWndShowBounds;                 // show cell bounds
	HWND hWndAutoCalcBounds;             // auto-calculate bounds

	HWND hWndCellAdd;
} NCERVIEWERDATA;



#define MAPPING_2D               0
#define MAPPING_1D_32K           1
#define MAPPING_1D_64K           2
#define MAPPING_1D_128K          3
#define MAPPING_1D_256K          4

//NitroPaint OBJ clipboard data
typedef struct NP_OBJ_ {
	int xMin;                            // minimal X coordinate of bounding box
	int yMin;                            // minimal Y coordinate of bounding box
	int width;                           // width of bounding box
	int height;                          // height of bounding box

	uint32_t offsObjData[5];             // offsets to OBJ data in 6-byte units (for each mapping mode)
	uint16_t presenceMask;               // presence of OBJ data for each mapping mode
	uint16_t nObj[5];                    // number of OBJ for each mapping mode

	uint16_t attr[0];
} NP_OBJ;


void RegisterNcerViewerClass(void);

void CellViewerGraphicsUpdated(HWND hWndEditor);

void CellViewerCopyObjData(NP_OBJ *obj);

NP_OBJ *CellViewerGetCopiedObjData(void);

HWND CreateNcerViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateNcerViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NCER *ncer);
