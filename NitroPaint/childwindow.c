#include "childwindow.h"
#include "nitropaint.h"
#include <Uxtheme.h>

BOOL __stdcall SetFontProc(HWND hWnd, LPARAM lParam) {
	SendMessage(hWnd, WM_SETFONT, (WPARAM) lParam, TRUE);
	return TRUE;
}

VOID SetWindowSize(HWND hWnd, int width, int height) {
	RECT rc = { 0 };
	rc.bottom = height;
	rc.right = width;
	AdjustWindowRect(&rc, GetWindowLong(hWnd, GWL_STYLE), !!GetMenu(hWnd) && !(GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_MDICHILD));
	SetWindowPos(hWnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE);
}

VOID DestroyChild(HWND hWnd) {
	SendMessage(hWnd, WM_CLOSE, 0, 0);
	if (IsWindow(hWnd)) DestroyWindow(hWnd);
}

VOID UpdateScrollbarVisibility(HWND hWnd) {
	SCROLLINFO scroll;
	scroll.fMask = SIF_ALL;
	ShowScrollBar(hWnd, SB_BOTH, TRUE);
	GetScrollInfo(hWnd, SB_HORZ, &scroll);
	if (scroll.nMax < (int) scroll.nPage) {
		EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH);
	} else {
		EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH);
	}
	GetScrollInfo(hWnd, SB_VERT, &scroll);
	if (scroll.nMax < (int) scroll.nPage) {
		EnableScrollBar(hWnd, SB_VERT, ESB_DISABLE_BOTH);
	} else {
		EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH);
	}
}

BOOL CALLBACK ScaleInterfaceProc(HWND hWnd, LPARAM lParam) {
	EnumChildWindows(hWnd, ScaleInterfaceProc, lParam);
	float scale = *(float *) lParam;

	//get bounding size
	RECT rcWindow;
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	GetWindowRect(hWnd, &rcWindow);
	POINT topLeft = { rcWindow.left, rcWindow.top };
	POINT bottomRight = { rcWindow.right, rcWindow.bottom };
	ScreenToClient(hWndParent, &topLeft);
	ScreenToClient(hWndParent, &bottomRight);

	//scale appropriately
	topLeft.x = (int) (topLeft.x * scale + 0.5f);
	topLeft.y = (int) (topLeft.y * scale + 0.5f);
	bottomRight.x = (int) (bottomRight.x * scale + 0.5f);
	bottomRight.y = (int) (bottomRight.y * scale + 0.5f);

	//set position
	int width = bottomRight.x - topLeft.x;
	int height = bottomRight.y - topLeft.y;
	SetWindowPos(hWnd, hWnd, topLeft.x, topLeft.y, width, height, SWP_NOZORDER);

	return TRUE;
}

VOID ScaleInterface(HWND hWnd, float scale) {
	//iterate child windows recursively
	EnumChildWindows(hWnd, ScaleInterfaceProc, (LPARAM) &scale);

	//resize parent
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	int width = (int) (rcClient.right * scale + 0.5f);
	int height = (int) (rcClient.bottom * scale + 0.5f);
	SetWindowPos(hWnd, hWnd, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE);
}

void setStyle(HWND hWnd, BOOL set, DWORD style) {
	if (set) {
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | style);
	} else {
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~style);
	}
}

HWND getMainWindow(HWND hWnd) {
	HWND hWndMdi = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
	HWND hWndMain = (HWND) GetWindowLong(hWndMdi, GWL_HWNDPARENT);
	return hWndMain;
}

LRESULT WINAPI DefChildProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
#if(g_useDarkTheme)
			SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
#endif
			//SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_HSCROLL | WS_VSCROLL);
			return 1;
		}
		case WM_HSCROLL:
		{
			WORD ctl = LOWORD(wParam);
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO scrollInfo;
			scrollInfo.cbSize = sizeof(scrollInfo);
			scrollInfo.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &scrollInfo);

			FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);

			int scrollOffsetX = scrollInfo.nPos;

			if (ctl == SB_THUMBPOSITION || ctl == SB_THUMBTRACK) {
				scrollOffsetX = HIWORD(wParam);
			} else if (ctl == SB_LEFT) {
				scrollOffsetX = 0;
			} else if (ctl == SB_RIGHT) {
				scrollOffsetX = frameData->contentWidth - (rcClient.right - rcClient.left);
			} else if (ctl == SB_LINERIGHT) {
				scrollOffsetX += 32;
				if (scrollOffsetX + rcClient.right - rcClient.left > frameData->contentWidth) {
					scrollOffsetX = frameData->contentWidth - (rcClient.right - rcClient.left);
				}
			} else if (ctl == SB_LINELEFT) {
				scrollOffsetX -= 32;
				if (scrollOffsetX < 0) scrollOffsetX = 0;
			}
			SetScrollPos(hWnd, SB_HORZ, scrollOffsetX, TRUE);

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_VSCROLL:
		{
			WORD ctl = LOWORD(wParam);
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO scrollInfo;
			scrollInfo.cbSize = sizeof(scrollInfo);
			scrollInfo.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &scrollInfo);

			FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);

			int scrollOffsetY = scrollInfo.nPos;

			if (ctl == SB_THUMBPOSITION || ctl == SB_THUMBTRACK) {
				scrollOffsetY = HIWORD(wParam);
			} else if (ctl == SB_TOP) {
				scrollOffsetY = 0;
			} else if (ctl == SB_BOTTOM) {
				scrollOffsetY = frameData->contentHeight - (rcClient.bottom - rcClient.top);
			} else if (ctl == SB_LINEDOWN) {
				scrollOffsetY += 32;
				if (scrollOffsetY + rcClient.bottom - rcClient.top > frameData->contentHeight) {
					scrollOffsetY = frameData->contentHeight - (rcClient.bottom - rcClient.top - frameData->paddingBottom);
				}
			} else if (ctl == SB_LINEUP) {
				scrollOffsetY -= 32;
				if (scrollOffsetY < 0) scrollOffsetY = 0;
			}
			SetScrollPos(hWnd, SB_VERT, scrollOffsetY, TRUE);

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_MOUSEWHEEL:
		{
			int amt = ((int) (short) HIWORD(wParam)) / WHEEL_DELTA;
			if (amt < 0) {
				amt = -amt;
				while(amt--)
					SendMessage(hWnd, WM_VSCROLL, SB_LINEDOWN, 0);
			} else if (amt > 0) {
				while(amt--)
					SendMessage(hWnd, WM_VSCROLL, SB_LINEUP, 0);
			}
			break;
		}
		case WM_SIZE:
		{
			BOOL repaint = FALSE;
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
			frameData->sizeLevel++;
			if (frameData->sizeLevel == 10) { //HACK: fix bug where some resizes cause the entire nonclient to disappear
				frameData->sizeLevel--;
				return DefWindowProc(hWnd, msg, wParam, lParam);
			}

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nPage = rcClient.right - rcClient.left + 1;
			info.fMask = SIF_PAGE;
			if ((int) info.nPage > frameData->contentWidth) {
				info.nPos = 0;
				info.fMask |= SIF_POS;
				repaint = TRUE;
			}
			SetScrollInfo(hWnd, SB_HORZ, &info, FALSE);

			info.fMask = SIF_PAGE;
			info.nPage = rcClient.bottom - rcClient.top + 1 - frameData->paddingBottom;
			if ((int) info.nPage > frameData->contentHeight) {
				info.nPos = 0;
				info.fMask |= SIF_POS;
				repaint = TRUE;
			}
			SetScrollInfo(hWnd, SB_VERT, &info, FALSE);

			if (repaint)InvalidateRect(hWnd, NULL, TRUE);

			//RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_INTERNALPAINT);
			frameData->sizeLevel--;
			break;
		}
		case WM_ENTERSIZEMOVE:
		{
			FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
			frameData->sizeLevel = 0;
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_EXSTYLE, GetWindowLong(hWndParent, GWL_EXSTYLE) | WS_EX_COMPOSITED);
			break;
		}
		case WM_EXITSIZEMOVE:
		{
			FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
			frameData->sizeLevel = 0;
			HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			SetWindowLong(hWndParent, GWL_EXSTYLE, GetWindowLong(hWndParent, GWL_EXSTYLE) & ~WS_EX_COMPOSITED);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}