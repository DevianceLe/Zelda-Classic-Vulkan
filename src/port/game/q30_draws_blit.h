// Screen->DrawBitmapEx, TileBlit and ComboBlit (upstream script_drawing.cpp's do_drawbitmapexr,
// do_tileblit and do_comboblit, ported as they are). Included once, from q30_draws.h.

#ifndef ZC_Q30_DRAWS_BLIT_H
#define ZC_Q30_DRAWS_BLIT_H

static void z30d_drawbitmapexr(BITMAP *bmp, int *sdci, int xoffset, int yoffset)
{
	/*
	//sdci[1]=layer
	//sdci[2]=bitmap
	//sdci[3]=sourcex
	//sdci[4]=sourcey
	//sdci[5]=sourcew
	//sdci[6]=sourceh
	//sdci[7]=destx
	//sdci[8]=desty
	//sdci[9]=destw
	//sdci[10]=desth
	//sdci[11]=rotation/angle
	//scdi[12] = pivot cx
	//sdci[13] = pivot cy
	//scdi[14] = effect flags
	
		
		const int BITDX_NORMAL = 0;
		const int BITDX_TRANS = 1; //Translucent
		const int BITDX_PIVOT = 2; //THe sprite will rotate at a specific point, instead of its center.
		const int BITDX_HFLIP = 4; //Horizontal Flip
		const int BITDX_VFLIP = 8; //Vertical Flip.
		//Note:	Some modes cannot be combined. if a combination is not supported, an error
		//	detailing this will be shown in allegro.log.
		
	//scdi[15] = litcolour
		//The allegro docs are wrong. The params are: rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour); 
		/not rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
	
	//sdci[16]=mask
	
	*/

	int bitmapIndex = sdci[2]/10000;
	int sx = sdci[3]/10000;
	int sy = sdci[4]/10000;
	int sw = sdci[5]/10000;
	int sh = sdci[6]/10000;
	int dx = sdci[7]/10000;
	int dy = sdci[8]/10000;
	int dw = sdci[9]/10000;
	int dh = sdci[10]/10000;
	float rot = sdci[11]/10000;
	int cx = sdci[12]/10000;
	int cy = sdci[13]/10000;
	int mode = sdci[14]/10000;
	int litcolour = sdci[15]/10000;
	bool masked = (sdci[16] != 0);
	
	

	//bugfix
	sx = vbound(sx, 0, 512);
	sy = vbound(sy, 0, 512);
	sw = vbound(sw, 0, 512 - sx); //keep the w/h within range as well
	sh = vbound(sh, 0, 512 - sy);


	if(sx >= ZScriptDrawingRenderTarget::BitmapWidth || sy >= ZScriptDrawingRenderTarget::BitmapHeight)
	return;

	bool stretched = (sw != dw || sh != dh);

	BITMAP *sourceBitmap = zscriptDrawingRenderTarget->GetBitmapPtr(bitmapIndex);
    
	if(!sourceBitmap)
	{
		Z_message("Warning: Screen->DrawBitmap(%d) contains invalid data or is not initialized.\n", bitmapIndex);
		Z_message("[Note* Deferred drawing or layering order possibly not set right.]\n");
		return;
	}
    
	BITMAP* subBmp = 0;
	
	/*
	if ( bitmapIndex == -1 ) {
		blit(bmp, sourceBitmap, sx, sy, 0, 0, dw, dh); 
	}
	*/
    
	if(rot != 0 || mode != 0)    
	{
		subBmp = script_drawing_commands.AquireSubBitmap(dw, dh);
        
		if(!subBmp)
		{
			Z_scripterrlog("bitmap->Blit failed to create a sub-bitmap to use for %s. Aborting.\n", "rotation");
			return;
		}
	}
    
    
	dx = dx + xoffset;
	dy = dy + yoffset;
    
	if(stretched) 
	{
		if(masked) //stretched and masked
		{	
			if ( rot == 0 ) //if not rotated
			{ 
				switch(mode) 
				{
					case 1:
					//transparent
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(bmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(bmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(bmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(bmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(bmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(bmp, subBmp, dx, dy, cx, cy,  degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					masked_stretch_blit(sourceBitmap, bmp, sx, sy, sw, sh, dx, dy, dw, dh);
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
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
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
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
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
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					masked_stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			}
		} //end if stretched and masked 
		
		else  //stretched, not masked
		{
			if ( rot == 0 )  //if not rotated
			{
				switch(mode) {
					case 1:
					//transparent
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(bmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(bmp, subBmp, dx,dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(bmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(bmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(bmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(bmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					stretch_blit(sourceBitmap, bmp, sx, sy, sw, sh, dx, dy, dw, dh);
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
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
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
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_v_flip_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
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
					//stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
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
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_trans_sprite(bmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_trans(bmp, subBmp, dx, dy,  cx, cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_v_flip(bmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_h_flip(bmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_vh_flip(bmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_lit_sprite(bmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					masked_blit(sourceBitmap, bmp, sx, sy, dx, dy, dw, dh);
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
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);	//transparent
					rotate_sprite_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);	//trans + v flip
					rotate_sprite_v_flip_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
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
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
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
					//stretch_blit(sourceBitmap, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					masked_blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			} //end rtated, masked
		} //end if masked

		else //not masked, and not stretched; just blit
		{ 
			
			if ( rot == 0 ) //if not rotated
			{ 
				switch(mode) 
				{
					case 1:
					//transparent
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_trans_sprite(bmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_trans(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_v_flip(bmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_h_flip(bmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and H-Flip.\n", bitmapIndex);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_vh_flip(bmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and VH-Flip.\n", bitmapIndex);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_lit_sprite(bmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_lit(bmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip_lit(bmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot, Flip, and Lit.\n", bitmapIndex);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(bmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					blit(sourceBitmap, bmp, sx, sy, dx, dy, dw, dh);
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
						blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);//transparent
						rotate_sprite_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					 
					break;
					
					case 2: 
						//pivot?
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rotate.\n", bitmapIndex);
					//return an error, cannot both rotate and pivot
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 4: 
						//flip v
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite_v_flip(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
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
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: Screen->DrawBitmap(%d) cannot both Pivot and Rorate.\n", bitmapIndex);
					break;
					
					case 20: 
						//lit + vflip
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
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
					//blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					blit(sourceBitmap, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: Screen->DrawBitmap(%d) mode flags not possible in this combination!\n", bitmapIndex);
				
				}
			} //end if rotated
		} //end if not masked
	} //end if not stretched
    
	//cleanup
	if(subBmp) 
	{
		script_drawing_commands.ReleaseSubBitmap(subBmp); //purge the temporary bitmap.
	}
}

static void z30d_tileblit(BITMAP *bmp, int *sdci, int xoffset, int yoffset, bool is_bmp, char const* funcstr)
{
	/*
	//sdci[1]=layer 
	//sdci[2]=tile
	//sdci[3]=cset
	//sdci[4]=sourcex
	//sdci[5]=sourcey
	//sdci[6]=sourcew
	//sdci[7]=sourceh
	//sdci[8]=destx
	//sdci[9]=desty
	//sdci[10]=destw
	//sdci[11]=desth
	//sdci[12]=rotation/angle
	//scdi[13] = pivot cx
	//sdci[14] = pivot cy
	//scdi[15] = effect flags
	
		// ZScript-side constant values:
		const int BITDX_NORMAL = 0;
		const int BITDX_TRANS = 1; //Translucent
		const int BITDX_PIVOT = 2; //THe sprite will rotate at a specific point, instead of its center.
		const int BITDX_HFLIP = 4; //Horizontal Flip
		const int BITDX_VFLIP = 8; //Vertical Flip.
		//Note:	Some modes cannot be combined. if a combination is not supported, an error
		//	detailing this will be shown in allegro.log.
		
	//scdi[16] = litcolour
		//The allegro docs are wrong. The params are: rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour); 
		//not rotate_sprite_lit(bmp, subBmp, dx, dy, degrees_to_fixed(rot));
	
	//sdci[17]=mask
	//sdci[Z30D_TARGET] Bitmap Pointer
	
	*/
	
	int tile = sdci[2]/10000;
	int cset = Z30D_WRAP_CS(sdci[3]/10000);
	
	int sx = sdci[4]/10000;
	int sy = sdci[5]/10000;
	int sw = sdci[6]/10000;
	//Z_scripterrlog("sh is: %d\n",sdci[5]/10000);
	int sh = sdci[7]/10000;
	//Z_scripterrlog("sh is: %d\n",sdci[6]/10000);
	int dx = sdci[8]/10000;
	int dy = sdci[9]/10000;
	int dw = sdci[10]/10000;
	int dh = sdci[11]/10000;
	float rot = sdci[12]/10000;
	int cx = sdci[13]/10000;
	int cy = sdci[14]/10000;
	int mode = sdci[15]/10000;
	int litcolour = sdci[16]/10000;
	bool masked = (sdci[17] != 0);
	
	int ref = 0;
	
	if ( is_bmp && (sdci[Z30D_TARGET]-10) != -2 && (sdci[Z30D_TARGET]-10) != -1 ) yoffset = 0; //Don't crop.
	//Do we need to also check the render target and do the same thing if the 
		//dest == -2 and the render target is not RT_SCREEN?
	dx += xoffset;
	dy += yoffset;
	
	BITMAP *destbmp = bmp;	// (a bitmap command's own bitmap: q30_scripts_draw.h resolved it)
	(void)ref;
	
	bool stretched = (sw != dw || sh != dh);
	
	if(sw <= 0 || sh <= 0)
		return;	// (upstream would make no bitmap and draw on none)
	BITMAP* srcbmp = create_bitmap_ex(8, sw, sh);
	if(!srcbmp)
		return;
	//Draw tiles to srcbmp
	{
		clear_bitmap(srcbmp);
		int tx = 0, ty = 0;
		if(sx < 0)
			tx = (sx-15)/16;
		else if(sx > 15)
			tx = sx/16;
		if(sy < 0)
			ty = (sy-15)/16;
		else if(sy > 15)
			ty = sy/16;
		
		int gxoff = -z30d_wrap(sx,0,15), gyoff = -z30d_wrap(sy,0,15);
		for(int ix = 0; ix <= sw; ix += 16)
		{
			for(int iy = 0; iy <= sh; iy += 16)
			{
				int t = tile+(tx+ix/16);
				int rowdiff = TILEROW(t) - TILEROW(tile);
				t += rowdiff * (sh/16) * TILES_PER_ROW;
				t += (ty+iy/16)*TILES_PER_ROW;
				overtile16(srcbmp, t, ix+gxoff, iy+gyoff, cset, 0);
			}
		}
		
		sx = sy = 0;
	}
    
	BITMAP* subBmp = nullptr;
    
	if(rot != 0 || mode != 0)    
	{
		subBmp = create_bitmap_ex(8,destbmp->w, destbmp->h);//script_drawing_commands.AquireSubBitmap(dw, dh);
		clear_bitmap(subBmp);
        
		if(!subBmp)
		{
			Z_scripterrlog("%s failed to create a sub-bitmap to use for %s. Aborting.\n", funcstr, "rotation");
			return;
		}
	}
    
	//dx = dx + xoffset; //don't do this here!
	//dy = dy + yoffset; //Nor this. It auto-offsets the bitmap by +56. Hmm. The fix that gleeok made isn't being applied to these functions. -Z ( 17th April, 2019 )
    
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
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(destbmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(destbmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(destbmp, subBmp, dx, dy, cx, cy,  degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot, Flip, and Lit.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					masked_stretch_blit(srcbmp, destbmp, sx, sy, sw, sh, dx, dy, dw, dh);
					break;
					
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 ) //if rotated
			{ 
				switch(mode)
				{
					case 1: 
						//transparent
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 4: 
						//flip v
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: %s cannot Rotate and Flip a Trans Sprite.\n", funcstr);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 20: 
						//lit + vflip
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot and Flip a Lit Sprite.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					masked_stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
				
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
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_trans_sprite(destbmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_trans(destbmp, subBmp, dx,dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_v_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_h_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_vh_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_lit_sprite(destbmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot, Flip, and Lit.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					stretch_blit(srcbmp, destbmp, sx, sy, sw, sh, dx, dy, dw, dh);
					break;
					
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 4: 
						//flip v
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: %s cannot Rotate and Flip a Trans Sprite.\n", funcstr);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 20: 
						//lit + vflip
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);//transparent
					rotate_sprite_v_flip_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot and Flip a Lit Sprite.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					rotate_sprite(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
				
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
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_trans_sprite(destbmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_trans(destbmp, subBmp, dx, dy,  cx, cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_v_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_h_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_vh_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_lit_sprite(destbmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite_v_flip_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot, Flip, and Lit.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					masked_blit(srcbmp, destbmp, sx, sy, dx, dy, dw, dh);
					break;
					
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);	//transparent
					rotate_sprite_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					
					break;
					
					case 2: 
						//pivot?
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 4: 
						//flip v
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);	//trans + v flip
					rotate_sprite_v_flip_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: %s cannot Rotate and Flip a Trans Sprite.\n", funcstr);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 20: 
						//lit + vflip
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot and Flip a Lit Sprite.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//stretch_blit(srcbmp, subBmp, sx, sy, sw, sh, 0, 0, dw, dh);
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					masked_blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
				
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
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_trans_sprite(destbmp, subBmp, dx, dy);
					break;
					
					
					case 2: 
						//pivot?
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_trans(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot));
					break;
					
					case 4: 
						//flip v
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_v_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 5: 
						//trans + v flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_V_FLIP);
					break;
					
					case 6: 
						//pivot + v flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					break;
					
					case 8: 
						//vlip h
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_h_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 9: 
						//trans + h flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_H_FLIP);
					break;
					
					case 10: 
						//flip H and pivot
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					//return error cannot pivot and h flip
					break;
					
					case 12:
						//vh flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_vh_flip(destbmp, subBmp, dx, dy);
					break;
					
					case 13: 
						//trans + vh flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_TRANS, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_lit_sprite(destbmp, subBmp, dx, dy, litcolour);
					break;
					
					case 18: 
						//pivot, lit
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_lit(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 20: 
						//lit + v flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_V_FLIP);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite_v_flip_lit(destbmp, subBmp, dx, dy,  cx,  cy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 24: 
						//lit + h flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_H_FLIP);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot, Flip, and Lit.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					draw_sprite_ex(destbmp, subBmp, dx, dy, DRAW_SPRITE_LIT, DRAW_SPRITE_VH_FLIP);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect
					blit(srcbmp, destbmp, sx, sy, dx, dy, dw, dh);
					break;
					
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
					
					
				}
			} //end if not rotated
			
			if ( rot != 0 )  //if rotated
			{
				switch(mode)
				{
					case 1: 
						blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);//transparent
					rotate_sprite_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					 
					break;
					
					case 2: 
						//pivot?
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					pivot_sprite(destbmp, subBmp, dx, dy, cx, cy, degrees_to_fixed(rot));
					//Pivoting requires two more args
					break;
					
					case 3: 
						//pivot + trans
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 4: 
						//flip v
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite_v_flip(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 5: 
						//trans + v flip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_trans(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					case 6: 
						//pivot + v flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return an error, cannot both rotate and pivot
					break;
					
					case 8: 
						//flip h
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					//return an error, cannot both rotate and flip H
					break;
					
					case 9: 
						//trans + h flip
					Z_message("Warning: %s cannot Rotate and Flip a Trans Sprite.\n", funcstr);
					//return an error, cannot rotate and flip a trans sprite
					break;
					
					case 10: 
						//flip H and pivot
					//return error cannot pivot and h flip
					Z_message("Warning: %s cannot both Pivot and H-Flip.\n", funcstr);
					break;
					
					case 12: 
						//vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 13: 
						//trans + vh flip
					//return an error, cannot rotate and VH flip a trans sprite
					Z_message("Warning: %s cannot both Rotate and VH-Flip.\n", funcstr);
					break;
					
					case 14: 
						//pivot and vh flip
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					//return error cannot both pivot and vh flip
					break;
					
					case 16: 
						//lit
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 18: 
						//pivot, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 20: 
						//lit + vflip
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh);
					rotate_sprite_v_flip_lit(destbmp, subBmp, dx, dy, degrees_to_fixed(rot),litcolour);
					break;
					
					case 22: 
						//Pivot, vflip, lit
					//return an error, cannot both rotate and pivot
					Z_message("Warning: %s cannot both Pivot and Rotate.\n", funcstr);
					break;
					
					case 24: 
						//lit + h flip
					//return an error, cannot both rotate and H flip
					Z_message("Warning: %s cannot both Rotate and H-Flip.\n", funcstr);
					break;
					
					case 26: 
						//pivot + lit + hflip
					Z_message("Warning: %s cannot both Pivot and Flip a Lit Sprite.\n", funcstr);
					//return error cannot pivot, lit, and flip
					break;
					
					case 28: 
						//lit + vh flip
					//return an error, cannot both rotate and VH flip
					Z_message("Warning: %s cannot both Pivot and VH-Flip.\n", funcstr);
					break;
					
					case 32: //gouraud
						//Probably not wort supporting. 
					//blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					//draw_gouraud_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int c1, int c2, int c3, int c4);
					break;
					
					case 0: 
						//no effect.
					blit(srcbmp, subBmp, sx, sy, 0, 0, dw, dh); 
					rotate_sprite(destbmp, subBmp, dx, dy, degrees_to_fixed(rot));
					break;
					
					default:
						return Z_message("Warning: %s mode flags not possible in this combination!\n", funcstr);
				
				}
			} //end if rotated
		} //end if not masked
	} //end if not stretched
    
	//cleanup
	if(subBmp) 
	{
		//script_drawing_commands.ReleaseSubBitmap(subBmp); //purge the temporary bitmap.
		destroy_bitmap(subBmp);
	}
	destroy_bitmap(srcbmp);
}

static void z30d_comboblit(BITMAP *bmp, int *sdci, int xoffset, int yoffset, bool is_bmp)
{
	//sdci[2]: combo -> tile
	int cid = sdci[2]/10000;
	if(unsigned(cid) >= MAXCOMBOS)
	{
		Z_scripterrlog("ComboBlit tried to draw invalid combo id '%d'\n", cid);
		return;
	}
	sdci[2] = combobuf[cid].tile * 10000;
	z30d_tileblit(bmp, sdci, xoffset, yoffset, is_bmp, "ComboBlit()");
}

#endif
