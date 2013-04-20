//------------------------------------------------------------------------------------------------
// File:    ngsclient.cpp
//
// Desc:    User-end connection to the server
//
//  Copyright 2006-2010 Karl Gluck. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are
//  permitted provided that the following conditions are met:
//
//     1. Redistributions of source code must retain the above copyright notice, this list of
//        conditions and the following disclaimer.
//
//     2. Redistributions in binary form must reproduce the above copyright notice, this list
//        of conditions and the following disclaimer in the documentation and/or other materials
//        provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY KARL GLUCK ``AS IS'' AND ANY EXPRESS OR IMPLIED
//  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KARL GLUCK OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
//  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
//  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  The views and conclusions contained in the software and documentation are those of the
//  authors and should not be interpreted as representing official policies, either expressed
//  or implied, of Karl Gluck.
//------------------------------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN         // Get rid of rarely-used stuff from Windows headers
#define DIRECTINPUT_VERSION 0x800   // Tell DirectInput what version to use

// Link header files required to compile the project
#include <winsock2.h>   // Basic networking library
#include <windows.h>    // Standard Windows header
#include <commctrl.h>   // Common controls for Windows (needed for the IP address control)
#include <dinput.h>     // Include DirectInput information
#include <d3dx9.h>      // Extended functions for managing Direct3D
#include <d3d9.h>       // Basic Direct3D functionality
#include <iostream>     // Used for error reporting
#include "animation.h"  // Controls animated X models
#include "resource.h"   // Icon
#include <stdio.h>

// Constants used in the program
#define DECRYPT_STREAM_SIZE 512                             /* File decryption byte window */
#define BACKGROUND_COLOR    D3DCOLOR_XRGB( 128, 128, 255 )  /* Sky-blue background color */
#define WINSOCK_VERSION     MAKEWORD(2,2)                   /* Use Winsock version 2.2 */
#define SERVER_COMM_PORT    27192                           /* Connect to port 27192 */
#define MAX_PACKET_SIZE     1024                            /* Largest packet is 1024 bytes */
#define MAX_USERS           16                              /* Maximum of 16 players */
#define UPDATE_FREQUENCY        10                          /* Update 10 times per second */
#define IDLE_UPDATE_FREQUENCY   2                           /* When idle, update twice a second */

// Tracks in the tiny_4anim.x animation file
#define TINYTRACK_RUN           1
#define TINYTRACK_WALK          2
#define TINYTRACK_IDLE          3

// When an error occurs, this has a value
LPCSTR g_strError = NULL;

/**
 * Stores a single coordinate in the terrain vertex buffer
 *   @author Karl Gluck
 */
struct TerrainVertex
{
    FLOAT x, y, z;
    FLOAT u, v;
};

/// Vertex definition for Direct3D
#define D3DFVF_TERRAINVERTEX (D3DFVF_XYZ|D3DFVF_TEX1)


/**
 * Sets up the Direct3D device object
 *   @param hWnd Window target for the device
 *   @param pD3D Direct3D parent object to create the device with
 *   @param pPresentationParameters Parameters structure to fill and use when creating the device
 *   @return The created device
 */
LPDIRECT3DDEVICE9 CreateD3DDevice( HWND hWnd, LPDIRECT3D9 pD3D,
                                   D3DPRESENT_PARAMETERS * pPresentationParameters )
{
    // Set up the structure used to create the Direct3D device.
    D3DPRESENT_PARAMETERS d3dpp; 
    ZeroMemory( &d3dpp, sizeof(d3dpp) );
    d3dpp.Windowed = FALSE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;

    // Get the current display mode
    D3DDISPLAYMODE d3ddm;
    if( FAILED( pD3D->GetAdapterDisplayMode( D3DADAPTER_DEFAULT, &d3ddm ) ) )
    {
        g_strError = "Error getting current display mode";
        return NULL;
    }

    // Set up the parameters structure to operate in the current mode.  There are only two
    // modes that Direct3D commonly suports for back buffer formats:  32 bit equal color and
    // 16-bit 565.  We cast the format into one of these two modes, taking the lower by
    // default.
    d3dpp.BackBufferWidth = d3ddm.Width;
    d3dpp.BackBufferHeight = d3ddm.Height;
    d3dpp.BackBufferFormat = (d3ddm.Format == D3DFMT_X8R8G8B8) ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // Create the device
    LPDIRECT3DDEVICE9 pd3dDevice;
    if( FAILED( pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                    &d3dpp, &pd3dDevice ) ) )
    {
        g_strError = "Unable to create the Direct3D device";
        return NULL;
    }

    // If it is supported, use a W-buffer.  This buffering system prevents "z-fighting", which is the
    // flickery effect that appears when polygon faces are in close proximity to one another.  The
    // z-buffer uses an exponential depth buffer, so there is more detail with objects that are close
    // to the camera.  A w-buffer uses a constant detail level throughout the viewspace, so even things
    // that are further away don't flicker.
    D3DCAPS9 caps;
    pd3dDevice->GetDeviceCaps(&caps);
    if (caps.RasterCaps&D3DPRASTERCAPS_WBUFFER) {
        pd3dDevice->SetRenderState(D3DRS_ZENABLE,D3DZB_USEW);
    } else {
        pd3dDevice->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
    }

    // Copy the parameters structure
    CopyMemory( pPresentationParameters, &d3dpp, sizeof(d3dpp) );

    // Return the device
    return pd3dDevice;
}


/**
 * Creates a window that takes up the entire screen
 *   @param hInstance Application instance
 *   @param strClassName Name of the window class to create
 *   @param strWindowTitle Caption for the window
 *   @return Newly created window handle, or NULL if creation failed
 */
HWND CreateFullscreenWindow( HINSTANCE hInstance, LPCSTR strClassName, LPCSTR strWindowTitle )
{
    // Create the window
    return CreateWindow( strClassName, strWindowTitle, WS_POPUP | WS_SYSMENU | WS_VISIBLE,
                         CW_USEDEFAULT, CW_USEDEFAULT, GetSystemMetrics( SM_CXSCREEN ),
                         GetSystemMetrics( SM_CYSCREEN ), GetDesktopWindow(), NULL,
                         hInstance, NULL );

}

/**
 * Handles the Windows message pump
 *   @param pElapsedTime Target variable for elapsed time in seconds or NULL
 *   @return Whether or not to continue the program
 */
BOOL HandleMessagePump( FLOAT * pElapsedTime )
{
    static FLOAT fLastTime = GetTickCount() / 1000.0f;

    // Used to process messages
    MSG msg;
    ZeroMemory( &msg, sizeof(msg) );

    // Handle the Windows stuff
    while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
    {
        // Handle the message
        TranslateMessage( &msg );
        DispatchMessage( &msg );

        // Exit on a quit message
        if( msg.message == WM_QUIT )
            return false;
    }

    // Calculate the elapsed time
    if( pElapsedTime )
    {
        float fTime = GetTickCount() / 1000.0f;
        *pElapsedTime = fTime - fLastTime;
        fLastTime = fTime;
    }
    else
    {
        fLastTime = GetTickCount() / 1000.0f;
    }

    // Success
    return TRUE;
}


/**
 * Sets up the default Direct3D states on the device
 *   @param pd3dDevice Device to set states for
 *   @return Success code
 */
HRESULT SetSceneStates( LPDIRECT3DDEVICE9 pd3dDevice )
{
    // Conversion float for the fog values
    FLOAT fConv;

    // Set rendering states
    pd3dDevice->SetRenderState( D3DRS_ZENABLE,  TRUE );
    pd3dDevice->SetRenderState( D3DRS_LIGHTING, FALSE );
    pd3dDevice->SetRenderState( D3DRS_FOGCOLOR, BACKGROUND_COLOR );
    pd3dDevice->SetRenderState( D3DRS_FOGSTART, *((DWORD*)&(fConv = 0.0f)) );
    pd3dDevice->SetRenderState( D3DRS_FOGEND,   *((DWORD*)&(fConv = 100.0f)) );
    pd3dDevice->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_LINEAR );
    pd3dDevice->SetRenderState( D3DRS_FOGENABLE,    TRUE );
    pd3dDevice->SetRenderState( D3DRS_DITHERENABLE, TRUE );

    // Set the filters for texture sampling and mipmapping
    pd3dDevice->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR );
    pd3dDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
    pd3dDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );

    // Get the current viewport
    D3DVIEWPORT9 vpt;
    if( FAILED( pd3dDevice->GetViewport( &vpt ) ) )
        return E_FAIL;

    // Build and set a perspective matrix
    D3DXMATRIXA16 mat;
    D3DXMatrixPerspectiveFovLH( &mat, D3DX_PI/4, (FLOAT)vpt.Width / (FLOAT)vpt.Height,
                                0.1f, 100.0f );
    pd3dDevice->SetTransform( D3DTS_PROJECTION, &mat );

    // Success
    return S_OK;
}


/**
 * Generates a vertex buffer that holds terrain information
 *   @param pd3dDevice Source device to create buffer with
 *   @param fScale Scaling value for the terrain in abstract units
 *   @return Newly created vertex buffer or NULL if the operation failed
 */
LPDIRECT3DVERTEXBUFFER9 CreateTerrainBuffer( LPDIRECT3DDEVICE9 pd3dDevice, FLOAT fScale )
{
    LPDIRECT3DVERTEXBUFFER9 pVB = NULL;

    // Create the buffer
    if( FAILED( pd3dDevice->CreateVertexBuffer( sizeof(TerrainVertex) * 4, D3DUSAGE_WRITEONLY,
                                                D3DFVF_TERRAINVERTEX, D3DPOOL_DEFAULT, &pVB,
                                                NULL ) ) )
        return NULL;

    // Use these vertices to create the terrain
    TerrainVertex vertices[] = 
    {
        { -1.0f, 0.0f, -1.0f, -1.0f, -1.0f },
        { -1.0f, 0.0f, +1.0f, -1.0f, +1.0f },
        { +1.0f, 0.0f, -1.0f, +1.0f, -1.0f },
        { +1.0f, 0.0f, +1.0f, +1.0f, +1.0f },
    };

    // Scale all of the planar coordinates
    for( int i = 0; i < sizeof(vertices) / sizeof(TerrainVertex); ++i )
    {
        vertices[i].x *= fScale;
        vertices[i].z *= fScale;
        vertices[i].u *= fScale;
        vertices[i].v *= fScale;
    }

    // Lock the vertex buffer
    LPVOID pVertices = NULL;
    if( SUCCEEDED( pVB->Lock( 0, 0, &pVertices, 0 ) ) )
    {
        // Copy the vertices
        memcpy( pVertices, vertices, sizeof(vertices) );

        // Unlock the buffer
        pVB->Unlock();
    }
    else
    {
        // Free the vertex buffer
        pVB->Release();

        // Return a null reference
        return NULL;
    }

    // Return the new buffer
    return pVB;
}


/**
 * Windows message handler.  Our version simply posts a quit message when the window is closed,
 * and gives all other messages to the default procedure.
 *   @param hWnd Source window
 *   @param uMsg Message
 *   @param wParam Parameter
 *   @param lParam Parameter
 *   @return Success code or error code
 */
LRESULT WINAPI WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    // Exit when the window is closed
    if( uMsg == WM_CLOSE )
        PostQuitMessage( 0 );
    else
        // Pass this message to the default processor
        return DefWindowProc( hWnd, uMsg, wParam, lParam );

    // Success
    return S_OK;
}


/**
 * Loads terrain data specific to this demo
 *   @param pd3dDevice Source device
 *   @param ppGrassTexture Destination variable for grass texture interface
 *   @param ppGrassVB Destination variable for grass vertex buffer interface
 *   @return Success or failure code
 */
HRESULT LoadTerrain( LPDIRECT3DDEVICE9 pd3dDevice, LPDIRECT3DTEXTURE9 * ppGrassTexture,
                     LPDIRECT3DVERTEXBUFFER9 * ppGrassVB )
{
    // Create the terrain information
    if( NULL != (*ppGrassVB = CreateTerrainBuffer( pd3dDevice, 100.0f )) &&
        SUCCEEDED(D3DXCreateTextureFromFile( pd3dDevice, "grass.jpg", ppGrassTexture )) )
    {
        return S_OK;
    }
    else
    {
        g_strError = "Error creating terrain";
        return E_FAIL;
    }
}


/**
 * Sets up the DirectInput interface
 *   @return Newly created DirectInput object or NULL if the function failed
 */
LPDIRECTINPUT8 CreateDirectInput()
{
    LPDIRECTINPUT8 pDI = NULL;

    if( FAILED( DirectInput8Create( GetModuleHandle(NULL), DIRECTINPUT_VERSION,
                                    IID_IDirectInput8, (VOID**)&pDI, NULL ) ) )
    {
        g_strError = "Couldn't create DirectInput";
        return NULL;
    }
    else
        return pDI;
}


/**
 * Creates the keyboard and mouse devices in DirectInput.
 *   @param pDI Source input object
 *   @param hWnd Target indow
 *   @param ppMouse Destination mouse interface variable
 *   @param ppKeyboard Destination keyboard interface variable
 *   @return Error or success code
 */
HRESULT CreateInputDevices( LPDIRECTINPUT8 pDI, HWND hWnd, LPDIRECTINPUTDEVICE8 * ppMouse,
                            LPDIRECTINPUTDEVICE8 * ppKeyboard )
{
    LPDIRECTINPUTDEVICE8 pKeyboard, pMouse;

    // Obtain an interface to the system keyboard device.
    if( FAILED( pDI->CreateDevice( GUID_SysKeyboard, &pKeyboard, NULL ) ) ||
        FAILED( pDI->CreateDevice( GUID_SysMouse, &pMouse, NULL ) ) )
    {
        // Release the keyboard
        if( pKeyboard )
            pKeyboard->Release();

        // Set up the error
        g_strError = "DirectInput was unable to allocate devices";

        // Exit with failure
        return E_FAIL;
    }

    if( FAILED( pKeyboard->SetDataFormat( &c_dfDIKeyboard ) ) ||
        FAILED( pKeyboard->SetCooperativeLevel( hWnd,
                    DISCL_FOREGROUND|DISCL_NONEXCLUSIVE|DISCL_NOWINKEY ) ) ||
        FAILED( pMouse->SetDataFormat( &c_dfDIMouse ) ) ||
        FAILED( pMouse->SetCooperativeLevel( hWnd,
                    DISCL_FOREGROUND|DISCL_EXCLUSIVE ) ) )
    {
        // Set the error state
        g_strError = "Couldn't set up device states for input";

        // Clean up the devices
        pKeyboard->Release();
        pMouse->Release();

        // Failure
        return E_FAIL;
    }

    // Store the devices
    *ppMouse = pMouse;
    *ppKeyboard = pKeyboard;

    // Success
    return S_OK;
}

/**
 * Loads basic mesh resource files.  This class could have much more complex functionality.  For
 * example, this class was originally created to allow for encrypted meshes to be loaded.
 *   @author Karl Gluck
 */
class BasicAllocateHierarchy : public AllocateHierarchy
{
    public:

        /**
         * Constructor to simply pass the number of blended matrices to the standard
         * allocate hierarchy class
         *   @param dwMaxBlendedMatrices Maximum number of hardware-supported matrix blends
         */
        BasicAllocateHierarchy( DWORD dwMaxBlendedMatrices ) :
          AllocateHierarchy( dwMaxBlendedMatrices )
        {
        }

    private:

        /**
         * Loads a texture resource from its file name.  This method is a simple implementation
         * of the load texture command (more complex versions may decrypt resources, get textures
         * from a cache, etc)
         *   @param strFileName Source file name to load
         *   @param ppTexture Destination for resultant texture interface
         *   @return Result code
         */
        STDMETHOD(LoadTexture)(THIS_ LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                                     LPDIRECT3DTEXTURE9 * ppTexture )
        {
            return D3DXCreateTextureFromFile( pDevice, strFileName, ppTexture );
        }

        /**
         * Loads a mesh hierarchy from an X file
         *   @param pDevice Source device object to create the structure with
         *   @param strFileName Name of the file to load
         *   @param ppFrameHierarchy Target variable for frame hierarchy
         *   @param ppAnimController Returns a pointer to the animation controller
         *   @return Result code
         */
        STDMETHOD(LoadMeshHierarchyFromX)(THIS_ LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                                                LPD3DXFRAME* ppFrameHierarchy,
                                                LPD3DXANIMATIONCONTROLLER* ppAnimController )
        {
            return D3DXLoadMeshHierarchyFromX( strFileName, 0, pDevice, this, NULL, ppFrameHierarchy, ppAnimController );
        }
};

/**
 * Stores the information for the player client-side
 *   @author Karl Gluck
 */
struct Player
{
    AnimatedMesh mesh;
    LPD3DXANIMATIONCONTROLLER pController;
    LPD3DXANIMATIONSET pWalkAnimation;
    LPD3DXANIMATIONSET pIdleAnimation;
    LPD3DXANIMATIONSET pRunAnimation;
    D3DXVECTOR3 vPosition;
    FLOAT fCurrentPlayerYaw;
    FLOAT fTargetPlayerYaw;
    FLOAT fCameraZoom;
    FLOAT fCurrentCameraYaw;
    FLOAT fTargetCameraYaw;
    FLOAT fCurrentCameraHeight;
    FLOAT fTargetCameraHeight;
    FLOAT fVelocity;
    D3DXMATRIXA16 matPosition;
    FLOAT fOriginYaw;
    DWORD dwState;
    DWORD dwCurrentTrack;
};


/**
 * Gets the current device state for the keyboard and mouse
 *   @param pKeyboard Keyboard interface
 *   @param pMouse Mouse interface
 *   @param keys 256-byte keys structure to fill with keyboard state
 *   @param pMs Mouse state to fill with data from the mouse
 *   @return Result code
 */
HRESULT UpdateInput( LPDIRECTINPUTDEVICE8 pKeyboard, LPDIRECTINPUTDEVICE8 pMouse,
                     BYTE * keys, DIMOUSESTATE * pMs )
{
    // Get device information
    if( SUCCEEDED( pKeyboard->GetDeviceState( sizeof(BYTE) * 256, keys ) ) &&
        SUCCEEDED( pMouse->GetDeviceState( sizeof(DIMOUSESTATE), pMs ) ) )
        return S_OK;

    // Failure to update device status
    return E_FAIL;
}

/**
 * Smooths a player to the selected animation
 *   @param pPlayer Player to transition
 *   @param pAnimationSet Destination animation
 */
VOID TransitionPlayerToAnimation( Player * pPlayer, LPD3DXANIMATIONSET pAnimationSet )
{
    // Get the controller
    LPD3DXANIMATIONCONTROLLER pController = pPlayer->pController;

    // Get the current time and add a transition time
    DOUBLE CurrentTime = pController->GetTime();
    DOUBLE TransitionTime = 0.2;

    // Fade out the old track
    pController->UnkeyAllTrackEvents( pPlayer->dwCurrentTrack );
    pController->KeyTrackEnable( pPlayer->dwCurrentTrack, FALSE, CurrentTime + TransitionTime );
    pController->KeyTrackWeight( pPlayer->dwCurrentTrack, 0.0, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
    pController->KeyTrackSpeed( pPlayer->dwCurrentTrack, 0.0f, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );

    // Toggle the current track
    pPlayer->dwCurrentTrack = 1 - pPlayer->dwCurrentTrack;

    // Set up this animation on the new track
    pController->UnkeyAllTrackEvents( pPlayer->dwCurrentTrack );
    pController->SetTrackPosition( pPlayer->dwCurrentTrack, 0.0 );
    pController->SetTrackAnimationSet( pPlayer->dwCurrentTrack, pAnimationSet );
    pController->SetTrackEnable( pPlayer->dwCurrentTrack, TRUE );
    pController->KeyTrackWeight( pPlayer->dwCurrentTrack, 1.0, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
    pController->KeyTrackSpeed( pPlayer->dwCurrentTrack, 1.0f, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
}

/**
 * Updates a client-side player from the user's input
 *   @param fElapsedTime How much time since the last frame was rendered
 *   @param keys Current keyboard state
 *   @param pMs Current mouse state
 *   @param pPlayer Player to update
 */
VOID UpdatePlayerFromInput( FLOAT fElapsedTime, const BYTE * keys, const DIMOUSESTATE * pMs, Player * pPlayer )
{
    if( pMs->rgbButtons[1] & 0x80 )
    {
        // Modify the camera state
        if( (keys[DIK_W] & 0x80) && (pPlayer->fCameraZoom > 1.0f) )    pPlayer->fCameraZoom -= fElapsedTime;
        if( keys[DIK_S]  & 0x80 )                                      pPlayer->fCameraZoom += fElapsedTime;

        // Turn the camera
        pPlayer->fTargetCameraYaw += pMs->lX * fElapsedTime;

        // Change the camera's height as long as it doesn't exceed a certain boundary
        if( (pPlayer->fTargetCameraHeight < 0.0f) || (pPlayer->fTargetCameraHeight > 30.0f) )
        {
            pPlayer->fTargetCameraHeight = max( pPlayer->fTargetCameraHeight,  0.0f );
            pPlayer->fTargetCameraHeight = min( pPlayer->fTargetCameraHeight, 30.0f );
        }
        else
            pPlayer->fTargetCameraHeight -= pMs->lY * fElapsedTime;

        // Go into the idle state
        if( pPlayer->dwState != 3 )
        {
            TransitionPlayerToAnimation( pPlayer, pPlayer->pIdleAnimation );
            pPlayer->dwState = 3;
        }
    }
    else
    {
        // Determine which set of keys has an effect
        BOOL bForward  = (keys[DIK_W]&0x80) && ((keys[DIK_W]&0x80) ^ (keys[DIK_S]&0x80)),
             bBackward = (keys[DIK_S]&0x80) && ((keys[DIK_W]&0x80) ^ (keys[DIK_S]&0x80)),
             bLeft     = (keys[DIK_A]&0x80) && ((keys[DIK_A]&0x80) ^ (keys[DIK_D]&0x80)),
             bRight    = (keys[DIK_D]&0x80) && ((keys[DIK_A]&0x80) ^ (keys[DIK_D]&0x80)),
             bShift    = (keys[DIK_LSHIFT]&0x80) || (keys[DIK_RSHIFT]&0x80);

        // Reset player angle
        pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw;

        if( bForward )
        {
            // Turn the player
            if( bRight )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw + D3DX_PI/4;
            if( bLeft  )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw - D3DX_PI/4;
        }
        else if( bBackward )
        {
            // Turn the player
            if( bRight )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw + 3*D3DX_PI/4;
            if( bLeft  )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw - 3*D3DX_PI/4;
            if( !bRight && !bLeft ) pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw + D3DX_PI;
        }
        else
        {
            // Make the player strafe
            if( bRight && !bLeft )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw + D3DX_PI/2;
            if( bLeft && !bRight )    pPlayer->fTargetPlayerYaw = pPlayer->fOriginYaw - D3DX_PI/2;
        }

        // Turn the camera and player
        float decrease = fabs(pPlayer->fTargetCameraYaw - pPlayer->fCurrentCameraYaw) / D3DX_PI;
        {
            float factor = (pMs->lX * 0.004f) * (1.0f - decrease);
            pPlayer->fOriginYaw += factor;
            pPlayer->fTargetPlayerYaw += factor;
            pPlayer->fTargetCameraYaw += factor;
        }

        // The character is moving only if a button is pressed without its opposite
        BOOL bMoving = (bForward ^ bBackward) || (bLeft ^ bRight);

        // If the player is moving, add to the speed and change the animation
        if( bMoving  )
        {
            if( bShift )
            {
                // Make the player run
                pPlayer->fVelocity -= fElapsedTime * 15.0f;

                if( pPlayer->dwState != TINYTRACK_RUN )
                {
                    TransitionPlayerToAnimation( pPlayer, pPlayer->pRunAnimation );
                    pPlayer->dwState = TINYTRACK_RUN;
                }
            }
            else
            {
                // Move the player
                pPlayer->fVelocity -= fElapsedTime * 5.5f;  

                if( pPlayer->dwState != TINYTRACK_WALK )
                {
                    TransitionPlayerToAnimation( pPlayer, pPlayer->pWalkAnimation );
                    pPlayer->dwState = TINYTRACK_WALK;
                }
            }
        }

        // If the user isn't pressing any keys, go to the idle state
        else
        {
            if( pPlayer->dwState != TINYTRACK_IDLE )
            {
                TransitionPlayerToAnimation( pPlayer, pPlayer->pIdleAnimation );
                pPlayer->dwState = TINYTRACK_IDLE;
            }
        }
    }
}

/**
 * Moves the client-side player and smoothes velocities
 *   @param fElapsedTime How much time has elapsed since the last frame
 *   @param pPlayer Player to update
 */
VOID UpdatePlayer( FLOAT fElapsedTime, Player * pPlayer )
{
    // Make sure that target yaw values are in the correct direction
    while( pPlayer->fTargetPlayerYaw - pPlayer->fCurrentPlayerYaw >  D3DX_PI ) pPlayer->fCurrentPlayerYaw += D3DX_PI*2.0f;
    while( pPlayer->fTargetPlayerYaw - pPlayer->fCurrentPlayerYaw < -D3DX_PI ) pPlayer->fCurrentPlayerYaw -= D3DX_PI*2.0f;

    // Smooth the current to the target values
    pPlayer->fCurrentCameraYaw = pPlayer->fCurrentCameraYaw + (1.0f - fElapsedTime) * 0.02f * (pPlayer->fTargetCameraYaw - pPlayer->fCurrentCameraYaw);
    pPlayer->fCurrentPlayerYaw = pPlayer->fCurrentPlayerYaw + (1.0f - fElapsedTime) * 0.20f * (pPlayer->fTargetPlayerYaw - pPlayer->fCurrentPlayerYaw);
    pPlayer->fCurrentCameraHeight = pPlayer->fCurrentCameraHeight + (1.0f - fElapsedTime) * 0.02f * (pPlayer->fTargetCameraHeight - pPlayer->fCurrentCameraHeight);

    // Move the player
    pPlayer->vPosition.x += sinf(pPlayer->fCurrentPlayerYaw) * pPlayer->fVelocity * fElapsedTime;
    pPlayer->vPosition.z += cosf(pPlayer->fCurrentPlayerYaw) * pPlayer->fVelocity * fElapsedTime;

    // Decay the player's velocity
    pPlayer->fVelocity *= 1.0f - (fElapsedTime * 8.0f);

    // Set up the player's position matrix
    D3DXMATRIXA16 matScale, matTransform, matRotation;
    D3DXMatrixScaling( &matScale, 0.0015f, 0.0015f, 0.0015f );
    D3DXMatrixTranslation( &matTransform, pPlayer->vPosition.x, pPlayer->vPosition.y, pPlayer->vPosition.z );
    D3DXMatrixRotationYawPitchRoll( &matRotation, pPlayer->fCurrentPlayerYaw + D3DX_PI, -D3DX_PI/2, 0.0f );
    D3DXMatrixMultiply( &pPlayer->matPosition, &matScale, &matRotation );
    D3DXMatrixMultiply( &pPlayer->matPosition, &pPlayer->matPosition, &matTransform );
}

/**
 * Sets up the Direct3D camera using a player structure
 *   @param pDevice Device to initialize player on
 *   @param pPlayer Player with the source camera data
 */
VOID SetPlayerCamera( LPDIRECT3DDEVICE9 pDevice, const Player * pPlayer )
{
    // Matrix being built for the view
    D3DXMATRIXA16 mat;

    // The camera looks down on the player from behind
    D3DXVECTOR3 eye( pPlayer->vPosition.x + sinf(pPlayer->fCurrentCameraYaw) * pPlayer->fCameraZoom,
                     pPlayer->vPosition.y + pPlayer->fCurrentCameraHeight + 0.4f,
                     pPlayer->vPosition.z + cosf(pPlayer->fCurrentCameraYaw) * pPlayer->fCameraZoom );
    D3DXVECTOR3 at( pPlayer->vPosition.x, pPlayer->vPosition.y + 0.4f, pPlayer->vPosition.z );

    // Build the matrix
    D3DXMatrixLookAtLH( &mat, &eye, &at, &D3DXVECTOR3( 0.0f, 1.0f, 0.0f ) );

    // Set the matrix
    pDevice->SetTransform( D3DTS_VIEW, &mat );
}

/**
 * Player data recieved from the server
 *   @author Karl Gluck
 */
struct OtherPlayer
{
    // Animation information
    LPD3DXANIMATIONCONTROLLER pController;
    LPD3DXANIMATIONSET pWalkAnimation;
    LPD3DXANIMATIONSET pIdleAnimation;
    LPD3DXANIMATIONSET pRunAnimation;
    DWORD dwCurrentTrack;

    // Internal data
    BOOL bActive;
    D3DXVECTOR3 vRenderPos;
    FLOAT fRenderYaw;

    // Data updated by server messages
    D3DXVECTOR3 vOldPos, vNewPos;
    D3DXVECTOR3 vOldVel, vNewVel;
    FLOAT fOldTime, fNewTime;
    DWORD dwState;
    FLOAT fYaw;
};

/**
 * List of message IDs that are transacted with the server
 *   @author Karl Gluck
 */
enum Message
{
    MSG_LOGON,
    MSG_LOGOFF,
    MSG_UPDATEPLAYER,
    MSG_CONFIRMLOGON,
    MSG_PLAYERLOGGEDOFF,
};

/**
 * First structure in every message
 *   @author Karl Gluck
 */
struct MessageHeader
{
    Message MsgID;
};

/**
 * Message sent to let the server know we want to log on
 *   @author Karl Gluck
 */
struct LogOnMessage
{
    MessageHeader   Header;

    LogOnMessage() { Header.MsgID = MSG_LOGON; }
};

/**
 * Client wants to disconnect from the server
 *   @author Karl Gluck
 */
struct LogOffMessage
{
    MessageHeader   Header;

    LogOffMessage() { Header.MsgID = MSG_LOGOFF; }
};

/**
 * Sent between the server and client to update player information
 *   @author Karl Gluck
 */
struct UpdatePlayerMessage
{
    MessageHeader   Header;
    DWORD           dwPlayerID;
    FLOAT           fVelocity[3];       // Expressed in meters / second
    FLOAT           fPosition[3];
    DWORD           dwState;
    FLOAT           fYaw;

    UpdatePlayerMessage() { Header.MsgID = MSG_UPDATEPLAYER; }
};

/**
 * Sent by the server to tell the client that it has successfully logged on
 *   @author Karl Gluck
 */
struct ConfirmLogOnMessage
{
    MessageHeader   Header;

    ConfirmLogOnMessage() { Header.MsgID = MSG_CONFIRMLOGON; }
};

/**
 * Another player has logged off
 *   @author Karl Gluck
 */
struct PlayerLoggedOffMessage
{
    MessageHeader   Header;
    DWORD           dwPlayerID;

    PlayerLoggedOffMessage() { Header.MsgID = MSG_PLAYERLOGGEDOFF; }
};

/**
 * Loads the Winsock DLL and initializes data
 *   @param pSocket Socket to set up
 *   @param pRecvEvent Event to initialize
 *   @return Operation success code
 */
HRESULT InitializeWinsock( SOCKET * pSocket, HANDLE * pRecvEvent )
{
    // Start up Winsock
    {
        // Stores information about Winsock
        WSADATA wsaData;

        // Call the DLL initialization function
        if( SOCKET_ERROR == WSAStartup( WINSOCK_VERSION, &wsaData ) ||
                    wsaData.wVersion != WINSOCK_VERSION )
            return E_FAIL;
    }

    // Set up the main socket 
    SOCKET sSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

    // Create the event
    HANDLE hRecvEvent = WSACreateEvent();
    if( SOCKET_ERROR == WSAEventSelect( sSocket, hRecvEvent, FD_READ ) )
    {
        closesocket( sSocket );
        return E_FAIL;
    }

    // Store data
    *pSocket = sSocket;
    *pRecvEvent = hRecvEvent;

    // Success
    return S_OK;
}


/**
 * Processor function for the enter-IP-address dialog
 *   @param hDlg Dialog handle
 *   @param uMsg Message to process
 *   @param wParam Variable parameter
 *   @param lParam Variable parameter
 *   @return Whether or not the message was processed
 */
INT_PTR CALLBACK IPAddressDlgProc( HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    switch( uMsg )
    {
        case WM_INITDIALOG:
            {
                // Set the default IP
                SendMessage( GetDlgItem(hDlg,IDC_IPADDRESS), IPM_SETADDRESS, 0, MAKEIPADDRESS(127,0,0,1) );

                // Store the IP address pointer
                SetWindowLong( hDlg, GWL_USERDATA, lParam );

            } break;

        // The user pressed a button
        case WM_COMMAND:
            switch( LOWORD(wParam) )
            {
                case IDOK:
                    {
                        // Get the IP pointer
                        LPDWORD pIp = (LPDWORD)GetWindowLong( hDlg, GWL_USERDATA );

                        // Get the IP address
                        if( 4 == SendMessage( GetDlgItem(hDlg,IDC_IPADDRESS), IPM_GETADDRESS, 0, (LPARAM)pIp ) )
                            EndDialog( hDlg, S_OK );
                        else
                            EndDialog( hDlg, E_FAIL );

                    }break;

                case IDCANCEL:
                    {
                        // Close the dialog box
                        EndDialog( hDlg, E_FAIL );

                    }break;
            }
            break;

        // Message wasn't processed
        default:
            return FALSE;
    }

    return TRUE;
}


/**
 * Runs the IP address dialog to let the user type in an address
 *   @return Host-byte order server address
 */
DWORD GetServerAddress()
{
    // This value is sent so that the IP can be stored
    DWORD dwIp;

    // Make sure that the IP address control is loaded
    INITCOMMONCONTROLSEX cex = { sizeof(cex), ICC_INTERNET_CLASSES };
    InitCommonControlsEx( &cex );

    // Pop a dialog box
    int ret = DialogBoxParam( GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SERVERIP),
                              GetDesktopWindow(), IPAddressDlgProc, (LPARAM)&dwIp );

    // Return a result based on whether or not the user entered an IP
    if( ret == S_OK )
        return ntohl(dwIp);
    else
        return 0;
}


/**
 * Attempts to establish a connection with the server
 *   @param sSocket Socket to connect with
 *   @param hRecvEvent Event that is set when data is received
 *   @return Success code
 */
HRESULT ConnectToServer( SOCKET sSocket, HANDLE hRecvEvent )
{
    // Let the user enter the server's IP address
    DWORD dwAddr = GetServerAddress();
    if( dwAddr == 0 )
        return E_FAIL;

    // Build a server address
    LPHOSTENT pTargetAddress = gethostbyaddr( (CHAR*)&dwAddr, 4, AF_INET );
    SOCKADDR_IN addr;
    ZeroMemory( &addr, sizeof(addr) );
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_COMM_PORT);
    addr.sin_addr   = *((LPIN_ADDR)*pTargetAddress->h_addr_list);

    // Send the log on message
    LogOnMessage packet;
    if( SOCKET_ERROR == sendto( sSocket, (CHAR*)&packet, sizeof(packet), 0,
                                (LPSOCKADDR)&addr, sizeof(SOCKADDR_IN) ) )
        return E_FAIL;

    // Wait for a reply or for a few seconds to pass
    if( WAIT_TIMEOUT == WSAWaitForMultipleEvents( 1, &hRecvEvent, TRUE, 3000, FALSE ) )
        return E_FAIL;

    // Recieve the result
    CHAR buffer[MAX_PACKET_SIZE];
    int length;
    SOCKADDR_IN src;
    ZeroMemory( &src, sizeof(src) );
    int fromlen = sizeof(SOCKADDR_IN);
    if( SOCKET_ERROR == (length = recvfrom( sSocket, buffer, sizeof(buffer), 0, (LPSOCKADDR)&src, &fromlen )) )
        return E_FAIL;

    // Connect to this address
    connect( sSocket, (LPSOCKADDR)&src, sizeof(SOCKADDR_IN) );

    // Success
    return S_OK;
}

/**
 * Smooths an OtherPlayer structure to a certain animation
 *   @param pPlayer Player to modify animation for
 *   @param pAnimationSet Destination animation
 */
VOID TransitionOtherPlayerToAnimation( OtherPlayer * pPlayer, LPD3DXANIMATIONSET pAnimationSet )
{
    // Get the controller
    LPD3DXANIMATIONCONTROLLER pController = pPlayer->pController;

    // Get the current time and add a transition time
    DOUBLE CurrentTime = pController->GetTime();
    DOUBLE TransitionTime = 0.2;

    // Fade out the old track
    pController->UnkeyAllTrackEvents( pPlayer->dwCurrentTrack );
    pController->KeyTrackEnable( pPlayer->dwCurrentTrack, FALSE, CurrentTime + TransitionTime );
    pController->KeyTrackWeight( pPlayer->dwCurrentTrack, 0.0, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
    pController->KeyTrackSpeed( pPlayer->dwCurrentTrack, 0.0f, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );

    // Toggle the current track
    pPlayer->dwCurrentTrack = 1 - pPlayer->dwCurrentTrack;

    // Set up this animation on the new track
    pController->UnkeyAllTrackEvents( pPlayer->dwCurrentTrack );
    pController->SetTrackPosition( pPlayer->dwCurrentTrack, 0.0 );
    pController->SetTrackAnimationSet( pPlayer->dwCurrentTrack, pAnimationSet );
    pController->SetTrackEnable( pPlayer->dwCurrentTrack, TRUE );
    pController->KeyTrackWeight( pPlayer->dwCurrentTrack, 1.0, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
    pController->KeyTrackSpeed( pPlayer->dwCurrentTrack, 1.0f, CurrentTime, TransitionTime, D3DXTRANSITION_LINEAR );
}

/**
 * Updates a player structure
 *   @param pPlayer Player to update
 *   @param pUpm Message to use for updating player
 *   @return Success code
 */
HRESULT UpdateOtherPlayer( OtherPlayer * pPlayer, UpdatePlayerMessage * pUpm )
{
    // If this player is inactive, activate it
    if( !pPlayer->bActive )
    {
        pPlayer->fNewTime = GetTickCount() / 1000.0f;
        pPlayer->vNewPos = D3DXVECTOR3( pUpm->fPosition[0], pUpm->fPosition[1], pUpm->fPosition[2] );
        pPlayer->vNewVel = D3DXVECTOR3( pUpm->fVelocity[0], pUpm->fVelocity[1], pUpm->fVelocity[2] );
        pPlayer->bActive = TRUE;

        pPlayer->vRenderPos = pPlayer->vNewPos;
        pPlayer->fRenderYaw = pPlayer->fYaw;
    }

    // Move old stuff backward
    pPlayer->vOldPos = pPlayer->vNewPos;
    pPlayer->vOldVel = pPlayer->vNewVel;
    pPlayer->fOldTime = pPlayer->fNewTime;

    // Update
    pPlayer->fNewTime = GetTickCount() / 1000.0f;
    pPlayer->vNewPos = D3DXVECTOR3( pUpm->fPosition[0], pUpm->fPosition[1], pUpm->fPosition[2] );
    pPlayer->vNewVel = D3DXVECTOR3( pUpm->fVelocity[0], pUpm->fVelocity[1], pUpm->fVelocity[2] );
    pPlayer->fYaw = pUpm->fYaw;

    // Change the new state
    if( pUpm->dwState != pPlayer->dwState )
    {
        // Choose the next animation
        LPD3DXANIMATIONSET pNewSet = NULL;
        switch( pUpm->dwState )
        {
            case TINYTRACK_WALK:        pNewSet = pPlayer->pWalkAnimation;          break;
            case TINYTRACK_IDLE:        pNewSet = pPlayer->pIdleAnimation;          break;
            case TINYTRACK_RUN:         pNewSet = pPlayer->pRunAnimation;           break;
        }

        // Smooth to the next animation
        TransitionOtherPlayerToAnimation( pPlayer, pNewSet );

        // Change the state
        pPlayer->dwState = pUpm->dwState;
    }

    // Success
    return S_OK;
}


/**
 * Handles a packet from the server
 *   @param pPlayers List of players
 *   @param pBuffer Data packet received
 *   @param dwSize How larget the packet is
 *   @return Success code
 */
HRESULT ProcessPacket( OtherPlayer * pPlayers, const CHAR * pBuffer, DWORD dwSize )
{
    MessageHeader * pMh = (MessageHeader*)pBuffer;

    switch( pMh->MsgID )
    {
        case MSG_UPDATEPLAYER:
            {
                UpdatePlayerMessage * pUpm = (UpdatePlayerMessage*)pBuffer;
                return UpdateOtherPlayer( &pPlayers[pUpm->dwPlayerID], pUpm );
            }

        case MSG_PLAYERLOGGEDOFF:
            {
                PlayerLoggedOffMessage * pPlom = (PlayerLoggedOffMessage*)pBuffer;
                pPlayers[pPlom->dwPlayerID].bActive = FALSE;
            } break;
    }

    // Success
    return S_OK;
}


/**
 * Handles messages from the network
 *   @param pPlayers List of players
 *   @param sSocket Socket to get data from
 *   @param hRecvEvent Event triggered when a packet is received
 *   @return Success code
 */
HRESULT ProcessNetworkMessages( OtherPlayer * pPlayers, SOCKET sSocket, HANDLE hRecvEvent )
{
    if( WAIT_OBJECT_0 == WaitForSingleObject( hRecvEvent, 0 ) )
    {
        // Buffers used to recieve data
        CHAR buffer[MAX_PACKET_SIZE];
        int size;
        SOCKADDR_IN addr;
        int fromlen = sizeof(SOCKADDR_IN);

        // Stores return codes
        HRESULT hr;

        // Get the packet
        while( SOCKET_ERROR != (size = recvfrom( sSocket, buffer, sizeof(buffer), 0, (LPSOCKADDR)&addr, &fromlen )) )
        {
            // Process information from the packet
            if( FAILED( hr = ProcessPacket( pPlayers, buffer, size ) ) )
                return hr;
        }
    }

    // Success
    return S_OK;
}


/**
 * Sets up a player structure
 *   @param pAm Source animated mesh
 *   @param pPlayer Player structure to initialize
 *   @return Success/error code
 */
HRESULT InitOtherPlayer( AnimatedMesh * pAm, OtherPlayer * pPlayer )
{
    ZeroMemory( pPlayer, sizeof(OtherPlayer) );

    if( FAILED( pAm->CloneAnimationController( 2, &pPlayer->pController ) ) )
        return E_FAIL;

    pPlayer->pController->GetAnimationSet( TINYTRACK_WALK,        &pPlayer->pWalkAnimation );
    pPlayer->pController->GetAnimationSet( TINYTRACK_IDLE,        &pPlayer->pIdleAnimation );
    pPlayer->pController->GetAnimationSet( TINYTRACK_RUN,         &pPlayer->pRunAnimation );

    // Set up an initial state
    pPlayer->pController->SetTrackAnimationSet( 0, pPlayer->pIdleAnimation );
    pPlayer->pController->SetTrackEnable( 0, TRUE );

    // Success
    return S_OK;
}


/**
 * Gets rid of data in a player structure
 *   @param pPlayer Player to free
 */
VOID ReleaseOtherPlayer( OtherPlayer * pPlayer )
{
    if( pPlayer->pWalkAnimation )
        pPlayer->pWalkAnimation->Release();
    if( pPlayer->pIdleAnimation )
        pPlayer->pIdleAnimation->Release();
    if( pPlayer->pRunAnimation )
        pPlayer->pRunAnimation->Release();
    if( pPlayer->pController )
        pPlayer->pController->Release();
    ZeroMemory( pPlayer, sizeof(OtherPlayer) );
}


/**
 * Sends a message to the server informing of a disconnect
 *   @param sSocket Socket to send message with
 */
VOID DisconnectFromServer( SOCKET sSocket )
{
    LogOffMessage lom;
    send( sSocket, (char*)&lom, sizeof(lom), 0 );
}


/**
 * Waits for a lost Direct3D device to return to a usable state, then resets the device using
 * the provided parameters.  Called after a lost device has been detected and all device-
 * dependant resources are unloaded.
 *   @param sSocket Socket to update server on
 *   @param pPlayer Player object being updated
 *   @param pd3dDevice Lost device to monitor for usable state
 *   @param pD3DParams Parameters structure to reset the device with
 *   @return Success or failure code
 */
HRESULT WaitForLostDevice( SOCKET sSocket, Player * pPlayer, LPDIRECT3DDEVICE9 pd3dDevice, D3DPRESENT_PARAMETERS * pD3DParams )
{
    // Server hasn't been updated yet
    FLOAT fLastUpdate = 0.0f;
    FLOAT fElapsedTime;

    // Set the player state
    {
        // Build false input information
        BYTE keys[256];
        DIMOUSESTATE ms;
        ZeroMemory( keys, sizeof(keys) );
        ZeroMemory( &ms,  sizeof(ms)   );

        // Update the player using fake information
        UpdatePlayerFromInput( 0.0f, keys, &ms, pPlayer );
    }

    // Handle windows messages while waiting for a device return
    while( HandleMessagePump( &fElapsedTime ) )
    {
        if( D3DERR_DEVICENOTRESET == pd3dDevice->TestCooperativeLevel() )
        {
            // Reset the device
            if( SUCCEEDED( pd3dDevice->Reset( pD3DParams ) ) )
                return S_OK;
            else
            {
                // Set error state
                g_strError = "Couldn't reset the Direct3D device state";

                // Failed to reset the device
                return E_FAIL;
            }
        }
        else
        {
            // Send a player update message
            FLOAT fTime = GetTickCount() / 1000.0f;

            // Update the server periodically
            if( (1.0f / IDLE_UPDATE_FREQUENCY) < (fTime - fLastUpdate) )
            {
                UpdatePlayerMessage upm;
                upm.dwPlayerID = 0;
                upm.fVelocity[0] = pPlayer->fVelocity;
                upm.fVelocity[1] = 0.0f;
                upm.fVelocity[2] = 0.0f;
                upm.fPosition[0] = pPlayer->vPosition.x;
                upm.fPosition[1] = pPlayer->vPosition.y;
                upm.fPosition[2] = pPlayer->vPosition.z;
                upm.dwState = pPlayer->dwState;
                upm.fYaw = pPlayer->fTargetPlayerYaw;

                // Send off the packet
                send( sSocket, (CHAR*)&upm, sizeof(upm), 0 );

                // Store the last update time
                fLastUpdate = fTime;

                OutputDebugString( "Updating server\n" );
            }
        }
    }

    // Exit because the message pump was closed
    return E_FAIL;
}


/**
 * Entry point to the program
 *   @param hInstance Instance of the application
 *   @return Result code
 */
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE, LPSTR, int )
{
    // Structures used in the program
    HWND hWnd;
    D3DPRESENT_PARAMETERS d3dpp;
    LPDIRECT3D9 pD3D = NULL;
    LPDIRECT3DDEVICE9 pd3dDevice = NULL;
    LPDIRECT3DTEXTURE9 pGrassTexture = NULL;
    LPDIRECT3DVERTEXBUFFER9 pGrassVB = NULL;
    LPDIRECTINPUT8 pDI = NULL;
    LPDIRECTINPUTDEVICE8 pMouse = NULL;
    LPDIRECTINPUTDEVICE8 pKeyboard = NULL;
    FLOAT fElapsedTime;

    // The very first thing we need to do is determine if the Direct3D library can be loaded,
    // and determine what kind of capabilities it has.
    D3DCAPS9 d3dCaps;
    if( NULL == (pD3D = Direct3DCreate9( D3D_SDK_VERSION )) ||
        FAILED(pD3D->GetDeviceCaps( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps )) )
    {
        // Display the error to the user
        MessageBox( NULL, "Direct3D Not Found!", "NetGame Skeleton by Unseen Studios", MB_OK );

        // Exit the program
        return S_OK;
    }

    // Get the Direct3D capabilities

    // Player management information
    Player player;
    ZeroMemory( &player, sizeof(player) );
    player.fCameraZoom = 4.0f;
    player.fCurrentCameraHeight = player.fTargetCameraHeight = 2.0f;
    BasicAllocateHierarchy allocHierarchy( d3dCaps.MaxVertexBlendMatrices );

    // Networking structures
    SOCKET sSocket;
    HANDLE hRecvEvent;
    OtherPlayer players[MAX_USERS];
    ZeroMemory( players, sizeof(players) );

    // This identity matrix is used to render the terrain
    D3DXMATRIXA16 mxIdentity;
    D3DXMatrixIdentity( &mxIdentity );

    // Register a standard window class
    WNDCLASS wc = { 0, WndProc, 0, 0, hInstance,
                    LoadIcon( hInstance, MAKEINTRESOURCE(IDI_MAINICON) ),
                    LoadCursor( NULL, IDC_ARROW ),
                    (HBRUSH)GetStockObject(WHITE_BRUSH),
                    NULL, "wnd_ngsunseen" };
    RegisterClass( &wc );

    // Create a window
    if( SUCCEEDED(InitializeWinsock( &sSocket, &hRecvEvent )) &&
        SUCCEEDED(ConnectToServer( sSocket, hRecvEvent )) &&
        NULL != (hWnd = CreateFullscreenWindow( hInstance, wc.lpszClassName, "NetGame Skeleton by Unseen Studios" )) &&
        NULL != (pd3dDevice = CreateD3DDevice( hWnd, pD3D, &d3dpp )) &&
        SUCCEEDED(LoadTerrain( pd3dDevice, &pGrassTexture, &pGrassVB )) &&
        NULL != (pDI = CreateDirectInput()) &&
        SUCCEEDED(CreateInputDevices( pDI, hWnd, &pMouse, &pKeyboard)) &&
        SUCCEEDED(player.mesh.LoadMeshFromX( pd3dDevice, "tiny/tiny_4anim.x", &allocHierarchy )) &&
        SUCCEEDED(player.mesh.CloneAnimationController( 2, &player.pController )) )
    {
        // Initialize the other player array
        {
            for( int i = 0; i < MAX_USERS; ++i )
                InitOtherPlayer( &player.mesh, &players[i] );
        }

        // Acquire the mouse and keyboard
        pMouse->Acquire();
        pKeyboard->Acquire();

        // Initialize the graphics device
        SetSceneStates( pd3dDevice );

        // Set up the animation sets
        player.pController->GetAnimationSet( TINYTRACK_WALK,        &player.pWalkAnimation );
        player.pController->GetAnimationSet( TINYTRACK_IDLE,        &player.pIdleAnimation );
        player.pController->GetAnimationSet( TINYTRACK_RUN,         &player.pRunAnimation );

        // Set up an initial player-state
        player.pController->SetTrackAnimationSet( 0, player.pIdleAnimation );
        player.pController->SetTrackEnable( 0, TRUE );

        // Do the loop
        while( HandleMessagePump( &fElapsedTime ) )
        {
            // Clear the scene
            pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
                               BACKGROUND_COLOR, 1.0f, 0 );

            // Update the messages from the server
            ProcessNetworkMessages( players, sSocket, hRecvEvent );

            // These variables are used to update input
            BYTE keys[256];
            DIMOUSESTATE ms;

            // Update the camera using input
            if( SUCCEEDED( UpdateInput( pKeyboard, pMouse, keys, &ms ) ) )
            {
                // Check to see if the user wants to exit
                if( keys[DIK_ESCAPE] & 0x80 )
                    break;

                // Change the player's velocities and animation with input from the user
                UpdatePlayerFromInput( fElapsedTime, keys, &ms, &player );

                // Move the player using velocities
                UpdatePlayer( fElapsedTime, &player );
            }

            // Tell the server what settings have changed
            {
                FLOAT fTime = GetTickCount() / 1000.0f;
                static FLOAT fLastUpdate = fTime;

                // Update the server periodically
                if( (1.0f / UPDATE_FREQUENCY) < (fTime - fLastUpdate) )
                {
                    UpdatePlayerMessage upm;
                    upm.dwPlayerID = 0;
                    upm.fVelocity[0] = player.fVelocity;
                    upm.fVelocity[1] = 0.0f;
                    upm.fVelocity[2] = 0.0f;
                    upm.fPosition[0] = player.vPosition.x;
                    upm.fPosition[1] = player.vPosition.y;
                    upm.fPosition[2] = player.vPosition.z;
                    upm.dwState = player.dwState;
                    upm.fYaw = player.fTargetPlayerYaw;

                    // Send off the packet
                    send( sSocket, (CHAR*)&upm, sizeof(upm), 0 );

                    // Store the last update time
                    fLastUpdate = fTime;
                }
            }
            
            // Begin rendering
            if( SUCCEEDED( pd3dDevice->BeginScene() ) )
            {
                // Set up the camera
                SetPlayerCamera( pd3dDevice, &player );

                // Draw the Stan model
                player.pController->AdvanceTime( max( fElapsedTime, 0.0f ), NULL );
                player.mesh.Render( &player.matPosition );

                // Draw the other players
                {
                    // Run through the list of players
                    for( int i = 0; i < MAX_USERS; ++i )
                    {
                        // If the player is active, render it
                        if( players[i].bActive )
                        {
                            // Advance the controller's time step
                            players[i].pController->AdvanceTime( max( fElapsedTime, 0.0f ), NULL );

                            // Extract render positions
                            {
                                FLOAT fTime = GetTickCount() / 1000.0f;
                                FLOAT fTimeToNew = fTime - players[i].fNewTime;
                                FLOAT fTimeDelta = players[i].fNewTime - players[i].fOldTime;
                                D3DXVECTOR3 vPosDiff = players[i].vNewPos - players[i].vOldPos;

                                D3DXVECTOR3 vNewRenderPos;
                                if( fTimeDelta > 0.0f )
                                    vNewRenderPos = players[i].vNewPos + (fTimeToNew / fTimeDelta) * vPosDiff;
                                else
                                    vNewRenderPos = players[i].vNewPos;

                                D3DXVec3Lerp( &players[i].vRenderPos, &players[i].vRenderPos, &vNewRenderPos, 0.5f );
                                players[i].fRenderYaw = players[i].fRenderYaw + 0.5f * (players[i].fYaw - players[i].fRenderYaw);
                            }

                            // Use the interpolated position to generate a matrix
                            D3DXMATRIXA16 matScale, matTransform, matRotation, matPosition;
                            D3DXMatrixScaling( &matScale, 0.0015f, 0.0015f, 0.0015f );
                            D3DXMatrixTranslation( &matTransform, players[i].vRenderPos.x,
                                                                  players[i].vRenderPos.y,
                                                                  players[i].vRenderPos.z );
                            D3DXMatrixRotationYawPitchRoll( &matRotation, players[i].fRenderYaw + D3DX_PI, -D3DX_PI/2, 0.0f );
                            D3DXMatrixMultiply( &matPosition, &matScale, &matRotation );
                            D3DXMatrixMultiply( &matPosition, &matPosition, &matTransform );

                            // Render the player matrix
                            player.mesh.Render( &matPosition );
                        }
                    }
                }

                // Set the identity matrix
                pd3dDevice->SetTransform( D3DTS_WORLD, &mxIdentity );

                // Select the grass texture
                pd3dDevice->SetTexture( 0, pGrassTexture );

                // Render the vertex buffer
                pd3dDevice->SetStreamSource( 0, pGrassVB, 0, sizeof(TerrainVertex) );
                pd3dDevice->SetFVF( D3DFVF_TERRAINVERTEX );
                pd3dDevice->DrawPrimitive( D3DPT_TRIANGLESTRIP, 0, 2 );

                // End scene rendering
                pd3dDevice->EndScene();
            }

            // Flip the scene to the monitor
            if( FAILED( pd3dDevice->Present( NULL, NULL, NULL, NULL ) ) )
            {
                // Free our leases on the mouse and keyboard
                pMouse->Unacquire();
                pKeyboard->Unacquire();

                // Erase other players' device objects
                {
                    for( int i = 0; i < MAX_USERS; ++i )
                        ReleaseOtherPlayer( &players[i] );
                }

                // Free the device-dependant objects
                player.pController->Release();
                player.pWalkAnimation->Release();
                player.pIdleAnimation->Release();
                player.pRunAnimation->Release();

                player.mesh.Release();
                pGrassTexture->Release();
                pGrassVB->Release();

                // Erase the references
                player.pController = NULL;
                player.pWalkAnimation = NULL;
                player.pIdleAnimation = NULL;
                player.pRunAnimation = NULL;
                pGrassTexture = NULL;
                pGrassVB = NULL;

                // Wait for the device to return
                if( FAILED( WaitForLostDevice( sSocket, &player, pd3dDevice, &d3dpp ) ) )
                    break;

                // Initialize D3D settings for this scene
                SetSceneStates( pd3dDevice );

                // Reload the device objects
                if( FAILED( LoadTerrain( pd3dDevice, &pGrassTexture, &pGrassVB ) ) ||
                    FAILED(player.mesh.LoadMeshFromX( pd3dDevice, "tiny/tiny_4anim.x", &allocHierarchy )) ||
                    FAILED(player.mesh.CloneAnimationController( 2, &player.pController )) )
                    break;

                // Bring other players back
                {
                    for( int i = 0; i < MAX_USERS; ++i )
                        InitOtherPlayer( &player.mesh, &players[i] );
                }

                // Set up the animation sets
                player.pController->GetAnimationSet( TINYTRACK_WALK,        &player.pWalkAnimation );
                player.pController->GetAnimationSet( TINYTRACK_IDLE,        &player.pIdleAnimation );
                player.pController->GetAnimationSet( TINYTRACK_RUN,         &player.pRunAnimation );

                // Set up an initial idle state
                player.pController->SetTrackAnimationSet( 0, player.pIdleAnimation );
                player.pController->SetTrackEnable( 0, TRUE );

                // Reset motion
                player.fVelocity = 0.0f;

                // Re-acquire input
                pMouse->Acquire();
                pKeyboard->Acquire();
            }
        }
    }

    // If the socket was created, disconnect
    if( sSocket )
    {
        DisconnectFromServer( sSocket );
    }
 
    // Shut down Winsock
    WSACloseEvent( hRecvEvent );
    closesocket( sSocket );
    WSACleanup();

    // Release DirectInput information
    if( pMouse )
    {
        pMouse->Unacquire();
        pMouse->Release();
    }
    if( pKeyboard )
    {
        pKeyboard->Unacquire();
        pKeyboard->Release();
    }
    if( pDI )
        pDI->Release();

    // Release all of the other players
    {
        for( int i = 0; i < MAX_USERS; ++i )
            ReleaseOtherPlayer( &players[i] );
    }

    // Get rid of animation stuff
    if( player.pWalkAnimation )
        player.pWalkAnimation->Release();
    if( player.pIdleAnimation )
        player.pIdleAnimation->Release();
    if( player.pRunAnimation )
        player.pRunAnimation->Release();
    if( player.pController )
        player.pController->Release();
    player.mesh.Release();

    // Release Direct3D resources
    if( pGrassTexture )
        pGrassTexture->Release();
    if( pGrassVB )
        pGrassVB->Release();
    if( pd3dDevice )
        pd3dDevice->Release();
    if( pD3D )
        pD3D->Release();

    // Get rid of the window class
    DestroyWindow( hWnd );
    UnregisterClass( wc.lpszClassName, hInstance );

    // Print out a message box with the error if one occured
    if( g_strError )
        MessageBox( NULL, g_strError, "NetGame Skeleton by Unseen Studios", MB_OK );

    // Success
    return S_OK;
}