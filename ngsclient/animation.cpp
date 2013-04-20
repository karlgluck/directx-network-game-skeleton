//------------------------------------------------------------------------------------------------
// File:    animation.cpp
//
// Desc:    Provides standardized interfaces for accessing Direct3D Extention's animated mesh
//          controllers.
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
#include <d3dx9.h>
#include "animation.h"
#include <tchar.h>


//------------------------------------------------------------------------------------------------
// Name:  SAFE_*
// Desc:  Frees memory or COM resources and resets the pointer to NULL
//------------------------------------------------------------------------------------------------
#define SAFE_RELEASE( p )       if( p ) { p->Release(); p = NULL; }
#define SAFE_DELETE( p )        if( p ) { delete p; p = NULL; }
#define SAFE_DELETE_ARRAY( a )  if( a ) { delete [] a; a = NULL; }


//------------------------------------------------------------------------------------------------
// Name:  DEBUG_MSG
// Desc:  Outputs a message to the debugger when compiling in debug mode
//------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(DEBUG)
#define DEBUG_MSG( m )  OutputDebugString( "[animation.cpp]" m "\n" );
#else
#define DEBUG_MSG( m )
#endif


//------------------------------------------------------------------------------------------------
// Name:  CreateBonePointers
// Desc:  Sets up bone pointers in this container
//------------------------------------------------------------------------------------------------
HRESULT MeshContainer::CreateBonePointers( MeshFrame * pFrameRoot )
{
    // If skinning information exists, set up bones
    if( pSkinInfo )
    {
        // Get rid of the array if it exists
        SAFE_DELETE_ARRAY( ppBoneMatrixPointers );

        // Get the number of bones this mesh has
        DWORD dwNumBones = pSkinInfo->GetNumBones();

        // Set up the array
        if( NULL == (ppBoneMatrixPointers = new D3DXMATRIX*[ dwNumBones ]) )
            return E_OUTOFMEMORY;

        // Set up the pointers using frames
        for( DWORD i = 0; i < dwNumBones; ++i )
        {
            // Get the frame for this bone
            D3DXFRAME* pFrame = D3DXFrameFind( pFrameRoot, pSkinInfo->GetBoneName(i) );

            // Make sure the frame was found
            if( !pFrame ) return E_FAIL;

            // Set the pointer
            ppBoneMatrixPointers[ i ] = &((MeshFrame*)(pFrame))->matCombined;
        }
    }
    else
    {
        // Output information
        DEBUG_MSG( "MeshContainer::CreateBonePointers:  Mesh has no skinning information" );
    }

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  AllocateHierarchy
// Desc:  Sets up the allocation hierarchy class
//------------------------------------------------------------------------------------------------
AllocateHierarchy::AllocateHierarchy( DWORD dwMaxBlendedMatrices )
{
    m_dwMaxBlendedMatrices = dwMaxBlendedMatrices;
}


//------------------------------------------------------------------------------------------------
// Name:  CreateFrame
// Desc:  Creates a new frame
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::CreateFrame( LPCSTR strName, D3DXFRAME** ppFrame )
{
    // Create a new frame using the derived structure
    MeshFrame* pMeshFrame = new MeshFrame();

    // If allocation failed, return error
    if( !pMeshFrame ) return E_OUTOFMEMORY;

    // Reset the frame's memory
    ZeroMemory( pMeshFrame, sizeof(MeshFrame) );

    // Set up the name to a default value if none exists, then copy it into the frame
    if( strName == NULL ) strName = "<none>";
    pMeshFrame->Name = AllocateString( strName );

    // Make sure we could allocate the name
    if( !pMeshFrame->Name )
    {
        DestroyFrame( pMeshFrame );
        return E_OUTOFMEMORY;
    }

    // Set the output frame
    *ppFrame = (D3DXFRAME*)pMeshFrame;

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  CreateMeshContainer
// Desc:  Sets up a mesh container
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::CreateMeshContainer( LPCSTR strName, const D3DXMESHDATA* pMeshData,
                                                const D3DXMATERIAL* pMaterials,
                                                const D3DXEFFECTINSTANCE* pEffects,
                                                DWORD dwMtrlCount, const DWORD* pAdjacency,
                                                ID3DXSkinInfo* pSkinInfo,
                                                D3DXMESHCONTAINER** ppContainer )
{
    // Create a mesh container using our custom derived structure
    MeshContainer* pMeshContainer = new MeshContainer;

    // Validate allocation
    if( !pMeshContainer ) return E_OUTOFMEMORY;

    // Reset mesh container memory
    ZeroMemory( pMeshContainer, sizeof( MeshContainer ) );

    // Only build animated meshes from here out
    if( pSkinInfo == NULL )
    {
        *ppContainer = pMeshContainer;
        return S_OK;
    }

    // Make sure the structure passed in is a mesh, not a patch
    if( pMeshData->Type != D3DXMESHTYPE_MESH )
    {
        DestroyMeshContainer( pMeshContainer );
        return E_FAIL;
    }

    // Generate a name if none exists
    if( !strName ) strName = "<none>";

    // Copy the name
    pMeshContainer->Name = AllocateString( strName );

    // Validate
    if( !pMeshContainer->Name )
    {
        DestroyMeshContainer( pMeshContainer );
        return E_OUTOFMEMORY;
    }

    // Copy mesh data
    pMeshContainer->MeshData.Type = pMeshData->Type;
    pMeshContainer->MeshData.pMesh = pMeshData->pMesh;
    pMeshContainer->MeshData.pMesh->AddRef();

    // Copy adjacency
    {
        // Get the number of faces
        DWORD dwFaces = pMeshData->pMesh->GetNumFaces();

        // Create the adjacency array.  This is three times the number of faces because each
        // face is bordered by three others.
        pMeshContainer->pAdjacency = new DWORD[ dwFaces * 3 ];

        // Confirm the allocation
        if( !pMeshContainer->pAdjacency )
        {
            // Free the memory
            DestroyMeshContainer( pMeshContainer );

            // Memory error
            return E_OUTOFMEMORY;
        }

        // Copy the adjacency data into the container
        memcpy( pMeshContainer->pAdjacency, pAdjacency, sizeof(DWORD) * dwFaces * 3 );
    }

    // Get a copy of the device
    IDirect3DDevice9* pDevice;
    pMeshContainer->MeshData.pMesh->GetDevice( &pDevice );

    // Ignore effects instances
    pMeshContainer->pEffects = NULL;

    // Allocate and copy materials
    pMeshContainer->NumMaterials = max( 1, dwMtrlCount );
    pMeshContainer->pMaterials   = new D3DXMATERIAL      [ pMeshContainer->NumMaterials ];
    pMeshContainer->ppTextures   = new IDirect3DTexture9*[ pMeshContainer->NumMaterials ];

    // Make sure that allocation succeeded
    if( pMeshContainer->pMaterials == NULL || pMeshContainer->ppTextures == NULL )
    {
        // Free the container
        DestroyMeshContainer( pMeshContainer );

        // Failure
        return E_OUTOFMEMORY;
    }

    // Copy materials
    if( dwMtrlCount > 0 )
    {
        // Copy the materials from the source
        memcpy( pMeshContainer->pMaterials, pMaterials, dwMtrlCount *sizeof(D3DXMATERIAL));

        // Loop through all of the materials
        for( DWORD i = 0; i < dwMtrlCount; ++i )
        {
            // Check to see if a filename exists
            if( pMeshContainer->pMaterials[i].pTextureFilename )
            {
                if( FAILED( LoadTexture( pDevice, pMeshContainer->pMaterials[i].pTextureFilename,
                                        &pMeshContainer->ppTextures[i] ) ) )
                {
                    // Reset the container
                    pMeshContainer->ppTextures[i] = NULL;

                    // Output a message to the debugger
                    DEBUG_MSG( "AllocateHierarchy::CreateMeshContainer:  Unable to load texture" );
                }
            }
            else
            {
                // Reset this texture
                pMeshContainer->ppTextures[i] = NULL;

                // Tell the debugger that there wasn't a texture for this material
                DEBUG_MSG( "Material didn't specify texture" );
            }
        }
    }
    else
    {
        // Create a basic material
        memset( &pMeshContainer->pMaterials[0].MatD3D, 0, sizeof(D3DMATERIAL9) );
        pMeshContainer->pMaterials[0].MatD3D.Diffuse.r = 0.5f;
        pMeshContainer->pMaterials[0].MatD3D.Diffuse.g = 0.5f;
        pMeshContainer->pMaterials[0].MatD3D.Diffuse.b = 0.5f;
        pMeshContainer->pMaterials[0].MatD3D.Specular =
                                                  pMeshContainer->pMaterials[0].MatD3D.Diffuse;

        // No texture for this material
        pMeshContainer->pMaterials[0].pTextureFilename = NULL;
    }

    // Save the skin information structure
    pMeshContainer->pSkinInfo = pSkinInfo;
    pMeshContainer->pSkinInfo->AddRef();

    // Get the bone offset matrices from the skin information
    pMeshContainer->pBoneMatrixOffsets = new D3DXMATRIX[ pSkinInfo->GetNumBones() ];
    if( pMeshContainer->pBoneMatrixOffsets == NULL )
    {
        // Delete the container
        DestroyMeshContainer( pMeshContainer );

        // No more memory
        return E_OUTOFMEMORY;
    }

    // Copy bones
    {
        DWORD numBones = pSkinInfo->GetNumBones();
        for( DWORD i = 0; i < numBones; ++i )
        {
            pMeshContainer->pBoneMatrixOffsets[ i ] =
                                             *(D3DXMATRIX*)pSkinInfo->GetBoneOffsetMatrix( i );
        }
    }

    // Generate a blended mesh
    HRESULT hr = pMeshContainer->pSkinInfo->ConvertToBlendedMesh(
                        pMeshContainer->MeshData.pMesh,
                        D3DXMESH_MANAGED|D3DXMESHOPT_VERTEXCACHE,
                        pMeshContainer->pAdjacency,
                        NULL, NULL, NULL,
                       &pMeshContainer->dwMaxFaceInfluences,
                       &pMeshContainer->dwNumAttributeGroups,
                       &pMeshContainer->pBoneCombinationBuffer,
                       &pMeshContainer->pMesh );

    // Check the return code
    if( FAILED( hr ) )
    {
        DestroyMeshContainer( pMeshContainer );
        return hr;
    }

    // Make sure the mesh has the proper vertex format
    {
        DWORD dwOldFVF = pMeshContainer->pMesh->GetFVF();
        DWORD dwNewFVF = (dwOldFVF & D3DFVF_POSITION_MASK) | D3DFVF_NORMAL | D3DFVF_TEX1;

        // If the formats don't match, recreate the mesh
        if( dwOldFVF != dwNewFVF )
        {
            // Temporary mesh container
            ID3DXMesh* pTempMesh = NULL;

            // Clone into the temporary mesh
            hr = pMeshContainer->pMesh->CloneMeshFVF(
                            pMeshContainer->pMesh->GetOptions(),
                            dwNewFVF, pDevice, &pTempMesh );

            // Failed?  Exit
            if( FAILED( hr ) )
            {
                DestroyMeshContainer( pMeshContainer );
                return hr;
            }

            // Release the current mesh
            SAFE_RELEASE( pMeshContainer->pMesh );

            // Store the new mesh
            pMeshContainer->pMesh = pTempMesh;

            // If the original mesh lacked normals, add them
            if( !(dwOldFVF & D3DFVF_NORMAL) )
            {
                hr = D3DXComputeNormals( pMeshContainer->pMesh, NULL );
                if( FAILED( hr ) ) 
                {
                    DestroyMeshContainer( pMeshContainer );
                    return hr;
                }
            }
        }

        // Convert the mesh's buffers from managed into default pool
        if( !(pMeshContainer->pMesh->GetOptions() & D3DXMESH_WRITEONLY) )
        {
            // Temporary info used for conversions
            LPD3DXMESH pTempMesh = NULL;
            DWORD dwOptions = 0;

            // If this is a 32-bit mesh, set that option
            if( pMeshContainer->pMesh->GetOptions() & D3DXMESH_32BIT )
                dwOptions = D3DXMESH_32BIT;

            // Clone the mesh
            hr = pMeshContainer->pMesh->CloneMeshFVF( D3DXMESH_WRITEONLY|dwOptions,
                                                      pMeshContainer->pMesh->GetFVF(),
                                                      pDevice, &pTempMesh );
            if( FAILED( hr ) )
            {
                DestroyMeshContainer( pMeshContainer );
                return hr;
            }

            // Release the current mesh
            SAFE_RELEASE( pMeshContainer->pMesh );

            // Store the new mesh
            pMeshContainer->pMesh = pTempMesh;
        }
    }

    // Calculate bone influences and do mesh type conversion
    {
        // Get the bone combination buffer
        D3DXBONECOMBINATION* pBoneComboBuffer = reinterpret_cast<D3DXBONECOMBINATION*>(pMeshContainer->pBoneCombinationBuffer->GetBufferPointer());

        // Generate hardware-friendly bone combinations
        for( pMeshContainer->dwStartSoftwareRenderAttribute = 0;
             pMeshContainer->dwStartSoftwareRenderAttribute < pMeshContainer->dwNumAttributeGroups;
           ++pMeshContainer->dwStartSoftwareRenderAttribute )
        {
               // Total number of influences on this subset
            DWORD dwTotalInfluences = 0;

            // Look through all bones
            for( DWORD i = 0; i < pMeshContainer->dwMaxFaceInfluences; ++i )
            {
                // Search this ID
                if( pBoneComboBuffer[pMeshContainer->dwStartSoftwareRenderAttribute].BoneId[i] != UINT_MAX )
                {
                    // Increase influences
                    ++dwTotalInfluences;
                }
            }

            // If there are too many influences, split the mesh
            if( dwTotalInfluences > m_dwMaxBlendedMatrices )
                break;
        }

        // If the mesh was split, add the software processing flag
        if( pMeshContainer->dwStartSoftwareRenderAttribute < pMeshContainer->dwNumAttributeGroups )
        {
            // Construct a temporary mesh
            ID3DXMesh* pTempMesh = NULL;

            // Clone to the software mesh
            hr = pMeshContainer->pMesh->CloneMeshFVF(
                    D3DXMESH_SOFTWAREPROCESSING|pMeshContainer->pMesh->GetOptions(),
                    pMeshContainer->pMesh->GetFVF(), pDevice, &pTempMesh );

            // Failed? Return error
            if( FAILED( hr ) )
            {
                DestroyMeshContainer( pMeshContainer );
                return hr;
            }

            // Release the current mesh
            SAFE_RELEASE( pMeshContainer->pMesh );

            // Copy in the new mesh
            pMeshContainer->pMesh = pTempMesh;
        }
    }

    // Release our device reference
    SAFE_RELEASE( pDevice );

    // Store the mesh container
    *ppContainer = pMeshContainer;

    // Return success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  DestroyFrame
// Desc:  Frees the allocated parts of a mesh frame
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::DestroyFrame( D3DXFRAME* pFrame )
{
    // Convert the incoming frame to one that we can use
    MeshFrame* pMeshFrame = (MeshFrame*)pFrame;

    // Free the name and container
    SAFE_DELETE_ARRAY( pMeshFrame->Name );
    SAFE_DELETE( pMeshFrame );

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  DestroyMeshContainer
// Desc:  Frees the allocated parts of a mesh container
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::DestroyMeshContainer( D3DXMESHCONTAINER* pContainer )
{
    // Convert to our custom container type
    MeshContainer* pMeshContainer = (MeshContainer*)pContainer;

    SAFE_DELETE_ARRAY( pMeshContainer->Name );
    SAFE_RELEASE( pMeshContainer->MeshData.pMesh );
    SAFE_DELETE_ARRAY( pMeshContainer->pAdjacency );
    SAFE_DELETE_ARRAY( pMeshContainer->pMaterials );

    // Get rid of the texture array
    if( pMeshContainer->ppTextures )
    {
        // Release each of the textures
        for( DWORD i = 0; i < pMeshContainer->NumMaterials; ++ i )
            SAFE_RELEASE( pMeshContainer->ppTextures[i] );

        // Delete the entire array
        SAFE_DELETE_ARRAY( pMeshContainer->ppTextures );
    }

    // Get rid of the skinning information
    SAFE_RELEASE( pMeshContainer->pSkinInfo );

    // Get rid of bone matrix/combo buffers and the mesh itself
    SAFE_RELEASE( pMeshContainer->pMesh );
    SAFE_RELEASE( pMeshContainer->pBoneCombinationBuffer );
    SAFE_DELETE_ARRAY( pMeshContainer->pBoneMatrixOffsets );
    SAFE_DELETE_ARRAY( pMeshContainer->ppBoneMatrixPointers );

    // Reset mesh information
    pMeshContainer->dwMaxFaceInfluences = 0;
    pMeshContainer->dwNumAttributeGroups = 0;

    // Delete the entire mesh container
    SAFE_DELETE( pMeshContainer );

    // Successful operation
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  LoadTexture
// Desc:  Obtains a texture from a source file
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::LoadTexture( LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                                        LPDIRECT3DTEXTURE9 * ppTexture )
{
    return D3DXCreateTextureFromFile( pDevice, strFileName, ppTexture );
}


//------------------------------------------------------------------------------------------------
// Name:  LoadMeshHierarchyFromX
// Desc:  Creates a mesh's hierarchy from a source X file
//------------------------------------------------------------------------------------------------
HRESULT AllocateHierarchy::LoadMeshHierarchyFromX( LPDIRECT3DDEVICE9 pDevice,
                                                   LPCSTR strFileName,
                                                   LPD3DXFRAME* ppFrameHierarchy,
                                                   LPD3DXANIMATIONCONTROLLER* ppAnimController )
{
    // Pass parameters to the default loading function
    return D3DXLoadMeshHierarchyFromX( strFileName, 0, pDevice, this, NULL,
                                       ppFrameHierarchy, ppAnimController );
}


//------------------------------------------------------------------------------------------------
// Name:  AllocateString
// Desc:  Acquires memory for a string
//------------------------------------------------------------------------------------------------
CHAR * AllocateHierarchy::AllocateString( LPCSTR strString )
{
    DWORD dwLen = (DWORD)_tcslen(strString);
    CHAR* strAllocString = new TCHAR[ dwLen + 1 ];
    if( !strAllocString ) return NULL;
    strncpy_s( strAllocString, dwLen + 1, strString, dwLen );
    strAllocString[ dwLen ] = '\0';
    return strAllocString;
}


//------------------------------------------------------------------------------------------------
// Name:  AnimatedMesh
// Desc:  Resets mesh data
//------------------------------------------------------------------------------------------------
AnimatedMesh::AnimatedMesh()
{
    m_pd3dDevice = NULL;
    m_pFrameRoot = NULL;
    m_pAnimationController = NULL;
    m_pAllocateHierarchy = NULL;
}


//------------------------------------------------------------------------------------------------
// Name:  ~AnimatedMesh
// Desc:  Cleans up if the user forgot to
//------------------------------------------------------------------------------------------------
AnimatedMesh::~AnimatedMesh()
{
    Release();
}


//------------------------------------------------------------------------------------------------
// Name:  LoadMeshFromX
// Desc:  
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::LoadMeshFromX( LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                       AllocateHierarchy * pAllocateHierarchy )
{
    // Get rid of current information if any exists
    Release();

    // Store the device and allocation class
    (m_pd3dDevice = pDevice)->AddRef();
    m_pAllocateHierarchy = pAllocateHierarchy;

    // Stores result codes
    HRESULT hr;

    // Load the mesh
    hr = m_pAllocateHierarchy->LoadMeshHierarchyFromX( pDevice, strFileName,
                                                       (D3DXFRAME**)&m_pFrameRoot,
                                                      &m_pAnimationController );
    if( FAILED( hr ) || !m_pAnimationController )
    {
        Release();
        return hr;
    }

    // Set up bone pointers for this mesh
    hr = SetupBonePointers( m_pFrameRoot );
    if( FAILED( hr ) )
    {
        Release();
        return hr;
    }

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  Release
// Desc:  
//------------------------------------------------------------------------------------------------
VOID AnimatedMesh::Release()
{
    // Get rid of the animation controller
    SAFE_RELEASE( m_pAnimationController );

    // Free the frame hierarchy
    if( m_pFrameRoot )
    {
        // Destroy the frame tree
        D3DXFrameDestroy( (D3DXFRAME*)m_pFrameRoot, m_pAllocateHierarchy );

        // Free the root
        m_pFrameRoot = NULL;
    }

    // Release the device
    SAFE_RELEASE( m_pd3dDevice );

    // Get rid of the allocation hierarchy pointer
    m_pAllocateHierarchy = NULL;
}


//------------------------------------------------------------------------------------------------
// Name:  CloneAnimationController
// Desc:  
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::CloneAnimationController( DWORD dwSimultaneousTracks,
                                                LPD3DXANIMATIONCONTROLLER* ppAnimationController )
{
    // Holds return codes
    HRESULT hr;

    // Stores the temporary animation controller;
    LPD3DXANIMATIONCONTROLLER pTempController;

    // Duplicate the controller interface
    hr = m_pAnimationController->CloneAnimationController(
                                m_pAnimationController->GetMaxNumAnimationOutputs(),
                                m_pAnimationController->GetMaxNumAnimationSets(),
                                dwSimultaneousTracks,
                                m_pAnimationController->GetMaxNumEvents(),
                               &pTempController );

    // If error, return
    if( FAILED( hr ) )
        return hr;

    // Set up the controller's tracks
    for( DWORD i = 0; i < dwSimultaneousTracks; ++i )
        pTempController->SetTrackEnable( i, FALSE );

    // Store the controller
    *ppAnimationController = pTempController;

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  Render
// Desc:  Draws the mesh
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::Render( const D3DXMATRIX* pWorldMatrix )
{
    // Update the mesh's frame hierarchy
    UpdateFrames( m_pFrameRoot, pWorldMatrix );

    // Render the mesh
    return DrawFrames( m_pFrameRoot );
}


//------------------------------------------------------------------------------------------------
// Name:  SetupBonePointers
// Desc:  Intializes bone pointers on all frames in the mesh
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::SetupBonePointers( MeshFrame* pFrame )
{
    // Return code storage
    HRESULT hr;

    // Update mesh container
    if( pFrame->pMeshContainer )
    {
        // Call setup routine
        hr = ((MeshContainer*)pFrame->pMeshContainer)->CreateBonePointers( m_pFrameRoot );
        if( FAILED( hr ) )
            return hr;
    }

    // Set up the siblings
    if( pFrame->pFrameSibling )
    {
        hr = SetupBonePointers( (MeshFrame*)pFrame->pFrameSibling );
        if( FAILED( hr ) )
            return hr;
    }

    // Check the children
    if( pFrame->pFrameFirstChild )
    {
        hr = SetupBonePointers( (MeshFrame*)pFrame->pFrameFirstChild );
        if( FAILED( hr ) )
            return hr;
    }

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  UpdateFrames
// Desc:  
//------------------------------------------------------------------------------------------------
void AnimatedMesh::UpdateFrames( MeshFrame* pFrame, const D3DXMATRIX* pParentMatrix )
{
    // Update siblings using iteration
    do
    {
        // Set up the transformation matrix
        D3DXMatrixMultiply( &pFrame->matCombined,
                            &pFrame->TransformationMatrix, pParentMatrix );

        // Transform children using recursion
        if( pFrame->pFrameFirstChild )
            UpdateFrames( (MeshFrame*)pFrame->pFrameFirstChild, &pFrame->matCombined );

        // Move to the next frame
        pFrame = (MeshFrame*)pFrame->pFrameSibling;

    }while( pFrame != NULL );
}


//------------------------------------------------------------------------------------------------
// Name:  DrawFrames
// Desc:  Recursively draws a frames' siblings and children
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::DrawFrames( MeshFrame* pFrame )
{
    // Holds return codes
    HRESULT hr;

    // Do drawing for siblings using iteration
    do
    {
        // Draw the container if it exists
        if( pFrame->pMeshContainer )
        {
            hr = DrawFrameMesh( pFrame );
            if( FAILED( hr ) )
                return hr;
        }

        // Draw children using recursion--it's a bit easier
        if( pFrame->pFrameFirstChild )
        {
            hr = DrawFrames( (MeshFrame*)pFrame->pFrameFirstChild );
            if( FAILED( hr ) )
                return hr;
        }

        // Move to the next frame
        pFrame = (MeshFrame*)pFrame->pFrameSibling;

    } while( pFrame != NULL );

    // Success
    return S_OK;
}


//------------------------------------------------------------------------------------------------
// Name:  DrawFrameMesh
// Desc:  Draws the mesh attached to a certain frame's container
//------------------------------------------------------------------------------------------------
HRESULT AnimatedMesh::DrawFrameMesh( MeshFrame* pMeshFrame )
{
    // Get the mesh container from this frame
    MeshContainer * pMeshContainer = (MeshContainer*)pMeshFrame->pMeshContainer;

    // Used to build matrices that are set into memory
    D3DXMATRIX temporaryMatrix;

    // If there is no skinning, return
    if( pMeshContainer->pSkinInfo == NULL )
        return S_OK;

    // Get bone combinations
    D3DXBONECOMBINATION* boneComboBuffer = reinterpret_cast<D3DXBONECOMBINATION*>
                                  (pMeshContainer->pBoneCombinationBuffer->GetBufferPointer() );

    // Used when calculating blends
    UINT blendNumber, attribute, matrixIndex;
    
    // The last attribute that was set into memory
    DWORD previousAttributeId;

    // Reset the attribute ID
    previousAttributeId = UNUSED32;

    // Stores return codes
    HRESULT hr;

    // Draw using default vertex processing for this device
    for( attribute = 0;
         attribute < pMeshContainer->dwStartSoftwareRenderAttribute;
       ++attribute )
    {
        // Reset the blend number
        blendNumber = 0;

        // Calc world matrices
        for( DWORD i = 0; i <  pMeshContainer->dwMaxFaceInfluences; ++i )
        {
            // Get the matrix index from the bone buffer
            matrixIndex = boneComboBuffer[attribute].BoneId[i];

            // Add this matrix
            if( matrixIndex != UINT_MAX )
            {
                // Add this to the blend number
                blendNumber = i;

                // Build the matrix
                D3DXMatrixMultiply( &temporaryMatrix,
                                    &pMeshContainer->pBoneMatrixOffsets[matrixIndex],
                                     pMeshContainer->ppBoneMatrixPointers[matrixIndex] );

                // Set the matrix into memory
                m_pd3dDevice->SetTransform( D3DTS_WORLDMATRIX(i), &temporaryMatrix );
            }
        }

        // Set the blend state
        m_pd3dDevice->SetRenderState( D3DRS_VERTEXBLEND, blendNumber );

        // Look up material used for the subset
        if( (previousAttributeId != boneComboBuffer[attribute].AttribId) ||
            (previousAttributeId == UNUSED32) )
        {
            // Set material
            m_pd3dDevice->SetMaterial(
                &pMeshContainer->pMaterials[boneComboBuffer[attribute].AttribId].MatD3D );

            // Set texture
            m_pd3dDevice->SetTexture( 0,
                 pMeshContainer->ppTextures[boneComboBuffer[attribute].AttribId]);

            // Store the attribute ID number
            previousAttributeId = boneComboBuffer[attribute].AttribId;
        }

        // Draw the mesh
        hr = pMeshContainer->pMesh->DrawSubset( attribute );

        // Check the return code
        if( FAILED( hr ) )
            return hr;
    }

    // Draw the rest with software processing
    if( pMeshContainer->dwStartSoftwareRenderAttribute < pMeshContainer->dwNumAttributeGroups )
    {
        // Enable software vertex processing
        m_pd3dDevice->SetSoftwareVertexProcessing( TRUE );

        // Draw using software vertex processing
        for( attribute = pMeshContainer->dwStartSoftwareRenderAttribute;
             attribute < pMeshContainer->dwNumAttributeGroups;
           ++attribute )
        {
            // Reset the blend number
            blendNumber = 0;

            // Calc world matrices
            for( DWORD i = 0; i < pMeshContainer->dwMaxFaceInfluences; ++i )
            {
                matrixIndex = boneComboBuffer[attribute].BoneId[i];
                if( matrixIndex != UINT_MAX )
                {
                    blendNumber = i;
                    D3DXMatrixMultiply( &temporaryMatrix,
                                        &pMeshContainer->pBoneMatrixOffsets[matrixIndex],
                                         pMeshContainer->ppBoneMatrixPointers[matrixIndex] );
                    m_pd3dDevice->SetTransform( D3DTS_WORLDMATRIX(i), &temporaryMatrix );
                }
            }

            // Set the blend state
            m_pd3dDevice->SetRenderState( D3DRS_VERTEXBLEND, blendNumber );

            // Look up material used for the subset
            if( (previousAttributeId != boneComboBuffer[attribute].AttribId) ||
                (previousAttributeId == UNUSED32) )
            {
                // Set material
                m_pd3dDevice->SetMaterial(
                    &pMeshContainer->pMaterials[boneComboBuffer[attribute].AttribId].MatD3D );

                // Set texture
                m_pd3dDevice->SetTexture( 0,
                     pMeshContainer->ppTextures[boneComboBuffer[attribute].AttribId]);

                // Store the attribute ID number
                previousAttributeId = boneComboBuffer[attribute].AttribId;
            }

            // Draw the mesh
            hr = pMeshContainer->pMesh->DrawSubset( attribute );

            // Check the return code
            if( FAILED( hr ) )
                return hr;
        }


        // Disable software vertex processing
        m_pd3dDevice->SetSoftwareVertexProcessing( FALSE );
    }

    // Set the blend state
    m_pd3dDevice->SetRenderState( D3DRS_VERTEXBLEND, 0 );

    // Success
    return S_OK;

}
