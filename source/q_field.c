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

//
// field.c
//

#include "com_local.h"
#include "key_public.h"
#include "ref_public.h"
#include "vid_public.h"
#include "q_field.h"

/*
================
IF_Init
================
*/
void IF_Init( inputField_t *field, size_t visibleChars, size_t maxChars, const char *text ) {
	memset( field, 0, sizeof( *field ) );

	field->maxChars = clamp( maxChars, 1, sizeof( field->text ) );
	field->visibleChars = clamp( visibleChars, 1, maxChars );
	if( text ) {
		field->cursorPos = Q_strncpyz( field->text, text, maxChars );
	}
}

/*
================
IF_Clear
================
*/
void IF_Clear( inputField_t *field ) {
	memset( field->text, 0, sizeof( field->text ) );
	field->cursorPos = 0;
}

/*
================
IF_Replace
================
*/
void IF_Replace( inputField_t *field, const char *text ) {
	field->cursorPos = Q_strncpyz( field->text, text, sizeof( field->text ) );
}

#if USE_CLIENT

/*
================
IF_KeyEvent
================
*/
qboolean IF_KeyEvent( inputField_t *field, int key ) {
	if( key == K_DEL ) {
		if( field->text[field->cursorPos] ) {
			memmove( field->text + field->cursorPos,
                field->text + field->cursorPos + 1,
                sizeof( field->text ) - field->cursorPos );
		}
		return qtrue;
	}
	
	if( key == K_BACKSPACE || ( key == 'h' && Key_IsDown( K_CTRL ) ) ) {
		if( field->cursorPos > 0 ) {
			memmove( field->text + field->cursorPos - 1,
                field->text + field->cursorPos,
                sizeof( field->text ) - field->cursorPos );
			field->cursorPos--;
		}
		return qtrue;
	}

    if( key == 'w' && Key_IsDown( K_CTRL ) ) {
        size_t oldpos = field->cursorPos;

        // kill trailing whitespace
		while( field->cursorPos > 0 && field->text[field->cursorPos] <= 32 ) {
			field->cursorPos--;
        }

        // kill this word
		while( field->cursorPos > 0 && field->text[ field->cursorPos - 1 ] > 32 ) {
			field->cursorPos--;
        }
		memmove( field->text + field->cursorPos, field->text + oldpos,
            sizeof( field->text ) - oldpos );
        return qtrue;
    }

    if( key == 'u' && Key_IsDown( K_CTRL ) ) {
		memmove( field->text, field->text + field->cursorPos,
            sizeof( field->text ) - field->cursorPos );
        field->cursorPos = 0;
        return qtrue;
    }

    if( key == 'k' && Key_IsDown( K_CTRL ) ) {
        field->text[field->cursorPos] = 0;
        return qtrue;
    }

    if( key == 'c' && Key_IsDown( K_CTRL ) ) {
        VID_SetClipboardData( field->text );
        return qtrue;
    }
	
	if( key == K_LEFTARROW || ( key == 'b' && Key_IsDown( K_CTRL ) ) ) {
		if( field->cursorPos > 0 ) {
			field->cursorPos--;
		}
		return qtrue;
	}

	if( key == K_RIGHTARROW || ( key == 'f' && Key_IsDown( K_CTRL ) ) ) {
		if( field->text[field->cursorPos] ) {
			field->cursorPos++;
		}
		return qtrue;
	}

	if( key == 'b' && Key_IsDown( K_ALT ) ) {
		if( field->cursorPos > 0 && field->text[field->cursorPos - 1] <= 32 ) {
			field->cursorPos--;
		}
		while( field->cursorPos > 0 && field->text[field->cursorPos] <= 32 ) {
			field->cursorPos--;
		}
		while( field->cursorPos > 0 && field->text[field->cursorPos - 1] > 32 ) {
			field->cursorPos--;
		}
		return qtrue;
	}

	if( key == 'f' && Key_IsDown( K_ALT ) ) {
		while( field->text[field->cursorPos] && field->text[field->cursorPos] <= 32 ) {
			field->cursorPos++;
		}
		while( field->text[field->cursorPos] > 32 ) {
			field->cursorPos++;
		}
		return qtrue;
	}

	if( key == K_HOME || ( key == 'a' && Key_IsDown( K_CTRL ) ) ) {
		field->cursorPos = 0;
		return qtrue;
	}

	if( key == K_END || ( key == 'e' && Key_IsDown( K_CTRL ) ) ) {
		field->cursorPos = strlen( field->text );
		return qtrue;
	}

	if( key == K_INS ) {
		Key_SetOverstrikeMode( Key_GetOverstrikeMode() ^ 1 );
		return qtrue;
	}

	return qfalse;
}

/*
================
IF_CharEvent
================
*/
qboolean IF_CharEvent( inputField_t *field, int key ) {
	if( key < 32 || key > 127 ) {
		return qfalse;	// non printable
	}

	if( field->cursorPos >= field->maxChars - 1 ) {
		// buffer limit was reached, just replace the last character
		field->text[field->cursorPos] = key;
		return qtrue;
	}

	if( Key_GetOverstrikeMode() ) {
		// replace the character at cursor and advance
		field->text[field->cursorPos] = key;
		field->cursorPos++;
		return qtrue;
	}

	// insert new character at cursor position
	memmove( field->text + field->cursorPos + 1, field->text + field->cursorPos,
        sizeof( field->text ) - field->cursorPos - 1 );
	field->text[field->cursorPos] = key;
	field->cursorPos++;

	return qtrue;
}

/*
================
IF_Draw

The input line scrolls horizontally if typing goes beyond the right edge.
Returns x offset of the rightmost character drawn.
================
*/
int IF_Draw( inputField_t *field, int x, int y, int flags, qhandle_t font ) {
	char *text = field->text;
	size_t cursorPos = field->cursorPos;
    size_t offset = 0;
	int ret;

	if( cursorPos >= sizeof( field->text ) ) {
		Com_Error( ERR_FATAL, "IF_Draw: bad field->cursorPos" );
	}

	// scroll horizontally
	if( cursorPos > field->visibleChars - 1 ) {
		cursorPos = field->visibleChars - 1;
		offset = field->cursorPos - cursorPos;
	}
    
	// draw text
	ret = R_DrawString( x, y, flags, field->visibleChars, text + offset, font );

	if( flags & UI_DRAWCURSOR ) {
    // draw blinking cursor
        if( ( com_localTime >> 8 ) & 1 ) {
            int c = Key_GetOverstrikeMode() ? 11 : '_';
            R_DrawChar( x + cursorPos * CHAR_WIDTH, y, flags, c, font );
        }
    }

    return ret;
}

#endif
