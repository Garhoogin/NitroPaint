#pragma once

#include <stdint.h>

#include "filecommon.h"

#define SCENE_TYPE_INVALID  0
#define SCENE_TYPE_SCN      1


#define SCN_VRAM_A_SIZE     (128*1024)  // 128KB
#define SCN_VRAM_B_SIZE     (128*1024)  // 128KB
#define SCN_VRAM_C_SIZE     (128*1024)  // 128KB
#define SCN_VRAM_D_SIZE     (128*1024)  // 128KB
#define SCN_VRAM_E_SIZE     ( 64*1024)  //  64KB
#define SCN_VRAM_F_SIZE     ( 16*1024)  //  16KB
#define SCN_VRAM_G_SIZE     ( 16*1024)  //  16KB
#define SCN_VRAM_H_SIZE     ( 32*1024)  //  32KB
#define SCN_VRAM_I_SIZE     ( 16*1024)  //  16KB



typedef enum ScnVramBank_ {
	SCN_VRAM_A,
	SCN_VRAM_B,
	SCN_VRAM_C,
	SCN_VRAM_D,
	SCN_VRAM_E,
	SCN_VRAM_F,
	SCN_VRAM_G,
	SCN_VRAM_H,
	SCN_VRAM_I
} ScnVramBank;

typedef enum ScnVramAllocation_ {
	SCN_VRAM_ALLOC_LCDC,
	SCN_VRAM_ALLOC_SUBP,
	SCN_VRAM_ALLOC_MAIN_BG,
	SCN_VRAM_ALLOC_MAIN_OBJ,
	SCN_VRAM_ALLOC_MAIN_BG_EXTPLTT,
	SCN_VRAM_ALLOC_MAIN_OBJ_EXTPLTT,
	SCN_VRAM_ALLOC_SUB_BG,
	SCN_VRAM_ALLOC_SUB_OBJ,
	SCN_VRAM_ALLOC_SUB_BG_EXTPLTT,
	SCN_VRAM_ALLOC_SUB_OBJ_EXTPLTT,
	SCN_VRAM_ALLOC_TEXIMAGE,
	SCN_VRAM_ALLOC_TEXPLTT
} ScnVramAllocation;

typedef enum ScnBgMode_ {
	SCN_BGMODE_0,           // 0: text text text text
	SCN_BGMODE_1,           // 1: 
	SCN_BGMODE_2,           // 2: 
	SCN_BGMODE_3,           // 3: 
	SCN_BGMODE_4,           // 4: 
	SCN_BGMODE_5,           // 5: 
	SCN_BGMODE_6,           // 6: 

	SCN_BGMODE_MASK = 0x07,
	SCN_BGMODE_3D   = 0x80  // BG0 is 3D (main only)
} ScnBgMode;

typedef enum ScnBgType_ {
	SCN_BGTYPE_NONE,
	SCN_BGTYPE_TEXT_16x16,
	SCN_BGTYPE_TEXT_256x1,
	SCN_BGTYPE_AFFINE_256x1,
	SCN_BGTYPE_AFFINEEXT_256x16,
	SCN_BGTYPE_BITMAP,
	SCN_BGTYPE_LARGE_BITMAP,
	SCN_BGTYPE_DIRECT_BITMAP
} ScnBgType;

typedef enum ScnVramBankAttr_ {
	SCN_VRAM_MST_MASK                = 0x07,
	SCN_VRAM_SLOT_MASK               = 0x18,
	SCN_VRAM_ENABLE_MASK             = 0x80,
	
	SCN_VRAM_MST_LCDC                = 0x00,  // LCDC: shared by all VRAM banks

	SCN_VRAM_AB_MST_MAIN_BG          = 0x01,  // VRAM A/B main BG
	SCN_VRAM_AB_MST_MAIN_OBJ         = 0x02,  // VRAM A/B main OBJ
	SCN_VRAM_AB_MST_TEXIMAGE         = 0x03,  // VRAM A/B texture image

	SCN_VRAM_C_MST_MAIN_BG           = 0x01,  // VRAM C main BG
	SCN_VRAM_C_MST_SUBP              = 0x02,  // VRAM C subprocessor
	SCN_VRAM_C_MST_TEXIMAGE          = 0x03,  // VRAM C texture image
	SCN_VRAM_C_MST_SUB_BG            = 0x04,  // VRAM C sub BG

	SCN_VRAM_D_MST_MAIN_BG           = 0x01,  // VRAM D main BG
	SCN_VRAM_D_MST_SUBP              = 0x02,  // VRAM D subprocessor
	SCN_VRAM_D_MST_TEXIMAGE          = 0x03,  // VRAM D texture image
	SCN_VRAM_D_MST_SUB_OBJ           = 0x04,  // VRAM D sub BG

	SCN_VRAM_E_MST_MAIN_BG           = 0x01,  // VRAM E main BG
	SCN_VRAM_E_MST_MAIN_OBJ          = 0x02,  // VRAM E main OBJ
	SCN_VRAM_E_MST_TEXPLTT           = 0x03,  // VRAM E texture palette
	SCN_VRAM_E_MST_MAIN_BG_EXTPLTT   = 0x04,  // VRAM E main BG extended palette

	SCN_VRAM_FG_MST_MAIN_BG          = 0x01,  // VRAM F/G main BG
	SCN_VRAM_FG_MST_MAIN_OBJ         = 0x02,  // VRAM F/G main OBJ
	SCN_VRAM_FG_MST_TEXPLTT          = 0x03,  // VRAM F/G texture palette
	SCN_VRAM_FG_MST_MAIN_BG_EXTPLTT  = 0x04,  // VRAM F/G main BG extended palette
	SCN_VRAM_FG_MST_MAIN_OBJ_EXTPLTT = 0x05,  // VRAM F/G main BJ extended palette

	SCN_VRAM_H_MST_SUB_BG            = 0x01,  // VRAM H sub BG
	SCN_VRAM_H_MST_SUB_BG_EXTPLTT    = 0x02,  // VRAM H sub BG extended palette
	
	SCN_VRAM_I_MST_SUB_BG            = 0x01,  // VRAM I sub BG
	SCN_VRAM_I_MST_SUB_OBJ           = 0x02,  // VRAM I sub OBJ
	SCN_VRAM_I_MST_SUB_OBJ_EXTPLTT   = 0x03,  // VRAM I sub OBJ extended palette

	SCN_VRAM_SLOT_0                  = 0x00,
	SCN_VRAM_SLOT_1                  = 0x08,
	SCN_VRAM_SLOT_2                  = 0x10,
	SCN_VRAM_SLOT_3                  = 0x18,

	SCN_VRAM_ENABLE                  = 0x80,
	SCN_VRAM_DISABLE                 = 0x00
} ScnVramBankAttr;

typedef struct ScnVramConfiguration_ {
	ScnVramBankAttr bankAttr[9];
} ScnVramConfiguration;

typedef struct ScnBgConfiguration_ {
	ScnBgMode mode;
	uint16_t bgcnt[4];
} ScnBgConfiguration;

typedef struct ScnG3xConfiguration_ {
	uint32_t disp3dcnt;

	uint32_t hasLights     : 1;
	uint32_t hasFog        : 1;
	uint32_t hasToon       : 1;
	uint32_t hasShininess  : 1;
	uint32_t hasEdgeColors : 1;

	uint16_t viewportW;
	uint16_t viewportH;
	uint16_t disp1dotDepth;

	uint32_t clearColor;
	uint16_t lightColors[4];
	uint32_t lightVectors[8]; // TODO: verify
	uint16_t fogOffset;
	uint32_t fogColor;
	uint8_t fogTable[32];
	uint16_t toonTable[32];
	uint8_t shininessTable[128]; // TODO: verify
	uint16_t edgeColors[8];
} ScnG3xConfiguration;

typedef struct ScnScene_ {
	char *name;                       // scene name
	ScnVramConfiguration vramConfig;  // scene VRAM configuration
	ScnBgConfiguration bgMain;
	ScnBgConfiguration bgSub;
	ScnG3xConfiguration g3dConfig;
} ScnScene;

typedef struct ScnSceneSet_ {
	ObjHeader header;
	unsigned int nScene;
	ScnScene *scenes;
} ScnSceneSet;

void ScnRegisterFormats(void);

int ScnWrite(ScnSceneSet *ss, BSTREAM *stream);

