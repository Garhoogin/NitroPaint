#include "childwindow.h"
#include "nitropaint.h"
#include <Uxtheme.h>

#define SCROLL_LINE_SIZE          32 // pixels per scroll line

BOOL CALLBACK SetFontProc(HWND hWnd, LPARAM lParam) {
	SendMessage(hWnd, WM_SETFONT, (WPARAM) lParam, TRUE);
	return TRUE;
}

void SetWindowSize(HWND hWnd, int width, int height) {
	RECT rc = { 0 };
	rc.bottom = height;
	rc.right = width;
	AdjustWindowRect(&rc, GetWindowLong(hWnd, GWL_STYLE), !!GetMenu(hWnd) && !(GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_MDICHILD));
	SetWindowPos(hWnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE);
}

void DestroyChild(HWND hWnd) {
	SendMessage(hWnd, WM_CLOSE, 0, 0);
	if (IsWindow(hWnd)) DestroyWindow(hWnd);
}

void UpdateScrollbarVisibility(HWND hWnd) {
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

void ScaleInterface(HWND hWnd, float scale) {
	//iterate child windows recursively
	EnumChildWindows(hWnd, ScaleInterfaceProc, (LPARAM) &scale);

	//resize parent
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	int width = (int) (rcClient.right * scale + 0.5f);
	int height = (int) (rcClient.bottom * scale + 0.5f);
	SetWindowPos(hWnd, hWnd, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE);
}

HWND getMainWindow(HWND hWnd) {
	HWND hWndMdi = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	HWND hWndMain = (HWND) GetWindowLongPtr(hWndMdi, GWL_HWNDPARENT);
	return hWndMain;
}

LRESULT WINAPI DefChildProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case WM_CREATE:
		{
			SetGUIFont(hWnd);
#if(g_useDarkTheme)
			SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
#endif
			//SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) | WS_HSCROLL | WS_VSCROLL);
			return 1;
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			int scrollbar = (msg == WM_HSCROLL) ? SB_HORZ : SB_VERT;

			SCROLLINFO scrollInfo;
			scrollInfo.cbSize = sizeof(scrollInfo);
			scrollInfo.fMask = SIF_ALL;
			GetScrollInfo(hWnd, scrollbar, &scrollInfo);

			int scrollOffset = scrollInfo.nPos;
			int maxScroll = 0, pageSize = 0, contentSize = 0;
			switch (scrollbar) {
				case SB_HORZ:
					pageSize = rcClient.right;
					contentSize = frameData->contentWidth;
					break;
				case SB_VERT:
					pageSize = rcClient.bottom;
					contentSize = frameData->contentHeight;
					break;
			}
			maxScroll = contentSize - pageSize;

			switch (LOWORD(wParam)) {
				case SB_THUMBPOSITION:
				case SB_THUMBTRACK:
					scrollOffset = HIWORD(wParam);
					break;
				case SB_LEFT:
				//case SB_TOP:
					scrollOffset = 0;
					break;
				case SB_RIGHT:
				//case SB_BOTTOM:
					scrollOffset = maxScroll;
					break;
				case SB_LINERIGHT:
				//case SB_LINEDOWN:
					scrollOffset += SCROLL_LINE_SIZE;
					break;
				case SB_LINELEFT:
				//case SB_LINEUP:
					scrollOffset -= SCROLL_LINE_SIZE;
					break;
				case SB_PAGELEFT:
				//case SB_PAGEUP:
					scrollOffset -= pageSize;
					break;
				case SB_PAGERIGHT:
				//case SB_PAGEDOWN:
					scrollOffset += pageSize;
					break;
			}

			if (scrollOffset > maxScroll) scrollOffset = maxScroll;
			if (scrollOffset < 0) scrollOffset = 0;

			SetScrollPos(hWnd, scrollbar, scrollOffset, TRUE);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_MOUSEWHEEL:
		{
			//scroll delta
			int amt = SCROLL_LINE_SIZE * (int) (short) HIWORD(wParam);
			amt = (amt + (amt < 0 ? -WHEEL_DELTA : WHEEL_DELTA) / 2) / WHEEL_DELTA;
			amt = -amt;

			//move
			SCROLLINFO scrollInfo = { 0 };
			scrollInfo.cbSize = sizeof(scrollInfo);
			scrollInfo.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &scrollInfo);

			scrollInfo.nPos += amt;
			if (scrollInfo.nPos < 0) scrollInfo.nPos = 0;
			if (scrollInfo.nPos >= scrollInfo.nMax) scrollInfo.nPos = scrollInfo.nMax;
			SendMessage(hWnd, WM_VSCROLL, MAKELONG(SB_THUMBPOSITION, scrollInfo.nPos), 0);

			break;
		}
		case WM_MOUSEHWHEEL:
			return 0;
		case WM_SIZE:
		{
			BOOL repaint = FALSE;
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nPage = rcClient.right + 1;
			info.fMask = SIF_PAGE;
			if (rcClient.right >= frameData->contentWidth) {
				info.nPos = 0;
				info.fMask |= SIF_POS;
				repaint = TRUE;
			}
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.fMask = SIF_PAGE;
			info.nPage = rcClient.bottom + 1;
			if (rcClient.bottom >= frameData->contentHeight) {
				info.nPos = 0;
				info.fMask |= SIF_POS;
				repaint = TRUE;
			}
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			break;
		}
		case WM_ENTERSIZEMOVE:
		{
			//HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
			//SetWindowLong(hWndParent, GWL_EXSTYLE, GetWindowLong(hWndParent, GWL_EXSTYLE) | WS_EX_COMPOSITED);
			break;
		}
		case WM_EXITSIZEMOVE:
		{
			//HWND hWndParent = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
			//SetWindowLong(hWndParent, GWL_EXSTYLE, GetWindowLong(hWndParent, GWL_EXSTYLE) & ~WS_EX_COMPOSITED);
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}
