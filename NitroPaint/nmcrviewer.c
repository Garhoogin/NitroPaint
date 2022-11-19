#include <Windows.h>

#include "nitropaint.h"
#include "nmcr.h"
#include "nmcrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nanrviewer.h"

extern HICON g_appIcon;

HBITMAP RenderNmcrFrame(NMCR *nmcr, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr, int cellIndex, int frame) {
	DWORD *px = (DWORD *) calloc(256 * 512, 4);

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

			nanrDrawFrame(px, nclr, ncgr, ncer, nanr, seqId, frame, 0, x, y);
		}
	}

	HBITMAP hBitmap = CreateBitmap(512, 256, 1, 32, px);
	free(px);
	return hBitmap;
}

LRESULT CALLBACK NmcrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NMCRVIEWERDATA *data = (NMCRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (NMCRVIEWERDATA *) calloc(1, sizeof(NMCRVIEWERDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case NV_INITIALIZE:
		{
			LPCWSTR path = (LPCWSTR) wParam;
			memcpy(data->szOpenFile, path, 2 * (wcslen(path) + 1));
			memcpy(&data->nmcr, (NMCR *) lParam, sizeof(NMCR));
			data->multiCell = 0;
			data->frameTimes = (int *) calloc(data->nmcr.multiCells[data->multiCell].nNodes, sizeof(int));
			data->frameNumbers = (int *) calloc(data->nmcr.multiCells[data->multiCell].nNodes, sizeof(int));

			InvalidateRect(hWnd, NULL, FALSE);
			SetTimer(hWnd, 1, 17, NULL);
			break;
		}
		case WM_TIMER:
		{
			//increment timers
			int nNode = data->nmcr.multiCells[data->multiCell].nNodes;
			for (int i = 0; i < nNode; i++) {
				data->frameTimes[i]++;
			}
			data->frame++;

			//increment frame if applicable
			NANR *nanr = NULL;
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nps = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			HWND hWndNanrViewer = nps->hWndNanrViewer;
			if (hWndNanrViewer != NULL) {
				NANRVIEWERDATA *nanrViewerData = (NANRVIEWERDATA *) GetWindowLongPtr(hWndNanrViewer, 0);
				nanr = &nanrViewerData->nanr;
			}
			
			if (nanr != NULL) {
				for (int i = 0; i < nNode; i++) {

				}
			}

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
			HWND hWndMain = getMainWindow(hWnd);
			NITROPAINTSTRUCT *nps = (NITROPAINTSTRUCT *) GetWindowLongPtr(hWndMain, 0);
			if (nps->hWndNclrViewer != NULL) {
				NCLRVIEWERDATA *nclrViewerData = (NCLRVIEWERDATA *) GetWindowLongPtr(nps->hWndNclrViewer, 0);
				nclr = &nclrViewerData->nclr;
			}
			if (nps->hWndNcgrViewer != NULL) {
				NCGRVIEWERDATA *ncgrViewerData = (NCGRVIEWERDATA *) GetWindowLongPtr(nps->hWndNcgrViewer, 0);
				ncgr = &ncgrViewerData->ncgr;
			}
			if (nps->hWndNcerViewer != NULL) {
				NCERVIEWERDATA *ncerViewerData = (NCERVIEWERDATA *) GetWindowLongPtr(nps->hWndNcerViewer, 0);
				ncer = &ncerViewerData->ncer;
			}
			if (nps->hWndNanrViewer != NULL) {
				NANRVIEWERDATA *nanrViewerData = (NANRVIEWERDATA *) GetWindowLongPtr(nps->hWndNanrViewer, 0);
				nanr = &nanrViewerData->nanr;
			}
			HBITMAP hBitmap = RenderNmcrFrame(&data->nmcr, nclr, ncgr, ncer, nanr, data->multiCell, data->frame);
			HDC hOffDC = CreateCompatibleDC(hDC);
			SelectObject(hOffDC, hBitmap);
			BitBlt(hDC, 0, 0, 512, 256, hOffDC, 0, 0, SRCCOPY);
			DeleteObject(hOffDC);
			DeleteObject(hBitmap);

			EndPaint(hWnd, &ps);
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_NMCR;
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

VOID RegisterNmcrViewerClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"NmcrViewerClass";
	wcex.lpfnWndProc = NmcrViewerWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

HWND CreateNmcrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NMCR nmcr;
	int n = nmcrReadFile(&nmcr, path);
	if (n) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"NmcrViewerClass", L"NMCR Viewer", WS_VISIBLE | WS_CLIPSIBLINGS | WS_HSCROLL | WS_VSCROLL | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) &nmcr);
	return h;
}
