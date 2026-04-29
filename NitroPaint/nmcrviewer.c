#include <Windows.h>
#include <CommCtrl.h>

#include "nitropaint.h"
#include "nmcr.h"
#include "nmcrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nanrviewer.h"
#include "resource.h"

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
	memset(data->rendered, 0, sizeof(data->rendered));

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
	data->scale = 2;
	data->showBorders = 1;

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

	data->hWndPreview = CreateWindow(L"NmcrPreviewClass", L"", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, data->hWnd, NULL, NULL, NULL);
	FbCreate(&data->fb, data->hWndPreview, 0, 0);

	//TODO: top bar: [Start/Pause] [Step/Stop] [Sequences] [x] Force Affine [x] Force Double Size
	data->hWndPlayPause = CreateButton(data->hWnd, L"Play", 0, 0, 0, 0, FALSE);
	data->hWndStop = CreateButton(data->hWnd, L"Stop", 0, 0, 0, 0, FALSE);

	SetGUIFont(data->hWnd);
}

static void NmcrViewerPreviewCenter(NMCRVIEWERDATA *data) {
	//get client
	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);

	//get view size
	int viewWidth = 512 * data->scale;
	int viewHeight = 256 * data->scale;

	//check dimensions of view
	if (rcClient.right < viewWidth) {
		//set scroll H
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewWidth - rcClient.right) / 2;
		SetScrollInfo(data->hWndPreview, SB_HORZ, &scroll, TRUE);
	}
	if (rcClient.bottom < viewHeight) {
		//set scroll V
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewHeight - rcClient.bottom) / 2;
		SetScrollInfo(data->hWndPreview, SB_VERT, &scroll, TRUE);
	}
}

static void NmcrViewerOnDestroy(NMCRVIEWERDATA *data) {
	NmcrViewerFreeSeqPlayers(data);

	//remove create/destroy NANR callback
	EditorRemoveCreateCallback(data->editorMgr, FILE_TYPE_NANR, NmcrViewerOnCreateNanrViewer, data);
	if (data->nanrViewer != NULL) {
		EditorRemoveDestroyCallback((EDITOR_DATA *) data->nanrViewer, NmcrViewerOnDestroyNanrViewer, data);
	}
}

static void NmcrViewerOnSize(NMCRVIEWERDATA *data) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	float dpiScale = GetDpiScale();
	int listWidth = UI_SCALE_COORD(200, dpiScale);
	int barHeight = UI_SCALE_COORD(22, dpiScale);
	int ctlWidth = UI_SCALE_COORD(50, dpiScale);

	MoveWindow(data->hWndMultiCellList, 0, 0, listWidth, rcClient.bottom, TRUE);
	MoveWindow(data->hWndPreview, listWidth, barHeight, rcClient.right - listWidth, rcClient.bottom - barHeight, FALSE);

	MoveWindow(data->hWndPlayPause, listWidth, 0, ctlWidth, barHeight, TRUE);
	MoveWindow(data->hWndStop, listWidth + ctlWidth, 0, ctlWidth, barHeight, TRUE);
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

static void NmcrViewerOnMenuCommand(NMCRVIEWERDATA *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
		case ID_VIEW_RENDERTRANSPARENCY:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
	}
}

static void NmcrViewerOnCommand(NMCRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	//TODO: UI command

	if (lParam == 0 && HIWORD(wParam) == 0) {
		NmcrViewerOnMenuCommand(data, LOWORD(wParam));
	}
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

			data->frameData.contentWidth = 512 * data->scale;
			data->frameData.contentHeight = 256 * data->scale;
			NmcrViewerPreviewCenter(data);

			ListView_SetItemCount(data->hWndMultiCellList, data->nmcr->nMultiCell);

			InvalidateRect(hWnd, NULL, FALSE);

			//DEBUG
			NmcrViewerStartPlayback(data);
			break;
		}
		case NV_ZOOMUPDATED:
			SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		case WM_SIZE:
			NmcrViewerOnSize(data);
			break;
		case WM_TIMER:
		{
			//increment timers
			if (NmcrViewerTick(data)) {
				InvalidateRect(hWnd, NULL, FALSE);
			}
			break;
		}
		case WM_COMMAND:
			NmcrViewerOnCommand(data, wParam, lParam);
			break;
		case WM_PAINT:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case WM_NOTIFY:
			return NmcrViewerOnNotify(data, wParam, (LPNMHDR) lParam);
		case WM_DESTROY:
			NmcrViewerOnDestroy(data);
			break;
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

static void NmcrViewerPreviewGetScroll(NMCRVIEWERDATA *data, int *scrollX, int *scrollY) {
	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(data->hWndPreview, SB_HORZ, &scrollH);
	GetScrollInfo(data->hWndPreview, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}

static void NmcrViewerOnPaint(NMCRVIEWERDATA *data) {
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(data->hWndPreview, &ps);

	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);

	int width = rcClient.right, height = rcClient.bottom;
	FbSetSize(&data->fb, width, height);

	NCLR *nclr = (NCLR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_PALETTE);
	NCGR *ncgr = (NCGR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_CHARACTER);
	NCER *ncer = (NCER *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	NANR *nanr = (NANR *) NmcrViewerGetAssociatedObject(data, FILE_TYPE_NANR);

	RenderNmcrFrame(data, &data->player, data->nmcr, nclr, ncgr, ncer, nanr);
	
	int scrollX = 0, scrollY = 0, scale = data->scale;
	NmcrViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int viewWidth = 512 * data->scale - scrollX;
	int viewHeight = 256 * data->scale - scrollY;
	if (viewWidth > rcClient.right) viewWidth = rcClient.right;
	if (viewHeight > rcClient.bottom) viewHeight = rcClient.bottom;

	COLOR32 bgColor = 0;
	if (nclr != NULL && nclr->nColors >= 1) {
		bgColor = ColorConvertFromDS(nclr->colors[0]);
		bgColor = REVERSE(bgColor);
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int srcX = (x + scrollX) / scale, srcY = (y + scrollY) / scale;

			COLOR32 sample = 0xFFF0F0F0;
			if (srcX < 512 && srcY < 256) {
				sample = data->rendered[srcX + srcY * 512];
			}

			if ((sample >> 24) == 0) {
				if (g_configuration.renderTransparent) {
					//render transparent checkerboard
					COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
					sample = checker[((x ^ y) >> 2) & 1];
				} else {
					//render backdrop color
					sample = bgColor;
				}
			}

			data->fb.px[x + y * width] = sample;
		}
	}

	//render borders
	if (data->showBorders) {
		CellViewerRenderGridlines(&data->fb, data->scale, scrollX, scrollY);
	}

	FbDraw(&data->fb, hDC, 0, 0, rcClient.right, rcClient.bottom, 0, 0);
	EndPaint(data->hWndPreview, &ps);
}

static void NmcrViewerPreviewOnRecalculate(NMCRVIEWERDATA *data) {
	int contentWidth = 512 * data->scale, contentHeight = 256 * data->scale;

	SCROLLINFO info;
	info.cbSize = sizeof(info);
	info.nMin = 0;
	info.nMax = contentWidth;
	info.fMask = SIF_RANGE;
	SetScrollInfo(data->hWndPreview, SB_HORZ, &info, TRUE);

	info.nMax = contentHeight;
	SetScrollInfo(data->hWndPreview, SB_VERT, &info, TRUE);
	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);
	SendMessage(data->hWndPreview, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
}

static LRESULT CALLBACK NmcrViewerPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NMCRVIEWERDATA *data = EditorGetData(hWndEditor);

	if (data != NULL) {
		data->frameData.contentWidth = 512 * data->scale;
		data->frameData.contentHeight = 256 * data->scale;

		if (GetWindowLongPtr(hWnd, 0) == 0) {
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
		}
	}

	switch (msg) {
		case WM_CREATE:
		{
			break;
		}
		case WM_PAINT:
		{
			NmcrViewerOnPaint(data);
			return 0;
		}
		case NV_RECALCULATE:
			NmcrViewerPreviewOnRecalculate(data);
			break;
		case WM_SIZE:
		{
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->frameData.contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			return DefChildProc(hWnd, msg, wParam, lParam);
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterNmcrViewerClass(void) {
	McbkRegisterFormats();

	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EditorRegister(L"NmcrViewerClass", NmcrViewerWndProc, L"Multi-Cell Viewer", sizeof(NMCRVIEWERDATA), features);
	RegisterGenericClass(L"NmcrPreviewClass", NmcrViewerPreviewWndProc, sizeof(void *));
}

HWND CreateNmcrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NMCR *nmcr) {
	HWND h = EditorCreate(L"NmcrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) NULL, (LPARAM) nmcr);
	return h;
}
