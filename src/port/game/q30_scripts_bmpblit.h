// bitmap->Blit with rotation, a pivot, flips or a lit colour onto the screen: upstream
// script_drawing.cpp's bmp_do_drawbitmapexr from "if(stretched)" to its end, ported as it is (its
// sbmp, the copied source rectangle here, and subBmp, made the size of the whole source bitmap as
// upstream's is). Included once, from q30_scripts_cmds.h; z30_queue_screen_blit and
// zasm30_do_screen_blit use it.

#ifndef ZC_Q30_SCRIPTS_BMPBLIT_H
#define ZC_Q30_SCRIPTS_BMPBLIT_H

static inline fixed z30b_degrees_to_fixed(float d)
{
	return ftofix(d * 0.7111111111111);     // (upstream drawing.h's DegtoFix)
}

static void z30b_blit_body(BITMAP *destBMP, BITMAP *sbmp, BITMAP *subBmp, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                           int32_t dx, int32_t dy, int32_t dw, int32_t dh, float rot, int32_t cx, int32_t cy, int32_t mode,
                           int32_t litcolour, bool masked)
{
	const int32_t bitmapIndex = -1;     // (upstream's messages name the target)
	bool stretched = (sw != dw || sh != dh);

	if(stretched) 
	{
		if(masked) 
		{	//stretched and masked
			if ( rot == 0 ) 
			{ //if not rotated
				switch(mode) 
				{
					case 1:
					//transparent
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(destBMP, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(destBMP, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(destBMP, subBmp, dx, dy, cx, cy,  z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect
					masked_stretch_blit(sbmp, destBMP, sx, sy, sw, sh, dx, dy, dw, dh);
					break;
					
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 ) //if rotated
			{ 
				switch(mode)
				{
					case 1: 
						//transparent
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rorate and H-Flip.\n", bitmapIndex);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot Rotate and Flip a Trans Sprite.\n", bitmapIndex);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and H-Flip.\n", bitmapIndex);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Flip a Lit Sprite.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect.
					masked_stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			}
		} //end if stretched and masked 
		
		else  //stretched, not masked
		{
			
		
			if ( rot == 0 ) //if not rotated
			{
				switch(mode) 
				{
					case 1:
					//transparent
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(destBMP, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(destBMP, subBmp, dx,dy,  cx,  cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(destBMP, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect
					stretch_blit(sbmp, destBMP, sx, sy, sw, sh, dx, dy, dw, dh);
					break;
					
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rorate and H-Flip.\n", bitmapIndex);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot Rotate and Flip a Trans Sprite.\n", bitmapIndex);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_v_flip_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and H-Flip.\n", bitmapIndex);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Flip a Lit Sprite.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect.
					stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			}
			
		} //end if stretched, but not masked
	}
	else //not stretched
	{ 
		
		if(masked) //if masked, but not stretched
		{ 
			
			if ( rot == 0 ) //if not rotated
			{ 
				switch(mode) 
				{
					case 1:
					//transparent
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_trans_sprite(destBMP, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_trans(destBMP, subBmp, dx, dy,  cx, cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_v_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_h_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_vh_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_lit_sprite(destBMP, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect
					masked_blit(sbmp, destBMP, sx, sy, dx, dy, dw, dh);
					break;
					
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);	//transparent
					rotate_sprite_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 5: 
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);	//trans + v flip
					rotate_sprite_v_flip_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rorate and H-Flip.\n", bitmapIndex);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot Rotate and Flip a Trans Sprite.\n", bitmapIndex);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and H-Flip.\n", bitmapIndex);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Flip a Lit Sprite.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect.
					masked_blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			} //end rtated, masked
		} //end if masked

		else  //not masked, and not stretched; just blit
		{
			
			if ( rot == 0 ) //if not rotated
			{ 
				switch(mode) 
				{
					case 1:
					//transparent
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_trans_sprite(destBMP, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_trans(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_v_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_h_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_vh_flip(destBMP, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_lit_sprite(destBMP, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_lit(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip_lit(destBMP, subBmp, dx, dy,  cx,  cy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destBMP, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect
					blit(sbmp, destBMP, sx, sy, dx, dy, dw, dh);
					break;
					
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
						blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);//transparent
					rotate_sprite_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					 
					break;
					
					case 2: 
						//pivot?
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite(destBMP, subBmp, dx, dy, cx, cy, z30b_degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite_v_flip(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rorate and H-Flip.\n", bitmapIndex);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot Rotate and Flip a Trans Sprite.\n", bitmapIndex);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and VH-Flip.\n", bitmapIndex);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Rotate and H-Flip.\n", bitmapIndex);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Flip a Lit Sprite.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int32_t x, int32_t y, int32_t c1, int32_t c2, int32_t c3, int32_t c4);
					break;
					
					case 0: 
						//no effect.
					blit(sbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite(destBMP, subBmp, dx, dy, z30b_degrees_to_fixed(rot));
					break;
					
					default:
						
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			} //end if rotated
		} //end if not masked
	} //end if not stretched
}

// sbmp holds the source rectangle at sx, sy; subw x subh is upstream's subBmp, the source bitmap's size.
static void z30b_blit_rotated(BITMAP *destBMP, BITMAP *sbmp, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh,
                              float rot, int cx, int cy, int mode, int litcolour, bool masked, int subw, int subh)
{
	if(sw <= 0 || sh <= 0)
		return;

	BITMAP *subBmp = NULL;

	if(rot != 0 || mode != 0)
	{
		subBmp = create_bitmap_ex(8, vbound(subw, 1, 4096), vbound(subh, 1, 4096));

		if(!subBmp)
		{
			Z_scripterrlog("bitmap->Blit failed to create a sub-bitmap to use for %s. Aborting.\n", "rotation");
			return;
		}

		clear_bitmap(subBmp);
	}

	z30b_blit_body(destBMP, sbmp, subBmp, sx, sy, sw, sh, dx, dy, dw, dh, rot, cx, cy, mode, litcolour, masked);

	if(subBmp)
		destroy_bitmap(subBmp);
}

#endif
