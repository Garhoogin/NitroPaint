#include "scene.h"

// ----- LCDC address for VRAM bank

#define VRAM_A_ADDR      0x06800000
#define VRAM_B_ADDR      0x06820000
#define VRAM_C_ADDR      0x06840000
#define VRAM_D_ADDR      0x06860000
#define VRAM_E_ADDR      0x06880000
#define VRAM_F_ADDR      0x06890000
#define VRAM_G_ADDR      0x06894000
#define VRAM_H_ADDR      0x06898000
#define VRAM_I_ADDR      0x068A0000



static ObjIdEntry sFormats[] = {
	{
		FILE_TYPE_SCENE, SCENE_TYPE_SCN, "SCN",
		OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED,
		NULL,
		NULL,
		(ObjWriter) ScnWrite
	}
};

static void ScnFreeScene(ScnScene *sc) {
	free(sc->name);
}

static void ScnFree(ObjHeader *hdr) {
	ScnSceneSet *ss = (ScnSceneSet *) hdr;
	for (unsigned i = 0; i < ss->nScene; i++) {
		ScnFreeScene(&ss->scenes[i]);
	}
	free(ss->scenes);
}

void ScnRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_SCENE, sizeof(ScnSceneSet), "Scene", NULL, ScnFree);

	for (size_t i = 0; i < sizeof(sFormats) / sizeof(sFormats[0]); i++) {
		ObjRegisterFormat(&sFormats[i]);
	}
}


int ScnWrite(ScnSceneSet *ss, BSTREAM *stream) {

	//magic 'SCST' (scene set)
	char magic[4] = "SCST";
	bstreamWrite(stream, magic, sizeof(magic));

	//Write scenes
	BSTREAM stmStrings, stmScenes;
	bstreamCreate(&stmStrings, NULL, 0);
	bstreamCreate(&stmScenes, NULL, 0);

	uint32_t nScene = ss->nScene;
	bstreamWrite(stream, &nScene, sizeof(nScene));

	for (unsigned int i = 0; i < ss->nScene; i++) {
		ScnScene *sc = &ss->scenes[i];

		//put string and table offset
		uint32_t strOffs = stmStrings.size;
		uint32_t scnOffs = stmScenes.size;
		bstreamWrite(stream, &scnOffs, sizeof(scnOffs));
		bstreamWrite(stream, &strOffs, sizeof(strOffs));

		//put string
		bstreamWrite(&stmStrings, sc->name, 1 + strlen(sc->name));
	}

	//fixup offsets

	//combine buffers
	bstreamWrite(stream, stmScenes.buffer, stmScenes.size);
	bstreamWrite(stream, stmStrings.buffer, stmStrings.size);

	bstreamFree(&stmScenes);
	bstreamFree(&stmStrings);

	return OBJ_STATUS_SUCCESS;
}

