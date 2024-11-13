#pragma once
#include <Windows.h>

//static control alignments
#define SCA_LEFT    0
#define SCA_RIGHT   1
#define SCA_CENTER  2

#define UI_SCALE_COORD(x,scale)    ((int)((x)*(scale)+0.5f))


//
// Sets or clears a window style.
//
void setStyle(HWND hWnd, BOOL set, DWORD style);

//
// Default modal window procedure.
//
LRESULT DefModalProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//
// Do modal.
//
void DoModal(HWND hWnd);

//
// DoModal with optional window close hook
//
void DoModalEx(HWND hWnd, BOOL closeHook);

//
// Do modal, but destroy the window when a handle is signaled.
//
void DoModalWait(HWND hWnd, HANDLE hWait);

//
// Create a button, optionally a default button.
//
HWND CreateButton(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL def);

//
// Create a checkbox.
//
HWND CreateCheckbox(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL checked);

//
// Create a groupbox.
//
HWND CreateGroupbox(HWND hWnd, LPCWSTR title, int x, int y, int width, int height);

//
// Create an edit control, optionally a number-only control.
//
HWND CreateEdit(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL number);

//
// Create a static control.
//
HWND CreateStatic(HWND hWnd, LPCWSTR text, int x, int y, int width, int height);

HWND CreateStaticAligned(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, int alignment);

//
// Create a combobox.
//
HWND CreateCombobox(HWND hWnd, LPCWSTR *items, int nItems, int x, int y, int width, int height, int def);

//
// Create a listbox.
//
HWND CreateListBox(HWND hWnd, int x, int y, int width, int height);

//
// Create a trackbar.
//
HWND CreateTrackbar(HWND hWnd, int x, int y, int width, int height, int vMin, int vMax, int vDef);


//
// Get if a checkbox is checked.
//
int GetCheckboxChecked(HWND hWnd);

//
// Get the number value of an edit control.
//
int GetEditNumber(HWND hWnd);

//
// Set the number value of an edit control.
//
void SetEditNumber(HWND hWnd, int n);

//
// Get trackbar position
//
int GetTrackbarPosition(HWND hWnd);

//
// Gets the selected listbox item.
//
int GetListBoxSelection(HWND hWnd);

//
// Sets the selected listbox item.
//
void SetListBoxSelection(HWND hWnd, int sel);

//
// Add an item to a listbox.
//
void AddListBoxItem(HWND hWnd, LPCWSTR item);

//
// Remove an item from a listbox.
//
void RemoveListBoxItem(HWND hWnd, int index);


//
// Replace an item in a listbox.
//
void ReplaceListBoxItem(HWND hWnd, int index, LPCWSTR newitem);


//
// Create a ListView in report mode.
//
HWND CreateListView(HWND hWnd, int x, int y, int width, int height);

//
// Create a ListView in multiple select mode.
//
HWND CreateCheckedListView(HWND hWnd, int x, int y, int width, int height);

//
// Add a column to a ListView.
//
void AddListViewColumn(HWND hWnd, LPWSTR name, int col, int width, int alignment);

//
// Add an item to a ListView.
//
void AddListViewItem(HWND hWnd, LPWSTR text, int row, int col);

//
// Adds a checked item to a listview.
//
void AddCheckedListViewItem(HWND hWnd, LPWSTR text, int row, BOOL checked);

//
// Gets the checked state of a listview item.
//
int CheckedListViewIsChecked(HWND hWnd, int item);
