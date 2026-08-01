#include <Windows.h>

#include "nitropaint.h"
#include "color.h"
#include "palops.h"
#include "childwindow.h"
#include "colorchooser.h"
#include "ui.h"

typedef struct PAL_OP_DATA_ {
	HWND hWndHue;
	HWND hWndSaturation;
	HWND hWndValue;
	HWND hWndRotate;
	HWND hWndSrcIndex;
	HWND hWndSrcLength;
	HWND hWndIgnoreFirst;
	HWND hWndDestOffset;
	HWND hWndDestCount;
	HWND hWndDestStride;
	HWND hWndComplete;

	PAL_OP *op;
	int init;
} PAL_OP_DATA;

static void PalopPopulateUI(PAL_OP_DATA *data, PAL_OP *op) {
	SendMessage(data->hWndIgnoreFirst, BM_SETCHECK, op->ignoreFirst, 0);
	SetEditNumber(data->hWndHue, op->hueRotate);
	SetEditNumber(data->hWndSaturation, op->saturationAdd);
	SetEditNumber(data->hWndValue, op->valueAdd);
	SetEditNumber(data->hWndRotate, op->paletteRotation);
	SetEditNumber(data->hWndSrcIndex, op->srcIndex);
	SetEditNumber(data->hWndSrcLength, op->srcLength);
	SetEditNumber(data->hWndDestOffset, op->dstOffset);
	SetEditNumber(data->hWndDestCount, op->dstCount);
	SetEditNumber(data->hWndDestStride, op->dstStride);
}

static void PalopReadUI(PAL_OP_DATA *data, PAL_OP *op) {
	op->ignoreFirst = GetCheckboxChecked(data->hWndIgnoreFirst);
	op->hueRotate = GetEditNumber(data->hWndHue);
	op->saturationAdd = GetEditNumber(data->hWndSaturation);
	op->valueAdd = GetEditNumber(data->hWndValue);
	op->paletteRotation = GetEditNumber(data->hWndRotate);
	op->srcIndex = GetEditNumber(data->hWndSrcIndex);
	op->srcLength = GetEditNumber(data->hWndSrcLength);
	op->dstOffset = GetEditNumber(data->hWndDestOffset);
	op->dstCount = GetEditNumber(data->hWndDestCount);
	op->dstStride = GetEditNumber(data->hWndDestStride);
}

void PalopRunOperation(COLOR *palIn, COLOR *palOut, int palSize, PAL_OP *op) {
	for (int i = 0; i < palSize; i++) {
		palOut[i] = palIn[i];
	}

	//run one entry for each iteration at a time
	COLOR *inBase = palIn + op->srcIndex;
	COLOR *outBase = palOut + op->srcIndex + op->dstOffset * op->dstStride;
	int outBaseIndex = op->srcIndex + op->dstOffset * op->dstStride;
	int blockLength = op->srcLength;
	for (int i = 0; i < blockLength; i++) {
		COLOR srcColor = inBase[i];
		COLOR32 as32 = ColorConvertFromDS(srcColor);
		COLOR32 out32;
		int ch, cs, cv;
		ConvertRGBToHSV(as32, &ch, &cs, &cv);

		for (int j = 0; j < op->dstCount; j++) {
			COLOR *destBlock = outBase + j * op->dstStride;
			ch += op->hueRotate;
			cs += op->saturationAdd;
			cv += op->valueAdd;

			ch %= 360;
			if (ch < 0) ch += 360;
			if (cs > 100) cs = 100;
			else if (cs < 0) cs = 0;
			if (cv > 100) cv = 100;
			else if (cv < 0) cv = 0;

			ConvertHSVToRGB(ch, cs, cv, &out32);
			if (i == 0 && op->ignoreFirst) {
				out32 = as32;
			}
			int outIndex = i;

			//process rotation
			int rotateBy = 0;
			if (i != 0 || !op->ignoreFirst) {
				if (op->ignoreFirst) {
					rotateBy = (j + 1) * op->paletteRotation;
					outIndex = ((outIndex - 1) + rotateBy) % (blockLength - 1);
					if (outIndex < 0) outIndex += blockLength;
					outIndex++;
				} else {
					rotateBy = (j + 1) * op->paletteRotation;
					outIndex = (outIndex + rotateBy) % blockLength;
					if (outIndex < 0) outIndex += blockLength;
				}
			}


			if (destBlock + outIndex < palOut + palSize) {
				destBlock[outIndex] = ColorConvertToDS(out32);
			}
		}
	}
}

static LRESULT CALLBACK PalopWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	PAL_OP_DATA *data = (PAL_OP_DATA *) UiDlgGetData(hWnd);
	PAL_OP *palOp = data->op;

	switch (msg) {
		case WM_CREATE:
		{
			int boxWidth = 200, box1Height = 133, box2Height = 106, box3Height = 79 + 27;
			int box1Y = 10;
			int box2Y = 20 + box1Height;
			int box3Y = 30 + box1Height + box2Height;
			int bottomY = 40 + box1Height + box2Height + box3Height;
			int leftX = 20;
			CreateGroupbox(hWnd, L"Palette Operation", 10, box1Y, boxWidth, box1Height);
			CreateGroupbox(hWnd, L"Source", 10, box2Y, boxWidth, box2Height);
			CreateGroupbox(hWnd, L"Destination", 10, box3Y, boxWidth, box3Height);

			CreateStatic(hWnd, L"Hue Rotation:", leftX, box1Y + 18, 100, 22);
			CreateStatic(hWnd, L"Saturation:", leftX, box1Y + 18 + 27, 100, 22);
			CreateStatic(hWnd, L"Value:", leftX, box1Y + 18 + 54, 100, 22);
			CreateStatic(hWnd, L"Palette Rotation:", leftX, box1Y + 18 + 81, 100, 22);

			CreateStatic(hWnd, L"Index:", leftX, box2Y + 18 + 27, 100, 22);
			CreateStatic(hWnd, L"Length:", leftX, box2Y + 18 + 54, 100, 22);

			CreateStatic(hWnd, L"Offset:", leftX, box3Y + 18, 100, 22);
			CreateStatic(hWnd, L"Count:", leftX, box3Y + 18 + 27, 100, 22);
			CreateStatic(hWnd, L"Stride:", leftX, box3Y + 18 + 54, 100, 22);

			
			data->hWndHue = CreateEdit(hWnd, L"0", leftX + 110, box1Y + 18, 50, 22, TRUE);
			data->hWndSaturation = CreateEdit(hWnd, L"0", leftX + 110, box1Y + 18 + 27, 50, 22, TRUE);
			data->hWndValue = CreateEdit(hWnd, L"0", leftX + 110, box1Y + 18 + 54, 50, 22, TRUE);
			data->hWndRotate = CreateEdit(hWnd, L"0", leftX + 110, box1Y + 18 + 81, 50, 22, TRUE);
			data->hWndIgnoreFirst = CreateCheckbox(hWnd, L"Ignore First", leftX, box2Y + 18, 150, 22, FALSE);
			data->hWndSrcIndex = CreateEdit(hWnd, L"0", leftX + 110, box2Y + 18 + 27, 50, 22, TRUE);
			data->hWndSrcLength = CreateEdit(hWnd, L"0", leftX + 110, box2Y + 18 + 54, 50, 22, TRUE);
			data->hWndDestOffset = CreateEdit(hWnd, L"0", leftX + 110, box3Y + 18, 50, 22, TRUE);
			data->hWndDestCount = CreateEdit(hWnd, L"0", leftX + 110, box3Y + 18 + 27, 50, 22, TRUE);
			data->hWndDestStride = CreateEdit(hWnd, L"0", leftX + 110, box3Y + 18 + 54, 50, 22, TRUE);
			data->hWndComplete = CreateButton(hWnd, L"Complete", 10 + boxWidth - 150, bottomY, 150, 22, TRUE);

			PalopPopulateUI(data, palOp);
			SetFocus(data->hWndHue);

			data->init = 1;
			if (palOp->updateCallback != NULL) {
				palOp->updateCallback(palOp);
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int idc = LOWORD(wParam);
			int notif = HIWORD(wParam);
			if (!data->init) break;

			//complete button
			if (hWndControl == data->hWndComplete) {
				palOp->result = 1;
				UiDlgEnd(hWnd);
				break;
			}

			//exit
			if (idc == IDCANCEL) {
				UiDlgEnd(hWnd);
				break;
			}

			//get clas
			WCHAR class[16];
			GetClassName(hWndControl, class, sizeof(class) / sizeof(*class));
			if ((_wcsicmp(class, L"EDIT") == 0 && notif == EN_CHANGE) ||
				(_wcsicmp(class, L"BUTTON") == 0 && notif == BN_CLICKED)) {
				PalopReadUI(data, palOp);
				if (palOp->updateCallback != NULL) {
					palOp->updateCallback(palOp);
				}
			}
			break;
		}
	}
	return DefModalProc(hWnd, msg, wParam, lParam);
}

int SelectPaletteOperation(PAL_OP *opStruct) {
	opStruct->result = 0;

	//create dialog
	PAL_OP_DATA data = { 0 };
	data.op = opStruct;

	UiDlgCreateModal(opStruct->hWndParent, PalopWndProc, L"Palette Operation", 220, 417, &data);
	return opStruct->result;
}
