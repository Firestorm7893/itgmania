#include "global.h"
#include "archutils/Win32/GraphicsWindow.h"
#include "LowLevelWindow_Win32.h"
#include "StepMania.h"
#include "RageUtil.h"
#include "RageLog.h"

static PIXELFORMATDESCRIPTOR g_CurrentPixelFormat;
static HGLRC g_HGLRC = NULL;

void DestroyGraphicsWindowAndOpenGLContext()
{
	if( g_HGLRC != NULL )
	{
		wglMakeCurrent( NULL, NULL );
		wglDeleteContext( g_HGLRC );
		g_HGLRC = NULL;
	}

	ZERO( g_CurrentPixelFormat );

	GraphicsWindow::DestroyGraphicsWindow();
}

void *LowLevelWindow_Win32::GetProcAddress( CString s )
{
	void *pRet = wglGetProcAddress( s );
	if( pRet != NULL )
		return pRet;

	return GetProcAddress( s );
}

LowLevelWindow_Win32::LowLevelWindow_Win32()
{
	ASSERT( g_HGLRC == NULL );

	GraphicsWindow::Initialize();
}

LowLevelWindow_Win32::~LowLevelWindow_Win32()
{
	DestroyGraphicsWindowAndOpenGLContext();
	GraphicsWindow::Shutdown();
}


int ChooseWindowPixelFormat( RageDisplay::VideoModeParams p, PIXELFORMATDESCRIPTOR *PixelFormat )
{
	ASSERT( g_hWndMain != NULL );
	ASSERT( GraphicsWindow::GetHDC() != NULL );

	ZERO( *PixelFormat );
	PixelFormat->nSize			= sizeof(PIXELFORMATDESCRIPTOR);
	PixelFormat->nVersion		= 1;
    PixelFormat->dwFlags			= PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL;
    PixelFormat->iPixelType		= PFD_TYPE_RGBA;
	PixelFormat->cColorBits		= p.bpp == 16? 16:24;
	PixelFormat->cDepthBits		= 16;

	return ChoosePixelFormat( GraphicsWindow::GetHDC(), PixelFormat );
}
void pump(const char *p)
{
	MSG msg;
	while( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) )
	{
		GetMessage( &msg, NULL, 0, 0 );
		LOG->Trace("%s msg: %i -> %i", p, msg.message, msg.hwnd );
		DispatchMessage( &msg );
	}
}


/* This function does not reset the video mode if it fails, because we might be trying
 * yet another video mode, so we'd just thrash the display.  On fatal error,
 * LowLevelWindow_Win32::~LowLevelWindow_Win32 will call Shutdown(). */
CString LowLevelWindow_Win32::TryVideoMode( RageDisplay::VideoModeParams p, bool &bNewDeviceOut )
{
	ASSERT_M( p.bpp == 16 || p.bpp == 32, ssprintf("%i", p.bpp) );

	bNewDeviceOut = false;

	/* We're only allowed to change the pixel format of a window exactly once. */
	bool bCanSetPixelFormat = true;

	/* Do we have an old window? */
	if( g_hWndMain == NULL )
	{
		/* No.  Always create and show the window before changing the video mode.
		 * Otherwise, some other window may have focus, and changing the video mode will
		 * cause that window to be resized. */
		GraphicsWindow::CreateGraphicsWindow( p );
		GraphicsWindow::ConfigureGraphicsWindow( p );
	} else {
		/* We already have a window.  Assume that it's pixel format has already been
		 * set. */
		LOG->Trace("Setting new window, can't reuse old");
		bCanSetPixelFormat = false;
	}
	
	ASSERT( g_hWndMain );

	/* Set the display mode: switch to a fullscreen mode or revert to windowed mode. */
	LOG->Trace("SetScreenMode ...");
	CString sErr = GraphicsWindow::SetScreenMode( p );
	if( !sErr.empty() )
		return sErr;

	PIXELFORMATDESCRIPTOR PixelFormat;
	int iPixelFormat = ChooseWindowPixelFormat( p, &PixelFormat );
	if( iPixelFormat == 0 )
	{
		/* Destroy the window. */
		DestroyGraphicsWindowAndOpenGLContext();
		return "Pixel format not found";
	}

	bool bNeedToSetPixelFormat = false;
	{
		/* We'll need to recreate it if the pixel format is going to change.  We
		 * aren't allowed to change the pixel format twice. */
		PIXELFORMATDESCRIPTOR DestPixelFormat;
		ZERO( DestPixelFormat );
		DescribePixelFormat( GraphicsWindow::GetHDC(), iPixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &DestPixelFormat );
		if( memcmp( &DestPixelFormat, &g_CurrentPixelFormat, sizeof(PIXELFORMATDESCRIPTOR) ) )
		{
			LOG->Trace("Reset: pixel format changing" );
			bNeedToSetPixelFormat = true;
		}
	}

	if( bNeedToSetPixelFormat && !bCanSetPixelFormat )
	{
		/*
		 * The screen mode has changed, so we need to set the pixel format.  If we're
		 * not allowed to do so, destroy the window and make a new one.
		 *
		 * For some reason, if we destroy the old window before creating the new one,
		 * the "fullscreen apps go under the taskbar" glitch will happen when we quit.
		 * We have to create the new window first, for some reason.
		 */
		LOG->Trace( "Mode requires new pixel format, and we've already set one; resetting OpenGL context" );
		if( g_HGLRC != NULL )
		{
			wglMakeCurrent( NULL, NULL );
			wglDeleteContext( g_HGLRC );
			g_HGLRC = NULL;
		}

		GraphicsWindow::RecreateGraphicsWindow(p);
//		DestroyGraphicsWindowAndOpenGLContext();
//		GraphicsWindow::CreateGraphicsWindow( p );
		bNewDeviceOut = true;
	}

	GraphicsWindow::ConfigureGraphicsWindow( p );

	GraphicsWindow::SetVideoModeParams( p );

	if( bNeedToSetPixelFormat )
	{
		/* Set the pixel format. */
		if( !SetPixelFormat(GraphicsWindow::GetHDC(), iPixelFormat, &PixelFormat) )
		{
			/* Destroy the window. */
			DestroyGraphicsWindowAndOpenGLContext();

			return werr_ssprintf( GetLastError(), "Pixel format failed" );
		}

		DescribePixelFormat( GraphicsWindow::GetHDC(), iPixelFormat, sizeof(g_CurrentPixelFormat), &g_CurrentPixelFormat );

		LOG->Info( "Got %i bpp (%i%i%i%i), %i depth",
			g_CurrentPixelFormat.cColorBits, g_CurrentPixelFormat.cRedBits,
			g_CurrentPixelFormat.cBlueBits, g_CurrentPixelFormat.cGreenBits,
			g_CurrentPixelFormat.cAlphaBits, g_CurrentPixelFormat.cDepthBits );
	}

	if( g_HGLRC == NULL )
	{
		g_HGLRC = wglCreateContext( GraphicsWindow::GetHDC() );
		if ( g_HGLRC == NULL )
		{
			DestroyGraphicsWindowAndOpenGLContext();
			return hr_ssprintf( GetLastError(), "wglCreateContext" );
		}

		if( !wglMakeCurrent( GraphicsWindow::GetHDC(), g_HGLRC ) )
		{
			DestroyGraphicsWindowAndOpenGLContext();
			return hr_ssprintf( GetLastError(), "wglCreateContext" );
		}
	}
	return "";	// we set the video mode successfully
}

void LowLevelWindow_Win32::SwapBuffers()
{
	::SwapBuffers( GraphicsWindow::GetHDC() );
}

void LowLevelWindow_Win32::Update( float fDeltaTime )
{
	GraphicsWindow::Update();
}

RageDisplay::VideoModeParams LowLevelWindow_Win32::GetVideoModeParams() const
{
	return GraphicsWindow::GetParams();
}

/*
 * (c) 2004 Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
