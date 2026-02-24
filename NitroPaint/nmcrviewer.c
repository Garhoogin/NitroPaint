#include <Windows.h>
#include <CommCtrl.h>

#include "nitropaint.h"
#include "nmcr.h"
#include "nmcrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nanrviewer.h"

#define PREVIEW_ICON_WIDTH     64
#define PREVIEW_ICON_HEIGHT    64
#define PREVIEW_ICON_PADDING_V 10

extern HICON g_appIcon;

static ObjHeader *NmcrViewerGetAssociatedObject(NMCRVIEWERDATA *data, int type) {
	NITROPAINTSTRUCT *np = (NITROPAINTSTRUCT *) data->editorMgr;
	
	HWND h = NULL;
	switch (type) {
		case FILE_TYPE_PALETTE: h = np->hWndNclrViewer; break;
		case FILE_TYPE_CHARACTER: h = np->hWndNcgrViewer; break;
		case FILE_TYPE_CELL: h = np->hWndNcerViewer; break;
		case FILE_TYPE_NANR: h = data->nanrViewer == NULL ? NULL : data->nanrViewer->hWnd; break;
	}

	if (h == NULL) return NULL;
	return EditorGetObject(h);
}


static MULTI_CELL *NmcrViewerGetMultiCell(NMCR *nmcr, int index) {
	if (index < 0 || index >= nmcr->nMultiCell) return NULL;
	return &nmcr->multiCells[index];
}

static MULTI_CELL *NmcrViewerGetCurrentMultiCell(NMCR *nmcr, McPlayer *player) {
	return NmcrViewerGetMultiCell(nmcr, player->multiCell);
}

static void NmcrViewerFreeSeqPlayers(NMCRVIEWERDATA *data) {
	for (int i = 0; i < data->player.nPlayers; i++) {
		AnmSeqPlayerFree(&data->player.nodePlayers[i]);
	}
	free(data->player.nodePlayers);
	
	data->player.nodePlayers = NULL;
	data->player.nPlayers = 0;
}

static void NmcrViewerSetMC(NMCRVIEWERDATA *data, int idx) {
	if (idx < 0 || idx >= data->nmcr->nMultiCell) return;

	NmcrViewerFreeSeqPlayers(data);

	data->player.multiCell = idx;
	data->player.nPlayers = data->nmcr->multiCells[idx].nNodes;
	data->player.nodePlayers = (AnmSeqPlayer *) calloc(data->player.nPlayers, sizeof(AnmSeqPlayer));

	//initialize sequence players
	NANR *nanr = (NANR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_NANR);
	for (int i = 0; i < data->player.nPlayers; i++) {
		AnmSeqPlayer *seqPlayer = &data->player.nodePlayers[i];
		AnmSeqPlayerSetup(seqPlayer, nanr, data->nmcr->multiCells[idx].hierarchy[i].sequenceNumber);
		AnmSeqPlayerStartPlayback(seqPlayer);
	}

	InvalidateRect(data->hWnd, NULL, FALSE);

	//TODO: when a NANR is closed, we must release all references to it!
	//TODO: when a NANR is opened, we must reinitialize all sequence players!
}

static int NmcrViewerGetNodeFrame(NMCRVIEWERDATA *data, int node) {
	MULTI_CELL *mc = NmcrViewerGetCurrentMultiCell(data->nmcr, &data->player);
	if (mc == NULL) return 0;

	if (node < 0 || node >= mc->nNodes || node >= data->player.nPlayers) return 0;

	return data->player.nodePlayers[node].currentFrame;
}

static void NmcrViewerStartPlayback(NMCRVIEWERDATA *data) {
	data->player.playing = 1;
	SetTimer(data->hWnd, 1, 17, NULL);
}

static void NmcrViewerPausePlayback(NMCRVIEWERDATA *data) {
	data->player.playing = 0;
	KillTimer(data->hWnd, 1);
}

static void NmcrViewerStopPlayback(NMCRVIEWERDATA *data) {
	NmcrViewerPausePlayback(data);
	//TODO
}

static void NmcrViewerResetPlayback(NMCRVIEWERDATA *data) {
	//TODO
}

static int NmcrViewerTick(NMCRVIEWERDATA *data) {
	NANR *nanr = (NANR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_NANR);
	if (nanr == NULL) {
		//stop playback if the player was ticked when no animation data exists
		NmcrViewerPausePlayback(data);
		return 1;
	}

	MULTI_CELL *mc = NmcrViewerGetCurrentMultiCell(data->nmcr, &data->player);
	if (mc == NULL) return 0;

	int changed = 0;
	for (int i = 0; i < data->player.nPlayers; i++) {
		CELL_HIERARCHY *hier = &mc->hierarchy[i];
		AnmSeqPlayer *seqPlayer = &data->player.nodePlayers[i];

		//tick the player
		int frame = seqPlayer->currentFrame;
		AnmSeqPlayerTickPlayback(seqPlayer);

		//if the player changes animation frame, mark a change
		if (seqPlayer->currentFrame != frame) changed = 1;
	}

	//return: true if the animation is changed
	return changed;
}

static void NmcrViewerRender(NMCRVIEWERDATA *data) {

}

static void NmcrViewerRenderCurrentMultiCellFrame(NMCRVIEWERDATA *data) {

}

static void RenderNmcrFrame(NMCRVIEWERDATA *data, McPlayer *player, NMCR *nmcr, NCLR *nclr, NCGR *ncgr, NCER *ncer, NANR *nanr) {
	//checkered background
	for (int i = 0; i < 512 * 256; i++) {
		int cc = ((i ^ (i >> 9)) >> 2) & 1;
		data->rendered[i] = 0xC0C0C0 + ((-cc) & 0x3F3F3F);
	}

	if (nmcr == NULL || nanr == NULL || ncer == NULL) return;

	MULTI_CELL *mc = NmcrViewerGetCurrentMultiCell(nmcr, player);
	if (mc == NULL) return;

	CELL_HIERARCHY *hierarchy = mc->hierarchy;
	int nNodes = mc->nNodes;

	//traverse backwards because of OAM ordering
	for (int i = nNodes - 1; i >= 0; i--) {
		CELL_HIERARCHY *entry = &hierarchy[i];
		int nodeAttr = entry->nodeAttr;
		int seqId = entry->sequenceNumber;
		int frame = NmcrViewerGetNodeFrame(data, i);

		AnmRenderSequenceFrame(data->rendered, nanr, ncer, ncgr, nclr, seqId, frame, entry->x, entry->y, 0, 0);
	}
}


// ----- UI code

static HWND NmcrViewerMultiCellListCreate(NMCRVIEWERDATA *data) {
	HWND hWndParent = data->hWnd;
	float scale = GetDpiScale();
	int listWidth = UI_SCALE_COORD(200, scale);

	DWORD lvStyle = WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VSCROLL | LVS_EDITLABELS | LVS_SINGLESEL | LVS_ICON | LVS_SHOWSELALWAYS
		| LVS_OWNERDATA;
	HWND h = CreateWindow(WC_LISTVIEW, L"", lvStyle, 0, 0, listWidth, 300, hWndParent, NULL, NULL, NULL);

	//set extended style
	ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_JUSTIFYCOLUMNS | LVS_EX_SNAPTOGRID);
	SendMessage(h, LVM_SETVIEW, LV_VIEW_TILE, 0);

	RECT rcClient;
	GetClientRect(h, &rcClient);

	//set tile view info
	LVTILEVIEWINFO lvtvi = { 0 };
	lvtvi.cbSize = sizeof(lvtvi);
	lvtvi.dwMask = LVTVIM_COLUMNS | LVTVIM_TILESIZE | LVTVIM_COLUMNS;
	lvtvi.dwFlags = LVTVIF_FIXEDSIZE;
	lvtvi.cLines = 1;
	lvtvi.sizeTile.cx = rcClient.right - GetSystemMetrics(SM_CXVSCROLL);
	lvtvi.sizeTile.cy = PREVIEW_ICON_HEIGHT + PREVIEW_ICON_PADDING_V;
	ListView_SetTileViewInfo(h, &lvtvi);

	//init columns
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.pszText = L"";
	lvc.fmt = LVCFMT_LEFT;
	ListView_InsertColumn(h, 0, &lvc);
	lvc.iSubItem = 1;
	ListView_InsertColumn(h, 1, &lvc);

	//create image list
	HIMAGELIST hLarge = ImageList_Create(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, ILC_MASK | ILC_COLOR24, 1, 1);
	ListView_SetImageList(h, hLarge, LVSIL_NORMAL);

	//SetWindowSubclass(h, CellViewerCellListSubclassProc, 1, 0);
	return h;
}

static void NmcrViewerOnPaint(NMCRVIEWERDATA *data) {
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(data->hWnd, &ps);

	float dpiScale = GetDpiScale();

	NCLR *nclr = (NCLR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_PALETTE);
	NCGR *ncgr = (NCGR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_CHARACTER);
	NCER *ncer = (NCER *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	NANR *nanr = (NANR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_NANR);

	//TODO: move this into a separate child window

	RenderNmcrFrame(data, &data->player, data->nmcr, nclr, ncgr, ncer, nanr);
	HBITMAP hBitmap = CreateBitmap(512, 256, 1, 32, data->rendered);

	HDC hOffDC = CreateCompatibleDC(hDC);
	SelectObject(hOffDC, hBitmap);
	BitBlt(hDC, UI_SCALE_COORD(200, dpiScale), 0, 512, 256, hOffDC, 0, 0, SRCCOPY);
	DeleteObject(hOffDC);
	DeleteObject(hBitmap);

	EndPaint(data->hWnd, &ps);
}

static void NmcrViewerOnDestroy(NMCRVIEWERDATA *data) {
	NmcrViewerFreeSeqPlayers(data);
}

static void NmcrViewerRegisterNanrViewer(NMCRVIEWERDATA *data, NANRVIEWERDATA *nanrViewerData) {
	//set new pointer
	data->nanrViewer = nanrViewerData;

	//update sequence players
	for (int i = 0; i < data->player.nPlayers; i++) {
		data->player.nodePlayers[i].animBank = nanrViewerData->nanr;
	}

	InvalidateRect(data->hWnd, NULL, FALSE);
}

static void NmcrViewerOnDestroyNanrViewer(EDITOR_DATA *nanrViewerData, void *param) {
	NMCRVIEWERDATA *data = (NMCRVIEWERDATA *) param;

	//remove the references to the NANR if it is the active viewer
	if ((NANRVIEWERDATA *) nanrViewerData == data->nanrViewer) {
		data->nanrViewer = NULL;
		for (int i = 0; i < data->player.nPlayers; i++) {
			data->player.nodePlayers[i].animBank = NULL;
		}

		//update graphics
		InvalidateRect(data->hWnd, NULL, FALSE);
	}
}

static void NmcrViewerOnCreateNanrViewer(EDITOR_DATA *nanrViewerData, void *param) {
	NMCRVIEWERDATA *data = (NMCRVIEWERDATA *) param;
	if (data->nanrViewer == NULL) {
		//if there is not an associated editor, associate one
		NmcrViewerRegisterNanrViewer(data, (NANRVIEWERDATA *) nanrViewerData);
	}
}

static void NmcrViewerOnCreate(NMCRVIEWERDATA *data) {
	data->hWndMultiCellList = NmcrViewerMultiCellListCreate(data);

	//attach to NANR viewer
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	if (nitroPaintStruct->hWndNanrViewer != NULL) {
		data->nanrViewer = EditorGetData(nitroPaintStruct->hWndNanrViewer);

		//register the destroy callback
		EditorRegisterDestroyCallback((EDITOR_DATA *) data->nanrViewer, NmcrViewerOnDestroyNanrViewer, data);
	}

	//register NANR viewer create callback
	EditorRegisterCreateCallback(data->editorMgr, FILE_TYPE_NANR, NmcrViewerOnCreateNanrViewer, data);
}

static void NmcrViewerOnSize(NMCRVIEWERDATA *data) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	float dpiScale = GetDpiScale();

	MoveWindow(data->hWndMultiCellList, 0, 0, UI_SCALE_COORD(200, dpiScale), rcClient.bottom, TRUE);
}

static BOOL NmcrViewerOnNotify(NMCRVIEWERDATA *data, WPARAM wParam, LPNMHDR hdr) {
	if (hdr->hwndFrom == data->hWndMultiCellList) {
		switch (hdr->code) {
			case NM_CLICK:
			case NM_DBLCLK:
			case NM_RCLICK:
			case NM_RDBLCLK:
			{
				LPNMITEMACTIVATE nma = (LPNMITEMACTIVATE) hdr;
				if (nma->iItem == -1) {
					//item being unselected. Mark variable to cancel the deselection.
					ListView_SetItemState(data->hWndMultiCellList, data->player.multiCell, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				}
				break;
			}
			case LVN_ITEMCHANGED:
			{
				LPNMLISTVIEW nm = (LPNMLISTVIEW) hdr;
				if (nm->uNewState & LVIS_SELECTED) {
					//selection changed
					NmcrViewerSetMC(data, nm->iItem/*, FALSE*/);
				}
				break;
			}
			case LVN_GETDISPINFO:
			{
				NMLVDISPINFO *di = (NMLVDISPINFO *) hdr;

				//fill out item structure
				if (di->item.mask & LVIF_COLFMT) {
					static int colFmt[2] = { 0 };
					di->item.piColFmt = colFmt;
				}
				if (di->item.mask & LVIF_COLUMNS) {
					di->item.cColumns = 1;
					di->item.puColumns[0] = 1;
				}
				if (di->item.mask & LVIF_IMAGE) {
					di->item.iImage = 0; // re-use imagelist images
					//AnmViewerRenderGlyphListImage(data, di->item.iItem);
				}
				if (di->item.mask & LVIF_TEXT) {
					//buffer is valid until the next call
					if (di->item.iSubItem == 0) {
						wsprintfW(data->listviewItemBuffers[0], L"[%d] Multi-Cell %d", di->item.iItem, di->item.iItem);
						di->item.pszText = data->listviewItemBuffers[0];
					} else {
						wsprintfW(data->listviewItemBuffers[1], L"%d sequences", data->nmcr->multiCells[di->item.iItem].nNodes);
						di->item.pszText = data->listviewItemBuffers[1];
					}
				}

				return TRUE;
			}
		}
	}
	return DefWindowProc(data->hWnd, WM_NOTIFY, wParam, (LPARAM) hdr);
}

LRESULT CALLBACK NmcrViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NMCRVIEWERDATA *data = (NMCRVIEWERDATA *) EditorGetData(hWnd);
	switch (msg) {
		case WM_CREATE:
		{
			NmcrViewerOnCreate(data);
			break;
		}
		case NV_INITIALIZE:
		{
			data->nmcr = (NMCR *) lParam;
			NmcrViewerSetMC(data, 0);

			ListView_SetItemCount(data->hWndMultiCellList, data->nmcr->nMultiCell);

			InvalidateRect(hWnd, NULL, FALSE);

			//DEBUG
			NmcrViewerStartPlayback(data);
			break;
		}
		case WM_SIZE:
		{
			NmcrViewerOnSize(data);
			break;
		}
		case WM_TIMER:
		{
			//increment timers
			if (NmcrViewerTick(data)) {
				InvalidateRect(hWnd, NULL, FALSE);
			}
			break;
		}
		case WM_COMMAND:
			break;
		case WM_NOTIFY:
			return NmcrViewerOnNotify(data, wParam, (LPNMHDR) lParam);
		case WM_KEYDOWN:
		{
			if (wParam == VK_SPACE) {
				//DEBUG: tick animation
				if (NmcrViewerTick(data)) {
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}
			break;
		}
		case WM_PAINT:
		{
			NmcrViewerOnPaint(data);
			return 0;
		}
		case WM_DESTROY:
			NmcrViewerOnDestroy(data);
			break;
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

void RegisterNmcrViewerClass(void) {
	McbkRegisterFormats();

	EditorRegister(L"NmcrViewerClass", NmcrViewerWndProc, L"Multi-Cell Viewer", sizeof(NMCRVIEWERDATA), 0);
}

HWND CreateNmcrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NMCR *nmcr) {
	HWND h = EditorCreate(L"NmcrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) NULL, (LPARAM) nmcr);
	return h;
}
