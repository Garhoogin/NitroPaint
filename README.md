# NitroPaint

NitroPaint is a general purpose DS graphics editor. It is capable of editing NCLR, NCGR, NSCR, NCER, and NSBTX files.

## NCLR Editor

In the NCLR editor, click any color space to open a color chooser. Here is where you can change the color of this entry. Right click an entry and you get the options to row copy and row paste, which copies the row of the highlighted entry and pastes it respectively. Click and drag a color entry to reposition it in the file. Do this while holding Ctrl to move the entire row. Editing the NCLR will have its changes reflected in the NCGR, NSCR, and NCER windows, if open.

## NCGR Editor

In the NCGR editor, click any tile to open the tile editor. Here you can select a color and draw in the tile pixel-by-pixel. Near the bottom of the NCGR window is the palette select. This changes which palette in the NCLR is used to display the NCGR in the editor. Next to that is the width select, which can change the width of an NCGR, which may make editing some parts easier. Right click on a tile to get the option to import a bitmap at that location. It can either create a new palette, overwriting the currently selected NCLR palette, or it can use the palette already being selected. Changes here are reflected in the NSCR and NCER windows if open. Zoom in/out and toggle gridlines in the View menu. Under File->Export Bitmap, you can export the NCGR file as a bmp image.

## NSCR Editor

In the NSCR editor, click any tile to edit its palette or character location. Right click it to flip it horizontally or vertically. Under File->Export Bitmap, you can export the NSCR file as a bmp image.

## NCER Editor

In the NCER editor, each object can be displayed, as well as each of its cells. In here you can edit the character origin and palette of each cell. Under File->Export Bitmap, you can export the currently viewed cell as a bitmap.

##NSBTX Editor

In the NSBTX editor, there are two windows on the left; one with the textures, and one with the palettes. Select a tetxure and its corresponding palette to preview that texture. On the top of the window will display the texture's format, size, and number of color entries in its palette. Click the replace button to replace the currently viewed texture with a Nitro TGA file. Under File->Export Bitmap, you can export the current selected texture as a Nitro TGA.

Any questions, email me at declan.c.moore [at] gmail.com.
