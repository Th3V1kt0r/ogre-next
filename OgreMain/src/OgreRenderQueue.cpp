/*
-----------------------------------------------------------------------------
This source file is part of OGRE-Next
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreStableHeaders.h"

#include "OgreRenderQueue.h"

#include "CommandBuffer/OgreCbDrawCall.h"
#include "CommandBuffer/OgreCbPipelineStateObject.h"
#include "CommandBuffer/OgreCbShaderBuffer.h"
#include "CommandBuffer/OgreCommandBuffer.h"
#include "OgreHardwareBufferManager.h"
#include "OgreHlms.h"
#include "OgreHlmsDatablock.h"
#include "OgreHlmsManager.h"
#include "OgreMaterial.h"
#include "OgreMaterialManager.h"
#include "OgreMovableObject.h"
#include "OgrePass.h"
#include "OgreProfiler.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneManagerEnumerator.h"
#include "OgreTechnique.h"
#include "OgreTimer.h"
#include "ParticleSystem/OgreParticleSystem2.h"
#include "Vao/OgreConstBufferPacked.h"
#include "Vao/OgreIndexBufferPacked.h"
#include "Vao/OgreIndirectBufferPacked.h"
#include "Vao/OgreReadOnlyBufferPacked.h"
#include "Vao/OgreVaoManager.h"
#include "Vao/OgreVertexArrayObject.h"

namespace Ogre
{
    AtomicScalar<uint32> v1::RenderOperation::MeshIndexId( 0 );

    const HlmsCache c_dummyCache( 0, HLMS_MAX, HLMS_CACHE_FLAGS_NONE, HlmsPso() );

    // clang-format off
    const int RqBits::SubRqIdBits           = 3;
    const int RqBits::TransparencyBits      = 1;
    const int RqBits::MacroblockBits        = 10;
    const int RqBits::ShaderBits            = 10;    //The higher 3 bits contain HlmsTypes
    const int RqBits::MeshBits              = 14;
    const int RqBits::TextureBits           = 11;
    const int RqBits::DepthBits             = 15;

    const int RqBits::SubRqIdShift          = 64                - SubRqIdBits;      //61
    const int RqBits::TransparencyShift     = SubRqIdShift      - TransparencyBits; //60
    const int RqBits::MacroblockShift       = TransparencyShift - MacroblockBits;   //50
    const int RqBits::ShaderShift           = MacroblockShift   - ShaderBits;       //40
    const int RqBits::MeshShift             = ShaderShift       - MeshBits;         //26
    const int RqBits::TextureShift          = MeshShift         - TextureBits;      //15
    const int RqBits::DepthShift            = TextureShift      - DepthBits;        //0

    const int RqBits::DepthShiftTransp      = TransparencyShift - DepthBits;        //45
    const int RqBits::MacroblockShiftTransp = DepthShiftTransp  - MacroblockBits;   //35
    const int RqBits::ShaderShiftTransp     = MacroblockShiftTransp - ShaderBits;   //25
    const int RqBits::MeshShiftTransp       = ShaderShiftTransp - MeshBits;         //11
    const int RqBits::TextureShiftTransp    = MeshShiftTransp   - TextureBits;      //0
    // clang-format on

    //---------------------------------------------------------------------
    RenderQueue::RenderQueue( HlmsManager *hlmsManager, SceneManager *sceneManager,
                              VaoManager *vaoManager ) :
        mHlmsManager( hlmsManager ),
        mSceneManager( sceneManager ),
        mVaoManager( vaoManager ),
        mRoot( Root::getSingletonPtr() ),
        mLastWasCasterPass( false ),
        mLastVaoName( 0 ),
        mLastVertexData( 0 ),
        mLastIndexData( 0 ),
        mLastTextureHash( 0 ),
        mCommandBuffer( 0 ),
        mRenderingStarted( 0u )
    {
        mCommandBuffer = new CommandBuffer();

        for( size_t i = 0; i < 256; ++i )
            mRenderQueues[i].mQueuedRenderablesPerThread.resize( sceneManager->getNumWorkerThreads() );

        // Set some defaults:
        // RQs [0; 100)   and [200; 225) are for v2 objects
        // RQs [100; 200) and [225; 256) are for v1 objects
        for( size_t i = 100u; i < 200u; ++i )
            setRenderQueueMode( static_cast<uint8>( i ), V1_FAST );
        for( size_t i = 225; i < 256u; ++i )
            setRenderQueueMode( static_cast<uint8>( i ), V1_FAST );

        setRenderQueueMode( kParticleSystemDefaultRenderQueueId, PARTICLE_SYSTEM );
    }
    //---------------------------------------------------------------------
    RenderQueue::~RenderQueue()
    {
        _releaseManualHardwareResources();

        delete mCommandBuffer;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::_releaseManualHardwareResources()
    {
        mCommandBuffer->clear();

        for( IndirectBufferPacked *buf : mUsedIndirectBuffers )
        {
            if( buf->getMappingState() != MS_UNMAPPED )
                buf->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyIndirectBuffer( buf );
        }
        mUsedIndirectBuffers.clear();

        for( IndirectBufferPacked *buf : mFreeIndirectBuffers )
        {
            if( buf->getMappingState() != MS_UNMAPPED )
                buf->unmap( UO_UNMAP_ALL );
            mVaoManager->destroyIndirectBuffer( buf );
        }
        mFreeIndirectBuffers.clear();
    }
    //-----------------------------------------------------------------------
    IndirectBufferPacked *RenderQueue::getIndirectBuffer( size_t numDraws )
    {
        size_t requiredBytes = numDraws * sizeof( CbDrawIndexed );

        IndirectBufferPackedVec::iterator itor = mFreeIndirectBuffers.begin();
        IndirectBufferPackedVec::iterator endt = mFreeIndirectBuffers.end();

        size_t smallestBufferSize = std::numeric_limits<size_t>::max();
        IndirectBufferPackedVec::iterator smallestBuffer = endt;

        // Find the smallest buffer in the pool that can fit the request.
        while( itor != endt )
        {
            size_t bufferSize = ( *itor )->getTotalSizeBytes();
            if( requiredBytes <= bufferSize && smallestBufferSize > bufferSize )
            {
                smallestBuffer = itor;
                smallestBufferSize = bufferSize;
            }

            ++itor;
        }

        if( smallestBuffer == endt )
        {
            // None found? Create a new one.
            mFreeIndirectBuffers.push_back(
                mVaoManager->createIndirectBuffer( requiredBytes, BT_DYNAMIC_PERSISTENT, 0, false ) );
            smallestBuffer = mFreeIndirectBuffers.end() - 1;
        }

        IndirectBufferPacked *retVal = *smallestBuffer;

        mUsedIndirectBuffers.push_back( *smallestBuffer );
        efficientVectorRemove( mFreeIndirectBuffers, smallestBuffer );

        return retVal;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::clear()
    {
        for( size_t i = 0; i < 256; ++i )
        {
            QueuedRenderableArrayPerThread::iterator itor =
                mRenderQueues[i].mQueuedRenderablesPerThread.begin();
            QueuedRenderableArrayPerThread::iterator endt =
                mRenderQueues[i].mQueuedRenderablesPerThread.end();

            while( itor != endt )
            {
                itor->q.clear();
                ++itor;
            }

            mRenderQueues[i].mQueuedRenderables.clear();
            mRenderQueues[i].mSorted = false;
        }
    }
    //-----------------------------------------------------------------------
    void RenderQueue::clearState()
    {
        mLastWasCasterPass = false;
        mLastVaoName = 0;
        mLastVertexData = 0;
        mLastIndexData = 0;
        mLastTextureHash = 0;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::addRenderableV1( uint8 renderQueueId, bool casterPass, Renderable *pRend,
                                       const MovableObject *pMovableObject )
    {
        addRenderable( 0, renderQueueId, casterPass, pRend, pMovableObject, true );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::addRenderableV2( size_t threadIdx, uint8 renderQueueId, bool casterPass,
                                       Renderable *pRend, const MovableObject *pMovableObject )
    {
        addRenderable( threadIdx, renderQueueId, casterPass, pRend, pMovableObject, false );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::addRenderable( size_t threadIdx, uint8 rqId, bool casterPass, Renderable *pRend,
                                     const MovableObject *pMovableObject, bool isV1 )
    {
        assert( rqId == pMovableObject->getRenderQueueGroup() );

        uint8 subId = pRend->getRenderQueueSubGroup();
        RealAsUint depth = pMovableObject->getCachedDistanceToCamera();

        assert( !mRenderQueues[rqId].mSorted && "Called addRenderable after render and before clear" );
        assert( subId <= OGRE_RQ_MAKE_MASK( RqBits::SubRqIdBits ) );

        uint32 hlmsHash = casterPass ? pRend->getHlmsCasterHash() : pRend->getHlmsHash();
        const HlmsDatablock *datablock = pRend->getDatablock();

        const bool transparent = datablock->mBlendblock[casterPass]->mIsTransparent != 0u;

        uint16 macroblock = datablock->mMacroblockHash[casterPass];
        uint16 texturehash = static_cast<uint16>( datablock->mTextureHash );

        // Flip the float to deal with negative & positive numbers
#if OGRE_DOUBLE_PRECISION == 0
        RealAsUint mask = static_cast<uint32>( -int( depth >> 31 ) ) | 0x80000000;
        depth = ( depth ^ mask );
#else
        RealAsUint mask = -int64( depth >> 63 ) | 0x8000000000000000;
        depth = ( depth ^ mask ) >> 32;
#endif
        uint32 quantizedDepth = static_cast<uint32>( depth ) >> ( 32 - RqBits::DepthBits );

        uint32 meshHash;

        if( isV1 )
        {
            v1::RenderOperation op;
            // v2 objects cache the alpha testing information. v1 is evaluated in realtime.
            pRend->getRenderOperation( op,
                                       casterPass & ( datablock->getAlphaTest() == CMPF_ALWAYS_PASS ) );
            meshHash = op.meshIndex;
        }
        else
        {
            uint8 meshLod = pMovableObject->getCurrentMeshLod();
            const VertexArrayObjectArray &vaos = pRend->getVaos( static_cast<VertexPass>( casterPass ) );

            assert( meshLod < vaos.size() &&
                    "Vaos meshLod/shadowLod not set. "
                    "Note: If this is a v1 object, it is in the wrong RenderQueue ID "
                    "(or the queue incorrectly set)." );

            VertexArrayObject *vao = vaos[meshLod];
            meshHash = vao->getRenderQueueId();
        }
        // TODO: Account for skeletal animation in any of the hashes (preferently on the material side)
        // TODO: Account for auto instancing animation in any of the hashes

#define OGRE_RQ_HASH( x, bits, shift ) ( uint64( (x)&OGRE_RQ_MAKE_MASK( ( bits ) ) ) << ( shift ) )

        uint64 hash;
        if( !transparent )
        {
            // clang-format off
            // Opaque objects are first sorted by material, then by mesh, then by depth front to back.
            hash =
            OGRE_RQ_HASH( subId,            RqBits::SubRqIdBits,        RqBits::SubRqIdShift )      |
            OGRE_RQ_HASH( transparent,      RqBits::TransparencyBits,   RqBits::TransparencyShift ) |
            OGRE_RQ_HASH( macroblock,       RqBits::MacroblockBits,     RqBits::MacroblockShift )   |
            OGRE_RQ_HASH( hlmsHash,         RqBits::ShaderBits,         RqBits::ShaderShift )       |
            OGRE_RQ_HASH( meshHash,         RqBits::MeshBits,           RqBits::MeshShift )         |
            OGRE_RQ_HASH( texturehash,      RqBits::TextureBits,        RqBits::TextureShift )      |
            OGRE_RQ_HASH( quantizedDepth,   RqBits::DepthBits,          RqBits::DepthShift );
            // clang-format on
        }
        else
        {
            // Transparent objects are sorted by depth back to front, then by material, then by mesh.
            quantizedDepth = quantizedDepth ^ 0xffffffff;
            // clang-format off
            hash =
            OGRE_RQ_HASH( subId,            RqBits::SubRqIdBits,        RqBits::SubRqIdShift )          |
            OGRE_RQ_HASH( transparent,      RqBits::TransparencyBits,   RqBits::TransparencyShift )     |
            OGRE_RQ_HASH( quantizedDepth,   RqBits::DepthBits,          RqBits::DepthShiftTransp )      |
            OGRE_RQ_HASH( macroblock,       RqBits::MacroblockBits,     RqBits::MacroblockShiftTransp ) |
            OGRE_RQ_HASH( hlmsHash,         RqBits::ShaderBits,         RqBits::ShaderShiftTransp )     |
            OGRE_RQ_HASH( meshHash,         RqBits::MeshBits,           RqBits::MeshShiftTransp );
            // clang-format on
        }

#undef OGRE_RQ_HASH

        mRenderQueues[rqId].mQueuedRenderablesPerThread[threadIdx].q.push_back(
            QueuedRenderable( hash, pRend, pMovableObject ) );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::renderPassPrepare( bool casterPass, bool dualParaboloid )
    {
        OgreProfileGroup( "Hlms Pass preparation", OGREPROF_RENDERING );

        ++mRenderingStarted;
        mRoot->_notifyRenderingFrameStarted();

        const size_t numWorkerThreads = mSceneManager->getNumWorkerThreads();
        const bool bUseMultithreadedShaderCompliation =
            mRoot->getRenderSystem()->supportsMultithreadedShaderCompilation() &&
            mSceneManager->getNumWorkerThreads() > 1u;

        for( size_t i = 0; i < HLMS_MAX; ++i )
        {
            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( i ) );
            if( hlms )
            {
                if( bUseMultithreadedShaderCompliation )
                    hlms->_setNumThreads( numWorkerThreads );

                mPassCache[i] = hlms->preparePassHash( mSceneManager->getCurrentShadowNode(), casterPass,
                                                       dualParaboloid, mSceneManager );
            }
        }
    }
    //-----------------------------------------------------------------------
    void RenderQueue::render( RenderSystem *rs, uint8 firstRq, uint8 lastRq, bool casterPass,
                              bool dualParaboloid )
    {
        if( mLastWasCasterPass != casterPass )
        {
            clearState();
            mLastWasCasterPass = casterPass;
        }

        OgreProfileBeginGroup( "Command Preparation", OGREPROF_RENDERING );

        rs->setCurrentPassIterationCount( 1 );

        size_t numNeededDraws = 0u;
        size_t numNeededV2Draws = 0u;
        size_t numNeededParticleDraws = 0u;
        for( size_t i = firstRq; i < lastRq; ++i )
        {
            if( mRenderQueues[i].mMode == FAST )
            {
                for( const ThreadRenderQueue &threadRenderQueue :
                     mRenderQueues[i].mQueuedRenderablesPerThread )
                {
                    numNeededV2Draws += threadRenderQueue.q.size();
                }
            }
            else if( mRenderQueues[i].mMode == PARTICLE_SYSTEM )
            {
                for( const ThreadRenderQueue &threadRenderQueue :
                     mRenderQueues[i].mQueuedRenderablesPerThread )
                {
                    numNeededParticleDraws += threadRenderQueue.q.size();
                }
            }
        }

        numNeededDraws = numNeededV2Draws + numNeededParticleDraws;

        mCommandBuffer->setCurrentRenderSystem( rs );

        ParallelHlmsCompileQueue *parallelCompileQueue = 0;

        if( rs->supportsMultithreadedShaderCompilation() && mSceneManager->getNumWorkerThreads() > 1u )
        {
            parallelCompileQueue = &mParallelHlmsCompileQueue;
            mParallelHlmsCompileQueue.start( mSceneManager, casterPass );
        }

        bool supportsIndirectBuffers = mVaoManager->supportsIndirectBuffers();

        IndirectBufferPacked *indirectBuffer = 0;
        unsigned char *indirectDraw = 0;
        unsigned char *startIndirectDraw = 0;

        if( numNeededDraws > 0 )
        {
            indirectBuffer = getIndirectBuffer( numNeededDraws );

            if( supportsIndirectBuffers )
            {
                indirectDraw = static_cast<unsigned char *>(
                    indirectBuffer->map( 0, indirectBuffer->getNumElements() ) );
            }
            else
            {
                indirectDraw = indirectBuffer->getSwBufferPtr();
            }

            startIndirectDraw = indirectDraw;
        }

        for( size_t i = firstRq; i < lastRq; ++i )
        {
            QueuedRenderableArray &queuedRenderables = mRenderQueues[i].mQueuedRenderables;
            QueuedRenderableArrayPerThread &perThreadQueue =
                mRenderQueues[i].mQueuedRenderablesPerThread;

            if( !mRenderQueues[i].mSorted )
            {
                OgreProfileGroupAggregate( "Sorting", OGREPROF_RENDERING );

                size_t numRenderables = 0;
                QueuedRenderableArrayPerThread::const_iterator itor = perThreadQueue.begin();
                QueuedRenderableArrayPerThread::const_iterator endt = perThreadQueue.end();

                while( itor != endt )
                {
                    numRenderables += itor->q.size();
                    ++itor;
                }

                queuedRenderables.reserve( numRenderables );

                itor = perThreadQueue.begin();
                while( itor != endt )
                {
                    queuedRenderables.appendPOD( itor->q.begin(), itor->q.end() );
                    ++itor;
                }

                // TODO: Exploit temporal coherence across frames then use insertion sorts.
                // As explained by L. Spiro in
                // http://www.gamedev.net/topic/661114-temporal-coherence-and-render-queue-sorting/?view=findpost&p=5181408
                // Keep a list of sorted indices from the previous frame (one per camera).
                // If we have the sorted list "5, 1, 4, 3, 2, 0":
                //  * If it grew from last frame, append: 5, 1, 4, 3, 2, 0, 6, 7 and use insertion sort.
                //  * If it's the same, leave it as is, and use insertion sort just in case.
                //  * If it's shorter, reset the indices 0, 1, 2, 3, 4; probably use quicksort or other
                //  generic sort
                //
                // TODO2: Explore sorting first on multiple threads, then merge sort into one.
                if( mRenderQueues[i].mSortMode == NormalSort )
                {
                    std::sort( queuedRenderables.begin(), queuedRenderables.end() );
                    mRenderQueues[i].mSorted = true;
                }
                else if( mRenderQueues[i].mSortMode == StableSort )
                {
                    std::stable_sort( queuedRenderables.begin(), queuedRenderables.end() );
                    mRenderQueues[i].mSorted = true;
                }
            }

            if( mRenderQueues[i].mMode == V1_LEGACY )
            {
                if( mLastVaoName )
                {
                    rs->_startLegacyV1Rendering();
                    mLastVaoName = 0;
                }
                renderES2( rs, casterPass, dualParaboloid, mPassCache, mRenderQueues[i] );
            }
            else if( mRenderQueues[i].mMode == V1_FAST )
            {
                if( mLastVaoName )
                {
                    *mCommandBuffer->addCommand<v1::CbStartV1LegacyRendering>() =
                        v1::CbStartV1LegacyRendering();
                    mLastVaoName = 0;
                }
                renderGL3V1( rs, casterPass, dualParaboloid, mPassCache, mRenderQueues[i],
                             parallelCompileQueue );
            }
            else if( numNeededParticleDraws > 0 && mRenderQueues[i].mMode == PARTICLE_SYSTEM )
            {
                indirectDraw =
                    renderParticles( rs, casterPass, mPassCache, mRenderQueues[i], parallelCompileQueue,
                                     indirectBuffer, indirectDraw, startIndirectDraw );
            }
            else if( numNeededV2Draws > 0 /*&& mRenderQueues[i].mMode == FAST*/ )
            {
                indirectDraw =
                    renderGL3( rs, casterPass, dualParaboloid, mPassCache, mRenderQueues[i],
                               parallelCompileQueue, indirectBuffer, indirectDraw, startIndirectDraw );
            }
        }

        if( supportsIndirectBuffers && indirectBuffer )
            indirectBuffer->unmap( UO_KEEP_PERSISTENT );

        if( parallelCompileQueue )
            mParallelHlmsCompileQueue.stopAndWait( mSceneManager );

        OgreProfileEndGroup( "Command Preparation", OGREPROF_RENDERING );

        OgreProfileBeginGroup( "Command Execution", OGREPROF_RENDERING );
        OgreProfileGpuBegin( "Command Execution" );

        for( size_t i = 0; i < HLMS_MAX; ++i )
        {
            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( i ) );
            if( hlms )
                hlms->preCommandBufferExecution( mCommandBuffer );
        }

        mCommandBuffer->execute();

        for( size_t i = 0; i < HLMS_MAX; ++i )
        {
            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( i ) );
            if( hlms )
                hlms->postCommandBufferExecution( mCommandBuffer );
        }

        --mRenderingStarted;

        OgreProfileGpuEnd( "Command Execution" );
        OgreProfileEndGroup( "Command Execution", OGREPROF_RENDERING );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::warmUpShadersCollect( const uint8 firstRq, const uint8 lastRq,
                                            const bool casterPass )
    {
        OgreProfileBeginGroup( "RenderQueue::warmUpShadersCollect", OGREPROF_RENDERING );

        for( size_t i = firstRq; i < lastRq; ++i )
        {
            QueuedRenderableArray &queuedRenderables = mRenderQueues[i].mQueuedRenderables;
            QueuedRenderableArrayPerThread &perThreadQueue =
                mRenderQueues[i].mQueuedRenderablesPerThread;

            if( !mRenderQueues[i].mSorted )
            {
                OgreProfileGroupAggregate( "Merging per-thread queues", OGREPROF_RENDERING );

                size_t numRenderables = 0;
                QueuedRenderableArrayPerThread::const_iterator itor = perThreadQueue.begin();
                QueuedRenderableArrayPerThread::const_iterator endt = perThreadQueue.end();

                while( itor != endt )
                {
                    numRenderables += itor->q.size();
                    ++itor;
                }

                queuedRenderables.reserve( numRenderables );

                itor = perThreadQueue.begin();
                while( itor != endt )
                {
                    queuedRenderables.appendPOD( itor->q.begin(), itor->q.end() );
                    ++itor;
                }

                mRenderQueues[i].mSorted = true;
            }

            // All render queue modes share the same path
            warmUpShaders( casterPass, mRenderQueues[i] );
        }

        mPendingPassCaches.insert( mPendingPassCaches.end(), mPassCache, mPassCache + HLMS_MAX );

        --mRenderingStarted;

        OgreProfileEndGroup( "RenderQueue::warmUpShadersCollect", OGREPROF_RENDERING );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::warmUpShadersTrigger( RenderSystem *rs )
    {
        OgreProfileBeginGroup( "RenderQueue::warmUpShadersTrigger", OGREPROF_RENDERING );

        if( rs->supportsMultithreadedShaderCompilation() && mSceneManager->getNumWorkerThreads() > 1u )
            mParallelHlmsCompileQueue.fireWarmUpParallel( mSceneManager );
        else
            mParallelHlmsCompileQueue.warmUpSerial( mHlmsManager, mPendingPassCaches.data() );

        mPendingPassCaches.clear();

        OgreProfileEndGroup( "RenderQueue::warmUpShadersTrigger", OGREPROF_RENDERING );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::renderES2( RenderSystem *rs, bool casterPass, bool dualParaboloid,
                                 HlmsCache passCache[HLMS_MAX],
                                 const RenderQueueGroup &renderQueueGroup )
    {
        v1::VertexData const *lastVertexData = mLastVertexData;
        v1::IndexData const *lastIndexData = mLastIndexData;
        HlmsCache const *lastHlmsCache = &c_dummyCache;
        uint32 lastHlmsCacheHash = 0;
        uint32 lastTextureHash = mLastTextureHash;
        // uint32 lastVertexDataId = ~0;

        const QueuedRenderableArray &queuedRenderables = renderQueueGroup.mQueuedRenderables;

        QueuedRenderableArray::const_iterator itor = queuedRenderables.begin();
        QueuedRenderableArray::const_iterator endt = queuedRenderables.end();

        while( itor != endt )
        {
            const QueuedRenderable &queuedRenderable = *itor;
            /*uint32 hlmsHash = casterPass ? queuedRenderable.renderable->getHlmsCasterHash() :
                                           queuedRenderable.renderable->getHlmsHash();*/
            const HlmsDatablock *datablock = queuedRenderable.renderable->getDatablock();
            v1::RenderOperation op;
            queuedRenderable.renderable->getRenderOperation(
                op, casterPass & ( datablock->getAlphaTest() == CMPF_ALWAYS_PASS ) );

            if( lastVertexData != op.vertexData )
            {
                lastVertexData = op.vertexData;
            }
            if( lastIndexData != op.indexData )
            {
                lastIndexData = op.indexData;
            }

            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );

            lastHlmsCacheHash = lastHlmsCache->hash;
            const HlmsCache *hlmsCache = hlms->getMaterial( lastHlmsCache, passCache[datablock->mType],
                                                            queuedRenderable, casterPass, nullptr );
            if( lastHlmsCacheHash != hlmsCache->hash )
            {
                rs->_setPipelineStateObject( &hlmsCache->pso );
                lastHlmsCache = hlmsCache;
            }

            lastTextureHash = hlms->fillBuffersFor( hlmsCache, queuedRenderable, casterPass,
                                                    lastHlmsCacheHash, lastTextureHash );

            const v1::CbRenderOp cmd( op );
            rs->_setRenderOperation( &cmd );

            rs->_render( op );

            ++itor;
        }

        mLastVertexData = lastVertexData;
        mLastIndexData = lastIndexData;
        mLastTextureHash = lastTextureHash;
    }
    //-----------------------------------------------------------------------
    uint8 *RenderQueue::renderParticles( RenderSystem *rs, const bool casterPass, HlmsCache passCache[],
                                         const RenderQueueGroup &renderQueueGroup,
                                         ParallelHlmsCompileQueue *parallelCompileQueue,
                                         IndirectBufferPacked *indirectBuffer, uint8 *indirectDraw,
                                         uint8 *startIndirectDraw )
    {
        uint32 lastVaoName = mLastVaoName;
        HlmsCache const *lastHlmsCache = &c_dummyCache;
        uint32 lastHlmsCacheHash = 0;

        int baseInstanceAndIndirectBuffers = 0;
        if( mVaoManager->supportsIndirectBuffers() )
            baseInstanceAndIndirectBuffers = 2;
        else if( mVaoManager->supportsBaseInstance() )
            baseInstanceAndIndirectBuffers = 1;

        const bool isUsingInstancedStereo = mSceneManager->isUsingInstancedStereo();
        const uint32 instancesPerDraw = isUsingInstancedStereo ? 2u : 1u;
        const uint32 baseInstanceShift = isUsingInstancedStereo ? 1u : 0u;

        CbDrawCall *drawCmd = 0;

        RenderingMetrics stats;

        uint8 particleSystemSlot[HLMS_MAX][2];
        for( size_t i = 0u; i < HLMS_MAX; ++i )
        {
            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( i ) );
            if( hlms )
            {
                particleSystemSlot[i][0] = hlms->getParticleSystemConstSlot();
                particleSystemSlot[i][1] = hlms->getParticleSystemSlot();
            }
            else
            {
                particleSystemSlot[i][0] = 0u;
                particleSystemSlot[i][1] = 0u;
            }
        }

        const QueuedRenderableArray &queuedRenderables = renderQueueGroup.mQueuedRenderables;

        QueuedRenderableArray::const_iterator itor = queuedRenderables.begin();
        QueuedRenderableArray::const_iterator endt = queuedRenderables.end();

        while( itor != endt )
        {
            const QueuedRenderable &queuedRenderable = *itor;
            const VertexArrayObjectArray &vaos = queuedRenderable.renderable->getVaos( VpNormal );

            VertexArrayObject *vao = vaos[0];
            const HlmsDatablock *datablock = queuedRenderable.renderable->getDatablock();

            const HlmsTypes hlmsType = static_cast<HlmsTypes>( datablock->mType );
            Hlms *hlms = mHlmsManager->getHlms( hlmsType );

            lastHlmsCacheHash = lastHlmsCache->hash;
            const HlmsCache *hlmsCache =
                hlms->getMaterial( lastHlmsCache, passCache[datablock->mType], queuedRenderable,
                                   casterPass, parallelCompileQueue );
            if( lastHlmsCacheHash != hlmsCache->hash )
            {
                CbPipelineStateObject *psoCmd = mCommandBuffer->addCommand<CbPipelineStateObject>();
                *psoCmd = CbPipelineStateObject( &hlmsCache->pso );
                lastHlmsCache = hlmsCache;

                // Flush the Vao when changing shaders. Needed by D3D11/12 & possibly Vulkan
                lastVaoName = 0;
            }

            const ParticleSystemDef *systemDef =
                ParticleSystemDef::castFromRenderable( queuedRenderable.renderable );

            const uint32 baseInstance = hlms->fillBuffersForV2( hlmsCache, queuedRenderable, casterPass,
                                                                lastHlmsCacheHash, mCommandBuffer );

            ConstBufferPacked *particleCommonBuffer = systemDef->_getGpuCommonBuffer();
            *mCommandBuffer->addCommand<CbShaderBuffer>() =
                CbShaderBuffer( VertexShader, particleSystemSlot[hlmsType][0], particleCommonBuffer, 0,
                                (uint32)particleCommonBuffer->getTotalSizeBytes() );
            ReadOnlyBufferPacked *particleDataBuffer = systemDef->_getGpuDataBuffer();
            *mCommandBuffer->addCommand<CbShaderBuffer>() =
                CbShaderBuffer( VertexShader, particleSystemSlot[hlmsType][1], particleDataBuffer, 0,
                                (uint32)particleDataBuffer->getTotalSizeBytes() );

            // We always break the commands (because we re-bind particleDataBuffer for every draw)
            {
                // Different mesh, vertex buffers or layout. Make a new draw call.
                //(or also the the Hlms made a batch-breaking command)

                OGRE_ASSERT_MEDIUM( vao->getVaoName() != 0u &&
                                    "Invalid Vao name! This can happen if a BT_IMMUTABLE buffer was "
                                    "recently created and VaoManager::_beginFrame() wasn't called" );

                if( lastVaoName != vao->getVaoName() )
                {
                    *mCommandBuffer->addCommand<CbVao>() = CbVao( vao );
                    *mCommandBuffer->addCommand<CbIndirectBuffer>() = CbIndirectBuffer( indirectBuffer );
                    lastVaoName = vao->getVaoName();
                }

                void *offset = reinterpret_cast<void *>(
                    static_cast<ptrdiff_t>( indirectBuffer->_getFinalBufferStart() ) +
                    ( indirectDraw - startIndirectDraw ) );

                if( vao->getIndexBuffer() )
                {
                    CbDrawCallIndexed *drawCall = mCommandBuffer->addCommand<CbDrawCallIndexed>();
                    *drawCall = CbDrawCallIndexed( baseInstanceAndIndirectBuffers, vao, offset );
                    drawCmd = drawCall;
                }
                else
                {
                    CbDrawCallStrip *drawCall = mCommandBuffer->addCommand<CbDrawCallStrip>();
                    *drawCall = CbDrawCallStrip( baseInstanceAndIndirectBuffers, vao, offset );
                    drawCmd = drawCall;
                }

                stats.mDrawCount += 1u;
            }

            // Different mesh, but same vertex buffers & layouts. Advance indirection buffer.
            ++drawCmd->numDraws;

            if( vao->mIndexBuffer )
            {
                CbDrawIndexed *drawIndexedPtr = reinterpret_cast<CbDrawIndexed *>( indirectDraw );
                indirectDraw += sizeof( CbDrawIndexed );

                drawIndexedPtr->primCount = vao->mPrimCount;
                drawIndexedPtr->instanceCount = instancesPerDraw;
                drawIndexedPtr->firstVertexIndex =
                    uint32( vao->mIndexBuffer->_getFinalBufferStart() + vao->mPrimStart );
                drawIndexedPtr->baseVertex = uint32( vao->mBaseVertexBuffer->_getFinalBufferStart() );
                drawIndexedPtr->baseInstance = baseInstance << baseInstanceShift;
            }
            else
            {
                CbDrawStrip *drawStripPtr = reinterpret_cast<CbDrawStrip *>( indirectDraw );
                indirectDraw += sizeof( CbDrawStrip );

                drawStripPtr->primCount = vao->mPrimCount;
                drawStripPtr->instanceCount = instancesPerDraw;
                drawStripPtr->firstVertexIndex =
                    uint32( vao->mBaseVertexBuffer->_getFinalBufferStart() + vao->mPrimStart );
                drawStripPtr->baseInstance = baseInstance << baseInstanceShift;
            }

            stats.mInstanceCount += instancesPerDraw;

            switch( vao->getOperationType() )
            {
            case OT_TRIANGLE_LIST:
                stats.mFaceCount += ( vao->mPrimCount / 3u ) * instancesPerDraw;
                break;
            case OT_TRIANGLE_STRIP:
            case OT_TRIANGLE_FAN:
                stats.mFaceCount += ( vao->mPrimCount - 2u ) * instancesPerDraw;
                break;
            default:
                break;
            }

            stats.mVertexCount += vao->mPrimCount * instancesPerDraw;

            ++itor;
        }

        rs->_addMetrics( stats );

        mLastVaoName = lastVaoName;
        mLastVertexData = 0;
        mLastIndexData = 0;
        mLastTextureHash = 0;

        return indirectDraw;
    }
    //-----------------------------------------------------------------------
    unsigned char *RenderQueue::renderGL3( RenderSystem *rs, bool casterPass, bool dualParaboloid,
                                           HlmsCache passCache[],
                                           const RenderQueueGroup &renderQueueGroup,
                                           ParallelHlmsCompileQueue *parallelCompileQueue,
                                           IndirectBufferPacked *indirectBuffer,
                                           unsigned char *indirectDraw,
                                           unsigned char *startIndirectDraw )
    {
        VertexArrayObject *lastVao = 0;
        uint32 lastVaoName = mLastVaoName;
        HlmsCache const *lastHlmsCache = &c_dummyCache;
        uint32 lastHlmsCacheHash = 0;

        int baseInstanceAndIndirectBuffers = 0;
        if( mVaoManager->supportsIndirectBuffers() )
            baseInstanceAndIndirectBuffers = 2;
        else if( mVaoManager->supportsBaseInstance() )
            baseInstanceAndIndirectBuffers = 1;

        const bool isUsingInstancedStereo = mSceneManager->isUsingInstancedStereo();
        const uint32 instancesPerDraw = isUsingInstancedStereo ? 2u : 1u;
        const uint32 baseInstanceShift = isUsingInstancedStereo ? 1u : 0u;
        uint32 instanceCount = instancesPerDraw;

        CbDrawCall *drawCmd = 0;
        CbSharedDraw *drawCountPtr = 0;

        RenderingMetrics stats;

        const QueuedRenderableArray &queuedRenderables = renderQueueGroup.mQueuedRenderables;

        QueuedRenderableArray::const_iterator itor = queuedRenderables.begin();
        QueuedRenderableArray::const_iterator endt = queuedRenderables.end();

        while( itor != endt )
        {
            const QueuedRenderable &queuedRenderable = *itor;
            uint8 meshLod = queuedRenderable.movableObject->getCurrentMeshLod();
            const VertexArrayObjectArray &vaos =
                queuedRenderable.renderable->getVaos( static_cast<VertexPass>( casterPass ) );

            VertexArrayObject *vao = vaos[meshLod];
            const HlmsDatablock *datablock = queuedRenderable.renderable->getDatablock();

            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );

            lastHlmsCacheHash = lastHlmsCache->hash;
            const HlmsCache *hlmsCache =
                hlms->getMaterial( lastHlmsCache, passCache[datablock->mType], queuedRenderable,
                                   casterPass, parallelCompileQueue );
            if( lastHlmsCacheHash != hlmsCache->hash )
            {
                CbPipelineStateObject *psoCmd = mCommandBuffer->addCommand<CbPipelineStateObject>();
                *psoCmd = CbPipelineStateObject( &hlmsCache->pso );
                lastHlmsCache = hlmsCache;

                // Flush the Vao when changing shaders. Needed by D3D11/12 & possibly Vulkan
                lastVaoName = 0;
            }

            uint32 baseInstance = hlms->fillBuffersForV2( hlmsCache, queuedRenderable, casterPass,
                                                          lastHlmsCacheHash, mCommandBuffer );

            if( drawCmd != mCommandBuffer->getLastCommand() || lastVaoName != vao->getVaoName() )
            {
                // Different mesh, vertex buffers or layout. Make a new draw call.
                //(or also the the Hlms made a batch-breaking command)

                OGRE_ASSERT_MEDIUM( vao->getVaoName() != 0u &&
                                    "Invalid Vao name! This can happen if a BT_IMMUTABLE buffer was "
                                    "recently created and VaoManager::_beginFrame() wasn't called" );

                if( lastVaoName != vao->getVaoName() )
                {
                    *mCommandBuffer->addCommand<CbVao>() = CbVao( vao );
                    *mCommandBuffer->addCommand<CbIndirectBuffer>() = CbIndirectBuffer( indirectBuffer );
                    lastVaoName = vao->getVaoName();
                }

                void *offset = reinterpret_cast<void *>(
                    static_cast<ptrdiff_t>( indirectBuffer->_getFinalBufferStart() ) +
                    ( indirectDraw - startIndirectDraw ) );

                if( vao->getIndexBuffer() )
                {
                    CbDrawCallIndexed *drawCall = mCommandBuffer->addCommand<CbDrawCallIndexed>();
                    *drawCall = CbDrawCallIndexed( baseInstanceAndIndirectBuffers, vao, offset );
                    drawCmd = drawCall;
                }
                else
                {
                    CbDrawCallStrip *drawCall = mCommandBuffer->addCommand<CbDrawCallStrip>();
                    *drawCall = CbDrawCallStrip( baseInstanceAndIndirectBuffers, vao, offset );
                    drawCmd = drawCall;
                }

                lastVao = 0;
                stats.mDrawCount += 1u;
            }

            if( lastVao != vao )
            {
                // Different mesh, but same vertex buffers & layouts. Advance indirection buffer.
                ++drawCmd->numDraws;

                if( vao->mIndexBuffer )
                {
                    CbDrawIndexed *drawIndexedPtr = reinterpret_cast<CbDrawIndexed *>( indirectDraw );
                    indirectDraw += sizeof( CbDrawIndexed );

                    drawCountPtr = drawIndexedPtr;
                    drawIndexedPtr->primCount = vao->mPrimCount;
                    drawIndexedPtr->instanceCount = instancesPerDraw;
                    drawIndexedPtr->firstVertexIndex =
                        uint32( vao->mIndexBuffer->_getFinalBufferStart() + vao->mPrimStart );
                    drawIndexedPtr->baseVertex =
                        uint32( vao->mBaseVertexBuffer->_getFinalBufferStart() );
                    drawIndexedPtr->baseInstance = baseInstance << baseInstanceShift;

                    instanceCount = instancesPerDraw;
                }
                else
                {
                    CbDrawStrip *drawStripPtr = reinterpret_cast<CbDrawStrip *>( indirectDraw );
                    indirectDraw += sizeof( CbDrawStrip );

                    drawCountPtr = drawStripPtr;
                    drawStripPtr->primCount = vao->mPrimCount;
                    drawStripPtr->instanceCount = instancesPerDraw;
                    drawStripPtr->firstVertexIndex =
                        uint32( vao->mBaseVertexBuffer->_getFinalBufferStart() + vao->mPrimStart );
                    drawStripPtr->baseInstance = baseInstance << baseInstanceShift;

                    instanceCount = instancesPerDraw;
                }

                lastVao = vao;
                stats.mInstanceCount += instancesPerDraw;
            }
            else
            {
                // Same mesh. Just go with instancing. Keep the counter in
                // an external variable, as the region can be write-combined
                instanceCount += instancesPerDraw;
                drawCountPtr->instanceCount = instanceCount;
                stats.mInstanceCount += instancesPerDraw;
            }

            switch( vao->getOperationType() )
            {
            case OT_TRIANGLE_LIST:
                stats.mFaceCount += ( vao->mPrimCount / 3u ) * instancesPerDraw;
                break;
            case OT_TRIANGLE_STRIP:
            case OT_TRIANGLE_FAN:
                stats.mFaceCount += ( vao->mPrimCount - 2u ) * instancesPerDraw;
                break;
            default:
                break;
            }

            stats.mVertexCount += vao->mPrimCount * instancesPerDraw;

            ++itor;
        }

        rs->_addMetrics( stats );

        mLastVaoName = lastVaoName;
        mLastVertexData = 0;
        mLastIndexData = 0;
        mLastTextureHash = 0;

        return indirectDraw;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::renderGL3V1( RenderSystem *rs, bool casterPass, bool dualParaboloid,
                                   HlmsCache passCache[], const RenderQueueGroup &renderQueueGroup,
                                   ParallelHlmsCompileQueue *parallelCompileQueue )
    {
        v1::RenderOperation lastRenderOp;
        HlmsCache const *lastHlmsCache = &c_dummyCache;
        uint32 lastHlmsCacheHash = 0;

        const bool supportsBaseInstance = mVaoManager->supportsBaseInstance();

        const bool isUsingInstancedStereo = mSceneManager->isUsingInstancedStereo();
        const uint32 instancesPerDraw = isUsingInstancedStereo ? 2u : 1u;
        const uint32 baseInstanceShift = isUsingInstancedStereo ? 1u : 0u;

        uint32 instanceCount = instancesPerDraw;

        v1::CbDrawCall *drawCmd = 0;

        RenderingMetrics stats;

        const QueuedRenderableArray &queuedRenderables = renderQueueGroup.mQueuedRenderables;

        QueuedRenderableArray::const_iterator itor = queuedRenderables.begin();
        QueuedRenderableArray::const_iterator endt = queuedRenderables.end();

        while( itor != endt )
        {
            const QueuedRenderable &queuedRenderable = *itor;

            const HlmsDatablock *datablock = queuedRenderable.renderable->getDatablock();

            v1::RenderOperation renderOp;
            queuedRenderable.renderable->getRenderOperation(
                renderOp, casterPass & ( datablock->getAlphaTest() == CMPF_ALWAYS_PASS ) );

            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );

            lastHlmsCacheHash = lastHlmsCache->hash;
            const HlmsCache *hlmsCache =
                hlms->getMaterial( lastHlmsCache, passCache[datablock->mType], queuedRenderable,
                                   casterPass, parallelCompileQueue );
            if( lastHlmsCache != hlmsCache )
            {
                CbPipelineStateObject *psoCmd = mCommandBuffer->addCommand<CbPipelineStateObject>();
                *psoCmd = CbPipelineStateObject( &hlmsCache->pso );
                lastHlmsCache = hlmsCache;

                // Flush the RenderOp when changing shaders. Needed by D3D11/12 & possibly Vulkan
                lastRenderOp.vertexData = 0;
                lastRenderOp.indexData = 0;
            }

            uint32 baseInstance = hlms->fillBuffersForV1( hlmsCache, queuedRenderable, casterPass,
                                                          lastHlmsCacheHash, mCommandBuffer );

            bool differentRenderOp = lastRenderOp.vertexData != renderOp.vertexData ||
                                     lastRenderOp.indexData != renderOp.indexData ||
                                     lastRenderOp.operationType != renderOp.operationType ||
                                     lastRenderOp.useGlobalInstancingVertexBufferIsAvailable !=
                                         renderOp.useGlobalInstancingVertexBufferIsAvailable;

            if( drawCmd != mCommandBuffer->getLastCommand() || differentRenderOp )
            {
                // Different mesh, vertex buffers or layout. If instanced, entities
                // likely use their own low level materials. Make a new draw call.
                //(or also the the Hlms made a batch-breaking command)

                if( differentRenderOp )
                {
                    *mCommandBuffer->addCommand<v1::CbRenderOp>() = v1::CbRenderOp( renderOp );
                    lastRenderOp = renderOp;
                }

                if( renderOp.useIndexes )
                {
                    v1::CbDrawCallIndexed *drawCall =
                        mCommandBuffer->addCommand<v1::CbDrawCallIndexed>();
                    *drawCall = v1::CbDrawCallIndexed( supportsBaseInstance );

                    /*drawCall->useGlobalInstancingVertexBufferIsAvailable =
                            renderOp.useGlobalInstancingVertexBufferIsAvailable;*/
                    drawCall->primCount = (uint32)renderOp.indexData->indexCount;
                    drawCall->instanceCount = instancesPerDraw;
                    drawCall->firstVertexIndex = (uint32)renderOp.indexData->indexStart;
                    drawCall->baseInstance = baseInstance << baseInstanceShift;

                    instanceCount = instancesPerDraw;

                    drawCmd = drawCall;
                }
                else
                {
                    v1::CbDrawCallStrip *drawCall = mCommandBuffer->addCommand<v1::CbDrawCallStrip>();
                    *drawCall = v1::CbDrawCallStrip( supportsBaseInstance );

                    /*drawCall->useGlobalInstancingVertexBufferIsAvailable =
                            renderOp.useGlobalInstancingVertexBufferIsAvailable;*/
                    drawCall->primCount = (uint32)renderOp.vertexData->vertexCount;
                    drawCall->instanceCount = instancesPerDraw;
                    drawCall->firstVertexIndex = (uint32)renderOp.vertexData->vertexStart;
                    drawCall->baseInstance = baseInstance << baseInstanceShift;

                    instanceCount = instancesPerDraw;

                    drawCmd = drawCall;
                }

                stats.mDrawCount += 1u;
                stats.mInstanceCount += instancesPerDraw;
            }
            else
            {
                // Same mesh. Just go with instancing. Keep the counter in
                // an external variable, as the region can be write-combined
                instanceCount += instancesPerDraw;
                drawCmd->instanceCount = instanceCount;
                stats.mInstanceCount += instancesPerDraw;
            }

            size_t primCount =
                renderOp.useIndexes ? renderOp.indexData->indexCount : renderOp.vertexData->vertexCount;
            switch( renderOp.operationType )
            {
            case OT_TRIANGLE_LIST:
                stats.mFaceCount += ( primCount / 3u ) * instancesPerDraw;
                break;
            case OT_TRIANGLE_STRIP:
            case OT_TRIANGLE_FAN:
                stats.mFaceCount += ( primCount - 2u ) * instancesPerDraw;
                break;
            default:
                break;
            }

            stats.mVertexCount += renderOp.vertexData->vertexCount * instancesPerDraw;

            ++itor;
        }

        rs->_addMetrics( stats );

        mLastVaoName = 0;
        mLastVertexData = 0;
        mLastIndexData = 0;
        mLastTextureHash = 0;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::warmUpShaders( const bool casterPass, const RenderQueueGroup &renderQueueGroup )
    {
        uint32 lastHlmsCacheHash = 0;

        const size_t passCacheBaseIdx = mPendingPassCaches.size();

        for( const QueuedRenderable &queuedRenderable : renderQueueGroup.mQueuedRenderables )
        {
            const HlmsDatablock *datablock = queuedRenderable.renderable->getDatablock();

            Hlms *hlms = mHlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );

            lastHlmsCacheHash = hlms->getMaterialSerial01(
                lastHlmsCacheHash, mPassCache[datablock->mType], datablock->mType + passCacheBaseIdx,
                queuedRenderable, casterPass, mParallelHlmsCompileQueue );
        }
    }
    //-----------------------------------------------------------------------
    void RenderQueue::_warmUpShadersThread( const size_t threadIdx )
    {
#ifdef OGRE_SHADER_THREADING_BACKWARDS_COMPATIBLE_API
#    ifdef OGRE_SHADER_THREADING_USE_TLS
        Hlms::msThreadId = static_cast<uint32>( threadIdx );
#    endif
#endif
        mParallelHlmsCompileQueue.updateWarmUpThread( threadIdx, mHlmsManager,
                                                      mPendingPassCaches.data() );
    }
    //-----------------------------------------------------------------------
    void RenderQueue::_compileShadersThread( size_t threadIdx )
    {
        mParallelHlmsCompileQueue.updateThread( threadIdx, mHlmsManager );
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::start( SceneManager *sceneManager, bool casterPass )
    {
        mKeepCompiling = true;
        uint32 timeout =
            casterPass ? 0u : Root::getSingleton().getRenderSystem()->getPsoRequestsTimeout();
        mCompilationDeadline = timeout == 0u
                                   ? UINT64_MAX
                                   : ( Root::getSingleton().getTimer()->getMilliseconds() + timeout );
        mCompilationIncompleteCounter = 0;
        sceneManager->_fireParallelHlmsCompile();
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::stopAndWait( SceneManager *sceneManager )
    {
        // There is no need to incur in the perf penalty of locking mMutex because we guarantee
        // no more entries will be added to mRequests. (Unless I missed something?).
        // The mMutex locks (and mSemaphore) already guarantee ordering.
        mKeepCompiling.store( false, std::memory_order::memory_order_relaxed );
        mSemaphore.increment( static_cast<uint32_t>( sceneManager->getNumWorkerThreads() ) );
        sceneManager->waitForParallelHlmsCompile();

        Root::getSingleton().getRenderSystem()->_notifyIncompletePsoRequests(
            mCompilationIncompleteCounter );
        mCompilationIncompleteCounter = 0;

        // No need to use mMutex because we guarantee no write access from other threads
        // after this point.
        //
        // IMPORTANT: After this exception is caught, the Hlms will likely be left in an inconsistent
        // state. We pretty much can't do anything more unless some heavy reinitialization is done.
        if( mExceptionFound )
        {
            std::exception_ptr threadedException = mThreadedException;
            mRequests.clear();  // We terminated threads early. (Does it matter?)
            mThreadedException = nullptr;
            mExceptionFound = false;
            std::rethrow_exception( threadedException );
        }
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::fireWarmUpParallel( SceneManager *sceneManager )
    {
        mCompilationDeadline = UINT64_MAX;
        mCompilationIncompleteCounter = 0;
        sceneManager->_fireWarmUpShadersCompile();
        OGRE_ASSERT_LOW( mRequests.empty() );  // Should be empty, whether we found an exception or not.
        // See comments in ParallelHlmsCompileQueue::stopAndWait implementation.
        // The only difference is that mRequests should be empty by now.
        if( mExceptionFound )
        {
            std::exception_ptr threadedException = mThreadedException;
            mThreadedException = nullptr;
            mExceptionFound = false;
            std::rethrow_exception( threadedException );
        }
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::updateWarmUpThread( size_t threadIdx, HlmsManager *hlmsManager,
                                                       const HlmsCache *passCaches )
    {
#ifdef OGRE_SHADER_THREADING_BACKWARDS_COMPATIBLE_API
#    ifdef OGRE_SHADER_THREADING_USE_TLS
        Hlms::msThreadId = static_cast<uint32>( threadIdx );
#    endif
#endif
        while( true )
        {
            mMutex.lock();
            if( mRequests.empty() )
            {
                mMutex.unlock();
                break;
            }
            Request request = std::move( mRequests.back() );
            mRequests.pop_back();
            mMutex.unlock();

            const HlmsDatablock *datablock = request.queuedRenderable.renderable->getDatablock();
            Hlms *hlms = hlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );
            try
            {
                hlms->compileStubEntry( passCaches[reinterpret_cast<size_t>( request.passCache )],
                                        request.reservedStubEntry, UINT64_MAX, request.queuedRenderable,
                                        request.renderableHash, request.finalHash, threadIdx );
                OGRE_ASSERT_LOW( request.reservedStubEntry->flags == HLMS_CACHE_FLAGS_NONE );
            }
            catch( Exception & )
            {
                ScopedLock lock( mMutex );
                // We can only report one exception.
                if( !mExceptionFound )
                {
                    mRequests.clear();  // Only way to signal other threads to stop early.
                    mExceptionFound = true;
                    mThreadedException = std::current_exception();
                }
            }
        }
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::warmUpSerial( HlmsManager *hlmsManager, const HlmsCache *passCaches )
    {
        mCompilationDeadline = UINT64_MAX;
        mCompilationIncompleteCounter = 0;
        for( const Request &request : mRequests )
        {
            const HlmsDatablock *datablock = request.queuedRenderable.renderable->getDatablock();
            Hlms *hlms = hlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );
            hlms->compileStubEntry( passCaches[reinterpret_cast<size_t>( request.passCache )],
                                    request.reservedStubEntry, UINT64_MAX, request.queuedRenderable,
                                    request.renderableHash, request.finalHash, 0u );
            OGRE_ASSERT_LOW( request.reservedStubEntry->flags == HLMS_CACHE_FLAGS_NONE );
        }

        mRequests.clear();
    }
    //-----------------------------------------------------------------------
    void ParallelHlmsCompileQueue::updateThread( size_t threadIdx, HlmsManager *hlmsManager )
    {
#ifdef OGRE_SHADER_THREADING_BACKWARDS_COMPATIBLE_API
#    ifdef OGRE_SHADER_THREADING_USE_TLS
        Hlms::msThreadId = static_cast<uint32>( threadIdx );
#    endif
#endif
        bool bStillHasWork = false;
        bool bKeepCompiling = true;

        ParallelHlmsCompileQueue::Request request;

        while( bKeepCompiling || bStillHasWork )
        {
            mSemaphore.decrementOrWait();

            bool bWorkGrabbed = false;

            mMutex.lock();
            if( !mRequests.empty() )
            {
                request = std::move( mRequests.back() );
                mRequests.pop_back();
                bWorkGrabbed = true;
            }
            bStillHasWork = !mRequests.empty();
            // mKeepCompiling MUST be read inside or before the mMutex, OTHERWISE if read afterwards:
            //  1. We could see mRequests is empty, bStillHasWork = false
            //  2. We leave mMutex
            //  3. More work is added to mRequests
            //  4. mKeepCompiling is set to false
            //  5. We read mKeepCompiling == false, bStillHasWork == false. We finish
            //  6. mRequests still has entries unprocessed
            //
            // By reading mKeepCompiling inside this lock, we guarantee that if bStillHasWork = false and
            // mKeepCompiling = false, then no more entries will be added to mRequests.
            //
            // If bStillHasWork = false then after we leave:
            //  1. If bKeepCompiling is true.
            //      a. After abandoning mMutex main thread adds more work
            //      b. Main thread sets mKeepCompiling to false
            //      c. bKeepCompiling is still true, so we will iterate once more
            //  2. If bKeepCompiling is true.
            //      a. Main thread sets mKeepCompiling to false
            //      b. bKeepCompiling is still true, so we will iterate once more
            //      c. mSemaphore will not stall, and we will see bStillHasWork = false &
            //          bKeepCompiling = false
            //      d. We finish
            //  3. If bKeepCompiling is false.
            //      a. After abandoning mMutex, we are guaranteed no more work will be added
            //
            // There is no need for main thread to lock mMutex just to set mKeepCompiling = false.
            // TSAN complains because it doesn't know we guarantee after main thread sets
            // mKeepCompiling = true it won't add more work.
            //
            // We use memory_order_relaxed because the mutex already guarantees ordering.
            bKeepCompiling = mKeepCompiling.load( std::memory_order::memory_order_relaxed );
            mMutex.unlock();

            if( bWorkGrabbed )
            {
                const HlmsDatablock *datablock = request.queuedRenderable.renderable->getDatablock();
                Hlms *hlms = hlmsManager->getHlms( static_cast<HlmsTypes>( datablock->mType ) );
                try
                {
                    hlms->compileStubEntry( *request.passCache, request.reservedStubEntry,
                                            mCompilationDeadline, request.queuedRenderable,
                                            request.renderableHash, request.finalHash, threadIdx );
                    if( request.reservedStubEntry->flags == HLMS_CACHE_FLAGS_COMPILATION_REQUIRED )
                        mCompilationIncompleteCounter.fetch_add(
                            1, std::memory_order::memory_order_relaxed );
                }
                catch( Exception & )
                {
                    ScopedLock lock( mMutex );
                    // We can only report one exception.
                    if( !mExceptionFound )
                    {
                        mKeepCompiling.store( false, std::memory_order::memory_order_relaxed );
                        mExceptionFound = true;
                        mThreadedException = std::current_exception();
                    }
                }
            }
        }
    }
    //-----------------------------------------------------------------------
    void RenderQueue::renderSingleObject( Renderable *pRend, const MovableObject *pMovableObject,
                                          RenderSystem *rs, bool casterPass, bool dualParaboloid )
    {
        if( mLastVaoName )
        {
            rs->_startLegacyV1Rendering();
            mLastVaoName = 0;
        }

        if( mLastWasCasterPass != casterPass )
        {
            clearState();
            mLastWasCasterPass = casterPass;
        }

        const HlmsDatablock *datablock = pRend->getDatablock();

        ++mRenderingStarted;
        mRoot->_notifyRenderingFrameStarted();

        Hlms *hlms = datablock->getCreator();
        HlmsCache passCache = hlms->preparePassHash( mSceneManager->getCurrentShadowNode(), casterPass,
                                                     dualParaboloid, mSceneManager );

        const QueuedRenderable queuedRenderable( 0, pRend, pMovableObject );
        v1::RenderOperation op;
        queuedRenderable.renderable->getRenderOperation(
            op, casterPass & ( datablock->getAlphaTest() == CMPF_ALWAYS_PASS ) );
        /*uint32 hlmsHash = casterPass ? queuedRenderable.renderable->getHlmsCasterHash() :
                                       queuedRenderable.renderable->getHlmsHash();*/

        if( mLastVertexData != op.vertexData )
        {
            mLastVertexData = op.vertexData;
        }
        if( mLastIndexData != op.indexData )
        {
            mLastIndexData = op.indexData;
        }

        const HlmsCache *hlmsCache =
            hlms->getMaterial( &c_dummyCache, passCache, queuedRenderable, casterPass, nullptr );
        rs->_setPipelineStateObject( &hlmsCache->pso );

        mLastTextureHash =
            hlms->fillBuffersFor( hlmsCache, queuedRenderable, casterPass, 0, mLastTextureHash );

        const v1::CbRenderOp cmd( op );
        rs->_setRenderOperation( &cmd );

        rs->_render( op );

        mLastVaoName = 0;
        --mRenderingStarted;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::frameEnded()
    {
        OGRE_ASSERT_LOW(
            mRenderingStarted == 0u &&
            "Called RenderQueue::frameEnded mid-render. This may happen if VaoManager::_update got "
            "called after RenderQueue::renderPassPrepare but before RenderQueue::render returns. Please "
            "move that VaoManager::_update call outside, otherwise we cannot guarantee rendering will "
            "be glitch-free, as the BufferPacked buffers from Hlms may be bound at the wrong offset. "
            "For more info see https://github.com/OGRECave/ogre-next/issues/33 and "
            "https://forums.ogre3d.org/viewtopic.php?f=25&t=95092#p545907" );

        mFreeIndirectBuffers.insert( mFreeIndirectBuffers.end(), mUsedIndirectBuffers.begin(),
                                     mUsedIndirectBuffers.end() );
        mUsedIndirectBuffers.clear();
    }
    //-----------------------------------------------------------------------
    void RenderQueue::setRenderQueueMode( uint8 rqId, Modes newMode )
    {
        mRenderQueues[rqId].mMode = newMode;
    }
    //-----------------------------------------------------------------------
    RenderQueue::Modes RenderQueue::getRenderQueueMode( uint8 rqId ) const
    {
        return mRenderQueues[rqId].mMode;
    }
    //-----------------------------------------------------------------------
    void RenderQueue::setSortRenderQueue( uint8 rqId, RqSortMode sortMode )
    {
        mRenderQueues[rqId].mSortMode = sortMode;
    }
    //-----------------------------------------------------------------------
    RenderQueue::RqSortMode RenderQueue::getSortRenderQueue( uint8 rqId ) const
    {
        return mRenderQueues[rqId].mSortMode;
    }
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    ParallelHlmsCompileQueue::ParallelHlmsCompileQueue() :
        mSemaphore( 0u ),
        mKeepCompiling( false ),
        mCompilationDeadline( 0 ),
        mCompilationIncompleteCounter( 0 ),
        mExceptionFound( false )
    {
    }
}  // namespace Ogre
