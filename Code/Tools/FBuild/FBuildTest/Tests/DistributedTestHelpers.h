// DistributedTestHelpers.h - Fake workers, probes and helpers for tests of the distribution protocol
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------
#include "Tools/FBuild/FBuildTest/Tests/FBuildTest.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/Gateway/GatewayWire.h"
#include "Tools/FBuild/FBuildCore/Helpers/Compressor.h"
#include "Tools/FBuild/FBuildCore/Helpers/JobStream.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchBlob.h"
#include "Tools/FBuild/FBuildCore/Helpers/PchId.h"
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"

// Core
#include "Core/Containers/Array.h"
#include "Core/Env/Types.h"
#include "Core/FileIO/ConstMemoryStream.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/FileStream.h"
#include "Core/FileIO/MemoryStream.h"
#include "Core/Mem/Mem.h"
#include "Core/Network/TCPConnectionPool.h"
#include "Core/Process/Atomic.h"
#include "Core/Process/Mutex.h"
#include "Core/Process/Semaphore.h"
#include "Core/Process/Thread.h"
#include "Core/Strings/AStackString.h"
#include "Core/Time/Timer.h"

#include <stdlib.h>
#include <string.h>

// DistributedTest
//------------------------------------------------------------------------------
namespace DistributedTest
{
    // Sends the chunks of a PCH to a worker, as a coordinator does
    class PchSender : public TCPConnectionPool
    {
    public:
        PchSender() = default;
        virtual ~PchSender() override { ShutdownAllConnections(); }
        explicit PchSender( const PchSender & other ) = delete;
        PchSender & operator=( const PchSender & other ) = delete;

        bool ConnectToWorker()
        {
            m_Connection = Connect( AStackString<>( "127.0.0.1" ), Protocol::kTestPort, 2000 );
            return ( m_Connection != nullptr );
        }

        void DisconnectFromWorker()
        {
            Disconnect( m_Connection );
            m_Connection = nullptr;
        }

        // The chunks from firstChunk up to, not including, endChunk
        bool SendChunks( uint64_t pchId,
                         uint32_t uncompressedSize,
                         const void * compressedData,
                         uint32_t compressedSize,
                         uint32_t chunkSize,
                         uint32_t firstChunk,
                         uint32_t endChunk )
        {
            for ( uint32_t i = firstChunk; i < endChunk; ++i )
            {
                const Protocol::MsgPchFile msg( pchId, uncompressedSize, compressedSize, chunkSize, i );
                const ConstMemoryStream payload( static_cast<const char *>( compressedData ) + ( (size_t)i * chunkSize ),
                                                 Protocol::MsgPchFile::GetChunkLength( compressedSize, chunkSize, i ) );
                if ( msg.Send( m_Connection, payload ) == false )
                {
                    return false;
                }
            }
            return true;
        }

    private:
        const ConnectionInfo * m_Connection = nullptr;
    };

    // A PCH as a client sends it
    struct CompressedPch
    {
        CompressedPch() = default;
        explicit CompressedPch( const CompressedPch & other ) = delete;
        CompressedPch & operator=( const CompressedPch & other ) = delete;

        Array<char>     m_Content;
        Compressor      m_Compressed;
        uint32_t        m_CompressedSize = 0;
        uint64_t        m_Id = 0;
    };

    inline void MakeTestContent( uint32_t size, uint32_t seed, Array<char> & outContent )
    {
        // Compressible, with some variation
        outContent.SetSize( size );
        for ( uint32_t i = 0; i < size; ++i )
        {
            outContent[ i ] = (char)( ( i % 251 ) ^ ( ( i / 4096 ) & 0xFF ) ^ seed );
        }
    }

    // Of its content
    inline void CompressPch( CompressedPch & pch )
    {
        pch.m_Compressed.CompressZstd( pch.m_Content.Begin(), pch.m_Content.GetSize(), Compressor::kZstdTransferLevel );
        pch.m_CompressedSize = (uint32_t)pch.m_Compressed.GetResultSize();
        pch.m_Id = PchId::Calc( pch.m_Content.Begin(), pch.m_Content.GetSize() );
    }

    inline void MakeCompressedPch( uint32_t size, uint32_t seed, CompressedPch & outPch )
    {
        MakeTestContent( size, seed, outPch.m_Content );
        CompressPch( outPch );
    }

    inline bool SendPch( PchSender & sender, const CompressedPch & pch )
    {
        return sender.SendChunks( pch.m_Id,
                                  (uint32_t)pch.m_Content.GetSize(),
                                  pch.m_Compressed.GetResult(),
                                  pch.m_CompressedSize,
                                  Protocol::kPchChunkSize,
                                  0,
                                  Protocol::MsgPchFile::GetNumChunks( pch.m_CompressedSize, Protocol::kPchChunkSize ) );
    }

    inline void GetStatsLogPath( AString & outPath )
    {
        VERIFY( FBuild::GetTempDir( outPath ) );
        outPath += "FBuild-distribution-stats.log";
    }

    // A log is shared by every FBuild on the machine, and a test looks at what is added to it
    inline uint64_t GetStatsLogSize( const AString & logPath )
    {
        FileStream f;
        return f.Open( logPath.Get(), FileStream::READ_ONLY ) ? f.GetFileSize() : 0;
    }

    // False if the log was rotated
    inline bool CountInStatsLogSince( const AString & logPath, uint64_t sizeBefore, const char * text, uint32_t & outCount )
    {
        outCount = 0;
        FileStream f;
        if ( f.Open( logPath.Get(), FileStream::READ_ONLY ) == false )
        {
            return false;
        }
        const uint64_t size = f.GetFileSize();
        if ( size < sizeBefore )
        {
            return false; // It was rotated: what was written is in another file, and what is here is not all of it
        }
        Array<char> added;
        added.SetSize( (size_t)( size - sizeBefore ) + 1 );
        if ( ( f.Seek( sizeBefore ) == false ) || ( f.Read( added.Begin(), (size_t)( size - sizeBefore ) ) != ( size - sizeBefore ) ) )
        {
            return false;
        }
        added[ added.GetSize() - 1 ] = 0;
        for ( const char * found = strstr( added.Begin(), text ); found; found = strstr( found + 1, text ) )
        {
            ++outCount;
        }
        return true;
    }

    // The jobs are built on the worker
    inline void MakeDistributedPchOptions( FBuildTestOptions & options )
    {
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_NoLocalConsumptionOfRemoteJobs = true; // The jobs need the PCH on the worker
        options.m_AllowLocalRace = false; // So that every object is built on the worker
    }

    inline void ClearWorkerPchStore()
    {
        AStackString<> cacheDir;
        VERIFY( FBuild::GetWorkerCacheDir( cacheDir ) );
        Array<AString> files;
        FileIO::GetFiles( cacheDir, AStackString( "*" ), true, &files );
        for ( const AString & file : files )
        {
            if ( file.EndsWith( PchBlob::kStoredFileName ) || file.EndsWith( PchBlob::kRawFileName ) )
            {
                FileIO::FileDelete( file.Get() );
            }
        }
    }

    // Totals over the lines starting with linePrefix in the client's stats log, for lines like
    // "  jobs:      12 msgs, 1.1 MB raw, 40.2 KB sent, ..." (first is raw, second is sent or received)
    inline void ReadStats( const AString & logPath, const char * linePrefix, uint32_t & outNumBlocks, uint64_t & outRawBytes, uint64_t & outOtherBytes, uint32_t * outNumMessages = nullptr )
    {
        outNumBlocks = 0;
        outRawBytes = 0;
        outOtherBytes = 0;
        if ( outNumMessages )
        {
            *outNumMessages = 0;
        }

        FileStream f;
        if ( f.Open( logPath.Get(), FileStream::READ_ONLY ) == false )
        {
            return;
        }
        Array<char> log;
        log.SetSize( (size_t)f.GetFileSize() + 1 );
        if ( f.Read( log.Begin(), log.GetSize() - 1 ) != ( log.GetSize() - 1 ) )
        {
            return;
        }
        log[ log.GetSize() - 1 ] = 0;

        // One block per connection. Sizes are written with a KB or MB unit.
        auto readBytes = []( char *& p )
        {
            const double amount = strtod( p, &p );
            const bool megabytes = ( p[ 1 ] == 'M' );
            return (uint64_t)( amount * ( megabytes ? 1048576.0 : 1024.0 ) );
        };
        const char * line = log.Begin();
        while ( ( line = strstr( line, linePrefix ) ) != nullptr )
        {
            char * p = nullptr;
            const uint32_t numMessages = (uint32_t)strtoul( line + strlen( linePrefix ), &p, 10 );
            p = strstr( p, ", " );
            if ( p == nullptr )
            {
                break;
            }
            p += 2;
            const uint64_t raw = readBytes( p );
            p = strstr( p, "raw, " );
            if ( p == nullptr )
            {
                break;
            }
            p += 5;
            const uint64_t other = readBytes( p );

            ++outNumBlocks;
            outRawBytes += raw;
            outOtherBytes += other;
            if ( outNumMessages )
            {
                *outNumMessages += numMessages;
            }
            line = p;
        }
    }

    // A worker that completes the handshake and leaves the rest to the test
    class FakeWorker : public TCPConnectionPool
    {
    public:
        explicit FakeWorker( uint32_t capacity,
                             const Array<uint64_t> & toolchainInventory = Array<uint64_t>(),
                             const Array<uint64_t> & pchInventory = Array<uint64_t>() )
            : m_Capacity( capacity )
            , m_ToolchainInventory( toolchainInventory )
            , m_PchInventory( pchInventory )
        {
        }
        explicit FakeWorker( const FakeWorker & other ) = delete;
        FakeWorker & operator=( const FakeWorker & other ) = delete;

        uint32_t GetNumConnections() const { return m_NumConnections.Load(); }

        // Ends the connections it has, as when a worker goes away, and listens for the next
        void DisconnectConnections()
        {
            Array<const ConnectionInfo *> connections;
            {
                MutexHolder mh( m_ConnectionsMutex );
                for ( const ConnectionState & state : m_Connections )
                {
                    connections.Append( state.m_Connection );
                }
            }
            for ( const ConnectionInfo * connection : connections )
            {
                Disconnect( connection );
            }
        }

        // Seconds after this worker was created that each connection's handshake arrived
        void GetConnectionTimes( Array<float> & outTimes ) const
        {
            MutexHolder mh( m_ConnectionTimesMutex );
            outTimes = m_ConnectionTimes;
        }

    protected:
        // The reply to the client's handshake: the ack, then the two inventories
        virtual void Handshake( const ConnectionInfo * connection )
        {
            const Protocol::MsgConnectionAck ack( m_Capacity );
            ack.Send( connection );
            SendInventory( connection, Protocol::MsgPchInventory( (uint32_t)m_PchInventory.GetSize() ), m_PchInventory );
            SendInventory( connection, Protocol::MsgToolchainInventory( (uint32_t)m_ToolchainInventory.GetSize() ), m_ToolchainInventory );
        }

        // With the ids as its payload, if there are any
        static void SendInventory( const ConnectionInfo * connection, const Protocol::IMessage & msg, const Array<uint64_t> & ids )
        {
            if ( ids.IsEmpty() )
            {
                msg.Send( connection );
                return;
            }
            MemoryStream payload;
            for ( const uint64_t id : ids )
            {
                payload.Write( id );
            }
            msg.Send( connection, payload );
        }

        // Every message after the handshake. The payload is null for a message that has none.
        virtual void OnMessage( const ConnectionInfo * connection,
                                const Protocol::IMessage & message,
                                void * payload,
                                uint32_t payloadSize ) = 0;

    private:
        virtual void OnReceive( const ConnectionInfo * connection, void * data, uint32_t size, bool & keepMemory ) override
        {
            keepMemory = true; // freed below

            // A message is in two parts if it has a payload, and the connections are read on their own threads
            const Protocol::IMessage * message = nullptr;
            void * payload = nullptr;
            {
                MutexHolder mh( m_ConnectionsMutex );
                ConnectionState * state = nullptr;
                for ( ConnectionState & s : m_Connections )
                {
                    if ( s.m_Connection == connection )
                    {
                        state = &s;
                    }
                }
                if ( state == nullptr )
                {
                    ConnectionState added;
                    added.m_Connection = connection;
                    m_Connections.Append( added );
                    state = &m_Connections[ m_Connections.GetSize() - 1 ];
                }
                if ( state->m_Message == nullptr )
                {
                    state->m_Message = static_cast<const Protocol::IMessage *>( data );
                    if ( state->m_Message->HasPayload() )
                    {
                        return;
                    }
                }
                else
                {
                    payload = data;
                }
                message = state->m_Message;
                state->m_Message = nullptr;
            }

            if ( message->GetType() == Protocol::MSG_CONNECTION )
            {
                {
                    MutexHolder mh( m_ConnectionTimesMutex );
                    m_ConnectionTimes.Append( m_Timer.GetElapsed() );
                }
                m_NumConnections.Increment();
                Handshake( connection );
            }
            else
            {
                OnMessage( connection, *message, payload, size );
            }

            FREE( (void *)message );
            FREE( payload );
        }

        const uint32_t m_Capacity;
        const Array<uint64_t> m_ToolchainInventory;
        const Array<uint64_t> m_PchInventory;
        struct ConnectionState
        {
            const ConnectionInfo *      m_Connection = nullptr;
            const Protocol::IMessage *  m_Message = nullptr; // Its header, until its payload arrives
        };
        mutable Mutex m_ConnectionsMutex;
        Array<ConnectionState> m_Connections;
        Atomic<uint32_t> m_NumConnections{ 0 };
        const Timer m_Timer;
        mutable Mutex m_ConnectionTimesMutex;
        Array<float> m_ConnectionTimes;
    };

    // Connects to a worker and records the messages it sends back, in order
    class HandshakeProbe : public TCPConnectionPool
    {
    public:
        size_t GetNumMessages() const
        {
            MutexHolder mh( m_Mutex );
            return m_MessageTypes.GetSize();
        }
        uint8_t GetMessageType( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_MessageTypes[ index ];
        }
        size_t GetNumPchNeeded() const
        {
            MutexHolder mh( m_Mutex );
            return m_PchNeededIds.GetSize();
        }
        uint64_t GetPchNeededId( size_t index ) const
        {
            MutexHolder mh( m_Mutex );
            return m_PchNeededIds[ index ];
        }
        bool IsDisconnected() const { return m_Disconnected.Load(); }
        uint32_t GetRefusedMajor() const { return m_RefusedMajor.Load(); }
        uint32_t GetRefusedMinor() const { return m_RefusedMinor.Load(); }
        uint32_t GetAckCapacity() const { return m_AckCapacity.Load(); }
        void GetToolchainIds( Array<uint64_t> & outToolIds ) const
        {
            MutexHolder mh( m_Mutex );
            outToolIds = m_ToolchainIds;
        }
        // While set, what arrives is not read, so what the worker sends backs up
        void StallReading( bool stall )
        {
            m_StallReading.Store( stall );
            if ( stall == false )
            {
                m_StallSemaphore.Signal();
            }
        }
        // The results received, in order
        void GetResults( Array<GatewayWire::ResultHeader> & out ) const
        {
            MutexHolder mh( m_Mutex );
            out = m_Results;
        }
        // The MSG_REQUEST_MANIFEST and MSG_REQUEST_FILE received, in order
        struct ToolchainRequest
        {
            uint64_t    m_ToolId = 0;
            uint32_t    m_FileId = 0;
            bool        m_IsManifest = false;
        };
        void GetToolchainRequests( Array<ToolchainRequest> & out ) const
        {
            MutexHolder mh( m_Mutex );
            out = m_ToolchainRequests;
        }

        // A job as a client sends it: the next chunk of the connection's job stream
        bool SendJob( const ConnectionInfo * ci, JobStreamEncoder & encoder, uint64_t toolId, int16_t level, const MemoryStream & job )
        {
            if ( encoder.Encode( job.GetData(), job.GetSize() ) == false )
            {
                return false;
            }
            const Protocol::MsgJob msg( toolId, level );
            return msg.Send( ci, ConstMemoryStream( encoder.GetResult(), encoder.GetResultSize() ) );
        }
        // Without a payload if payload is null
        bool SendMessage( const ConnectionInfo * ci, const Protocol::IMessage & msg, const void * payload, size_t payloadSize )
        {
            return payload ? msg.Send( ci, ConstMemoryStream( payload, payloadSize ) ) : msg.Send( ci );
        }

    private:
        virtual void OnReceive( const ConnectionInfo *, void * data, uint32_t size, bool & keepMemory ) override
        {
            keepMemory = true; // freed below

            while ( m_StallReading.Load() )
            {
                m_StallSemaphore.Wait( 10 );
            }

            void * payload = nullptr;
            if ( m_CurrentMessage == nullptr )
            {
                m_CurrentMessage = static_cast<const Protocol::IMessage *>( data );
                if ( m_CurrentMessage->HasPayload() )
                {
                    return;
                }
            }
            else
            {
                payload = data;
            }

            {
                MutexHolder mh( m_Mutex );
                m_MessageTypes.Append( (uint8_t)m_CurrentMessage->GetType() );
                if ( m_CurrentMessage->GetType() == Protocol::MSG_REFUSED )
                {
                    const Protocol::MsgRefused * refused = static_cast<const Protocol::MsgRefused *>( m_CurrentMessage );
                    m_RefusedMajor.Store( refused->GetProtocolVersionMajor() );
                    m_RefusedMinor.Store( refused->GetProtocolVersionMinor() );
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_CONNECTION_ACK )
                {
                    m_AckCapacity.Store( static_cast<const Protocol::MsgConnectionAck *>( m_CurrentMessage )->GetWorkerCapacity() );
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_PCH_NEEDED )
                {
                    m_PchNeededIds.Append( static_cast<const Protocol::MsgPchNeeded *>( m_CurrentMessage )->GetPchId() );
                }
                if ( ( m_CurrentMessage->GetType() == Protocol::MSG_TOOLCHAIN_INVENTORY ) && payload )
                {
                    const uint64_t * toolIds = static_cast<const uint64_t *>( payload );
                    for ( size_t i = 0; i < ( size / sizeof( uint64_t ) ); ++i )
                    {
                        m_ToolchainIds.Append( toolIds[ i ] );
                    }
                }
                if ( ( m_CurrentMessage->GetType() == Protocol::MSG_JOB_RESULT ) && payload )
                {
                    GatewayWire::ResultHeader header;
                    if ( GatewayWire::ReadResultHeader( payload, size, header ) )
                    {
                        m_Results.Append( header );
                    }
                }
                if ( ( m_CurrentMessage->GetType() == Protocol::MSG_JOB_RESULT_COMPRESSED ) && payload )
                {
                    // Only the data after the header is compressed
                    GatewayWire::ResultHeader header;
                    if ( GatewayWire::ReadResultHeader( payload, size, header ) )
                    {
                        m_Results.Append( header );
                    }
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_REQUEST_MANIFEST )
                {
                    ToolchainRequest & request = m_ToolchainRequests.EmplaceBack();
                    request.m_ToolId = static_cast<const Protocol::MsgRequestManifest *>( m_CurrentMessage )->GetToolId();
                    request.m_IsManifest = true;
                }
                if ( m_CurrentMessage->GetType() == Protocol::MSG_REQUEST_FILE )
                {
                    const Protocol::MsgRequestFile * msg = static_cast<const Protocol::MsgRequestFile *>( m_CurrentMessage );
                    ToolchainRequest & request = m_ToolchainRequests.EmplaceBack();
                    request.m_ToolId = msg->GetToolId();
                    request.m_FileId = msg->GetFileId();
                }
            }

            FREE( (void *)m_CurrentMessage );
            FREE( payload );
            m_CurrentMessage = nullptr;
        }
        virtual void OnDisconnected( const ConnectionInfo * ) override { m_Disconnected.Store( true ); }

        mutable Mutex m_Mutex;
        Array<uint8_t> m_MessageTypes;
        Array<uint64_t> m_ToolchainIds;
        Array<uint64_t> m_PchNeededIds;
        Array<GatewayWire::ResultHeader> m_Results;
        Array<ToolchainRequest> m_ToolchainRequests;
        Atomic<bool> m_Disconnected{ false };
        Atomic<uint32_t> m_RefusedMajor{ 0 };
        Atomic<uint32_t> m_RefusedMinor{ 0 };
        Atomic<uint32_t> m_AckCapacity{ 0 };
        Atomic<bool> m_StallReading{ false };
        Semaphore m_StallSemaphore;
        const Protocol::IMessage * m_CurrentMessage = nullptr;
    };

    // Aborts the build once a condition holds, or after a timeout, so that a test fails instead of hanging.
    // Does nothing if the build ends first.
    class BuildAborter
    {
    public:
        template <class CONDITION>
        BuildAborter( const CONDITION & condition, float timeoutSeconds )
            : m_Condition( &condition )
            , m_Check( []( const void * c ) { return ( *static_cast<const CONDITION *>( c ) )(); } )
            , m_TimeoutSeconds( timeoutSeconds )
        {
            m_Thread.Start( ThreadFunc, "BuildAborter", this );
        }
        ~BuildAborter() { m_Stop.Store( true ); m_Thread.Join(); }
        explicit BuildAborter( const BuildAborter & other ) = delete;
        BuildAborter & operator=( const BuildAborter & other ) = delete;

        bool HasReached() const { return m_Reached.Load(); }    // The condition held
        bool HasTimedOut() const { return m_TimedOut.Load(); }  // It did not hold in time

    private:
        static uint32_t ThreadFunc( void * param )
        {
            BuildAborter * a = static_cast<BuildAborter *>( param );
            const Timer timer;
            while ( a->m_Stop.Load() == false )
            {
                if ( a->m_Check( a->m_Condition ) )
                {
                    a->m_Reached.Store( true );
                    break;
                }
                if ( timer.GetElapsed() > a->m_TimeoutSeconds )
                {
                    a->m_TimedOut.Store( true );
                    break;
                }
                Thread::Sleep( 1 );
            }
            if ( a->m_Stop.Load() == false )
            {
                FBuild::AbortBuild();
            }
            return 0;
        }
        const void * const m_Condition;
        bool ( * const m_Check )( const void * );
        const float m_TimeoutSeconds;
        Thread m_Thread;
        Atomic<bool> m_Stop{ false };
        Atomic<bool> m_Reached{ false };
        Atomic<bool> m_TimedOut{ false };
    };

    // Watches the workers a build gives up on, which it does while the build runs
    class DenyListPoller
    {
    public:
        explicit DenyListPoller( const FBuild & build ) : m_Build( build ) { m_Thread.Start( ThreadFunc, "DenyListPoller", this ); }
        ~DenyListPoller() { m_Stop.Store( true ); m_Thread.Join(); }
        explicit DenyListPoller( const DenyListPoller & other ) = delete;
        DenyListPoller & operator=( const DenyListPoller & other ) = delete;
        uint32_t GetMax() const { return m_Max.Load(); }

    private:
        static uint32_t ThreadFunc( void * param )
        {
            DenyListPoller * p = static_cast<DenyListPoller *>( param );
            while ( p->m_Stop.Load() == false )
            {
                const uint32_t n = p->m_Build.GetNumDenyListedWorkers();
                if ( n > p->m_Max.Load() )
                {
                    p->m_Max.Store( n );
                }
                Thread::Sleep( 1 );
            }
            return 0;
        }
        const FBuild & m_Build;
        Thread m_Thread;
        Atomic<bool> m_Stop{ false };
        Atomic<uint32_t> m_Max{ 0 };
    };

    struct LocalBuildResult
    {
        bool        m_Built = false;
        float       m_Seconds = 0.0f;
        uint32_t    m_MaxDenyListed = 0; // The most workers the build had given up on at one time
    };

    // Builds the library that compiles here, with distribution allowed and a worker listening that is
    // given no job. The reconnect delay is short so that a worker that is tried again shows within the build.
    inline LocalBuildResult BuildPchLocal( TCPConnectionPool & pool, float reconnectDelay = 10.0f )
    {
        LocalBuildResult result;
        FBuildTestOptions options;
        options.m_ConfigFile = "Tools/FBuild/FBuildTest/Data/TestDistributed/fbuild.bff";
        options.m_AllowDistributed = true;
        options.m_NumWorkerThreads = 1;
        options.m_ForceCleanBuild = true;
        options.m_DistributionReconnectDelay = reconnectDelay;
        FBuildForTest fBuild( options );
        if ( ( fBuild.Initialize() == false ) || ( pool.Listen( Protocol::kTestPort ) == false ) )
        {
            return result;
        }
        DenyListPoller poller( fBuild );
        const Timer timer;
        result.m_Built = fBuild.Build( "../tmp/Test/Distributed/Local/distpchlocal.lib" );
        result.m_Seconds = timer.GetElapsed();
        pool.ShutdownAllConnections();
        result.m_MaxDenyListed = poller.GetMax();
        return result;
    }
}
