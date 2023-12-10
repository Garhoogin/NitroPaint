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


// ----- mechanics of MCS communication

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

	return 1;
}

void PreviewEnd(void) {
	CloseHandle(sNvcStream);
	sNvcStream = NULL;
}



static void NvcWriteRegisterN(uint32_t regaddr, const void *src, unsigned int size) {
	DWORD dwWritten = 0;
	NvcWriteMessage *packet = (NvcWriteMessage *) calloc(sizeof(NvcWriteMessage) + size, 1);
	packet->header.opcode = NVC_MSG_WRITE_REG;
	packet->header.length = size + sizeof(NvcWriteMessage);
	packet->size = size;
	packet->dest = regaddr;
	memcpy(packet->bytes, src, size);
	WriteFile(sNvcStream, packet, sizeof(NvcWriteMessage) + size, &dwWritten, NULL);
	free(packet);
}

static void NvcCopyData(uint32_t destaddr, const void *src, unsigned int size) {
	DWORD dwWritten = 0;

	//ensure we transfer units of 32 at a time, up to 0x2000 bytes (including header)
	unsigned int transferSize = size;
	if (transferSize + sizeof(NvcWriteMessage) > 0x2000) transferSize = (0x2000 - sizeof(NvcWriteMessage)) & ~0x1F;

	NvcWriteMessage *packet = (NvcWriteMessage *) calloc(sizeof(NvcWriteMessage) + transferSize, 1);
	while (size > 0) {
		if (transferSize > size) transferSize = size;

		packet->header.opcode = NVC_MSG_COPY;
		packet->header.length = transferSize + sizeof(NvcWriteMessage);
		packet->size = transferSize;
		packet->dest = destaddr;
		memcpy(packet->bytes, src, transferSize);
		WriteFile(sNvcStream, packet, sizeof(NvcWriteMessage) + transferSize, &dwWritten, NULL);

		size -= transferSize;
		destaddr += transferSize;
		src = (const void *) (((const unsigned char *) src) + transferSize);
	}
	free(packet);
}

static void NvcWriteRegister32(uint32_t regaddr, uint32_t val) {
	NvcWriteRegisterN(regaddr, &val, sizeof(val));
}

static void NvcWriteRegister16(uint32_t regaddr, uint16_t val) {
	NvcWriteRegisterN(regaddr, &val, sizeof(val));
}

static void NvcWriteRegisterNx16(uint32_t regaddr, uint16_t *data, size_t size) {
	while (size >= sizeof(uint16_t)) {
		NvcWriteRegister16(regaddr, *data);
		regaddr += sizeof(uint16_t);
		data++;
		size -= sizeof(uint16_t);
	}
}

static void NvcHello(void) {
	DWORD dwWritten;
	NvcMessageHeader packet = { 0 };
	packet.opcode = NVC_MSG_INIT;
	packet.length = sizeof(packet);
	WriteFile(sNvcStream, &packet, sizeof(packet), &dwWritten, NULL);
}

static void NvcInit(int useExtPalette) {
	DWORD dwWritten;
	NvcHello();

	NvcInitMessage packet = { 0 };
	packet.header.opcode = NVC_MSG_VRAM_CONFIG;
	packet.header.length = sizeof(packet);

	//BG, OBJ, BG ext pltt, OBJ ext pltt, sub BG, sub OBJ, sub BG ext pltt, sub OBJ ext pltt, LCDC
	uint16_t bgBankConfig[] =      { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77 };
	uint16_t bgBankConfigExt[] =   { 0x08, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17 };
	for (int i = 0; i < 9; i++) {
		packet.forX = i;
		packet.banks = (useExtPalette ? bgBankConfigExt : bgBankConfigExt)[i];
		WriteFile(sNvcStream, &packet, sizeof(packet), &dwWritten, NULL);
	}
}

static void NvcResetState(int useExtPalette) {
	int16_t identityMatrix[] = { 0x0100, 0x0000, 0x0000, 0x0100 };
	int16_t scrollValues[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	uint16_t bgcnts[] = { 0, 0, 0, 0 };
	NvcInit(useExtPalette);
	NvcWriteRegisterNx16(REG_BG0CNT, bgcnts, sizeof(bgcnts));                // BG control registers
	NvcWriteRegisterNx16(REG_BG2PA, identityMatrix, sizeof(identityMatrix)); // reset matrix for BG 2
	NvcWriteRegisterNx16(REG_BG3PA, identityMatrix, sizeof(identityMatrix)); // reset matrix for BG 3
	NvcWriteRegisterNx16(REG_BG0HOFS, scrollValues, sizeof(scrollValues));   // reset BG scroll
	NvcWriteRegister16(REG_BLDCNT, 0);                                       // no blend effects
	NvcWriteRegister16(REG_MASTER_BRIGHT, 0);                                // normal brightness
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

static void NvcShowBG(int bgNo, int useExtPalette) {
	int bgMode = (useExtPalette ? 5 : 0);

	uint32_t cnt = 0x01010000; //graphics display, character block 1
	cnt |= bgMode;
	cnt |= (useExtPalette << 30); 
	cnt |= ((1 << bgNo) << 8);
	NvcWriteRegister32(REG_DISPCNT, cnt);
}

int PreviewLoadBgPalette(NCLR *nclr) {
	if (sNvcStream == NULL) return 0;

	//should we copy using an index table?
	if (nclr->idxTable != NULL) {
		//copy up to 512 bytes to standard paeltte, copy up to 0x2000 bytes to extended palette
		int paletteSize = nclr->nColors * sizeof(COLOR);
		NvcCopyData(MM_BG_PLTT, nclr->colors, (paletteSize > 0x200) ? 0x200 : paletteSize);
		NvcCopyData(MM_NVC_BG_EXT_PLTT_SLOT(3), nclr->colors, (paletteSize > 0x2000) ? 0x2000 : paletteSize);
	} else {
		//copy with index table
		int paletteSize = (1 << nclr->nBits);
		for (int i = 0; i < nclr->nPalettes; i++) {
			int srcofs = i * paletteSize;
			int dstofs = nclr->idxTable[i] * paletteSize;

			if (dstofs + paletteSize <= 0x200) NvcCopyData(MM_BG_PLTT + dstofs, nclr->colors + srcofs, paletteSize);
			if (dstofs + paletteSize <= 0x2000) NvcCopyData(MM_NVC_BG_EXT_PLTT_SLOT(3) + dstofs, nclr->colors + srcofs, paletteSize);
		}
	}

	return 1;
}

int PreviewLoadBgCharacter(NCGR *ncgr) {
	BSTREAM gfxStream;
	bstreamCreate(&gfxStream, NULL, 0);
	ChrWriteChars(ncgr, &gfxStream);
	NvcCopyData(MM_VRAM_START + 0x10000, gfxStream.buffer, gfxStream.size);
	bstreamFree(&gfxStream);
	return 1;
}

int PreviewLoadBgScreen(NSCR *nscr, int depth, int useExtPalette) {
	int screenSize = 0, bgWidth, bgHeight;
	screenSize = NvcGetBgSize(nscr->nWidth, nscr->nHeight, useExtPalette, &bgWidth, &bgHeight);

	//setup BG control
	uint16_t bgcnt = (screenSize << 14);
	if (!useExtPalette) bgcnt |= ((depth == 8) << 7);
	NvcWriteRegister16(REG_BG0CNT + 2 * 3, bgcnt);

	//load BG screen
	if (bgWidth == nscr->nWidth && bgHeight == nscr->nHeight) {
		//copy direct
		NvcCopyData(MM_VRAM_START, nscr->data, nscr->dataSize);
	} else {
		//resize to hw-supported size
		int tilesX = nscr->nWidth / 8, tilesY = nscr->nHeight / 8;
		uint16_t *copy = (uint16_t *) calloc(tilesX * tilesY, sizeof(uint16_t));

		for (int y = 0; y < tilesY; y++) {
			for (int x = 0; x < tilesX; x++) {
				copy[x + y * (bgWidth / 8)] = nscr->data[x + y * tilesX];
			}
		}

		NvcCopyData(MM_VRAM_START, copy, tilesX * tilesY * sizeof(uint16_t));
		free(copy);
	}

	NvcShowBG(3, useExtPalette); //BG 3
	return 1;
}

int PreviewScreen(NSCR *nscr, NCGR *ncgr, NCLR *nclr) {
	if (sNvcStream == NULL) return 0;

	//use extended palette? 
	int useExtPalette = (nclr->nColors > 256) || (nclr->extPalette) || (ncgr->extPalette) || (nscr->fmt == SCREENFORMAT_AFFINEEXT);

	//reset BG state
	NvcResetState(useExtPalette);

	//load component parts
	PreviewLoadBgPalette(nclr);
	PreviewLoadBgCharacter(ncgr);
	PreviewLoadBgScreen(nscr, ncgr->nBits, useExtPalette);
	return 1;
}



