#include <Windows.h>
#include <CommCtrl.h>

#include "ui.h"

HWND CreateButton(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL def) {
	return CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | WS_TABSTOP | (def ? BS_DEFPUSHBUTTON : 0), x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateCheckbox(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL checked) {
	HWND h = CreateWindow(L"BUTTON", text, WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
	return h;
}

HWND CreateGroupbox(HWND hWnd, LPCWSTR title, int x, int y, int width, int height) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS | BS_GROUPBOX;
	return CreateWindow(L"BUTTON", title, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateEdit(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL number) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP | (number ? ES_NUMBER : 0);
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", text, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	return h;
}

HWND CreateStatic(HWND hWnd, LPCWSTR text, int x, int y, int width, int height) {
	return CreateWindow(L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateStaticAligned(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, int alignment) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE;
	if (alignment == SCA_RIGHT) dwStyle |= SS_RIGHT;
	if (alignment == SCA_CENTER) dwStyle |= SS_CENTER;
	return CreateWindow(L"STATIC", text, dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
}

HWND CreateCombobox(HWND hWnd, LPCWSTR *items, int nItems, int x, int y, int width, int height, int def) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST | WS_TABSTOP;
	HWND h = CreateWindow(L"COMBOBOX", L"", dwStyle, x, y, width, 100, hWnd, NULL, NULL, NULL);
	if (items != NULL && nItems > 0) {
		for (int i = 0; i < nItems; i++) {
			LPCWSTR item = items[i];
			SendMessage(h, CB_ADDSTRING, wcslen(item), (LPARAM) item);
		}
	}
	SendMessage(h, CB_SETCURSEL, def, 0);
	return h;
}

HWND CreateListBox(HWND hWnd, int x, int y, int width, int height) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY;
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	return  h;
}

HWND CreateTrackbar(HWND hWnd, int x, int y, int width, int height, int vMin, int vMax, int vDef) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	HWND h = CreateWindow(TRACKBAR_CLASS, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(h, TBM_SETRANGE, TRUE, vMin | (vMax << 16));
	SendMessage(h, TBM_SETPOS, TRUE, vDef);
	return h;
}

int GetCheckboxChecked(HWND hWnd) {
	return SendMessage(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int GetEditNumber(HWND hWnd) {
	WCHAR buffer[32];
	SendMessage(hWnd, WM_GETTEXT, sizeof(buffer) / sizeof(*buffer), (LPARAM) buffer);
	return _wtol(buffer);
}

void SetEditNumber(HWND hWnd, int n) {
	WCHAR buffer[32];
	int len = wsprintfW(buffer, L"%d", n);
	SendMessage(hWnd, WM_SETTEXT, len, (LPARAM) buffer);
}

int GetTrackbarPosition(HWND hWnd) {
	return SendMessage(hWnd, TBM_GETPOS, 0, 0);
}



int GetListBoxSelection(HWND hWnd) {
	return SendMessage(hWnd, LB_GETCURSEL, 0, 0);
}

void SetListBoxSelection(HWND hWnd, int sel) {
	SendMessage(hWnd, LB_SETCURSEL, sel, 0);
}

void AddListBoxItem(HWND hWnd, LPCWSTR item) {
	SendMessage(hWnd, LB_ADDSTRING, 0, (LPARAM) item);
}

void RemoveListBoxItem(HWND hWnd, int index) {
	SendMessage(hWnd, LB_DELETESTRING, index, 0);
}

void ReplaceListBoxItem(HWND hWnd, int index, LPCWSTR newitem) {
	SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
	SendMessage(hWnd, LB_DELETESTRING, index, 0);
	SendMessage(hWnd, LB_INSERTSTRING, index, (LPARAM) newitem);
	SendMessage(hWnd, LB_SETCURSEL, index, 0);
	SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(hWnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}



HWND CreateListView(HWND hWnd, int x, int y, int width, int height) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | LVS_REPORT | LVS_EDITLABELS
		| LVS_SINGLESEL | WS_VSCROLL | WS_BORDER;
	HWND hWndLv = CreateWindowEx(0, WC_LISTVIEW, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(hWndLv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
	return hWndLv;
}

void AddListViewColumn(HWND hWnd, LPWSTR name, int col, int width, int alignment) {
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM
		| LVCF_MINWIDTH | LVCF_DEFAULTWIDTH | LVCF_IDEALWIDTH;
	lvc.iSubItem = col;
	lvc.pszText = name;
	lvc.cx = lvc.cxMin = lvc.cxIdeal = lvc.cxDefault = width;
	lvc.fmt = LVCFMT_LEFT | LVCFMT_FIXED_WIDTH;
	if (alignment == SCA_RIGHT) lvc.fmt |= LVCFMT_RIGHT;
	ListView_InsertColumn(hWnd, col, &lvc);
}

void AddListViewItem(HWND hWnd, LPWSTR text, int row, int col) {
	LVITEM lvi = { 0 };
	lvi.mask = LVIF_TEXT | LVIF_STATE;
	lvi.pszText = text;
	lvi.state = 0;
	lvi.iSubItem = col;
	lvi.iItem = row;
	if (col == 0) {
		ListView_InsertItem(hWnd, &lvi);
	} else {
		ListView_SetItem(hWnd, &lvi);
	}
}

//subclass proc that focuses the parent on WM_CLOSE (less flicker)
LRESULT CALLBACK ModalCloseHookProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	if (msg == WM_CLOSE) {
		//prep parent for focus
		HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
		if (hWndParent != NULL) {
			setStyle(hWndParent, FALSE, WS_DISABLED);
			SetActiveWindow(hWndParent);
			SetForegroundWindow(hWndParent);
		}
	}
	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

void DoModalEx(HWND hWnd, BOOL closeHook) {
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	ShowWindow(hWnd, SW_SHOW);
	SetActiveWindow(hWnd);
	SetForegroundWindow(hWnd);
	if (hWndParent != NULL) setStyle(hWndParent, TRUE, WS_DISABLED);

	//override the WndProc. 
	if (closeHook) SetWindowSubclass(hWnd, ModalCloseHookProc, 1, 0);

	//enter our own message loop
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		//normal dispatching
		if (!IsDialogMessage(hWnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		//check the window still exists. If not, prepare the main window for focus
		if (!IsWindow(hWnd)) {
			break;
		}
	}

	if (hWndParent != NULL) {
		setStyle(hWndParent, FALSE, WS_DISABLED);
		SetActiveWindow(hWndParent);
		SetForegroundWindow(hWndParent);
	}
}

void DoModal(HWND hWnd) {
	DoModalEx(hWnd, TRUE);
}

void DoModalWait(HWND hWnd, HANDLE hWait) {
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	ShowWindow(hWnd, SW_SHOW);
	SetActiveWindow(hWnd);
	SetForegroundWindow(hWnd);
	if (hWndParent != NULL) setStyle(hWndParent, TRUE, WS_DISABLED);

	//override the WndProc. 
	SetWindowSubclass(hWnd, ModalCloseHookProc, 1, 0);

	//enter our own message loop
	MSG msg;
	DWORD waitResult;
	while (1) {
		waitResult = MsgWaitForMultipleObjects(1, &hWait, FALSE, INFINITE, QS_ALLINPUT);
		if (waitResult == WAIT_OBJECT_0) { //event signaled
			//destroy window cleanly
			if (hWndParent != NULL) {
				setStyle(hWndParent, FALSE, WS_DISABLED);
				SetActiveWindow(hWndParent);
				SetForegroundWindow(hWndParent);
			}
			DestroyWindow(hWnd);
			break;
		}

		//BEWARE: MsgWaitForMultipleObjects only alerts us of new messages:
		BOOL quit = FALSE;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				quit = TRUE;
				break;
			}

			//normal dispatching
			if (!IsDialogMessage(hWnd, &msg)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		//check the window still exists. If not, prepare the main window for focus
		if (quit || !IsWindow(hWnd)) {
			break;
		}
	}

	if (hWndParent != NULL) {
		setStyle(hWndParent, FALSE, WS_DISABLED);
		SetActiveWindow(hWndParent);
		SetForegroundWindow(hWndParent);
	}
}
