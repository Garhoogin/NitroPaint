#include <Windows.h>

#include "nitropaint.h"
#include "nmcr.h"
#include "nmcrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nanrviewer.h"

extern HICON g_appIcon;

static HBITMAP RenderNmcrFrame(NMCR *nmcr, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr, int cellIndex, int frame) {
	COLOR32 *px = (COLOR32 *) calloc(256 * 512, 4);

	for (int i = 0; i < 512 * 256; i++) {
		int cc = ((i ^ (i >> 9)) >> 2) & 1;
		px[i] = 0xC0C0C0 + ((-cc) & 0x3F3F3F);
	}

	if (nmcr != NULL && nclr != NULL && ncgr != NULL && ncer != NULL && nanr != NULL) {
		MULTI_CELL *mc = nmcr->multiCells + cellIndex;
		CELL_HIERARCHY *hierarchy = mc->hierarchy;
		int nNodes = mc->nNodes;
		
		for (int i = nNodes - 1; i >= 0; i--) { //traverse backwards, because OAM is funny
			CELL_HIERARCHY *entry = hierarchy + i;
			int nodeAttr = entry->nodeAttr;
			int x = entry->x;
			int y = entry->y;
			int seqId = entry->sequenceNumber;

			AnmRenderSequenceFrame(px, nanr, ncer, ncgr, nclr, seqId, frame, x, y, 0, 0);
		}
	}

	HBITMAP hBitmap = CreateBitmap(512, 256, 1, 32, px);
	free(px);
	return hBitmap;
}

LRESULT CALLBACK NmcrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NMCRVIEWERDATA *data = (NMCRVIEWERDATA *) EditorGetData(hWnd);
	switch (msg) {
		case NV_INITIALIZE:
		{
			LPCWSTR path = (LPCWSTR) wParam;
			data->nmcr = (NMCR *) lParam;
			data->multiCell = 0;
			EditorSetFile(hWnd, path);

			InvalidateRect(hWnd, NULL, FALSE);
			SetTimer(hWnd, 1, 17, NULL);
			break;
		}
		case WM_TIMER:
		{
			//increment timers
			data->frame++;

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			NCLR *nclr = NULL;
			NCGR *ncgr = NULL;
			NCER *ncer = NULL;
			NANR *nanr = NULL;
			NITROPAINTSTRUCT *nps = (NITROPAINTSTRUCT *) data->editorMgr;
			if (nps->hWndNclrViewer != NULL) nclr = (NCLR *) EditorGetObject(nps->hWndNclrViewer);
			if (nps->hWndNcgrViewer != NULL) ncgr = (NCGR *) EditorGetObject(nps->hWndNcgrViewer);
			if (nps->hWndNcerViewer != NULL) ncer = (NCER *) EditorGetObject(nps->hWndNcerViewer);
			if (nps->hWndNanrViewer != NULL) nanr = (NANR *) EditorGetObject(nps->hWndNanrViewer);

			HBITMAP hBitmap = RenderNmcrFrame(data->nmcr, nclr, ncgr, ncer, nanr, data->multiCell, data->frame);
			HDC hOffDC = CreateCompatibleDC(hDC);
			SelectObject(hOffDC, hBitmap);
			BitBlt(hDC, 0, 0, 512, 256, hOffDC, 0, 0, SRCCOPY);
			DeleteObject(hOffDC);
			DeleteObject(hBitmap);

			EndPaint(hWnd, &ps);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

void RegisterNmcrViewerClass(void) {
	nmcrRegisterFormats();

	EditorRegister(L"NmcrViewerClass", NmcrViewerWndProc, L"NMCR Viewer", sizeof(NMCRVIEWERDATA), 0);
}

HWND CreateNmcrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NMCR *nmcr = (NMCR *) calloc(1, sizeof(NMCR));
	if (nmcrReadFile(nmcr, path)) {
		free(nmcr);
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	HWND h = EditorCreate(L"NmcrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) nmcr);
	return h;
}
