/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
 * gl_surf.c -- surface post-processing code
 * 
 */
#include "gl_local.h"

lightmapBuilder_t lm;

static cvar_t   *gl_coloredlightmaps;
static cvar_t   *gl_brightness;
static cvar_t   *gl_modulate_mask;

static float colorScale, colorAdj;

/*
=============================================================================

LIGHTMAP COLOR ADJUSTING

=============================================================================
*/

void GL_AdjustColor( byte *dst, const byte *src, int what ) {
    float r, g, b, min, max, mid;

    r = src[0];
    g = src[1];
    b = src[2];

    if( colorScale != 1.0f ) {
        min = max = r;
        if ( g < min ) min = g;
        if ( b < min ) min = b;
        if ( g > max ) max = g;
        if ( b > max ) max = b;
        mid = 0.5 * ( min + max );
        r = mid + ( r - mid ) * colorScale;
        g = mid + ( g - mid ) * colorScale;
        b = mid + ( b - mid ) * colorScale;
    }

    if( gl_modulate_mask->integer & what ) {
        r *= gl_modulate->value;
        g *= gl_modulate->value;
        b *= gl_modulate->value;
    } 

    max = g;
    if( r > max ) {
        max = r;
    }
    if( b > max ) {
        max = b;
    }

    if( max > 255 ) {
        r *= 255.0f / max;
        g *= 255.0f / max;
        b *= 255.0f / max;
    }

    // atu brightness adjustments
    r += colorAdj;
    g += colorAdj;
    b += colorAdj;

    dst[0] = clamp( r, 0, 255 );
    dst[1] = clamp( g, 0, 255 );
    dst[2] = clamp( b, 0, 255 );
}

/*
=============================================================================

LIGHTMAPS BUILDING

=============================================================================
*/

static qboolean LM_AllocBlock( int w, int h, int *s, int *t ) {
    int i, j;
    int x, y, maxInuse, minInuse;

	x = 0; y = LM_BLOCK_HEIGHT;
    minInuse = LM_BLOCK_HEIGHT;
    for( i = 0; i < LM_BLOCK_WIDTH - w; i++ ) {
        maxInuse = 0;
        for( j = 0; j < w; j++ ) {
            if( lm.inuse[ i + j ] >= minInuse ) {
                break;
            }
            if( maxInuse < lm.inuse[ i + j ] ) {
                maxInuse = lm.inuse[ i + j ];
            }
        }
        if( j == w ) {
            x = i;
            y = minInuse = maxInuse;
        }
    }

    if( y + h > LM_BLOCK_HEIGHT ) {
        return qfalse;
    }
    
    for( i = 0; i < w; i++ ) {
        lm.inuse[ x + i ] = y + h;
    }

    *s = x;
    *t = y;
    return qtrue;
}

static void LM_InitBlock( void ) {
    int i;
    
    for( i = 0; i < LM_BLOCK_WIDTH; i++ ) {
        lm.inuse[i] = 0;
    }
}

static void LM_UploadBlock( void ) {
    int comp = colorScale ? GL_RGB : GL_LUMINANCE;

    qglActiveTextureARB( GL_TEXTURE1_ARB );
    qglBindTexture( GL_TEXTURE_2D, LM_TEXNUM + lm.numMaps );
	qglTexImage2D( GL_TEXTURE_2D, 0, comp, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    qglActiveTextureARB( GL_TEXTURE0_ARB );

    if( lm.highWater < ++lm.numMaps ) {
        lm.highWater = lm.numMaps;
    }
}

static void LM_BuildSurfaceLightmap( mface_t *surf, vec_t *vbo ) {
    byte *ptr, *dst, *src;
    int i, j;
    int smax, tmax, s, t;
    
    smax = ( surf->extents[0] >> 4 ) + 1;
    tmax = ( surf->extents[1] >> 4 ) + 1;

    if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
        LM_UploadBlock();
        if( lm.numMaps == LM_MAX_LIGHTMAPS ) {
            Com_Error( ERR_DROP, "%s: LM_MAX_LIGHTMAPS exceeded\n", __func__ );
        }
        LM_InitBlock();
        if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
            Com_Error( ERR_DROP, "%s: LM_AllocBlock( %d, %d ) failed\n",
                __func__, smax, tmax );
        }
    }
    
    src = surf->lightmap;
    dst = &lm.buffer[ ( t * LM_BLOCK_WIDTH + s ) << 2 ];

    for( i = 0; i < tmax; i++ ) {
        ptr = dst;
        for( j = 0; j < smax; j++ ) {
            GL_AdjustColor( ptr, src, 1 );
            ptr[3] = 255;

            src += 3; ptr += 4;
        }
        dst += LM_BLOCK_WIDTH * 4;
    }
    
    surf->texnum[1] = LM_TEXNUM + lm.numMaps;

    s = ( s << 4 ) + 8;
    t = ( t << 4 ) + 8;

    s -= surf->texturemins[0]; 
    t -= surf->texturemins[1];

    for( i = 0; i < surf->numsurfedges; i++ ) {
        vbo[5] += s;
        vbo[6] += t;
        vbo[5] /= LM_BLOCK_WIDTH * 16;
        vbo[6] /= LM_BLOCK_HEIGHT * 16;
        vbo += VERTEX_SIZE;
    }
}

/*
=============================================================================

POLYGONS BUILDING

=============================================================================
*/

static void GL_BuildSurfacePoly( bsp_t *bsp, mface_t *surf, vec_t *vbo ) {
	msurfedge_t *src_surfedge;
	mvertex_t *src_vert;
	medge_t *src_edge;
	mtexinfo_t *texinfo = surf->texinfo;
    int i;
    vec2_t scale, tc, mins, maxs;
    int bmins[2], bmaxs[2];
	
    surf->texnum[0] = texinfo->image->texnum;

    // normalize texture coordinates
    scale[0] = 1.0f / texinfo->image->width;
    scale[1] = 1.0f / texinfo->image->height;

    if( ( texinfo->c.flags & SURF_WARP ) && qglProgramStringARB ) {
        surf->texnum[1] = r_warptexture->texnum;
        scale[0] *= 0.5f;
        scale[1] *= 0.5f;
    }

	mins[0] = mins[1] = 99999;
	maxs[0] = maxs[1] = -99999;

	src_surfedge = surf->firstsurfedge;
	for( i = 0; i < surf->numsurfedges; i++ ) {
		src_edge = src_surfedge->edge;
		src_vert = src_edge->v[src_surfedge->vert];
        src_surfedge++;

        // vertex coordinates
		VectorCopy( src_vert->point, vbo );
		
		// texture0 coordinates
		tc[0] = DotProduct( vbo, texinfo->axis[0] ) + texinfo->offset[0];
		tc[1] = DotProduct( vbo, texinfo->axis[1] ) + texinfo->offset[1];

		if( mins[0] > tc[0] ) mins[0] = tc[0];
		if( maxs[0] < tc[0] ) maxs[0] = tc[0];
		
		if( mins[1] > tc[1] ) mins[1] = tc[1];
		if( maxs[1] < tc[1] ) maxs[1] = tc[1];

        vbo[3] = tc[0] * scale[0];
        vbo[4] = tc[1] * scale[1];

        // texture1 coordinates
        if( ( texinfo->c.flags & SURF_WARP ) && qglProgramStringARB ) {
            vbo[5] = vbo[3];
            vbo[6] = vbo[4];
        } else {
            vbo[5] = tc[0];
            vbo[6] = tc[1];
        }

		vbo += VERTEX_SIZE;
	}

    // calculate surface extents
    bmins[0] = floor( mins[0] / 16 );
    bmins[1] = floor( mins[1] / 16 );
    bmaxs[0] = ceil( maxs[0] / 16 );
    bmaxs[1] = ceil( maxs[1] / 16 );

	surf->texturemins[0] = bmins[0] << 4;
	surf->texturemins[1] = bmins[1] << 4;

	surf->extents[0] = ( bmaxs[0] - bmins[0] ) << 4;
	surf->extents[1] = ( bmaxs[1] - bmins[1] ) << 4;
}

void GL_FreeWorld( void ) {
    if( !gl_static.world.cache ) {
        return;
    }

    BSP_Free( gl_static.world.cache );

    if( gl_static.world.vertices ) {
        Hunk_Free( &gl_static.world.pool );
    } else if( qglDeleteBuffersARB ) {
        GLuint buf = 1;

        qglDeleteBuffersARB( 1, &buf );
    }

    lm.numMaps = 0;
    LM_InitBlock();
    
    memset( &gl_static.world, 0, sizeof( gl_static.world ) );
}
    
void GL_LoadWorld( const char *name ) {
    char buffer[MAX_QPATH];
    mface_t *surf;
    int i, size, count;
    vec_t *vbo;
    bsp_t *bsp;
    mtexinfo_t *info;
    image_t *image;

    if( !( bsp = BSP_Load( name ) ) ) {
        Com_Error( ERR_DROP, "%s: couldn't load %s: %s",
            __func__, name, BSP_GetError() );
    }

	// check if the required world model was already loaded
    if( gl_static.world.cache == bsp ) {
        for( i = 0; i < bsp->numtexinfo; i++ ) {
            bsp->texinfo[i].image->registration_sequence = registration_sequence;
        }
	    for( i = 0; i < bsp->numnodes; i++ ) {
            bsp->nodes[i].visframe = 0;
        }
	    for( i = 0; i < bsp->numleafs; i++ ) {
            bsp->leafs[i].visframe = 0;
        }
		Com_DPrintf( "%s: reused old world model\n", __func__ );
        bsp->refcount--;
        return;
    }

	gl_coloredlightmaps = Cvar_Get( "gl_coloredlightmaps", "1", CVAR_ARCHIVE|CVAR_FILES );
	gl_brightness = Cvar_Get( "gl_brightness", "0", CVAR_ARCHIVE|CVAR_FILES );
	gl_modulate_mask = Cvar_Get( "gl_modulate_mask", "3", CVAR_FILES );

	colorScale = Cvar_ClampValue( gl_coloredlightmaps, 0, 1 );
	colorAdj = 255 * Cvar_ClampValue( gl_brightness, -1, 1 );

    // free previous model, if any
    GL_FreeWorld();

    gl_static.world.cache = bsp;

    // registers all texinfo
    for( i = 0, info = bsp->texinfo; i < bsp->numtexinfo; i++, info++ ) {
		Q_concat( buffer, sizeof( buffer ), "textures/", info->name, ".wal", NULL );
        image = IMG_Find( buffer, it_wall );
        info->image = image ? image : r_notexture;
    }

    // calculate vertex buffer size in bytes
    count = 0;
    for( i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++ ) {
        count += surf->numsurfedges;
    }
    size = count * VERTEX_SIZE * 4;

    vbo = NULL;
    if( qglBindBufferARB ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 1 );
        
        qglBufferDataARB( GL_ARRAY_BUFFER_ARB, size, NULL, GL_STATIC_DRAW_ARB );

        GL_ShowErrors( __func__ );

        vbo = qglMapBufferARB( GL_ARRAY_BUFFER_ARB, GL_READ_WRITE_ARB );
        if( vbo ) {
            gl_static.world.vertices = NULL;
            Com_DPrintf( "%s: %d bytes of vertex data as VBO\n", __func__, size );
        } else {
            Com_EPrintf( "Failed to map VBO data in client memory\n" );
        }
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
    }
    
    if( !vbo ) {
        Hunk_Begin( &gl_static.world.pool, size );
        vbo = Hunk_Alloc( &gl_static.world.pool, size );
        Hunk_End( &gl_static.world.pool );

        Com_DPrintf( "%s: %d bytes of vertex data on hunk\n", __func__, size );
        gl_static.world.vertices = vbo;
    }

    // post process all surfaces
    count = 0;
    for( i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++ ) {
        if( surf->texinfo->c.flags & SURF_SKY ) {
            continue;
        }
        surf->firstvert = count;
        GL_BuildSurfacePoly( bsp, surf, vbo );

        if( surf->lightmap && !gl_fullbright->integer &&
            !( surf->texinfo->c.flags & SURF_NOLM_MASK ) )
        {
            LM_BuildSurfaceLightmap( surf, vbo );
        }

        count += surf->numsurfedges;
        vbo += surf->numsurfedges * VERTEX_SIZE;
    }
    
    if( qglBindBufferARB && !gl_static.world.vertices ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 1 );
        if( !qglUnmapBufferARB( GL_ARRAY_BUFFER_ARB ) ) {
            Com_Error( ERR_DROP, "Failed to unmap VBO data" );
        }
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
    }

    // upload the last lightmap
    for( i = 0; i < LM_BLOCK_WIDTH; i++ ) {
        if( lm.inuse[i] ) {
            LM_UploadBlock();
            break;
        }
    }

    Com_DPrintf( "%s: %d lightmaps built\n", __func__, lm.numMaps );
}

