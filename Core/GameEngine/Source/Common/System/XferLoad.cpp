/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: XferLoad.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, February 2002
// Desc:   Xfer implementation for loading from disk
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/Snapshot.h"
#include "Common/XferLoad.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferLoad::XferLoad()
{

	m_xferMode = XFER_LOAD;
	m_fileFP = nullptr;
	m_unicodeFormat = UNICODE_FORMAT_UNKNOWN;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
XferLoad::~XferLoad()
{

	// warn the user if a file was left open
	if( m_fileFP != nullptr )
	{

		DEBUG_CRASH(( "Warning: Xfer file '%s' was left open", m_identifier.str() ));
		close();

	}

}

//-------------------------------------------------------------------------------------------------
/** Open file 'identifier' for reading */
//-------------------------------------------------------------------------------------------------
void XferLoad::open( AsciiString identifier )
{

	// sanity, check to see if we're already open
	if( m_fileFP != nullptr )
	{

		DEBUG_CRASH(( "Cannot open file '%s' cause we've already got '%s' open",
									identifier.str(), m_identifier.str() ));
		throw XFER_FILE_ALREADY_OPEN;

	}

	// call base class
	Xfer::open( identifier );

	// the format of the Unicode strings is found again for each file
	m_unicodeFormat = UNICODE_FORMAT_UNKNOWN;

	// open the file
	m_fileFP = fopen( identifier.str(), "rb" );
	if( m_fileFP == nullptr )
	{

		DEBUG_CRASH(( "File '%s' not found", identifier.str() ));
		throw XFER_FILE_NOT_FOUND;

	}

}

//-------------------------------------------------------------------------------------------------
/** Close our current file */
//-------------------------------------------------------------------------------------------------
void XferLoad::close()
{

	// sanity, if we don't have an open file we can do nothing
	if( m_fileFP == nullptr )
	{

		DEBUG_CRASH(( "Xfer close called, but no file was open" ));
		throw XFER_FILE_NOT_OPEN;

	}

	// close the file
	fclose( m_fileFP );
	m_fileFP = nullptr;

	// erase the filename
	m_identifier.clear();

}

//-------------------------------------------------------------------------------------------------
/** Read a block size descriptor from the file at the current position */
//-------------------------------------------------------------------------------------------------
Int XferLoad::beginBlock()
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != nullptr, ("Xfer begin block - file pointer for '%s' is null",
										 m_identifier.str()) );

	// read block size
	XferBlockSize blockSize;
	if( fread( &blockSize, sizeof( XferBlockSize ), 1, m_fileFP ) != 1 )
	{

		DEBUG_CRASH(( "Xfer - Error reading block size for '%s'", m_identifier.str() ));
		return 0;

	}

	// return the block size
	return blockSize;

}

// ------------------------------------------------------------------------------------------------
/** End block ... this does nothing when reading */
// ------------------------------------------------------------------------------------------------
void XferLoad::endBlock()
{

}

//-------------------------------------------------------------------------------------------------
/** Skip forward 'dataSize' bytes in the file */
//-------------------------------------------------------------------------------------------------
void XferLoad::skip( Int dataSize )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != nullptr, ("XferLoad::skip - file pointer for '%s' is null",
										 m_identifier.str()) );

	// sanity
	DEBUG_ASSERTCRASH( dataSize >=0, ("XferLoad::skip - dataSize '%d' must be greater than 0",
										 dataSize) );

	// skip datasize in the file from the current position
	if( fseek( m_fileFP, dataSize, SEEK_CUR ) != 0 )
		throw XFER_SKIP_ERROR;

}

// ------------------------------------------------------------------------------------------------
/** Entry point for xfering a snapshot */
// ------------------------------------------------------------------------------------------------
void XferLoad::xferSnapshot( Snapshot *snapshot )
{

	if( snapshot == nullptr )
	{

		DEBUG_CRASH(( "XferLoad::xferSnapshot - Invalid parameters" ));
		throw XFER_INVALID_PARAMETERS;

	}

	// run the xfer function of the snapshot
	snapshot->xfer( this );

	// add this snapshot to the game state for later post processing if not restricted
	if( BitIsSet( getOptions(), XO_NO_POST_PROCESSING ) == FALSE )
		TheGameState->addPostProcessSnapshot( snapshot );

}

// ------------------------------------------------------------------------------------------------
/** Read string from file and store in ascii string */
// ------------------------------------------------------------------------------------------------
void XferLoad::xferAsciiString( AsciiString *asciiStringData )
{

	// read bytes of string length to follow
	UnsignedByte len;
	xferUnsignedByte( &len );

	// read all the string data
	const Int MAX_XFER_LOAD_STRING_BUFFER = 1024;
	static Char buffer[ MAX_XFER_LOAD_STRING_BUFFER ];

	if( len > 0 )
		xferUser( buffer, sizeof( Byte ) * len );
	buffer[ len ] = 0;  // terminate

	// save into ascii string
	asciiStringData->set( buffer );

}

// ------------------------------------------------------------------------------------------------
/** Read string from file and store in unicode string */
// ------------------------------------------------------------------------------------------------
void XferLoad::xferUnicodeString( UnicodeString *unicodeStringData )
{

	// read bytes of string length to follow
	UnsignedByte len;
	xferUnsignedByte( &len );

	// read all the string data
	const Int MAX_XFER_LOAD_STRING_BUFFER = 1024;
	static WideChar buffer[ MAX_XFER_LOAD_STRING_BUFFER ];

	if( len > 0 && sizeof( WideChar ) != 2 && m_unicodeFormat == UNICODE_FORMAT_UNKNOWN )
	{

		// Find the format from the first string that has characters. In the 4-byte format, the
		// bytes 2 and 3 of the data are the upper half of the first character, so they are zero.
		// In the UTF-16 format, they are the second character (never zero), or, for a string of
		// one character, the first bytes of the next item in the file (the length of the map
		// label, never zero).
		unsigned char probe[ 4 ] = { 0, 0, 0, 0 };
		long position = ftell( m_fileFP );
		size_t probeSize = fread( probe, 1, sizeof( probe ), m_fileFP );
		if( position < 0 || fseek( m_fileFP, position, SEEK_SET ) != 0 )
		{

			DEBUG_CRASH(( "XferLoad - Error reading from file '%s'", m_identifier.str() ));
			throw XFER_READ_ERROR;

		}
		if( probeSize == sizeof( probe ) && (probe[ 2 ] != 0 || probe[ 3 ] != 0) )
			m_unicodeFormat = UNICODE_FORMAT_UTF16;
		else
			m_unicodeFormat = UNICODE_FORMAT_NATIVE;

	}

	if( len > 0 && m_unicodeFormat == UNICODE_FORMAT_UTF16 )
	{

		// the length counts UTF-16 code units, a pair of surrogates makes one character
		UnsignedShort units[ 256 ];
		xferUser( units, sizeof( UnsignedShort ) * len );
		Int count = 0;
		for( Int i = 0; i < len; ++i )
		{

			UnsignedInt character = units[ i ];
			if( character >= 0xD800 && character <= 0xDBFF && i + 1 < len &&
					units[ i + 1 ] >= 0xDC00 && units[ i + 1 ] <= 0xDFFF )
			{
				character = 0x10000 + ((character - 0xD800) << 10) + (units[ i + 1 ] - 0xDC00);
				++i;
			}
			buffer[ count++ ] = (WideChar)character;

		}
		buffer[ count ] = 0;  // terminate

	}
	else
	{

		if( len > 0 )
			xferUser( buffer, sizeof( WideChar ) * len );
		buffer[ len ] = 0;  // terminate

	}

	// save into unicode string
	unicodeStringData->set( buffer );

}

//-------------------------------------------------------------------------------------------------
/** Perform the read operation */
//-------------------------------------------------------------------------------------------------
void XferLoad::xferImplementation( void *data, Int dataSize )
{

	// sanity
	DEBUG_ASSERTCRASH( m_fileFP != nullptr, ("XferLoad - file pointer for '%s' is null",
										 m_identifier.str()) );

	// read data from file
	if( fread( data, dataSize, 1, m_fileFP ) != 1 )
	{

		DEBUG_CRASH(( "XferLoad - Error reading from file '%s'", m_identifier.str() ));
		throw XFER_READ_ERROR;

	}

}

