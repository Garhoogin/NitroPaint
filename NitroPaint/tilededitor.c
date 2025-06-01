#include "tilededitor.h"

static void SwapInts(int *i1, int *i2) {
	int temp = *i1;
	*i1 = *i2;
	*i2 = temp;
}


// ----- margin functions

void TedMarginPaint(HWND hWnd, EDITOR_DATA *data, TedData *ted) {
	//margin dimensions
	int marginSize = MARGIN_SIZE;
	int marginBorderSize = MARGIN_BORDER_SIZE;
	int tileW = ted->tileWidth * data->scale;
	int tileH = ted->tileHeight * data->scale;

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	//exclude clip rect
	ExcludeClipRect(hDC, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, rcClient.right, rcClient.bottom);

	//get render size
	int renderWidth = rcClient.right;
	int renderHeight = rcClient.bottom;

	//clamp size of render area to the size of graphics view
	if (ted->hWndViewer != NULL) {
		RECT rcView;
		GetWindowRect(ted->hWndViewer, &rcView);

		int viewWndWidth = rcView.right - rcView.left + MARGIN_TOTAL_SIZE;
		int viewWndHeight = rcView.bottom - rcView.top + MARGIN_TOTAL_SIZE;
		if (viewWndWidth < renderWidth) renderWidth = viewWndWidth;
		if (viewWndHeight < renderHeight) renderHeight = viewWndHeight;
	}
	int viewWidth = renderWidth - MARGIN_TOTAL_SIZE;
	int viewHeight = renderHeight - MARGIN_TOTAL_SIZE;

	//create framebuffer
	FbSetSize(&ted->fbMargin, renderWidth, renderHeight);

	//get mouse coord
	POINT mouse;
	mouse.x = ted->mouseX;
	mouse.y = ted->mouseY;

	//get scroll pos
	int scrollX, scrollY;
	TedGetScroll(ted, &scrollX, &scrollY);

	//get hovered row/column
	int hovRow = ted->hoverY, hovCol = ted->hoverX;

	//get hit test
	int hit = TedHitTest(data, ted, ted->mouseX, ted->mouseY);

	//render guide margins
	{
		//draw top margin
		for (int y = 0; y < MARGIN_TOTAL_SIZE; y++) {
			for (int x = marginSize; x < renderWidth; x++) {
				COLOR32 col = 0x000000;

				if (x >= MARGIN_TOTAL_SIZE) {
					int curCol = (x - MARGIN_TOTAL_SIZE + scrollX) / tileW;
					BOOL inSel = (curCol >= min(ted->selStartX, ted->selEndX)) && (curCol <= max(ted->selStartX, ted->selEndX));

					if (inSel) col = 0x808000; //indicate selection
					if (curCol == hovCol) {
						if (!inSel) col = 0x800000; //indicate hovered
						else col = 0xC04000;
					}
				}

				//border pixels
				if (y >= marginSize) {
					col = RGB(160, 160, 160);
					if (y == (marginSize + 1)) col = RGB(105, 105, 105);
				}

				if (x < renderWidth && y < renderHeight) {
					ted->fbMargin.px[y * renderWidth + x] = col;
				}
			}
		}

		//draw left margin
		for (int y = marginSize; y < renderHeight; y++) {
			for (int x = 0; x < MARGIN_TOTAL_SIZE; x++) {
				if (x >= y) continue;
				COLOR32 col = 0x000000;

				if (y >= MARGIN_TOTAL_SIZE) {
					int curRow = (y - MARGIN_TOTAL_SIZE + scrollY) / tileH;
					BOOL inSel = (curRow >= min(ted->selStartY, ted->selEndY)) && (curRow <= max(ted->selStartY, ted->selEndY));

					if (inSel) col = 0x808000; //indicate selection
					if (curRow == hovRow) {
						if (!inSel) col = 0x800000; //indicate hovered
						else col = 0xC04000;
					}
				}

				//border pixels
				if (x >= marginSize) {
					col = RGB(160, 1160, 160);
					if (x == (marginSize + 1)) col = RGB(105, 105, 105);
				}

				if (x < renderWidth && y < renderHeight) {
					ted->fbMargin.px[y * renderWidth + x] = col;
				}
			}
		}

		//draw ticks
		int tickHeight = 4;
		for (int x = 0; x < viewWidth; x++) {
			if (((x + scrollX) % tileW) == 0) {
				//tick
				FbDrawLine(&ted->fbMargin, 0xFFFFFF, x + MARGIN_TOTAL_SIZE, 0, x + MARGIN_TOTAL_SIZE, tickHeight - 1);
			}
		}
		for (int y = 0; y < viewHeight; y++) {
			if (((y + scrollY) % tileH) == 0) {
				FbDrawLine(&ted->fbMargin, 0xFFFFFF, 0, y + MARGIN_TOTAL_SIZE, tickHeight - 1, y + MARGIN_TOTAL_SIZE);
			}
		}

		//draw selection edges
		if (ted->selStartX != -1 && ted->selStartY != -1) {
			int selX1 = (min(ted->selStartX, ted->selEndX) + 0) * tileW + MARGIN_TOTAL_SIZE - scrollX;
			int selX2 = (max(ted->selStartX, ted->selEndX) + 1) * tileW + MARGIN_TOTAL_SIZE - scrollX;
			int selY1 = (min(ted->selStartY, ted->selEndY) + 0) * tileH + MARGIN_TOTAL_SIZE - scrollY;
			int selY2 = (max(ted->selStartY, ted->selEndY) + 1) * tileH + MARGIN_TOTAL_SIZE - scrollY;
			FbDrawLine(&ted->fbMargin, 0xFFFF00, selX1, 0, selX1, MARGIN_SIZE - 1);
			FbDrawLine(&ted->fbMargin, 0xFFFF00, selX2, 0, selX2, MARGIN_SIZE - 1);
			FbDrawLine(&ted->fbMargin, 0xFFFF00, 0, selY1, MARGIN_SIZE - 1, selY1);
			FbDrawLine(&ted->fbMargin, 0xFFFF00, 0, selY2, MARGIN_SIZE - 1, selY2);
		}

		//draw mouse pos?
		if (mouse.x != -1 && mouse.y != -1) {
			FbDrawLine(&ted->fbMargin, 0xFF0000, mouse.x + MARGIN_TOTAL_SIZE, 0, mouse.x + MARGIN_TOTAL_SIZE, marginSize - 1);
			FbDrawLine(&ted->fbMargin, 0xFF0000, 0, mouse.y + MARGIN_TOTAL_SIZE, marginSize - 1, mouse.y + MARGIN_TOTAL_SIZE);
		}
	}

	//fill top-left corner
	COLOR32 cornerColor = 0x000000;
	if ((hit & HIT_TYPE_MASK) == HIT_MARGIN && (hit & HIT_MARGIN_LEFT) && (hit & HIT_MARGIN_TOP)) cornerColor = 0x800000;

	for (int y = 0; y < MARGIN_TOTAL_SIZE; y++) {
		for (int x = 0; x < MARGIN_TOTAL_SIZE; x++) {
			if (x < MARGIN_SIZE || y < MARGIN_SIZE) {
				if (x < renderWidth && y < renderHeight) {
					ted->fbMargin.px[x + y * renderWidth] = cornerColor;
				}
			}
		}
	}

	//blit
	FbDraw(&ted->fbMargin, hDC, 0, 0, renderWidth, MARGIN_TOTAL_SIZE, 0, 0);
	FbDraw(&ted->fbMargin, hDC, 0, MARGIN_TOTAL_SIZE, MARGIN_TOTAL_SIZE, renderHeight - MARGIN_TOTAL_SIZE, 0, MARGIN_TOTAL_SIZE);
	EndPaint(hWnd, &ps);
}

void TedUpdateMargins(TedData *data) {
	HWND hWnd = data->hWnd;

	//get client rect
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	//trigger update only for margin painting
	RECT rcLeft = { 0 }, rcTop = { 0 };
	rcLeft.right = MARGIN_TOTAL_SIZE;
	rcLeft.bottom = rcClient.bottom;
	rcTop.right = rcClient.right;
	rcTop.bottom = MARGIN_TOTAL_SIZE;
	InvalidateRect(hWnd, &rcLeft, FALSE);
	InvalidateRect(hWnd, &rcTop, FALSE);
}


// ----- viewer functions

void TedOnViewerPaint(EDITOR_DATA *data, TedData *ted) {
	HWND hWnd = ted->hWndViewer;

	//margin dimensions
	int marginSize = MARGIN_SIZE;
	int marginBorderSize = MARGIN_BORDER_SIZE;
	int tileW = ted->tileWidth * data->scale;
	int tileH = ted->tileHeight * data->scale;

	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	int viewWidth = rcClient.right;
	int viewHeight = rcClient.bottom;
	FbSetSize(&ted->fb, viewWidth, viewHeight);

	//get mouse coord
	POINT mouse;
	mouse.x = ted->mouseX;
	mouse.y = ted->mouseY;

	//hit test
	int hit = ted->mouseDown ? ted->mouseDownHit : TedHitTest(data, ted, mouse.x, mouse.y);
	int hitType = hit & HIT_TYPE_MASK;

	//get scroll pos
	int scrollX, scrollY;
	TedGetScroll(ted, &scrollX, &scrollY);

	//get graphics bounding size
	int tilesX = ted->tilesX, tilesY = ted->tilesY;
	int renderWidth = tilesX * tileW - scrollX;
	int renderHeight = tilesY * tileH - scrollY;
	if (renderWidth > viewWidth) renderWidth = viewWidth;
	if (renderHeight > viewHeight) renderHeight = viewHeight;

	//render size can become negative
	if (renderWidth < 0) renderWidth = 0;
	if (renderHeight < 0) renderHeight = 0;

	//get hovered row/column
	int hovRow = ted->hoverY, hovCol = ted->hoverX;

	//render character graphics
	if (ted->renderCallback != NULL) {
		ted->renderCallback(ted->hWnd, &ted->fb, scrollX, scrollY, renderWidth, renderHeight);
	}

	//mark highlighted tiles
	int selStartX = min(ted->selStartX, ted->selEndX);
	int selEndX = max(ted->selStartX, ted->selEndX);
	int selStartY = min(ted->selStartY, ted->selEndY);
	int selEndY = max(ted->selStartY, ted->selEndY);
	if (selStartX != -1 && selStartY != -1) {
		//tint selection tiles
		int blendR = 255, blendG = 255, blendB = 0, blendW = 1;
		if (hitType == HIT_SEL) {
			blendB = 127;
			blendW = 2;
		}

		for (int y = 0; y < renderHeight; y++) {
			for (int x = 0; x < renderWidth; x++) {
				int curRow = (y + scrollY) / tileH;
				int curCol = (x + scrollX) / tileW;

				if (curCol >= selStartX && curCol <= selEndX && curRow >= selStartY && curRow <= selEndY) {
					//mark selected
					COLOR32 col = ted->fb.px[x + y * viewWidth];

					int b = (col >> 0) & 0xFF;
					int g = (col >> 8) & 0xFF;
					int r = (col >> 16) & 0xFF;
					r = (r + blendR * blendW + blendW / 2) / (blendW + 1);
					g = (g + blendG * blendW + blendW / 2) / (blendW + 1);
					b = (b + blendB * blendW + blendW / 2) / (blendW + 1);
					ted->fb.px[x + y * viewWidth] = b | (g << 8) | (r << 16);
				}

			}
		}
	}

	//mark hovered tile
	if (hovRow != -1 && hovCol != -1 && ted->mouseOver) {
		//mark tile
		int highlightTile = 1;
		if (ted->suppressHighlightCallback != NULL) {
			highlightTile = !ted->suppressHighlightCallback(ted->hWnd);
		}

		if (highlightTile) {
			if (hitType != HIT_SEL) {
				for (int y = 0; y < tileH; y++) {
					for (int x = 0; x < tileW; x++) {
						int pxX = x - scrollX + hovCol * tileW;
						int pxY = y - scrollY + hovRow * tileH;

						if (pxX >= 0 && pxY >= 0 && pxX < renderWidth && pxY < renderHeight) {
							COLOR32 col = ted->fb.px[pxX + pxY * viewWidth];

							//bit trick: average with white
							col = (col >> 1) | 0x808080;
							ted->fb.px[pxX + pxY * viewWidth] = col;
						}
					}
				}
			}
		}
	} else if (hovRow != -1 || hovCol != -1) {
		//mark hovered row/column
		for (int y = 0; y < renderHeight; y++) {
			for (int x = 0; x < renderWidth; x++) {
				int curRow = (y + scrollY) / tileH;
				int curCol = (x + scrollX) / tileW;

				if (curRow == hovRow || curCol == hovCol) {
					COLOR32 col = ted->fb.px[x + y * viewWidth];

					//bit trick: average with white
					col = (col >> 1) | 0x808080;
					ted->fb.px[x + y * viewWidth] = col;
				}
			}
		}
	}

	//render gridlines
	if (data->showBorders) {
		//mark tile boundaries (deliberately do not mark row/col 0)
		for (int y = tileH - (scrollY % tileH); y < renderHeight; y += tileH) {
			for (int x = 0; x < renderWidth; x++) {
				//invert the pixel if (x^y) is even
				if (((x ^ y) & 1) == 0) {
					ted->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
		for (int y = 0; y < renderHeight; y++) {
			for (int x = tileW - (scrollX % tileW); x < renderWidth; x += tileW) {
				//invert the pixel if (x^y) is even
				if (((x ^ y) & 1) == 0) {
					ted->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
		for (int y = tileH - (scrollY % tileH); y < renderHeight; y += tileH) {
			for (int x = tileW - (scrollX % tileW); x < renderWidth; x += tileW) {
				//since we did the gridlines in two passes, pass over the intersections to flip them once more
				if (((x ^ y) & 1) == 0) {
					ted->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}

		//if scale is >= 16x, mark each pixel
		if (data->scale >= 16) {
			int pxSize = data->scale;
			for (int y = pxSize - (scrollY % pxSize); y < renderHeight; y += pxSize) {
				if ((y + scrollY) % tileH == 0) continue; //skip grid-marked rows

				for (int x = pxSize - (scrollX % pxSize); x < renderWidth; x += pxSize) {
					if ((x + scrollX) % tileW == 0) continue; //skip grid-marked columns

					ted->fb.px[x + y * viewWidth] ^= 0xFFFFFF;
				}
			}
		}
	}


	//draw selection border
	if (TedHasSelection(ted)) {
		//shrink borders by 1px when touching the edges of the editor or when gridlines are hidden, since
		//that alignment looks out of place there.
		int dx = -scrollX;
		int dy = -scrollY;
		int borderOffsetX = -(!data->showBorders || selEndX == (ted->tilesX - 1));
		int borderOffsetY = -(!data->showBorders || selEndY == (ted->tilesY - 1));
		FbDrawLine(&ted->fb, 0xFFFF00, selStartX * tileW + dx, selStartY * tileH + dy,
			(selEndX + 1) * tileW + dx - 1, selStartY * tileH + dy);
		FbDrawLine(&ted->fb, 0xFFFF00, selStartX * tileW + dx, selStartY * tileH + dy,
			selStartX * tileW + dx, (selEndY + 1) * tileH + dy - 1);
		FbDrawLine(&ted->fb, 0xFFFF00, (selEndX + 1) * tileW + dx + borderOffsetX, selStartY * tileH + dy,
			(selEndX + 1) * tileW + dx + borderOffsetX, (selEndY + 1) * tileH + dy + borderOffsetY);
		FbDrawLine(&ted->fb, 0xFFFF00, selStartX * tileW + dx, (selEndY + 1) * tileH + dy + borderOffsetY,
			(selEndX + 1) * tileW + dx + borderOffsetX, (selEndY + 1) * tileH + dy + borderOffsetY);
	}

	//draw background color
	if (renderHeight < viewHeight) {
		for (int y = renderHeight; y < viewHeight; y++) {
			for (int x = 0; x < viewWidth; x++) {
				ted->fb.px[x + y * viewWidth] = 0xF0F0F0;
			}
		}
	}
	if (renderWidth < viewWidth) {
		for (int y = 0; y < renderHeight; y++) {
			for (int x = renderWidth; x < viewWidth; x++) {
				ted->fb.px[x + y * viewWidth] = 0xF0F0F0;
			}
		}
	}

	FbDraw(&ted->fb, hDC, 0, 0, viewWidth, viewHeight, 0, 0);
	EndPaint(hWnd, &ps);
}

void TedGetScroll(TedData *ted, int *scrollX, int *scrollY) {
	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(ted->hWndViewer, SB_HORZ, &scrollH);
	GetScrollInfo(ted->hWndViewer, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}





// ----- general functions

int TedHitTest(EDITOR_DATA *data, TedData *ted, int x, int y) {
	//check margins
	if (x < 0 || y < 0) {
		if (x == -1 && y == -1) return HIT_NOWHERE;

		//if both less than zero, hit corner
		if (x < 0 && y < 0) return HIT_MARGIN | HIT_MARGIN_LEFT | HIT_MARGIN_TOP;

		//sides
		if (x < 0) return HIT_MARGIN | HIT_MARGIN_LEFT;
		if (y < 0) return HIT_MARGIN | HIT_MARGIN_TOP;

		return HIT_MARGIN;
	}

	//if the point is outside of the client area of the viewer, hit test should return nowhere.
	RECT rcClientViewer;
	GetClientRect(ted->hWndViewer, &rcClientViewer);
	if (x >= (rcClientViewer.right) || y >= (rcClientViewer.bottom)) {
		return HIT_NOWHERE;
	}

	int tileW = ted->tileWidth * data->scale;
	int tileH = ted->tileHeight * data->scale;

	//get scroll info
	int scrollX, scrollY;
	TedGetScroll(ted, &scrollX, &scrollY);

	//if we have a selection, test for hits.
	if (TedHasSelection(ted)) {

		//get selection bounds in client area
		int selX1 = (min(ted->selStartX, ted->selEndX) + 0) * tileW - scrollX - SEL_BORDER_THICKNESS / 2; //padding for convenience
		int selX2 = (max(ted->selStartX, ted->selEndX) + 1) * tileW - scrollX + SEL_BORDER_THICKNESS / 2;
		int selY1 = (min(ted->selStartY, ted->selEndY) + 0) * tileH - scrollY - SEL_BORDER_THICKNESS / 2;
		int selY2 = (max(ted->selStartY, ted->selEndY) + 1) * tileH - scrollY + SEL_BORDER_THICKNESS / 2;
		if (x >= selX1 && x < selX2 && y >= selY1 && y < selY2) {
			//within selection bounds
			int hit = HIT_SEL;

			if (x < (selX1 + SEL_BORDER_THICKNESS)) hit |= HIT_SEL_LEFT;
			if (x >= (selX2 - SEL_BORDER_THICKNESS)) hit |= HIT_SEL_RIGHT;
			if (y < (selY1 + SEL_BORDER_THICKNESS)) hit |= HIT_SEL_TOP;
			if (y >= (selY2 - SEL_BORDER_THICKNESS)) hit |= HIT_SEL_BOTTOM;

			if (x >= (selX1 + SEL_BORDER_THICKNESS) && x < (selX2 - SEL_BORDER_THICKNESS)
				&& y >= (selY1 + SEL_BORDER_THICKNESS) && y < (selY2 - SEL_BORDER_THICKNESS)) {
				hit |= HIT_SEL_CONTENT;
			}

			return hit;
		}
	}

	//no selection hit. try content hit.
	int contentWidth = ted->tilesX * tileW;
	int contentHeight = ted->tilesY * tileH;
	if ((x + scrollX) < contentWidth && (y + scrollY) < contentHeight) {
		//content hit
		return HIT_CONTENT;
	}

	return HIT_NOWHERE;
}

void TedUpdateCursor(EDITOR_DATA *data, TedData *ted) {
	HWND hWnd = ted->hWndViewer;

	//get scroll pos
	int scrollX, scrollY;
	TedGetScroll(ted, &scrollX, &scrollY);

	//get pixel coordinate
	int pxX = -1, pxY = -1;
	if (ted->mouseOver) {
		pxX = (ted->mouseX + scrollX) / data->scale;
		pxY = (ted->mouseY + scrollY) / data->scale;
	}

	int curHit = TedHitTest(data, ted, ted->mouseX, ted->mouseY);

	//if mouse is hovered, get hoeverd character pos
	if (ted->mouseOver && (curHit & HIT_FLAGS_MASK) != HIT_NOWHERE) {
		ted->hoverX = (ted->mouseX + scrollX) / (ted->tileWidth * data->scale);
		ted->hoverY = (ted->mouseY + scrollY) / (ted->tileHeight * data->scale);
	} else {
		//un-hover
		ted->hoverX = -1;
		ted->hoverY = -1;
	}

	//if a tile is hovered, set hovered index.
	int lastHoveredIndex = ted->hoverIndex;
	if (ted->hoverX != -1 && ted->hoverY != -1 && curHit != HIT_NOWHERE) {
		ted->hoverIndex = ted->hoverX + ted->hoverY * ted->tilesX;
	} else {
		ted->hoverIndex = -1;
	}

	if (ted->mouseDown) {

		//get mouse movement if mouse down
		int dx = ted->mouseX - ted->dragStartX;
		int dy = ted->mouseY - ted->dragStartY;

		//get mouse drag start position if mouse down
		int dragStartTileX = (ted->dragStartX + scrollX) / (ted->tileWidth * data->scale);
		int dragStartTileY = (ted->dragStartY + scrollY) / (ted->tileHeight * data->scale);

		//check hit type for mouse-down.
		int hit = ted->mouseDownHit;
		int hitType = hit & HIT_TYPE_MASK;

		//if the mouse hits the selection, handle selection gesture.
		if (hitType == HIT_SEL) {
			//if the mouse is down, check hit flags.
			if (hit & HIT_SEL_CONTENT) {
				//mouse-down on content, carry out drag procedure.
				int nextX = ted->hoverX - dragStartTileX + ted->selDragStartX;
				int nextY = ted->hoverY - dragStartTileY + ted->selDragStartY;

				ted->selEndX = nextX + ted->selEndX - ted->selStartX;
				ted->selEndY = nextY + ted->selEndY - ted->selStartY;
				ted->selStartX = nextX;
				ted->selStartY = nextY;

				//get selection bound
				int selX, selY, selWidth, selHeight;
				TedGetSelectionBounds(ted, &selX, &selY, &selWidth, &selHeight);

				//check bounds of movement
				int dx = 0, dy = 0;
				if (selX < 0) dx = -selX;
				if ((selX + selWidth) > ted->tilesX) dx = -(selX + selWidth - ted->tilesX);
				if (selY < 0) dy = -selY;
				if ((selY + selHeight) > ted->tilesY) dy = -(selY + selHeight - ted->tilesY);

				TedOffsetSelection(ted, dx, dy);
			} else {
				//hit on selection border.

				int moveX = (hit & HIT_SEL_LEFT) || (hit & HIT_SEL_RIGHT);
				int moveY = (hit & HIT_SEL_TOP) || (hit & HIT_SEL_BOTTOM);
				if (moveX) ted->selEndX = ted->hoverX;
				if (moveY) ted->selEndY = ted->hoverY;
			}
		} else {

			//else, if the editor is in a selection mode, handle select mode mouse down.
			if (ted->isSelectionModeCallback != NULL && ted->isSelectionModeCallback(ted->hWnd)) {
				//if in content, start or continue a selection.
				if (hitType == HIT_CONTENT) {
					//if the mouse is down, start or continue a selection.
					if (!TedHasSelection(ted)) {
						ted->selStartX = ted->hoverX;
						ted->selStartY = ted->hoverY;

						//set cursor to crosshair
						SetCursor(LoadCursor(NULL, IDC_CROSS));
					}

					ted->selEndX = ted->hoverX;
					ted->selEndY = ted->hoverY;
				}
			}

			if (ted->updateCursorCallback != NULL) {
				ted->updateCursorCallback(ted->hWnd, pxX, pxY);
			}
			
		}
	}

	if (ted->hoverIndex != lastHoveredIndex) {
		if (ted->tileHoverCallback != NULL) {
			ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
		}
	}

	//repaint viewer and update margin rendering
	InvalidateRect(ted->hWndViewer, NULL, FALSE);
	TedUpdateMargins(ted);
}

void TedReleaseCursor(EDITOR_DATA *data, TedData *ted) {
	ted->mouseDown = FALSE;
	ted->mouseDownTop = FALSE;
	ted->mouseDownLeft = FALSE;
	ted->dragStartX = -1;
	ted->dragStartY = -1;
	ted->selDragStartX = -1;
	ted->selDragStartY = -1;
	ReleaseCapture();

	TedUpdateCursor(data, ted);
}



int TedHasSelection(TedData *ted) {
	if (ted->selStartX == -1 || ted->selStartY == -1) return 0;
	return 1;
}

void TedDeselect(TedData *ted) {
	ted->selStartX = -1;
	ted->selEndX = -1;
	ted->selStartY = -1;
	ted->selEndY = -1;
}

int TedGetSelectionBounds(TedData *ted, int *x, int *y, int *width, int *height) {
	//get bounds
	int x1 = min(ted->selStartX, ted->selEndX);
	int x2 = max(ted->selStartX, ted->selEndX) + 1;
	int y1 = min(ted->selStartY, ted->selEndY);
	int y2 = max(ted->selStartY, ted->selEndY) + 1;

	*x = x1;
	*y = y1;
	*width = x2 - x1;
	*height = y2 - y1;
	return 1;
}

int TedIsSelectedAll(TedData *ted) {
	if (!TedHasSelection(ted)) return 0;

	int selX, selY, selW, selH;
	TedGetSelectionBounds(ted, &selX, &selY, &selW, &selH);

	return (selX == 0 && selY == 0) && (selW == ted->tilesX && selH == ted->tilesY);
}

void TedSelectAll(TedData *ted) {
	ted->selStartX = 0;
	ted->selStartY = 0;
	ted->selEndX = ted->tilesX - 1;
	ted->selEndY = ted->tilesY - 1;
}

void TedOffsetSelection(TedData *ted, int dx, int dy) {
	ted->selStartX += dx;
	ted->selStartY += dy;
	ted->selEndX += dx;
	ted->selEndY += dy;
}

void TedSelect(TedData *ted, int selX, int selY, int selW, int selH) {
	ted->selStartX = selX;
	ted->selStartY = selY;
	ted->selEndX = selX + selW - 1;
	ted->selEndY = selY + selH - 1;

	//bounds check
	if (ted->selStartX >= ted->tilesX) ted->selStartX = ted->tilesX - 1;
	if (ted->selStartY >= ted->tilesY) ted->selStartY = ted->tilesY - 1;
	if (ted->selEndX >= ted->tilesX) ted->selEndX = ted->tilesX - 1;
	if (ted->selEndY >= ted->tilesY) ted->selEndY = ted->tilesY - 1;
}

void TedMakeSelectionCornerEnd(TedData *ted, int hit) {
	//if hit test hits top, make min Y first
	if (hit & HIT_SEL_TOP && ted->selEndY > ted->selStartY) {
		SwapInts(&ted->selStartY, &ted->selEndY);
	}
	if (hit & HIT_SEL_BOTTOM && ted->selEndY < ted->selStartY) {
		SwapInts(&ted->selStartY, &ted->selEndY);
	}

	//if hit test hits left, make min X first
	if (hit & HIT_SEL_LEFT && ted->selEndX > ted->selStartX) {
		SwapInts(&ted->selStartX, &ted->selEndX);
	}
	if (hit & HIT_SEL_RIGHT && ted->selEndX < ted->selStartX) {
		SwapInts(&ted->selStartX, &ted->selEndX);
	}
}

void TedGetPasteLocation(TedData *ted, BOOL contextMenu, int *tileX, int *tileY) {
	if (TedHasSelection(ted)) {
		//has selection: paste at top-left corner of selection region
		int selW, selH;
		TedGetSelectionBounds(ted, tileX, tileY, &selW, &selH);
	} else if (contextMenu) {
		//context modal: paste at last mouse position before modal
		*tileX = ted->contextHoverX;
		*tileY = ted->contextHoverY;
	} else if (ted->mouseOver && ted->hoverIndex != -1) {
		//mouse in client area: paste at mouse position
		*tileX = ted->hoverX;
		*tileY = ted->hoverY;
	} else {
		//mouse out of client area: paste at origin
		*tileX = 0;
		*tileY = 0;
	}
}

void TedUpdateSize(EDITOR_DATA *data, TedData *ted, int tilesX, int tilesY) {
	ted->tilesX = tilesX;
	ted->tilesY = tilesY;

	//update UI and check bounds
	if (ted->selStartX >= ted->tilesX) ted->selStartX = ted->tilesX - 1;
	if (ted->selEndX >= ted->tilesX) ted->selEndX = ted->tilesX - 1;
	if (ted->selStartY >= ted->tilesY) ted->selStartY = ted->tilesY - 1;
	if (ted->selEndY >= ted->tilesY) ted->selEndY = ted->tilesY - 1;

	//update
	TedUpdateMargins(ted);
	TedUpdateCursor(data, ted);
	InvalidateRect(ted->hWndViewer, NULL, FALSE);
}

void TedTrackPopup(EDITOR_DATA *data, TedData *ted) {
	//release mouse to prevent input issues
	TedReleaseCursor(data, ted);

	HMENU hPopup = NULL;
	hPopup = ted->getPopupMenuCallback(ted->hWnd);
	if (hPopup == NULL) return;

	POINT mouse;
	GetCursorPos(&mouse);
	TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, ted->hWnd, NULL);
	DeleteObject(hPopup);
}



// ----- message handling functions

BOOL TedSetCursor(EDITOR_DATA *data, TedData *ted, WPARAM wParam, LPARAM lParam) {
	if (LOWORD(lParam) != HTCLIENT) {
		//nonclient area: default processing
		return DefWindowProc(ted->hWndViewer, WM_SETCURSOR, wParam, lParam);
	}

	//get mouse coordinates current: prevent outdated mouse coordinates from message order
	POINT mouse;
	GetCursorPos(&mouse);
	ScreenToClient(ted->hWndViewer, &mouse);

	//test hit
	int hit = TedHitTest(data, ted, mouse.x, mouse.y);
	int type = hit & HIT_TYPE_MASK;

	//nowhere: default processing
	if (type == HIT_NOWHERE) return DefWindowProc(ted->hWndViewer, WM_SETCURSOR, wParam, lParam);

	//content: decide based on edit mode
	if (type == HIT_CONTENT) {
		HCURSOR hCursor = ted->getCursorProc(ted->hWnd, hit);
		SetCursor(hCursor);
		return TRUE;
	}

	//selection: set cursor
	if (type == HIT_SEL) {
		if (hit & HIT_SEL_CONTENT) {
			//content, set cursor to move cursor
			SetCursor(LoadCursor(NULL, IDC_SIZEALL));
			return TRUE;
		}

		int winHit = HTCLIENT;

		//convert to win32 hit type
		int border = hit & HIT_FLAGS_MASK;
		switch (border) {
			case HIT_SEL_TOP: winHit = HTTOP; break;
			case HIT_SEL_LEFT: winHit = HTLEFT; break;
			case HIT_SEL_RIGHT: winHit = HTRIGHT; break;
			case HIT_SEL_BOTTOM: winHit = HTBOTTOM; break;
			case HIT_SEL_TOP | HIT_SEL_LEFT: winHit = HTTOPLEFT; break;
			case HIT_SEL_TOP | HIT_SEL_RIGHT: winHit = HTTOPRIGHT; break;
			case HIT_SEL_BOTTOM | HIT_SEL_LEFT: winHit = HTBOTTOMLEFT; break;
			case HIT_SEL_BOTTOM | HIT_SEL_RIGHT: winHit = HTBOTTOMRIGHT; break;
		}

		//pass off default behavior for a sizing border hit
		return DefWindowProc(ted->hWndViewer, WM_SETCURSOR, wParam, MAKELONG(winHit, HIWORD(lParam)));
	}

	//default processing
	return DefWindowProc(ted->hWndViewer, WM_SETCURSOR, wParam, lParam);
}

void TedMainOnMouseMove(EDITOR_DATA *data, TedData *ted, UINT msg, WPARAM wParam, LPARAM lParam) {
	//if mouse left, update
	if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE) {
		//check the last mouse event window handle. This makes sure the mouse can move from parent
		//to child seamlessly without one message interfering with the other.
		if (ted->hWndLastMouse == ted->hWnd || ted->hWndLastMouse == NULL) {
			ted->mouseX = ted->lastMouseX = -1;
			ted->mouseY = ted->lastMouseY = -1;
			ted->hoverX = -1;
			ted->hoverY = -1;
			ted->hoverIndex = -1;
			ted->hWndLastMouse = NULL;
			ted->mouseOver = FALSE;
			ted->tileHoverCallback(ted->hWnd, -1, -1);
		}
	} else {
		ted->hWndLastMouse = ted->hWnd;
		ted->mouseOver = TRUE;
		ted->lastMouseX = ted->mouseX;
		ted->lastMouseY = ted->mouseY;
		ted->mouseX = ((short) LOWORD(lParam)) - MARGIN_TOTAL_SIZE;
		ted->mouseY = ((short) HIWORD(lParam)) - MARGIN_TOTAL_SIZE;
		ted->hoverIndex = -1;

		//get client rect
		RECT rcClient;
		GetClientRect(ted->hWnd, &rcClient);

		//get scroll info
		int scrollX, scrollY;
		TedGetScroll(ted, &scrollX, &scrollY);

		if (ted->mouseDown && (ted->mouseDownTop || ted->mouseDownLeft)) {
			//clamp mouse pos to editor area
			if (ted->mouseX < 0) ted->mouseX = 0;
			if (ted->mouseY < 0) ted->mouseY = 0;
		}
		int curCol = (ted->mouseX + scrollX) / (ted->tileWidth * data->scale);
		int curRow = (ted->mouseY + scrollY) / (ted->tileHeight * data->scale);

		//if the mouse is down, handle gesture
		if (ted->mouseDown) {
			if (ted->mouseDownTop) {
				//clamp mouse position
				if (ted->mouseY >= 0) ted->mouseY = -2;
				if (ted->mouseY < -MARGIN_TOTAL_SIZE) ted->mouseY = -MARGIN_TOTAL_SIZE;
				if (ted->mouseX < 0) ted->mouseX = 0;

				//update selection
				ted->selEndX = curCol;
			}
			if (ted->mouseDownLeft) {
				//clamp mouse position
				if (ted->mouseX >= 0) ted->mouseX = -2;
				if (ted->mouseX < -MARGIN_TOTAL_SIZE) ted->mouseX = -MARGIN_TOTAL_SIZE;
				if (ted->mouseY < 0) ted->mouseY = 0;

				//update selection
				ted->selEndY = curRow;
			}
		}

		//check the mouse over the margins. If so, hover that row and/or column.
		BOOL inLeft = ted->mouseX >= -MARGIN_TOTAL_SIZE && ted->mouseX < 0;
		BOOL inTop = ted->mouseY >= -MARGIN_TOTAL_SIZE && ted->mouseY < 0;

		if (inLeft && !inTop) {
			//on left margin
			ted->hoverY = (ted->mouseY + scrollY) / (ted->tileHeight * data->scale);
			ted->hoverX = -1;
		}
		if (inTop && !inLeft) {
			//on top margin
			ted->hoverX = (ted->mouseX + scrollX) / (ted->tileWidth * data->scale);
			ted->hoverY = -1;
		}
		if (inTop && inLeft) {
			//on topleft corner
			ted->hoverX = -1;
			ted->hoverY = -1;
		}
	}

	//notify of mouse leave if we're not already processing a mouse leave
	if (msg != WM_MOUSELEAVE && msg != WM_NCMOUSELEAVE) {
		TRACKMOUSEEVENT tme = { 0 };
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = ted->hWnd;
		TrackMouseEvent(&tme);
	}

	TedUpdateMargins(ted);
}

int TedMainOnEraseBkgnd(EDITOR_DATA *data, TedData *ted, WPARAM wParam, LPARAM lParam) {
	HDC hDC = (HDC) wParam;

	//do not erase margins
	RECT rcClient;
	GetClientRect(ted->hWnd, &rcClient);

	//clamp size of render area to the size of graphics view
	if (ted->hWndViewer != NULL) {
		RECT rcView;
		GetWindowRect(ted->hWndViewer, &rcView);

		int viewWndWidth = rcView.right - rcView.left + MARGIN_TOTAL_SIZE;
		int viewWndHeight = rcView.bottom - rcView.top + MARGIN_TOTAL_SIZE;
		if (viewWndWidth < rcClient.right) rcClient.right = viewWndWidth;
		if (viewWndHeight < rcClient.bottom) rcClient.bottom = viewWndHeight;
	}

	//exclude the margins from the background erase
	ExcludeClipRect(hDC, 0, 0, rcClient.right, MARGIN_TOTAL_SIZE);
	ExcludeClipRect(hDC, 0, 0, MARGIN_TOTAL_SIZE, rcClient.bottom);

	//invalidate margins
	RECT rcLeft = { 0 }, rcTop = { 0 };
	rcTop.right = rcClient.right;
	rcTop.bottom = MARGIN_TOTAL_SIZE;
	rcLeft.bottom = rcClient.bottom;
	rcLeft.right = MARGIN_TOTAL_SIZE;

	DefWindowProc(ted->hWnd, WM_ERASEBKGND, wParam, lParam);
	InvalidateRect(ted->hWnd, &rcLeft, FALSE);
	InvalidateRect(ted->hWnd, &rcTop, FALSE);

	return 1;
}

void TedOnLButtonDown(EDITOR_DATA *data, TedData *ted) {
	ted->mouseDown = TRUE;
	ted->dragStartX = ted->mouseX;
	ted->dragStartY = ted->mouseY;
	ted->selDragStartX = ted->selStartX;
	ted->selDragStartY = ted->selStartY;
	ted->mouseDownHit = TedHitTest(data, ted, ted->mouseX, ted->mouseY);

	int hit = ted->mouseDownHit;
	if (hit == HIT_NOWHERE) {
		//if in selection mode, clear selection.
		if (ted->isSelectionModeCallback != NULL) {
			if (ted->isSelectionModeCallback(ted->hWnd)) TedDeselect(ted);
		}
		TedReleaseCursor(data, ted);
		if (ted->tileHoverCallback != NULL) {
			ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
		}
		return;
	}

	//get scroll info
	int scrollX, scrollY;
	TedGetScroll(ted, &scrollX, &scrollY);

	//get content view size
	int contentW, contentH;
	contentW = ted->tilesX * ted->tileWidth * data->scale - scrollX;
	contentH = ted->tilesY * ted->tileHeight * data->scale - scrollY;

	int curRow = (ted->mouseY + scrollY) / (ted->tileHeight * data->scale);
	int curCol = (ted->mouseX + scrollX) / (ted->tileWidth * data->scale);

	if ((hit & HIT_TYPE_MASK) == HIT_MARGIN && ted->allowSelection) {
		int hitWhere = hit & HIT_FLAGS_MASK;
		if (hitWhere == HIT_MARGIN_LEFT && ted->mouseX < contentH) {
			ted->mouseDownLeft = TRUE;

			//start or edit a selection
			ted->selStartY = curRow;
			ted->selEndY = curRow;
			if (ted->selStartX == -1 || ted->selEndX == -1) {
				ted->selStartX = 0;
				ted->selEndX = ted->tilesX - 1;
			}
		}
		if (hitWhere == HIT_MARGIN_TOP && ted->mouseX < contentW) {
			ted->mouseDownTop = TRUE;

			//start or edit a selection
			ted->selStartX = curCol;
			ted->selEndX = curCol;
			if (ted->selStartY == -1 || ted->selEndY == -1) {
				ted->selStartY = 0;
				ted->selEndY = ted->tilesY - 1;
			}
		}
		if (hitWhere == (HIT_MARGIN_TOP | HIT_MARGIN_LEFT)) {
			//select all
			if (!TedIsSelectedAll(ted)) {
				TedSelectAll(ted);
			} else {
				TedDeselect(ted);
			}
		}
		if (ted->tileHoverCallback != NULL) {
			ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
		}
	} else if (ted->mouseDownHit == HIT_NOWHERE) {
		//hit nowhere, release cursor.
		TedReleaseCursor(data, ted);
	}

	//set capture
	SetCapture(ted->hWnd);
}

void TedOnRButtonDown(TedData *ted) {
	ted->contextHoverX = ted->hoverX;
	ted->contextHoverY = ted->hoverY;
}

void TedViewerOnMouseMove(EDITOR_DATA *data, TedData *ted, UINT msg, WPARAM wParam, LPARAM lParam) {
	//update mouse coords
	if (msg == WM_MOUSELEAVE || msg == WM_NCMOUSELEAVE) {
		if (ted->hWndLastMouse == ted->hWndViewer || ted->hWndLastMouse == NULL) {
			//mouse left client area: set pos to (-1, -1).
			ted->mouseX = ted->lastMouseX = -1;
			ted->mouseY = ted->lastMouseY = -1;
			ted->hoverX = -1;
			ted->hoverY = -1;
			ted->hoverIndex = -1;
			ted->hWndLastMouse = NULL;
			ted->mouseOver = FALSE;
			ted->tileHoverCallback(ted->hWnd, -1, -1);
		}
	} else {
		//mouse moved in client area.
		ted->lastMouseX = ted->mouseX;
		ted->lastMouseY = ted->mouseY;
		ted->mouseX = (short) LOWORD(lParam);
		ted->mouseY = (short) HIWORD(lParam);
		ted->hWndLastMouse = ted->hWndViewer;
		ted->mouseOver = TRUE;

		//bounds check mouse position to client area if mouse down
		if (ted->mouseDown) {
			RECT rcClient;
			GetClientRect(ted->hWndViewer, &rcClient);

			if (ted->mouseX < 0) ted->mouseX = 0;
			if (ted->mouseY < 0) ted->mouseY = 0;
			if (ted->mouseX >= rcClient.right) ted->mouseX = rcClient.right - 1;
			if (ted->mouseY >= rcClient.bottom) ted->mouseY = rcClient.bottom - 1;

			//clamp additionally to the valid content.
			int scrollX, scrollY, contentW, contentH;
			TedGetScroll(ted, &scrollX, &scrollY);
			contentW = ted->tilesX * ted->tileWidth * data->scale - scrollX;
			contentH = ted->tilesY * ted->tileHeight * data->scale - scrollY;

			if (ted->mouseX >= contentW) ted->mouseX = contentW - 1;
			if (ted->mouseY >= contentH) ted->mouseY = contentH - 1;
		}
	}

	//notify of mouse leave if we're not already processing a mouse leave
	if (msg != WM_MOUSELEAVE && msg != WM_NCMOUSELEAVE) {
		TRACKMOUSEEVENT tme = { 0 };
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = ted->hWndViewer;
		TrackMouseEvent(&tme);
	}

	//update cursor
	TedUpdateCursor(data, ted);
}

void TedViewerOnLButtonDown(EDITOR_DATA *data, TedData *ted) {
	ted->mouseDown = TRUE;
	SetFocus(ted->hWnd);
	SetCapture(ted->hWndViewer);

	//hit test
	ted->mouseDownHit = TedHitTest(data, ted, ted->mouseX, ted->mouseY);

	int hit = ted->mouseDownHit;
	int hitType = hit & HIT_TYPE_MASK;

	if (hit == HIT_NOWHERE) {
		//hit nowhere. If in selection mode, clear selection.
		if (ted->isSelectionModeCallback != NULL) {
			if (ted->isSelectionModeCallback(ted->hWnd)) TedDeselect(ted);
		}
		TedReleaseCursor(data, ted); //ChrViewerReleaseCursor(data);
		if (ted->tileHoverCallback != NULL) {
			ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
		}
		return;
	}

	if (hitType == HIT_SEL) {
		//mouse-down on selection, do not clear selection.
		//make the corner/edge of the hit the end point.
		TedMakeSelectionCornerEnd(ted, hit);
	}

	if (hitType == HIT_SEL) {
		ted->dragStartX = ted->mouseX;
		ted->dragStartY = ted->mouseY;
		ted->selDragStartX = ted->selStartX;
		ted->selDragStartY = ted->selStartY;
	}

	if (ted->isSelectionModeCallback != NULL) {
		if (ted->isSelectionModeCallback(ted->hWnd)) {
			//selection mode mouse-down
			ted->dragStartX = ted->mouseX;
			ted->dragStartY = ted->mouseY;
			ted->selDragStartX = ted->selStartX;
			ted->selDragStartY = ted->selStartY;

			if (hitType == HIT_NOWHERE || hitType == HIT_CONTENT) {
				if (TedHasSelection(ted)) {
					//discard selection
					TedDeselect(ted);
				} else {
					//make selection of click point
					ted->selStartX = ted->selEndX = ted->hoverX;
					ted->selStartY = ted->selEndY = ted->hoverY;
				}

				if (ted->tileHoverCallback != NULL) {
					ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
				}
			} else if (hitType == HIT_SEL) {
				//do not clear selection.
			}
		}
	}
}

void TedViewerOnKeyDown(EDITOR_DATA *data, TedData *ted, WPARAM wParam, LPARAM lParam) {
	int selX, selY, selW, selH;
	TedGetSelectionBounds(ted, &selX, &selY, &selW, &selH);

	switch (wParam) {
		case VK_LEFT:
		case VK_RIGHT:
		case VK_UP:
		case VK_DOWN:
		{
			if (TedHasSelection(ted)) {
				int shift = GetKeyState(VK_SHIFT) < 0;

				int dx = 0, dy = 0;
				switch (wParam) {
					case VK_UP: dy = -1; break;
					case VK_DOWN: dy = 1; break;
					case VK_LEFT: dx = -1; break;
					case VK_RIGHT: dx = 1; break;
				}

				if (!shift) {
					//offset whole selection
					int newX = selX + dx, newY = selY + dy;
					if (newX >= 0 && newY >= 0 && (newX + selW) <= ted->tilesX && (newY + selH) <= ted->tilesY) {
						TedOffsetSelection(ted, dx, dy);
					}
				} else {
					//offset selection end
					int newX = ted->selEndX + dx, newY = ted->selEndY + dy;
					if (newX >= 0 && newX < ted->tilesX && newY >= 0 && newY < ted->tilesY) {
						ted->selEndX += dx;
						ted->selEndY += dy;
					}
				}

				InvalidateRect(ted->hWndViewer, NULL, FALSE);
				TedUpdateMargins(ted);
				TedUpdateCursor(data, ted);
				if (ted->tileHoverCallback != NULL) ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
			}
			break;
		}
		case VK_ESCAPE:
		{
			//deselect
			InvalidateRect(ted->hWndViewer, NULL, FALSE);
			TedDeselect(ted);
			TedUpdateMargins(ted);
			if (ted->tileHoverCallback != NULL) ted->tileHoverCallback(ted->hWnd, ted->hoverX, ted->hoverY);
			break;
		}
	}
}



void TedInit(TedData *ted, HWND hWnd, HWND hWndViewer, int tileWidth, int tileHeight) {
	memset(ted, 0, sizeof(TedData));
	ted->tileWidth = tileWidth;
	ted->tileHeight = tileHeight;
	ted->hoverX = -1;
	ted->hoverY = -1;
	ted->mouseX = ted->lastMouseX = -1;
	ted->mouseY = ted->lastMouseY = -1;
	ted->hoverIndex = -1;
	ted->selStartX = -1;
	ted->selStartY = -1;
	ted->selEndX = -1;
	ted->selEndY = -1;
	ted->allowSelection = 1;
	ted->hWnd = hWnd;
	ted->hWndViewer = hWndViewer;

	//create dummy framebuffers
	FbCreate(&ted->fb, hWnd, 1, 1);
	FbCreate(&ted->fbMargin, hWnd, 1, 1);
}

void TedDestroy(TedData *ted) {
	FbDestroy(&ted->fb);
	FbDestroy(&ted->fbMargin);
}
