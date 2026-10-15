// TestPchDispatch.cpp
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "FBuildTest.h"

#include "Tools/FBuild/FBuildCore/WorkerPool/PchDispatch.h"

// Core
#include "Core/Containers/Array.h"

// TestPchDispatch
//------------------------------------------------------------------------------
class TestPchDispatch : public FBuildTest
{
private:
    DECLARE_TESTS

    void RunnableJobsGoFirst() const;
    void HeldJobsAreBounded() const;
    void HeldJobsAreForThePchBeingSent() const;
    void NextPchIsTheOneWithMostJobs() const;
    void NextPchIsQueuedBehindTheCurrentOne() const;
    void EmptyQueue() const;

    class IdList : public Array<uint64_t>
    {
    public:
        template <size_t N>
        explicit IdList( const uint64_t ( &values )[ N ] )
        {
            for ( const uint64_t id : values )
            {
                Append( id );
            }
        }
    };

    // The queue holds the PCH id of each job, the last being the next to be taken
    static int32_t Select( const Array<uint64_t> & queue,
                           const Array<uint64_t> & onWorker,
                           const Array<uint64_t> & sending,
                           uint32_t numHeld,
                           uint32_t maxHeld )
    {
        PchWorkerState state;
        state.m_OnWorker = &onWorker;
        state.m_Sending = &sending;
        state.m_NumHeld = numHeld;
        state.m_MaxHeld = maxHeld;
        return PchDispatch::SelectJob( queue.GetSize(), [ &queue ]( size_t i ) { return queue[ i ]; }, state );
    }
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchDispatch )
    REGISTER_TEST( RunnableJobsGoFirst )
    REGISTER_TEST( HeldJobsAreBounded )
    REGISTER_TEST( HeldJobsAreForThePchBeingSent )
    REGISTER_TEST( NextPchIsTheOneWithMostJobs )
    REGISTER_TEST( NextPchIsQueuedBehindTheCurrentOne )
    REGISTER_TEST( EmptyQueue )
REGISTER_TESTS_END

// RunnableJobsGoFirst
//------------------------------------------------------------------------------
void TestPchDispatch::RunnableJobsGoFirst() const
{
    // A job with no PCH can start at once, even behind one that cannot
    {
        const IdList queue( { 1, 2, 1, 0, 1 } );
        TEST_ASSERT( Select( queue, Array<uint64_t>(), Array<uint64_t>(), 0, 4 ) == 3 );
    }

    // So can a job whose PCH the worker has, however many others are waiting for theirs
    {
        const IdList queue( { 2, 1, 1, 1 } );
        const IdList onWorker( { 2 } );
        const IdList sending( { 1 } );
        TEST_ASSERT( Select( queue, onWorker, sending, 0, 4 ) == 0 );

        // The bound on jobs held for a PCH never keeps a runnable job from the worker
        TEST_ASSERT( Select( queue, onWorker, sending, 4, 4 ) == 0 );
    }

    // The most expensive runnable job goes first
    {
        const IdList queue( { 2, 2, 1 } );
        const IdList onWorker( { 2 } );
        TEST_ASSERT( Select( queue, onWorker, Array<uint64_t>(), 0, 4 ) == 1 );
    }
}

// HeldJobsAreBounded
//------------------------------------------------------------------------------
void TestPchDispatch::HeldJobsAreBounded() const
{
    const IdList queue( { 1, 1, 1 } );
    const IdList sending( { 1 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 0, 2 ) == 2 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 1, 2 ) == 2 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 2, 2 ) == -1 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 3, 2 ) == -1 );
}

// HeldJobsAreForThePchBeingSent
//------------------------------------------------------------------------------
void TestPchDispatch::HeldJobsAreForThePchBeingSent() const
{
    // The PCH sent first is the one jobs are held for, not the one with the most jobs
    const IdList queue( { 1, 1, 2, 1 } );
    const IdList sending( { 2, 1 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 0, 4 ) == 2 );

    // Once none are left for it, the PCH sent next takes its place
    const IdList queue2( { 1, 1 } );
    TEST_ASSERT( Select( queue2, Array<uint64_t>(), sending, 0, 4 ) == 1 );
}

// NextPchIsTheOneWithMostJobs
//------------------------------------------------------------------------------
void TestPchDispatch::NextPchIsTheOneWithMostJobs() const
{
    {
        const IdList queue( { 1, 2, 2, 3 } );
        TEST_ASSERT( Select( queue, Array<uint64_t>(), Array<uint64_t>(), 0, 4 ) == 2 );
    }

    // Workers sharing an uplink choose alike: ties go to the lowest id
    {
        const IdList queue( { 5, 3, 5, 3 } );
        TEST_ASSERT( Select( queue, Array<uint64_t>(), Array<uint64_t>(), 0, 4 ) == 3 );
    }
}

// NextPchIsQueuedBehindTheCurrentOne
//------------------------------------------------------------------------------
void TestPchDispatch::NextPchIsQueuedBehindTheCurrentOne() const
{
    // One PCH on its way with no jobs left: the link is kept busy by starting the next
    const IdList queue( { 3, 2, 2 } );
    const IdList sending( { 1 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 0, 4 ) == 2 );

    // Two already on their way: no third is started
    const IdList sendingTwo( { 1, 9 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sendingTwo, 0, 4 ) == -1 );
}

// EmptyQueue
//------------------------------------------------------------------------------
void TestPchDispatch::EmptyQueue() const
{
    TEST_ASSERT( Select( Array<uint64_t>(), Array<uint64_t>(), Array<uint64_t>(), 0, 4 ) == -1 );
}

//------------------------------------------------------------------------------
