#include <Windows.h>
#include <CommCtrl.h>
#include <Uxtheme.h>
#include <Shlwapi.h>
#include <ShlObj.h>

#include "ui.h"
#include "nitropaint.h"

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
			UiCbAddString(h, items[i]);
		}
	}
	UiCbSetCurSel(h, def);
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

wchar_t *UiEditGetText(HWND hWnd) {
	unsigned int length = SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0);
	wchar_t *buf = (wchar_t *) calloc(length + 1, sizeof(wchar_t));
	if (buf == NULL) return NULL;

	SendMessage(hWnd, WM_GETTEXT, length + 1, (LPARAM) buf);
	return buf;
}

void UiEditSetText(HWND hWnd, const wchar_t *text) {
	SendMessage(hWnd, WM_SETTEXT, 0, (LPARAM) text);
}

int GetEditNumber(HWND hWnd) {
	WCHAR buffer[32];
	SendMessage(hWnd, WM_GETTEXT, sizeof(buffer) / sizeof(*buffer), (LPARAM) buffer);
	return _wtol(buffer);
}

void SetEditNumber(HWND hWnd, int n) {
	WCHAR buffer[32];
	int len = wsprintfW(buffer, L"%d", n);
	UiEditSetText(hWnd, buffer);
}

int GetTrackbarPosition(HWND hWnd) {
	return SendMessage(hWnd, TBM_GETPOS, 0, 0);
}


int UiCbGetCurSel(HWND hWnd) {
	return SendMessage(hWnd, CB_GETCURSEL, 0, 0);
}

void UiCbSetCurSel(HWND hWnd, int sel) {
	SendMessage(hWnd, CB_SETCURSEL, sel, 0);
}

void UiCbAddString(HWND hWnd, const wchar_t *str) {
	SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM) str);
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

HWND CreateVirtualListView(HWND hWnd, int x, int y, int width, int height) {
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | LVS_REPORT | LVS_EDITLABELS
		| LVS_SINGLESEL | LVS_OWNERDATA | WS_VSCROLL | WS_BORDER;
	HWND hWndLv = CreateWindowEx(0, WC_LISTVIEW, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(hWndLv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
	return hWndLv;
}

HWND CreateCheckedListView(HWND hWnd, int x, int y, int width, int height) {
	DWORD dwExStyle = LVS_EX_CHECKBOXES;
	DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | LVS_REPORT | LVS_NOCOLUMNHEADER | WS_VSCROLL | WS_BORDER;
	HWND hWndLv = CreateWindowEx(0, WC_LISTVIEW, L"", dwStyle, x, y, width, height, hWnd, NULL, NULL, NULL);
	SendMessage(hWndLv, LVM_SETEXTENDEDLISTVIEWSTYLE, dwExStyle, dwExStyle);
	AddListViewColumn(hWndLv, L"", 0, width - 2 - GetSystemMetrics(SM_CXVSCROLL), SCA_LEFT);
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

void AddCheckedListViewItem(HWND hWnd, LPWSTR text, int row, BOOL checked) {
	LVITEM lvi = { 0 };
	lvi.mask = LVIF_TEXT | LVIF_STATE;
	lvi.pszText = text;
	lvi.iSubItem = 0;
	lvi.iItem = row;
	ListView_InsertItem(hWnd, &lvi);
	ListView_SetItemState(hWnd, row, (1 + !!checked) << 12, LVIS_STATEIMAGEMASK);
}

int CheckedListViewIsChecked(HWND hWnd, int item) {
	int state = SendMessage(hWnd, LVM_GETITEMSTATE, item, LVIS_STATEIMAGEMASK);
	return (state >> 12) - 1;
}



HWND UiStatusbarCreate(HWND hWndParent, int nPart, const int *widths) {
	HWND h = CreateWindow(STATUSCLASSNAME, L"", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hWndParent, NULL, NULL, NULL);

	//construct parts
	if (nPart) {
		int x = 0;
		int *parts = (int *) calloc(nPart, sizeof(int));

		float dpiScale = GetDpiScale();

		for (int i = 0; i < nPart; i++) {
			//convert to right-edge
			int width = widths[i];
			if (width != -1) parts[i] = UI_SCALE_COORD(width + x, dpiScale);
			else parts[i] = -1;
			x += width;
		}

		SendMessage(h, SB_SETPARTS, nPart, (LPARAM) parts);
		free(parts);
	}

	return h;
}

void UiStatusbarSetText(HWND hWndSB, int iPart, const wchar_t *text) {
	SendMessage(hWndSB, SB_SETTEXT, iPart, (LPARAM) text);
}



// ----- dialog routines

wchar_t *UiDlgBrowseForFolder(HWND hWndParent, const wchar_t *title) {
	//use new browse for folder
	LPWSTR ppName = NULL;
	IFileOpenDialog *dlg = NULL;
	IShellItem *item = NULL;
	wchar_t *path = NULL;

	HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, &dlg);
	if (SUCCEEDED(hr)) {

		//set options
		SUCCEEDED(hr) && (hr = dlg->lpVtbl->SetOptions(dlg, FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST));
		SUCCEEDED(hr) && (hr = dlg->lpVtbl->SetTitle(dlg, title));

		//show dialog
		SUCCEEDED(hr) && (hr = dlg->lpVtbl->Show(dlg, hWndParent));

		//get result
		SUCCEEDED(hr) && (hr = dlg->lpVtbl->GetResult(dlg, &item));
		SUCCEEDED(hr) && (hr = item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &ppName));
		if (!SUCCEEDED(hr)) goto Error;

		//copy string
		path = calloc(wcslen(ppName) + 1, sizeof(WCHAR));
		memcpy(path, ppName, wcslen(ppName) * sizeof(WCHAR));
		CoTaskMemFree(ppName);
	Error:
		if (item != NULL) item->lpVtbl->Release(item);
		if (dlg != NULL) dlg->lpVtbl->Release(dlg);

	} else {
		//use old browse for folder
		path = (WCHAR *) calloc(MAX_PATH, sizeof(WCHAR));

		BROWSEINFO bf;
		bf.hwndOwner = hWndParent;
		bf.pidlRoot = NULL;
		bf.pszDisplayName = path;
		bf.lpszTitle = title;
		bf.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE; //I don't much like the new dialog style
		bf.lpfn = NULL;
		bf.lParam = 0;
		bf.iImage = 0;
		PIDLIST_ABSOLUTE idl = SHBrowseForFolder(&bf);

		if (idl == NULL) {
			free(path);
			return NULL;
		}

		SHGetPathFromIDList(idl, path);
		CoTaskMemFree(idl);
	}
	return path;
}



// ----- modal routines

//subclass proc that focuses the parent on WM_CLOSE (less flicker)
LRESULT CALLBACK ModalCloseHookProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	if (msg == WM_CLOSE) {
		//prep parent for focus
		HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
		if (hWndParent != NULL) {
			EnableWindow(hWndParent, TRUE);
			SetActiveWindow(hWndParent);
			SetForegroundWindow(hWndParent);
		}
	}
	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void DpiScaleModal(HWND hWnd) {
	float dpiScale = GetDpiScale();

	if (dpiScale != 1.0f) {
		DpiScaleChildren(hWnd, dpiScale);

		RECT rcClient, rcWindow;
		GetClientRect(hWnd, &rcClient);
		GetWindowRect(hWnd, &rcWindow);

		rcClient.right = (int) (rcClient.right * dpiScale + 0.5f);
		rcClient.bottom = (int) (rcClient.bottom * dpiScale + 0.5f);
		AdjustWindowRect(&rcClient, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL);

		MoveWindow(hWnd, rcWindow.left, rcWindow.top, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, TRUE);
	}
}

static BOOL CALLBACK UiSetThemeProc(HWND hWnd, LPARAM lParam) {
	SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
	return TRUE;
}

static BOOL CALLBACK UiPushbuttonEnumProc(HWND hWnd, LPARAM lparam) {
	WCHAR cls[32];
	GetClassName(hWnd, cls, sizeof(cls) / sizeof(cls[0]));
	if (_wcsicmp(cls, L"BUTTON") != 0) return TRUE; //not found

	DWORD dwType = GetWindowLong(hWnd, GWL_STYLE) & BS_TYPEMASK;
	if (dwType != BS_DEFPUSHBUTTON) return TRUE; //not found
	
	//simulate button command
	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	SendMessage(hWndParent, WM_COMMAND, BN_CLICKED << 16, (LPARAM) hWnd);
	return FALSE; //found, stop enumerating
}

static void UiHandleCommandOk(HWND hWnd) {
	//enumerate child windows, send command to first button with BS_DEFPUSHBUTTON style.
	EnumChildWindows(hWnd, UiPushbuttonEnumProc, 0);
}

LRESULT DefModalProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_NCCREATE:
			//handle DPI awareness
			DoHandleNonClientDpiScale(hWnd);
			break;
		case 0x02E0://WM_DPICHANGED:
			//handle DPI update
			return HandleWindowDpiChange(hWnd, wParam, lParam);
		case WM_COMMAND:
			//if escape pressed, close dialog
			if (lParam == 0 && LOWORD(wParam) == IDCANCEL) SendMessage(hWnd, WM_CLOSE, 0, 0);
			if (lParam == 0 && LOWORD(wParam) == IDOK) UiHandleCommandOk(hWnd);
			break;
#if(g_useDarkTheme)
		case WM_CREATE:
		case NV_INITIALIZE:
			EnumChildWindows(hWnd, UiSetThemeProc, 0);
			break;
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
			SetBkMode((HDC) wParam, TRANSPARENT);
			SetTextColor((HDC) wParam, RGB(255, 255, 255));
			return (LRESULT) (HBRUSH) GetClassLongPtr(hWnd, GCL_HBRBACKGROUND);
		case WM_CTLCOLORDLG:
			break;
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			SetBkMode((HDC) wParam, TRANSPARENT);
			SetTextColor((HDC) wParam, RGB(255, 255, 255));
			break;
#endif
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void DoModalEx(HWND hWnd, BOOL closeHook) {
	//do DPI scaling
	DpiScaleModal(hWnd);

	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	ShowWindow(hWnd, SW_SHOW);
	SetActiveWindow(hWnd);
	SetForegroundWindow(hWnd);
	if (hWndParent != NULL) EnableWindow(hWndParent, FALSE);

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
		EnableWindow(hWndParent, TRUE);
		SetActiveWindow(hWndParent);
		SetForegroundWindow(hWndParent);
	}
}

void DoModal(HWND hWnd) {
	DoModalEx(hWnd, TRUE);
}

void DoModalWait(HWND hWnd, HANDLE hWait) {
	//do DPI scaling
	DpiScaleModal(hWnd);

	HWND hWndParent = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	ShowWindow(hWnd, SW_SHOW);
	SetActiveWindow(hWnd);
	SetForegroundWindow(hWnd);
	if (hWndParent != NULL) EnableWindow(hWndParent, FALSE);

	//enter our own message loop
	MSG msg;
	DWORD waitResult;
	while (1) {
		waitResult = MsgWaitForMultipleObjects(1, &hWait, FALSE, INFINITE, QS_ALLINPUT);
		if (waitResult == WAIT_OBJECT_0) { //event signaled
			//destroy window cleanly
			if (hWndParent != NULL) {
				EnableWindow(hWndParent, TRUE);
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
		EnableWindow(hWndParent, TRUE);
		SetActiveWindow(hWndParent);
		SetForegroundWindow(hWndParent);
	}
}


typedef struct UiMgrCommandKey_ {
	HWND hWnd;
	int notif;
} UiMgrCommandKey;

static UiMgrCommandProc UiCtlMgrGetCommandProc(UiCtlManager *mgr, HWND hWnd, int notif) {
	UiMgrCommandProc proc = NULL;
	UiMgrCommandKey key;
	key.hWnd = hWnd;
	key.notif = notif;
	StStatus status = StMapGet(&mgr->cmdMap, &key, &proc);
	if (ST_SUCCEEDED(status)) return proc;

	return NULL;
}

int UiCtlMgrInit(UiCtlManager *mgr, void *param) {
	//init command map
	StStatus status = StMapCreate(&mgr->cmdMap, sizeof(UiMgrCommandKey), sizeof(UiMgrCommandProc));
	if (!ST_SUCCEEDED(status)) return 0;

	mgr->param = param;
	mgr->init = 1;
	return 1;
}

int UiCtlMgrFree(UiCtlManager *mgr) {
	//free command map
	if (!mgr->init) return 0;

	mgr->init = 0;
	StMapFree(&mgr->cmdMap);
	return 1;
}

int UiCtlMgrAddCommand(UiCtlManager *mgr, HWND hWnd, int notif, UiMgrCommandProc proc) {
	if (!mgr->init) return 0;

	//add a command entry
	UiMgrCommandKey key;
	key.hWnd = hWnd;
	key.notif = notif;
	StStatus status = StMapPut(&mgr->cmdMap, &key, &proc);
	return ST_SUCCEEDED(status);
}

void UiCtlMgrOnCommand(UiCtlManager *mgr, HWND hWnd, WPARAM wParam, LPARAM lParam) {
	if (!mgr->init) return;

	//process UI command
	HWND hWndControl = (HWND) lParam;
	if (hWndControl != NULL) {
		//look up control callback
		UiMgrCommandProc proc = UiCtlMgrGetCommandProc(mgr, hWndControl, HIWORD(wParam));
		if (proc != NULL) proc(hWnd, hWndControl, HIWORD(wParam), mgr->param);
	} else if (HIWORD(wParam) == 0) {
		//on menu command
	} else if (HIWORD(wParam) == 1) {
		//on accelerator
	}
}
