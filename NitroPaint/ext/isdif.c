#include "isdif.h"

#include <Windows.h>

static HMODULE sMod = NULL;
static int sInit = 0;

static IsdDevice *(*pfnIsdOpen) (const wchar_t *path, IsdShareMode shareMode) = NULL;
static void (*pfnIsdClose) (IsdDevice *pDev) = NULL;
static IsdStatus (*pfnIsdGetLastError) (IsdDevice *pDev) = NULL;
static int (*pfnIsdMonEnter) (IsdDevice *pDev) = NULL;
static int (*pfnIsdMonLeave) (IsdDevice *pDev) = NULL;
static int (*pfnIsdSelectProc) (IsdDevice *pDev, IsdProc proc) = NULL;
static int (*pfnIsdReadMem) (IsdDevice *pDev, void *dest, uint32_t src, size_t len) = NULL;
static int (*pfnIsdWriteMem) (IsdDevice *pDev, uint32_t dest, const void *src, size_t len) = NULL;
static int (*pfnIsdMonGetStatus) (IsdDevice *pDev, IsdMonStatus *pStatus) = NULL;

static IsdDevice *gDev = NULL;


int IsdifInit(void) {
	if (sInit) return 1;

	sMod = LoadLibrary(L"ISDIF.dll");
	if (sMod == NULL) return 0;

	//get the procedure addresses
	if (!(pfnIsdOpen         = (VOID FAR *) GetProcAddress(sMod, "IsdOpen"        ))) return 0;
	if (!(pfnIsdClose        = (VOID FAR *) GetProcAddress(sMod, "IsdClose"       ))) return 0;
	if (!(pfnIsdGetLastError = (VOID FAR *) GetProcAddress(sMod, "IsdGetLastError"))) return 0;
	if (!(pfnIsdMonEnter     = (VOID FAR *) GetProcAddress(sMod, "IsdMonEnter"    ))) return 0;
	if (!(pfnIsdMonLeave     = (VOID FAR *) GetProcAddress(sMod, "IsdMonLeave"    ))) return 0;
	if (!(pfnIsdSelectProc   = (VOID FAR *) GetProcAddress(sMod, "IsdSelectProc"  ))) return 0;
	if (!(pfnIsdReadMem      = (VOID FAR *) GetProcAddress(sMod, "IsdReadMem"     ))) return 0;
	if (!(pfnIsdWriteMem     = (VOID FAR *) GetProcAddress(sMod, "IsdWriteMem"    ))) return 0;
	if (!(pfnIsdMonGetStatus = (VOID FAR *) GetProcAddress(sMod, "IsdMonGetStatus"))) return 0;

	sInit = 1;
	return 1;
}

int IsdifOpen(void) {
	//already open?
	if (gDev != NULL) return 1;

	//open in exclusive mode
	gDev = pfnIsdOpen(NULL, ISD_SHARE_MODE_EXCLUSIVE);
	if (gDev == NULL) return 0;

	//select ARM9
	if (!pfnIsdSelectProc(gDev, TS_PROC_MAIN)) {
		IsdifClose();
		return 0;
	}

	return 1;
}

int IsdifClose(void) {
	//already close?
	if (gDev == NULL) return 1;

	pfnIsdClose(gDev);
	gDev = NULL;
	return 1;
}

int IsdifStop(void) {
	return pfnIsdMonEnter(gDev);
}

int IsdifResume(void) {
	return pfnIsdMonLeave(gDev);
}

int IsdifReadMem(void *dest, uint32_t src, size_t size) {
	return pfnIsdReadMem(gDev, dest, src, size);
}

int IsdifWriteMem(uint32_t dest, const void *src, size_t size) {
	return pfnIsdWriteMem(gDev, dest, src, size);
}

int IsdifGetMonStatus(IsdMonStatus *pStatus) {
	return pfnIsdMonGetStatus(gDev, pStatus);
}

IsdStatus IsdGetLastError(void) {
	return pfnIsdGetLastError(gDev);
}

