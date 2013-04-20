//------------------------------------------------------------------------------------------------
// File:    animation.h
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
#ifndef __ANIMATION_H__
#define __ANIMATION_H__


/**
 * Encapsulates a frame in the allocation hierarchy
 *   @author Karl Gluck
 */
struct MeshFrame : public D3DXFRAME
{
    /// Combined transformation matrix for this frame
    D3DXMATRIXA16 matCombined;
};


/**
 * Derived mesh container structure to set up for doing skinned meshes
 *   @author Karl Gluck
 */
struct MeshContainer : public D3DXMESHCONTAINER
{
    /// Textures used by this mesh
    IDirect3DTexture9** ppTextures;

    /// The mesh that does the dirty work for this container.
    ID3DXMesh* pMesh;

    /// Bone offset matrices retrieved from the D3DXMESHCONTAINER::pSkinInfo interface
    D3DXMATRIX* pBoneMatrixOffsets;

    /// Array used to look up transformation matrices.  The only function in this class,
    /// createBonePointers, sets up this array.
    ///     @see createBonePointers
    D3DXMATRIX** ppBoneMatrixPointers;

    /// Maximum number of matrix influences on a single face
    DWORD dwMaxFaceInfluences;

    /// How many different subsets the mesh has
    DWORD dwNumAttributeGroups;

    /// Buffer in which bones are combined to do animation
    ID3DXBuffer* pBoneCombinationBuffer;

    /// Which subset of the mesh to begin rendering with software
    DWORD dwStartSoftwareRenderAttribute;

    /**
     * Utility function to initialize the bone pointers for this mesh container
     *   @param pFrameRoot Root frame from which to generate pointers
     *   @return Result of the function
     */
    HRESULT CreateBonePointers( MeshFrame * pFrameRoot );
};


/**
 * Allocation class that is used by the "D3DXLoadMeshHierarchyFromX*" functions to handle the
 * creation and removal of frame objects.  All of the defined functions are callbacks, so
 * there is no need to call them explicitly--just pass an instance of this class to the
 * function and it will handle the rest.<br><br>
 *
 * This class supports a software workaround for a maximum number of blended matrices.  If the
 * user's computer doesn't support 4 blended matrices in hardware, it will use a software
 * switch for the parts of the mesh that use that many matrices.  This requires that the D3D
 * device be created in mixed or software vertex processing mode, though.<br><br>
 *
 * There is the option to change the way resources are loaded by deriving from this class and
 * overriding the appropriate resource loading function in order to support encrypted files or
 * other methods of storage.
 *
 *   @author Karl Gluck
 */
class AllocateHierarchy : public ID3DXAllocateHierarchy
{
    public:

        /**
         * Sets up the allocation hierarchy object
         *   @param dwMaxBlendedMatrices Maximum number of matrices that can affect each face of
         *                               the mesh.
         *   @param distributor Resource loading mechanism
         */
        AllocateHierarchy( DWORD dwMaxBlendedMatrices );

        /**
         * Creates a frame using the custom derived structure.
         *   @param strName Name of the new frame to create
         *   @param ppFrame Address to the pointer to be filled with the newly allocated
         *                   frame object.
         *   @return Code designating whether or not this function succeeded.
         *   @see dcAnimation_Frame
         */
        STDMETHOD(CreateFrame)(THIS_ LPCSTR strName, D3DXFRAME** ppFrame );

        /**
         * Creates a mesh container using source information.
         *   @param strName Name of the mesh
         *   @param pMeshData Mesh information
         *   @param pMaterials Materials of the mesh
         *   @param pEffects Effects on the mesh
         *   @param dwMtrlCount The number of materials this mesh has
         *   @param pAdjacency Adjacency array of the mesh
         *   @param pSkinInfo Skin information for the mesh
         *   @param ppContainer Output mesh container
         *   @return Success/failure HRESULT code
         */
        STDMETHOD(CreateMeshContainer)(THIS_ LPCSTR strName,
                                       const D3DXMESHDATA* pMeshData,
                                       const D3DXMATERIAL* pMaterials,
                                       const D3DXEFFECTINSTANCE* pEffects,
                                       DWORD dwMtrlCount,
                                       const DWORD* pAdjacency,
                                       ID3DXSkinInfo* pSkinInfo,
                                       D3DXMESHCONTAINER** ppContainer );

        /**
         * Destroys a frame that was allocated with CreateFrame
         *   @param pFrame Frame that needs to have its memory freed
         *   @return Success/failure code
         */
        STDMETHOD(DestroyFrame)(THIS_ D3DXFRAME* pFrame );

        /**
         * Used to free the memory used by a mesh container allocated using the function
         * CreateMeshContainer.
         *   @param pContainer The structure to free the memory of
         *   @return Success/failure result
         */
        STDMETHOD(DestroyMeshContainer)(THIS_ D3DXMESHCONTAINER* pContainer );

    public:

        /**
         * Loads a texture resource from its file name.  This function is defined by this class
         * but can be overridden if the app needs to load an encrypted file or load a file from
         * a resource.
         *   @param strFileName Source file name to load
         *   @param ppTexture Destination for resultant texture interface
         *   @return Result code
         */
        STDMETHOD(LoadTexture)(THIS_ LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                                     LPDIRECT3DTEXTURE9 * ppTexture );

        /**
         * Loads a mesh hierarchy from an X file.  This function is defined by this mesh class
         * by default, but can be overridden in case the user needs to load encrypted files or
         * load files from resources.
         *   @param pDevice Source device object to create the structure with
         *   @param strFileName Name of the file to load
         *   @param ppFrameHierarchy Target variable for frame hierarchy
         *   @param ppAnimController Returns a pointer to the animation controller
         *   @return Result code
         */
        STDMETHOD(LoadMeshHierarchyFromX)(THIS_ LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                                                LPD3DXFRAME* ppFrameHierarchy,
                                                LPD3DXANIMATIONCONTROLLER* ppAnimController );
    private:

        /**
         * Takes static text and allocates a buffer for it, then copies the text into the buffer.
         *   @param strString String to allocate
         *   @return Newly allocated string buffer
         */
        TCHAR * AllocateString( LPCSTR strString );

        /// Stores the maximum number of matrix blends that the GPU can do
        DWORD m_dwMaxBlendedMatrices;
};


/**
 * Stores the full definition of a skinned mesh.  The instance class should be used to render
 * it and update its matrices.
 *   @author Karl Gluck
 */
class AnimatedMesh
{
    public:

        /**
         * Initializes internal mesh variables
         */
        AnimatedMesh();

        /**
         * Cleans up the mesh
         */
        ~AnimatedMesh();

        /**
         * Loads this animated mesh from a source X file.
         *   @param pDevice Device to create the mesh on
         *   @param strFileName Name of the file to load
         *   @param pAllocateHierarchy Allocation hierarchy structure
         *   @return Result code
         */
        HRESULT LoadMeshFromX( LPDIRECT3DDEVICE9 pDevice, LPCSTR strFileName,
                               AllocateHierarchy * pAllocateHierarchy );

        /**
         * Deletes this mesh container
         */
        VOID Release();

        /**
         * Creates a copy of the internal animation controller that manages a separate version of
         * this animated mesh.
         *   @param dwSimultaneousTracks How many tracks can exist at once on this controller
         *   @param ppAnimationController Destination for newly created animation controller interface
         *   @return Result code
         */
        HRESULT CloneAnimationController( DWORD dwSimultaneousTracks,
                                          LPD3DXANIMATIONCONTROLLER* ppAnimationController );

        /**
         * Draws the mesh to the screen.  It will be drawn in the last post assigned by a call
         * to the Update function.
         *   @return Result code
         */
        HRESULT Render( const D3DXMATRIX* pWorldMatrix );

    private:

        /**
         * Initializes bone pointers for this frame and all children/siblings
         *   @param pFrame Frame to set bone pointers on
         *   @return Result code
         */
        HRESULT SetupBonePointers( MeshFrame* pFrame );

        /**
         * Does a hierarchial transformation on the frame set specified
         *   @param pFrame Frame to update
         *   @param pParentMatrix Parent transformation matrix
         */
        void UpdateFrames( MeshFrame* pFrame, const D3DXMATRIX* pParentMatrix );

        /**
         * Draws all meshes on this frame or on those that are siblings/children of it
         *   @param pFrame The parent frame to draw
         *   @return Result code
         */
        HRESULT DrawFrames( MeshFrame* pFrame );

        /**
         * Renders the mesh attached to the specified frame
         *   @param pMeshFrame The frame to draw
         *   @return Result code
         */
        HRESULT DrawFrameMesh( MeshFrame* pMeshFrame );

    private:

        /// Local reference to the main rendering device.
        IDirect3DDevice9* m_pd3dDevice;

        /// Root frame that encompasses the entire mesh hierarchy.  This structure is shared
        /// between all instances.
        MeshFrame* m_pFrameRoot;

        /// Controller object that instances clone from to set up their own tracks.  This lets
        /// each instance have its own separate animations running at the same time.
        ID3DXAnimationController* m_pAnimationController;

        /// User allocation hierarchy
        AllocateHierarchy* m_pAllocateHierarchy;
};


/**
 * 
 *   @author Karl Gluck
 */
class AnimatedMeshInstance
{

};


#endif // __ANIMATION_H__