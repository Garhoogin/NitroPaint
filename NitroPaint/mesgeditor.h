#pragma once

#include "editor.h"
#include "mesg.h"
#include "ui.h"
#include "framebuffer.h"
#include "nftrviewer.h"

typedef struct MesgEditorData_ {
	EDITOR_BASIC_MEMBERS;
	MesgFile *mesg;
	UiCtlManager mgr;
	FrameBuffer fbPreview;

	NFTRVIEWERDATA *fontEditor; // associated font editor
	StList fontEditors;         // font editors in dropdown

	HWND hWndList;
	HWND hWndEdit;
	HWND hWndFontLabel;
	HWND hWndFontList;
	HWND hWndEditData;
	HWND hWndEditDataSize;
	HWND hWndEncoding;

	HWND hWndDecodeSystemTags;

	unsigned int curMsg;
	int decodeSystemTags;

	int updatingEdit;
	int suppressListRedraw;
} MesgEditorData;

void MesgEditorRegisterClass(void);

HWND CreateMesgEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path);

HWND CreateMesgEditorImmediate(int x, int y, int width, int height, HWND hWndParent, MesgFile *mesg);
