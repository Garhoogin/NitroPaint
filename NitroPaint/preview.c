#include <Windows.h>
#include <stdio.h>

#include "color.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "texture.h"

typedef HANDLE (__stdcall *pfnNNS_McsOpenStream) (USHORT port, DWORD dwFlags);

//NVC protocol not documented!
static HMODULE hMcsDll = NULL; //nnsmcs.dll
static HANDLE sNvcStream = NULL;

static pfnNNS_McsOpenStream NNS_McsOpenStream = NULL;

// ----- NVC message codes

#define NVC_MJ_SYS           (0<<8)
#define NVC_MJ_MEM           (1<<8)
#define NVC_MJ_ANM           (2<<8)
#define NVC_MJ_VRAM          (3<<8)

#define NVC_MSG_INIT         (NVC_MJ_SYS  | 0)
#define NVC_MSG_WRITE_REG    (NVC_MJ_MEM  | 3)
#define NVC_MSG_COPY         (NVC_MJ_MEM  | 5)
#define NVC_MSG_VRAM_CONFIG  (NVC_MJ_VRAM | 0)

// ----- memory map locations

#define MM_ITCM_START        0x01000000
#define MM_ITCM_END          0x02000000
#define MM_RAM_START         0x02000000
#define MM_RAM_END           0x02400000
#define MM_IO_START          0x04000000
#define MM_IO_END            0x05000000
#define MM_PLTT_START        0x05000000
#define MM_PLTT_END          0x06000000
#define MM_VRAM_START        0x06000000
#define MM_VRAM_END          0x07000000
#define MM_OAM_START         0x07000000
#define MM_OAM_END           0x08000000

#define MM_BG_PLTT           (MM_PLTT_START+0x000)
#define MM_OBJ_PLTT          (MM_PLTT_START+0x100)
#define MM_BG_PLTT_SUB       (MM_PLTT_START+0x200)
#define MM_OBJ_PLTT_SUB      (MM_PLTT_START+0x300)

// ----- special NVC locations

#define MM_NVC_BG_EXT_PLTT          0x06FF0000
#define MM_NVC_BG_EXT_PLTT_SLOT(n)  (MM_NVC_BG_EXT_PLTT+(n)*0x2000)
#define MM_NVC_OBJ_EXT_PLTT         0x06FF8000
#define MM_NVC_OBJ_EXT_PLTT_SLOT(n) (MM_NVC_OBJ_EXT_PLTT+(n)*0x2000)

// ----- relevant HW registers

#define REG_DISPCNT       0x04000000
#define REG_BG0CNT        0x04000008
#define REG_BG0HOFS       0x04000010
#define REG_BG2PA         0x04000020
#define REG_BG3PA         0x04000030
#define REG_BLDCNT        0x04000050
#define REG_MASTER_BRIGHT 0x0400006C

#define PREVIEW_BG        3

// ----- preview modes

#define PREVIEW_MODE_NONE    0
#define PREVIEW_MODE_BG      1
#define PREVIEW_MODE_OBJ     2
#define PREVIEW_MODE_TEXTURE 3

typedef struct NvcMessageHeader_ {
	uint16_t opcode;
	uint16_t length;
} NvcMessageHeader;

typedef struct NvcInitMessage_ {
	NvcMessageHeader header;
	uint16_t forX;
	uint16_t banks;
} NvcInitMessage;

typedef struct NvcWriteMessage_ {
	NvcMessageHeader header;
	uint32_t dest;
	uint32_t size;
	unsigned char bytes[0];
} NvcWriteMessage;



// ----- Internal state to keep track of current preview state

static int sGraphicsDepth = 4;
static int sScreenUsesNonzeroPalettes = 0; //if current BG screen uses nonzero palettes
static int sPreviewMode = PREVIEW_MODE_NONE;
static uint32_t sDispCnt;  //DISPCNT
static uint16_t sBgCnt[4]; //BG controls 0-3

// ----- mechanics of MCS communication

static BOOL WriteFileAsync(HANDLE hFile, const void *d, size_t size, DWORD *nWritten) {
	//try pump messages if we have any
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	OVERLAPPED overlapped = { 0 };
	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	overlapped.Offset = 0xFFFFFFFF;
	overlapped.OffsetHigh = 0xFFFFFFFF;

	DWORD dwWritten = 0;
	BOOL success = WriteFile(hFile, d, size, &dwWritten, &overlapped);
	if (success || GetLastError() != ERROR_IO_PENDING) {
		*nWritten = dwWritten;
		CloseHandle(overlapped.hEvent);
		return success; //IO completed sync
	}

	//pump thread messages while waiting
	while (1) {
		DWORD waitResult = MsgWaitForMultipleObjects(1, &overlapped.hEvent, TRUE, INFINITE, QS_ALLINPUT);
		if (waitResult == WAIT_OBJECT_0) {
			//IO completion
			break;
		}

		//pop message(s) received
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	//IO completion
	success = GetOverlappedResult(hFile, &overlapped, nWritten, TRUE);

	CloseHandle(overlapped.hEvent);
	return success;
}

static HANDLE NvcLoadLibrary(LPCWSTR name) {
	//load DLL from NVC path
	WCHAR path[MAX_PATH + 1] = { 0 };
	WCHAR expandPath[MAX_PATH + 1] = { 0 };
	wsprintfW(path, L"%%NITROVIEWER_ROOT%%\\lib\\%s", name);
	ExpandEnvironmentStrings(path, expandPath, MAX_PATH);

	HMODULE hm = LoadLibrary(expandPath);
	if (hm == INVALID_HANDLE_VALUE) return NULL;
	return hm;
}

int PreviewInit(void) {
	//ensure DLLs loaded
	if (hMcsDll == NULL) hMcsDll = NvcLoadLibrary(L"NNSMCS.DLL");
	if (hMcsDll == NULL) return 0;

	//get function pointers
	if (NNS_McsOpenStream == NULL) NNS_McsOpenStream = (pfnNNS_McsOpenStream) GetProcAddress(hMcsDll, "NNS_McsOpenStream");
	if (NNS_McsOpenStream == NULL) return 0;

	//create stream
	if (sNvcStream == NULL || sNvcStream == INVALID_HANDLE_VALUE) sNvcStream = NNS_McsOpenStream('NC', 0);
	if (sNvcStream == NULL || sNvcStream == INVALID_HANDLE_VALUE) return 0;
	sPreviewMode = PREVIEW_MODE_NONE;
	sScreenUsesNonzeroPalettes = 0;

	return 1;
}

void PreviewEnd(void) {
	CloseHandle(sNvcStream);
	sNvcStream = NULL;
	sPreviewMode = PREVIEW_MODE_NONE;
}



static BOOL NvcWriteSafe(HANDLE hFile, const void *data, size_t size) {
	DWORD nWritten = 0;
	BOOL b = WriteFileAsync(hFile, data, size, &nWritten);
	return b && (size == nWritten);
}

static int NvcWriteRegisterN(uint32_t regaddr, const void *src, unsigned int size) {
	int status;
	NvcWriteMessage *packet = (NvcWriteMessage *) calloc(sizeof(NvcWriteMessage) + size, 1);
	packet->header.opcode = NVC_MSG_WRITE_REG;
	packet->header.length = size + sizeof(NvcWriteMessage);
	packet->size = size;
	packet->dest = regaddr;
	memcpy(packet->bytes, src, size);
	status = NvcWriteSafe(sNvcStream, packet, sizeof(NvcWriteMessage) + size);
	free(packet);
	return status;
}

static int NvcCopyData(uint32_t destaddr, const void *src, unsigned int size) {
	//ensure we transfer units of 32 at a time, up to 0x2000 bytes (including header)
	int status = 1;
	unsigned int transferSize = size;
	if (transferSize + sizeof(NvcWriteMessage) > 0x2000) transferSize = (0x2000 - sizeof(NvcWriteMessage)) & ~0x1F;

	NvcWriteMessage *packet = (NvcWriteMessage *) calloc(sizeof(NvcWriteMessage) + transferSize, 1);
	while (status && size > 0) {
		if (transferSize > size) transferSize = size;

		packet->header.opcode = NVC_MSG_COPY;
		packet->header.length = transferSize + sizeof(NvcWriteMessage);
		packet->size = transferSize;
		packet->dest = destaddr;
		memcpy(packet->bytes, src, transferSize);
		status = NvcWriteSafe(sNvcStream, packet, sizeof(NvcWriteMessage) + transferSize);

		size -= transferSize;
		destaddr += transferSize;
		src = (const void *) (((const unsigned char *) src) + transferSize);
	}
	free(packet);
	return status;
}

static int NvcWriteRegister32(uint32_t regaddr, uint32_t val) {
	return NvcWriteRegisterN(regaddr, &val, sizeof(val));
}

static int NvcWriteRegister16(uint32_t regaddr, uint16_t val) {
	return NvcWriteRegisterN(regaddr, &val, sizeof(val));
}

static int NvcHello(void) {
	NvcMessageHeader packet = { 0 };
	packet.opcode = NVC_MSG_INIT;
	packet.length = sizeof(packet);
	return NvcWriteSafe(sNvcStream, &packet, sizeof(packet));
}

static int NvcInit(void) {
	int status = NvcHello();
	if (!status) return 0;

	NvcInitMessage packet = { 0 };
	packet.header.opcode = NVC_MSG_VRAM_CONFIG;
	packet.header.length = sizeof(packet);

	//BG, OBJ, BG ext pltt, OBJ ext pltt, sub BG, sub OBJ, sub BG ext pltt, sub OBJ ext pltt, LCDC
	const uint16_t bgBankConfigExt[] = {
		0x008, //Main BG:              A
		0x000, //Main OBJ:
		0x060, //Main BG Ext Pltt:     FG
		0x000, //Main OBJ Ext Pltt: 
		0x000, //Sub BG:
		0x000, //Sub OBJ:
		0x000, //Sub BG Ext Pltt:
		0x000, //Sub OBJ Ext Pltt: 
		0x017  //LCDC:                 ABC E
	};
	for (int i = 0; i < 9; i++) {
		packet.forX = i;
		packet.banks = bgBankConfigExt[i];
		status = NvcWriteSafe(sNvcStream, &packet, sizeof(packet));
		if (!status) return 0;
	}

	sGraphicsDepth = 4;
	sScreenUsesNonzeroPalettes = 0;
	return 1;
}

static int NvcResetStateForBG(void) {
	int16_t identityMatrix[] = { 0x0100, 0x0000, 0x0000, 0x0100 };
	int16_t scrollValues[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	memset(sBgCnt, 0, sizeof(sBgCnt));
	sGraphicsDepth = 4;

	int status = 1;
	status = status && NvcInit();
	status = status && NvcWriteRegisterN(REG_BG0CNT, sBgCnt, sizeof(sBgCnt));                   // BG control registers
	status = status && NvcWriteRegisterN(REG_BG2PA, identityMatrix, sizeof(identityMatrix));    // reset matrix for BG 2
	status = status && NvcWriteRegisterN(REG_BG3PA, identityMatrix, sizeof(identityMatrix));    // reset matrix for BG 3
	status = status && NvcWriteRegisterN(REG_BG0HOFS, scrollValues, sizeof(scrollValues));      // reset BG scroll
	status = status && NvcWriteRegister16(REG_BLDCNT, 0);                                       // no blend effects
	status = status && NvcWriteRegister16(REG_MASTER_BRIGHT, 0);                                // normal brightness
	sPreviewMode = PREVIEW_MODE_BG;
	sScreenUsesNonzeroPalettes = 0;
	return status;
}

static int NvcGetBgSize(int width, int height, int useExtPalette, int *pHwWidth, int *pHwHeight) {
	int screenSize = 0;

	if (!useExtPalette) {
		if (width <= 256 && height <= 256) screenSize = 0, *pHwWidth = 256, *pHwHeight = 256;
		else if (width <= 512 && height <= 256) screenSize = 1, *pHwWidth = 512, *pHwHeight = 256;
		else if (width <= 256 && height <= 512) screenSize = 2, *pHwWidth = 256, *pHwHeight = 512;
		else if (width <= 512 && height <= 512) screenSize = 3, *pHwWidth = 512, *pHwHeight = 512;
		else screenSize = 3, *pHwWidth = 512, *pHwHeight = 512;
	} else {
		if (width <= 128 && height <= 128) screenSize = 0, *pHwWidth = 128, *pHwHeight = 128;
		else if (width <= 256 && height <= 256) screenSize = 1, *pHwWidth = 256, *pHwHeight = 256;
		else if (width <= 512 && height <= 512) screenSize = 2, *pHwWidth = 512, *pHwHeight = 512;
		else if (width <= 1024 && height <= 1024) screenSize = 3, *pHwWidth = 1024, *pHwHeight = 1024;
		else screenSize = 3, *pHwWidth = 1024, *pHwHeight = 1024;
	}

	return screenSize;
}

static int NvcShowBG(int bgNo, int useExtPalette) {
	int bgMode = (useExtPalette ? 5 : 0);

	sDispCnt = 0x01010000; //graphics display, character block 1
	sDispCnt |= bgMode;
	sDispCnt |= (useExtPalette << 30);
	sDispCnt |= ((1 << bgNo) << 8);
	return NvcWriteRegister32(REG_DISPCNT, sDispCnt);
}

int PreviewLoadBgPalette(NCLR *nclr) {
	int status = 1;
	if (sNvcStream == NULL) return 0;
	if (sPreviewMode != PREVIEW_MODE_BG) NvcResetStateForBG();

	//should we copy using an index table?
	if (nclr->idxTable == NULL) {
		//copy up to 512 bytes to standard paeltte, copy up to 0x2000 bytes to extended palette
		int paletteSize = nclr->nColors * sizeof(COLOR);
		status = status && NvcCopyData(MM_BG_PLTT, nclr->colors, (paletteSize > 0x200) ? 0x200 : paletteSize);
		status = status && NvcCopyData(MM_NVC_BG_EXT_PLTT_SLOT(3), nclr->colors, (paletteSize > 0x2000) ? 0x2000 : paletteSize);
	} else {
		//copy with index table
		int paletteSize = (1 << nclr->nBits) * sizeof(COLOR);
		for (int i = 0; i < nclr->nPalettes; i++) {
			int srcofs = i * paletteSize;
			int dstofs = nclr->idxTable[i] * paletteSize;
			int end = dstofs + paletteSize;

			if (end <= 0x200) status = status && NvcCopyData(MM_BG_PLTT + dstofs, nclr->colors + (srcofs / 2), paletteSize);
			if (end <= 0x2000) status = status && NvcCopyData(MM_NVC_BG_EXT_PLTT_SLOT(3) + dstofs, nclr->colors + (srcofs / 2), paletteSize);
		}
	}

	return status;
}

int PreviewLoadBgCharacter(NCGR *ncgr) {
	int status = 1;
	if (sNvcStream == NULL) return 0;
	if (sPreviewMode != PREVIEW_MODE_BG) NvcResetStateForBG();

	BSTREAM gfxStream;
	bstreamCreate(&gfxStream, NULL, 0);
	ChrWriteChars(ncgr, &gfxStream);
	status = NvcCopyData(MM_VRAM_START + 0x10000, gfxStream.buffer, gfxStream.size);
	bstreamFree(&gfxStream);
	if (!status) return 0;

	if (sGraphicsDepth != ncgr->nBits) {
		//update DISPCNT for our new graphics bit depth
		if (!(sDispCnt & 0x40000000)) {
			//extended palette not used? set bit depth in screen info. If screen is using multiple palettes, enable extended palette
			if (sScreenUsesNonzeroPalettes && ncgr->nBits == 8) {
				sDispCnt |= 0x40000000;
				sBgCnt[PREVIEW_BG] &= ~(1 << 7); //clear depth field
				status = status && NvcWriteRegister32(REG_DISPCNT, sDispCnt);
			} else {
				sBgCnt[PREVIEW_BG] &= ~(1 << 7); //clear depth field
				sBgCnt[PREVIEW_BG] |= (ncgr->nBits == 8) << 7;
			}
		} else {
			//extended palette used.
			if (ncgr->nBits == 4) {
				//If we're setting the depth to 4-bit, clear the extended palette flag
				sDispCnt &= ~0x40000000;
				status = status && NvcWriteRegister32(REG_DISPCNT, sDispCnt);
			} else {
				//Setting depth to 8-bit (why was extended palette on before?)
			}
		}
		status = status && NvcWriteRegister16(REG_BG0CNT + 2 * PREVIEW_BG, sBgCnt[PREVIEW_BG]);

		sGraphicsDepth = ncgr->nBits;
	}
	return status;
}

int PreviewLoadBgScreen(NSCR *nscr) {
	int status = 1;
	if (sNvcStream == NULL) return 0;
	if (sPreviewMode != PREVIEW_MODE_BG) NvcResetStateForBG();

	//determine if an extended palette should be used to preview (not all file formats store screen format)
	int useExtPalette = 0;
	for (unsigned int i = 0; i < nscr->dataSize / 2; i++) {
		int palno = nscr->data[i] >> 12;
		if (palno > 0) {
			sScreenUsesNonzeroPalettes = 1;
			if (sGraphicsDepth == 8) useExtPalette = 1;
			break;
		}
	}

	int screenSize = 0, bgWidth, bgHeight, bgTilesX, bgTilesY;
	screenSize = NvcGetBgSize(nscr->nWidth, nscr->nHeight, useExtPalette, &bgWidth, &bgHeight);
	bgTilesX = bgWidth / 8, bgTilesY = bgHeight / 8;

	//setup BG control
	sBgCnt[PREVIEW_BG] = (screenSize << 14);
	if (!useExtPalette) sBgCnt[PREVIEW_BG] |= ((sGraphicsDepth == 8) << 7);
	status = status && NvcWriteRegister16(REG_BG0CNT + 2 * PREVIEW_BG, sBgCnt[PREVIEW_BG]);

	//load BG screen
	if (bgWidth == nscr->nWidth && bgHeight == nscr->nHeight) {
		//copy direct
		status = status && NvcCopyData(MM_VRAM_START, nscr->data, nscr->dataSize);
	} else {
		//resize to hw-supported size
		int tilesX = nscr->nWidth / 8, tilesY = nscr->nHeight / 8;
		uint16_t *copy = (uint16_t *) calloc(bgTilesX * bgTilesY, sizeof(uint16_t));

		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				copy[x + y * bgTilesX] = nscr->data[x + y * tilesX];
			}
		}

		status = status && NvcCopyData(MM_VRAM_START, copy, bgTilesX * bgTilesY * sizeof(uint16_t));
		free(copy);
	}

	status = status && NvcShowBG(3, useExtPalette); //BG 3
	return status;
}

int PreviewScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr) {
	if (sNvcStream == NULL) return 0;

	//reset BG state
	int status = 1;
	if (sPreviewMode != PREVIEW_MODE_BG) status = status && NvcResetStateForBG();

	//load component parts
	status = status && PreviewLoadBgPalette(nclr);
	status = status && PreviewLoadBgCharacter(ncgr);
	status = status && PreviewLoadBgScreen(nscr);
	return status;
}



