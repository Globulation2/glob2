/* 
  Utility to create fonts for SFont or BFont
  (c) Patrick Kooman, 2002
  contact: patrick@2dgame-tutorial.com


  This file is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This file is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  To view the licence online, go to: http://www.gnu.org/copyleft/gpl.html
  */

#include <stdio.h>
#include <SDL_ttf.h>

#undef main

/* Creates a Surface which can be used by SFont or BFont. In addition to the
 Surface which is returned by TTF, we use a fixed color for transparency. Because
 TTF uses "255 - text color" as the colorkey, you can use each color for the text. 
 (Pretty smart actually!). However, because we are saving the file, we don't know the
 colorkey anymore, so we have to set it. We use black (0, 0, 0) as the transparent color,
 because both SFont and BFont require it. This means that you cannot use this color as a text color.
 */
SDL_Surface* CreateFontSurface (const char* str_ttf, int i_style, int i_size, Uint8 ui_r, Uint8 ui_g, Uint8 ui_b) {
  /* SFont and DFont compatible character set */
  const char*   str_charset = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_'abcdefghijklmnopqrstuvwxyz{|}~" ;
  TTF_Font*     ttf_font = NULL ;
  /* Targets for the rendered characters */
  SDL_Surface*  p_surf_ch [95] ;
  /* All characters gets blitted onto this Surface */
  SDL_Surface*  p_surf_str = NULL ;
  SDL_Rect      sdl_rc_dst ;
  /* Render color */
  SDL_Color sdl_fg = {ui_r, ui_g, ui_b} ;
  SDL_Color sdl_bg = {0, 0, 0} ;
  char ch_tmp [2] ;
  int i, j ;
  int i_total_w, i_total_h ;
  int i_character = 0 ;
  Uint8 ui_index = 0 ;
  /* Render the characters to a Surface with SDL_ttf */
	if (TTF_Init() < 0) {
		fprintf(stderr, "Error: couldn't initialize TTF: %s\n", SDL_GetError());
		return NULL ;
	}
	ttf_font = TTF_OpenFont (str_ttf, i_size) ;
	if (ttf_font == NULL) {
		fprintf(stderr, "Error: couldn't load %d pt font from %s: %s\n", i_size, str_ttf, SDL_GetError()) ;
		return NULL ;
	}
  TTF_SetFontStyle (ttf_font, i_style) ;
  /* Creates a 8 bits Software Surface per character */
  for (i = 0; i < strlen (str_charset); ++i) {
    ch_tmp [0] = str_charset [i] ;
    ch_tmp [1] = '\0' ;
    p_surf_ch [i] = TTF_RenderText_Shaded (ttf_font, ch_tmp, sdl_fg, sdl_bg) ;
    if (p_surf_ch [i] == NULL) {
      for (j = 0; j < strlen (str_charset); ++j) {
        SDL_FreeSurface (p_surf_ch [j]) ;
      }
      TTF_CloseFont (ttf_font) ;
      TTF_Quit () ;
      fprintf(stderr, "Error: couldn't render the charset: %s\n", SDL_GetError());
      return NULL ;
    }
  }
  /* We don't use TTF anymore */
  TTF_CloseFont (ttf_font) ;
  TTF_Quit () ;
  /* Calculate how large the Surface must be */
  i_total_w = 1 ;
  i_total_h = p_surf_ch [0]->h ;
  for (i = 0; i < strlen (str_charset); ++i) {
    i_total_w += p_surf_ch [i]->w + 1 ;
  }
  /* Create the large Surface */
  p_surf_str = SDL_CreateRGBSurface (SDL_SWSURFACE, i_total_w, i_total_h, 8, 0, 0, 0, 0) ;
  /* Copy the palette. (It doesn't matter which character-surface we use, so we just
  take the first one. */
  p_surf_str->format->palette->ncolors = p_surf_ch [0]->format->palette->ncolors ;
  memcpy (p_surf_str->format->palette->colors, p_surf_ch [0]->format->palette->colors, p_surf_str->format->palette->ncolors * sizeof (SDL_Color)) ;
  /* Only the first two indexes of the palette are set by TTF :
  Index 0: Transparent color (255 - Textcolor)
  Index 1: Text color

  First overwrite the palette values for index 1 with our fixed color black */
  p_surf_str->format->palette->colors [0].r = 0 ;
  p_surf_str->format->palette->colors [0].g = 0 ;
  p_surf_str->format->palette->colors [0].b = 0 ;

  /* Secondly, set a third palette index for the pink color (255, 0, 255) */
  p_surf_str->format->palette->colors [2].r = 255 ;
  p_surf_str->format->palette->colors [2].g = 0 ;
  p_surf_str->format->palette->colors [2].b = 255 ;

  /* Blit characters on the large Surface*/
  sdl_rc_dst.x = sdl_rc_dst.y = 0 ;
  for (i = 0; i < strlen (str_charset); ++i) {
    /* Set pink pixel in left corner before character */
    ((Uint8*) p_surf_str->pixels) [sdl_rc_dst.x] = 2 ;
    ++sdl_rc_dst.x ;
    SDL_BlitSurface (p_surf_ch [i], NULL, p_surf_str, &sdl_rc_dst) ;
    sdl_rc_dst.x += p_surf_ch [i]->w ;
  }
  /* Set last pink pixel */
  ((Uint8*) p_surf_str->pixels) [sdl_rc_dst.x] = 2 ;
  /* Cleanup characters */
  for (i = 0; i < strlen (str_charset); ++i) {
    SDL_FreeSurface (p_surf_ch [i]) ;
  }
  /* We made it! */
  return p_surf_str ;  
}

#undef main

/* Main function. Reads input from 'fontdesc.txt' */
int main(int argc, char *argv[]) {
  char ch_ttf [256] ;
  char ch_filename [256] ;
  int i_size = 0, i_style = TTF_STYLE_NORMAL ;
  int i_bold = 0, i_italic = 0 ;
  Uint8 ui_r, ui_g, ui_b ;
  FILE* fp = NULL ;
  SDL_Surface *p_surf = NULL ;
  /* Try to open the description-file */
  fp = fopen ("fontdesc.txt","r") ;
  if (fp == NULL) {
    fprintf (stderr, "Error: could not open \"%s\"\n", argv[0]) ;
    return 0 ;
  }
  /* Read the font name */
  fgets (ch_ttf, 256, fp) ;
  if (strlen (ch_ttf) > 0) {
    /* Remove the newline */
    ch_ttf [strlen (ch_ttf) - 1] = '\0' ;
  }
  /* Read the settings */
  fscanf (fp, "size:%d,bold:%d,italic:%d,r:%d,g:%d,b:%d", &i_size, &i_bold, &i_italic, &ui_r, &ui_g, &ui_b) ;
  if (i_bold) {
    i_style |= TTF_STYLE_BOLD ;
  }
  if (i_italic) {
    i_style |= TTF_STYLE_ITALIC ;
  }
  fclose (fp) ;
  /* Initialize SDL */
  if (SDL_Init (SDL_INIT_VIDEO) < 0) {
    fprintf (stderr, "Error: could not initialize SDL.\n") ;
    return 0 ;
  }
  /* Create the font */
  p_surf = CreateFontSurface (ch_ttf, i_style, i_size, ui_r, ui_g, ui_b) ;
  if (p_surf == NULL) {
    fprintf (stderr, "Error: could not create the font Surface.\n") ;
    return 0 ;
  }
  /* Save the font Surface */
  sprintf (ch_filename, "%s_%d_%d_%d_%d_%d.bmp", ch_ttf, i_style, i_size, ui_r, ui_g, ui_b) ;
  if (SDL_SaveBMP (p_surf, ch_filename) == -1) {
    fprintf (stderr, "Error: could not save the font Surface as \"%s\".\n", ch_filename) ;
  }
  else {
    fprintf (stdout, "Font has been saved as \"%s\".\n", ch_filename) ;
  }
  /* Cleanup */
  SDL_FreeSurface (p_surf) ;
  /* Quit SDL */
  SDL_Quit () ;
  return 1;
}