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
    void JobsWithoutAPchKeepTheirOrder() const;
    void HeldJobsAreBounded() const;
    void HeldJobsAreForThePchBeingSent() const;
    void NextPchIsTheOneWithMostJobs() const;
    void WorkersChooseTheSameNextPch() const;
    void NextPchIsQueuedBehindTheCurrentOne() const;
    void PchsAlreadyRequestedAreNotQueuedAgain() const;
    void NoPchIsQueuedWithoutAFreeSlot() const;
    void PchsQueuedAreBoundedByFreeSlots() const;
    void FirstCopyOfAPchAlwaysGoes() const;
    void FurtherCopiesNeedEnoughJobs() const;
    void PchsAreRankedByTheirShareOfJobs() const;
    void StalledHoldersDoNotCount() const;
    void ConnectionsThatCannotTakeAJobAreNotHolders() const;
    void HoldersAreCountedAcrossConnectionsInOneTick() const;
    void SmallPchIsSkippedForOneWithEnoughJobs() const;
    void EqualSharesGoToTheLowestId() const;
    void NoJobsWaitingMeansNoPchEvenWithAHolder() const;
    void EmptyQueue() const;
    void PchSendStateTransitions() const;
    void PchSendStateWhenTheWorkerNeedsAPch() const;

    static constexpr uint32_t kMaxHeld = 4; // Far more than any case here holds

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
    // The PCHs the queued jobs need, with how many jobs need each
    static constexpr uint32_t kManySlots = 64;
    static uint64_t SelectPch( const Array<uint64_t> & pchIds,
                               const Array<uint32_t> & numJobs,
                               const PchSendState & state,
                               uint32_t freeSlots = kManySlots,
                               const Array<uint32_t> * numHolders = nullptr )
    {
        PchDemand demand;
        demand.m_PchIds = pchIds;
        demand.m_NumWaiting = numJobs;
        for ( size_t i = 0; i < pchIds.GetSize(); ++i )
        {
            demand.m_NumHolders.Append( numHolders ? ( *numHolders )[ i ] : 0u );
        }
        return PchDispatch::SelectPchToQueue( demand, state, freeSlots );
    }

    static int32_t Select( const Array<uint64_t> & queue,
                           const Array<uint64_t> & onWorker,
                           const Array<uint64_t> & sending,
                           uint32_t numHeld,
                           uint32_t maxHeld )
    {
        PchWorkerState state;
        state.m_OnWorker = onWorker;
        state.m_Sending = sending;
        state.m_NumHeld = numHeld;
        state.m_MaxHeld = maxHeld;
        return PchDispatch::SelectJob( queue.GetSize(), [ &queue ]( size_t i ) { return queue[ i ]; }, state );
    }
};

// Register Tests
//------------------------------------------------------------------------------
REGISTER_TESTS_BEGIN( TestPchDispatch )
    REGISTER_TEST( RunnableJobsGoFirst )
    REGISTER_TEST( JobsWithoutAPchKeepTheirOrder )
    REGISTER_TEST( HeldJobsAreBounded )
    REGISTER_TEST( HeldJobsAreForThePchBeingSent )
    REGISTER_TEST( NextPchIsTheOneWithMostJobs )
    REGISTER_TEST( WorkersChooseTheSameNextPch )
    REGISTER_TEST( NextPchIsQueuedBehindTheCurrentOne )
    REGISTER_TEST( PchsAlreadyRequestedAreNotQueuedAgain )
    REGISTER_TEST( NoPchIsQueuedWithoutAFreeSlot )
    REGISTER_TEST( PchsQueuedAreBoundedByFreeSlots )
    REGISTER_TEST( FirstCopyOfAPchAlwaysGoes )
    REGISTER_TEST( FurtherCopiesNeedEnoughJobs )
    REGISTER_TEST( PchsAreRankedByTheirShareOfJobs )
    REGISTER_TEST( StalledHoldersDoNotCount )
    REGISTER_TEST( ConnectionsThatCannotTakeAJobAreNotHolders )
    REGISTER_TEST( HoldersAreCountedAcrossConnectionsInOneTick )
    REGISTER_TEST( SmallPchIsSkippedForOneWithEnoughJobs )
    REGISTER_TEST( EqualSharesGoToTheLowestId )
    REGISTER_TEST( NoJobsWaitingMeansNoPchEvenWithAHolder )
    REGISTER_TEST( EmptyQueue )
    REGISTER_TEST( PchSendStateTransitions )
    REGISTER_TEST( PchSendStateWhenTheWorkerNeedsAPch )
REGISTER_TESTS_END

// RunnableJobsGoFirst
//------------------------------------------------------------------------------
void TestPchDispatch::RunnableJobsGoFirst() const
{
    // A job with no PCH can start at once, even behind one that cannot
    {
        const IdList queue( { 1, 2, 1, 0, 1 } );
        TEST_ASSERT( Select( queue, Array<uint64_t>(), Array<uint64_t>(), 0, kMaxHeld ) == 3 );
    }

    // So can a job whose PCH the worker has, however many others are waiting for theirs
    {
        const IdList queue( { 2, 1, 1, 1 } );
        const IdList onWorker( { 2 } );
        const IdList sending( { 1 } );
        TEST_ASSERT( Select( queue, onWorker, sending, 0, kMaxHeld ) == 0 );

        // The bound on jobs held for a PCH never keeps a runnable job from the worker
        TEST_ASSERT( Select( queue, onWorker, sending, kMaxHeld, kMaxHeld ) == 0 );
    }

    // The most expensive runnable job goes first
    {
        const IdList queue( { 2, 2, 1 } );
        const IdList onWorker( { 2 } );
        TEST_ASSERT( Select( queue, onWorker, Array<uint64_t>(), 0, kMaxHeld ) == 1 );
    }
}

// JobsWithoutAPchKeepTheirOrder
//------------------------------------------------------------------------------
void TestPchDispatch::JobsWithoutAPchKeepTheirOrder() const
{
    // A build without PCHs takes jobs from the end of the queue, as it always did
    const IdList queue( { 0, 0, 0 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), Array<uint64_t>(), 0, kMaxHeld ) == 2 );
}

// HeldJobsAreBounded
//------------------------------------------------------------------------------
void TestPchDispatch::HeldJobsAreBounded() const
{
    const uint32_t kBound = 2;
    const IdList queue( { 1, 1, 1 } );
    const IdList sending( { 1 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 0, kBound ) == 2 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, kBound - 1, kBound ) == 2 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, kBound, kBound ) == -1 );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, kBound + 1, kBound ) == -1 );

    // A worker always takes one, and at most half of its slots are held
    TEST_ASSERT( PchDispatch::MaxHeldJobs( 1 ) == 1 );
    TEST_ASSERT( PchDispatch::MaxHeldJobs( 18 ) == 9 );
}

// HeldJobsAreForThePchBeingSent
//------------------------------------------------------------------------------
void TestPchDispatch::HeldJobsAreForThePchBeingSent() const
{
    // The PCH sent first is the one jobs are held for, not the one with the most jobs
    const IdList queue( { 1, 1, 2, 1 } );
    const IdList sending( { 2, 1 } );
    TEST_ASSERT( Select( queue, Array<uint64_t>(), sending, 0, kMaxHeld ) == 2 );

    // Once none are left for it, the PCH sent next takes its place
    const IdList queue2( { 1, 1 } );
    TEST_ASSERT( Select( queue2, Array<uint64_t>(), sending, 0, kMaxHeld ) == 1 );

    // A job for a PCH that is not on its way is not given to the worker
    const IdList queue3( { 3, 3 } );
    TEST_ASSERT( Select( queue3, Array<uint64_t>(), sending, 0, kMaxHeld ) == -1 );
    TEST_ASSERT( Select( queue3, Array<uint64_t>(), Array<uint64_t>(), 0, kMaxHeld ) == -1 );
}

// NextPchIsTheOneWithMostJobs
//------------------------------------------------------------------------------
void TestPchDispatch::NextPchIsTheOneWithMostJobs() const
{
    {
        const IdList pchIds( { 1, 2, 3 } );
        Array<uint32_t> numJobs;
        numJobs.Append( 1 );
        numJobs.Append( 7 );
        numJobs.Append( 3 );
        TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState() ) == 2 );
    }

    // Workers sharing an uplink choose alike: ties go to the lowest id
    {
        const IdList pchIds( { 5, 3, 4 } );
        Array<uint32_t> numJobs;
        numJobs.Append( 2 );
        numJobs.Append( 2 );
        numJobs.Append( 1 );
        TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState() ) == 3 );
    }

    // A PCH no job waits for is not sent
    {
        const IdList pchIds( { 5 } );
        Array<uint32_t> numJobs;
        numJobs.Append( 0 );
        TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState() ) == 0 );
    }
}

// WorkersChooseTheSameNextPch
//------------------------------------------------------------------------------
void TestPchDispatch::WorkersChooseTheSameNextPch() const
{
    // Two workers that have different PCHs already are given the same one to send next
    const IdList pchIds( { 7, 4, 9 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 2 );
    numJobs.Append( 2 );
    numJobs.Append( 2 );
    PchSendState firstWorker;
    const uint64_t first[] = { 1 };
    firstWorker.OnInventory( first, 1 );
    PchSendState secondWorker;
    const uint64_t second[] = { 9, 2 };
    secondWorker.OnInventory( second, 2 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, firstWorker ) == 4 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, secondWorker ) == 4 );
}

// NextPchIsQueuedBehindTheCurrentOne
//------------------------------------------------------------------------------
void TestPchDispatch::NextPchIsQueuedBehindTheCurrentOne() const
{
    // The next PCH is queued while the current one is sent, so the link has no gap between them
    const IdList pchIds( { 2, 3 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 1 );
    numJobs.Append( 4 );
    PchSendState state;
    TEST_ASSERT( state.OnRequested( 1 ) );
    TEST_ASSERT( SelectPch( pchIds, numJobs, state ) == 3 );

    // The cap is reached: no further PCH is queued until one has been handed to the socket
    TEST_ASSERT( state.OnRequested( 3 ) );
    TEST_ASSERT( state.GetSending().GetSize() == PchDispatch::kMaxSendingPchs );
    TEST_ASSERT( SelectPch( pchIds, numJobs, state ) == 0 );
    state.OnDelivered( 1 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, state ) == 2 );
}

// PchsAlreadyRequestedAreNotQueuedAgain
//------------------------------------------------------------------------------
void TestPchDispatch::PchsAlreadyRequestedAreNotQueuedAgain() const
{
    const IdList pchIds( { 1, 2, 3 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 9 ); // On the worker
    numJobs.Append( 8 ); // On its way
    numJobs.Append( 1 );
    PchSendState state;
    const uint64_t inventory[] = { 1 };
    state.OnInventory( inventory, 1 );
    TEST_ASSERT( state.OnRequested( 2 ) );
    TEST_ASSERT( SelectPch( pchIds, numJobs, state ) == 3 );
}

// NoPchIsQueuedWithoutAFreeSlot
//------------------------------------------------------------------------------
void TestPchDispatch::NoPchIsQueuedWithoutAFreeSlot() const
{
    // A worker that cannot take a job has no use for a PCH
    const IdList pchIds( { 2 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 5 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), 0 ) == 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), 1 ) == 2 );
}

// PchsQueuedAreBoundedByFreeSlots
//------------------------------------------------------------------------------
void TestPchDispatch::PchsQueuedAreBoundedByFreeSlots() const
{
    const IdList pchIds( { 2 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 5 );
    PchSendState state;
    TEST_ASSERT( state.OnRequested( 1 ) );

    // One slot, and one PCH already on its way for it
    TEST_ASSERT( SelectPch( pchIds, numJobs, state, 1 ) == 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, state, 2 ) == 2 );
}

// FirstCopyOfAPchAlwaysGoes
//------------------------------------------------------------------------------
void TestPchDispatch::FirstCopyOfAPchAlwaysGoes() const
{
    // Nobody has the PCH, so it goes however few jobs wait: otherwise those jobs would never run
    const IdList pchIds( { 5 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 1 );
    Array<uint32_t> numHolders;
    numHolders.Append( 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), 1, &numHolders ) == 5 );
}

// FurtherCopiesNeedEnoughJobs
//------------------------------------------------------------------------------
void TestPchDispatch::FurtherCopiesNeedEnoughJobs() const
{
    const IdList pchIds( { 5 } );
    Array<uint32_t> numHolders;
    numHolders.Append( 1 );

    // The jobs, shared with the holder, must be kMinJobsPerPchCopy each
    Array<uint32_t> fewJobs;
    fewJobs.Append( ( 2 * PchDispatch::kMinJobsPerPchCopy ) - 1 );
    TEST_ASSERT( SelectPch( pchIds, fewJobs, PchSendState(), kManySlots, &numHolders ) == 0 );

    Array<uint32_t> enoughJobs;
    enoughJobs.Append( 2 * PchDispatch::kMinJobsPerPchCopy );
    TEST_ASSERT( SelectPch( pchIds, enoughJobs, PchSendState(), kManySlots, &numHolders ) == 5 );
}

// PchsAreRankedByTheirShareOfJobs
//------------------------------------------------------------------------------
void TestPchDispatch::PchsAreRankedByTheirShareOfJobs() const
{
    const IdList pchIds( { 1, 2 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 12 );
    numJobs.Append( 6 );

    // 12 jobs among 3 workers is 4 each, 6 jobs for the first copy is 6
    Array<uint32_t> numHolders;
    numHolders.Append( 2 );
    numHolders.Append( 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), kManySlots, &numHolders ) == 2 );

    // With no holders it is the one with the most jobs
    Array<uint32_t> noHolders;
    noHolders.Append( 0 );
    noHolders.Append( 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), kManySlots, &noHolders ) == 1 );
}

// StalledHoldersDoNotCount
//------------------------------------------------------------------------------
void TestPchDispatch::StalledHoldersDoNotCount() const
{
    // A worker with a free slot is a holder however long since its last result
    TEST_ASSERT( PchDispatch::IsActiveHolder( 1, 1000.0f ) );

    // One with none is, until it has returned nothing for kHolderStallSeconds
    TEST_ASSERT( PchDispatch::IsActiveHolder( 0, PchDispatch::kHolderStallSeconds - 1.0f ) );
    TEST_ASSERT( PchDispatch::IsActiveHolder( 0, PchDispatch::kHolderStallSeconds + 1.0f ) == false );

    // At exactly kHolderStallSeconds it has stalled
    TEST_ASSERT( PchDispatch::IsActiveHolder( 0, PchDispatch::kHolderStallSeconds ) == false );
}

// ConnectionsThatCannotTakeAJobAreNotHolders
//------------------------------------------------------------------------------
void TestPchDispatch::ConnectionsThatCannotTakeAJobAreNotHolders() const
{
    TEST_ASSERT( PchDispatch::CountsAsHolder( false, 1, 1, 0.0f ) );

    // Still awaiting its inventories, however much capacity it has
    TEST_ASSERT( PchDispatch::CountsAsHolder( true, 1, 1, 0.0f ) == false );

    // No capacity, so no job can ever run there
    TEST_ASSERT( PchDispatch::CountsAsHolder( false, 0, 0, 0.0f ) == false );

    // Stalled with no free slot
    TEST_ASSERT( PchDispatch::CountsAsHolder( false, 1, 0, PchDispatch::kHolderStallSeconds ) == false );
}

// HoldersAreCountedAcrossConnectionsInOneTick
//------------------------------------------------------------------------------
void TestPchDispatch::HoldersAreCountedAcrossConnectionsInOneTick() const
{
    // Two workers with a free slot each and one PCH with too few jobs for a second copy
    const IdList pchIds( { 7 } );
    PchDemand demand;
    demand.m_PchIds = pchIds;
    demand.m_NumWaiting.Append( PchDispatch::kMinJobsPerPchCopy - 1 );
    demand.m_NumHolders.Append( 0 );

    PchSendState workers[ 2 ];
    for ( PchSendState & state : workers )
    {
        PchDispatch::AddHolders( demand, state ); // Nobody has it yet
    }
    TEST_ASSERT( demand.m_NumHolders[ 0 ] == 0 );

    uint32_t numQueued[ 2 ] = { 0, 0 };
    for ( size_t i = 0; i < 2; ++i )
    {
        PchDispatch::QueuePchs( demand, workers[ i ], 1, []( uint64_t ) { return true; }, [ &numQueued, i ]( uint64_t ) { ++numQueued[ i ]; } );
    }
    TEST_ASSERT( numQueued[ 0 ] == 1 ); // The first copy goes to the first worker
    TEST_ASSERT( numQueued[ 1 ] == 0 ); // The second sees the holder counted in the same tick
    TEST_ASSERT( demand.m_NumHolders[ 0 ] == 1 );

    // The next tick counts the holder from its state
    PchDemand next;
    next.m_PchIds = pchIds;
    next.m_NumWaiting.Append( PchDispatch::kMinJobsPerPchCopy - 1 );
    next.m_NumHolders.Append( 0 );
    PchDispatch::AddHolders( next, workers[ 0 ] );
    PchDispatch::AddHolders( next, workers[ 1 ] );
    TEST_ASSERT( next.m_NumHolders[ 0 ] == 1 );

    // A PCH with nothing to send is not considered again
    PchDemand unsendable;
    unsendable.m_PchIds = pchIds;
    unsendable.m_NumWaiting.Append( 1 );
    unsendable.m_NumHolders.Append( 0 );
    PchSendState state;
    uint32_t numSent = 0;
    PchDispatch::QueuePchs( unsendable, state, 1, []( uint64_t ) { return false; }, [ &numSent ]( uint64_t ) { ++numSent; } );
    TEST_ASSERT( numSent == 0 );
    TEST_ASSERT( unsendable.m_NumWaiting[ 0 ] == 0 );
}

// SmallPchIsSkippedForOneWithEnoughJobs
//------------------------------------------------------------------------------
void TestPchDispatch::SmallPchIsSkippedForOneWithEnoughJobs() const
{
    // The first PCH has a holder and too few jobs for a further copy, the second has no holder
    const IdList pchIds( { 1, 2 } );
    Array<uint32_t> numJobs;
    numJobs.Append( PchDispatch::kMinJobsPerPchCopy ); // Shared with the holder: below the minimum
    numJobs.Append( 1 );
    Array<uint32_t> numHolders;
    numHolders.Append( 1 );
    numHolders.Append( 0 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), kManySlots, &numHolders ) == 2 );
}

// EqualSharesGoToTheLowestId
//------------------------------------------------------------------------------
void TestPchDispatch::EqualSharesGoToTheLowestId() const
{
    // Both have a holder and the same share, whichever order they are listed in
    Array<uint32_t> numJobs;
    numJobs.Append( 2 * PchDispatch::kMinJobsPerPchCopy );
    numJobs.Append( 2 * PchDispatch::kMinJobsPerPchCopy );
    Array<uint32_t> numHolders;
    numHolders.Append( 1 );
    numHolders.Append( 1 );
    TEST_ASSERT( SelectPch( IdList( { 3, 9 } ), numJobs, PchSendState(), kManySlots, &numHolders ) == 3 );
    TEST_ASSERT( SelectPch( IdList( { 9, 3 } ), numJobs, PchSendState(), kManySlots, &numHolders ) == 3 );
}

// NoJobsWaitingMeansNoPchEvenWithAHolder
//------------------------------------------------------------------------------
void TestPchDispatch::NoJobsWaitingMeansNoPchEvenWithAHolder() const
{
    const IdList pchIds( { 4 } );
    Array<uint32_t> numJobs;
    numJobs.Append( 0 );
    Array<uint32_t> numHolders;
    numHolders.Append( 1 );
    TEST_ASSERT( SelectPch( pchIds, numJobs, PchSendState(), kManySlots, &numHolders ) == 0 );
}

// EmptyQueue
//------------------------------------------------------------------------------
void TestPchDispatch::EmptyQueue() const
{
    TEST_ASSERT( Select( Array<uint64_t>(), Array<uint64_t>(), Array<uint64_t>(), 0, kMaxHeld ) == -1 );
    TEST_ASSERT( SelectPch( Array<uint64_t>(), Array<uint32_t>(), PchSendState() ) == 0 );
}

// PchSendStateTransitions
//------------------------------------------------------------------------------
void TestPchDispatch::PchSendStateTransitions() const
{
    PchSendState state;

    // Requests are queued in order, once
    TEST_ASSERT( state.OnRequested( 5 ) );
    TEST_ASSERT( state.OnRequested( 3 ) );
    TEST_ASSERT( state.OnRequested( 5 ) == false );
    TEST_ASSERT( state.GetSending().GetSize() == 2 );
    TEST_ASSERT( state.GetSending()[ 0 ] == 5 );
    TEST_ASSERT( state.GetSending()[ 1 ] == 3 );
    TEST_ASSERT( state.IsRequested( 5 ) );
    TEST_ASSERT( state.IsOnWorker( 5 ) == false );

    // The first PCH reaching the worker moves it off the sending list, and the next stays
    state.OnDelivered( 5 );
    TEST_ASSERT( state.IsOnWorker( 5 ) );
    TEST_ASSERT( state.GetSending().GetSize() == 1 );
    TEST_ASSERT( state.GetSending()[ 0 ] == 3 );

    // Delivering twice, or requesting what the worker has, changes nothing
    state.OnDelivered( 5 );
    TEST_ASSERT( state.GetOnWorker().GetSize() == 1 );
    TEST_ASSERT( state.OnRequested( 5 ) == false );
    TEST_ASSERT( state.GetSending().GetSize() == 1 );

    // The worker's inventory covers a PCH that was already requested, and ones never requested
    const uint64_t inventory[] = { 3, 8, 8 };
    state.OnInventory( inventory, 3 );
    TEST_ASSERT( state.GetSending().IsEmpty() );
    TEST_ASSERT( state.IsOnWorker( 3 ) );
    TEST_ASSERT( state.IsOnWorker( 8 ) );
    TEST_ASSERT( state.GetOnWorker().GetSize() == 3 );
    TEST_ASSERT( state.OnRequested( 8 ) == false );
}

// PchSendStateWhenTheWorkerNeedsAPch
//------------------------------------------------------------------------------
void TestPchDispatch::PchSendStateWhenTheWorkerNeedsAPch() const
{
    // A PCH the worker reported or was sent is taken back when it says a job needs it, and is queued
    // to be sent again
    {
        PchSendState state;
        state.OnDelivered( 7 );
        TEST_ASSERT( state.IsOnWorker( 7 ) );
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_QUEUED );
        TEST_ASSERT( state.IsOnWorker( 7 ) == false );
        TEST_ASSERT( state.GetSending().GetSize() == 1 );
        TEST_ASSERT( state.GetSending()[ 0 ] == 7 );

        // Asking while it is queued changes nothing, however often
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_ALREADY_QUEUED );
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_ALREADY_QUEUED );
        TEST_ASSERT( state.GetSending().GetSize() == 1 );
        TEST_ASSERT( state.OnRequested( 7 ) == false ); // and it is still not requested twice

        // Once it has been sent it is on the worker again
        state.OnDelivered( 7 );
        TEST_ASSERT( state.IsOnWorker( 7 ) );
        TEST_ASSERT( state.GetSending().IsEmpty() );
    }

    // Other PCHs are left as they were, and the order of the queue is the order of the requests
    {
        PchSendState state;
        state.OnDelivered( 1 );
        state.OnDelivered( 2 );
        TEST_ASSERT( state.OnRequested( 3 ) );
        TEST_ASSERT( state.OnNeeded( 1 ) == PchSendState::NEEDED_QUEUED );
        TEST_ASSERT( state.IsOnWorker( 2 ) );
        TEST_ASSERT( state.GetSending().GetSize() == 2 );
        TEST_ASSERT( state.GetSending()[ 0 ] == 3 );
        TEST_ASSERT( state.GetSending()[ 1 ] == 1 );
    }

    // A PCH the state knows nothing about is queued as well
    {
        PchSendState state;
        TEST_ASSERT( state.OnNeeded( 9 ) == PchSendState::NEEDED_QUEUED );
        TEST_ASSERT( state.IsRequested( 9 ) );
    }

    // A worker that keeps losing a PCH is given up on after a number of sends, for each PCH
    {
        PchSendState state;
        state.OnDelivered( 5 );
        state.OnDelivered( 6 );
        const uint32_t kMax = PchSendState::kMaxResends;
        for ( uint32_t i = 0; i < kMax; ++i )
        {
            TEST_ASSERT( state.OnNeeded( 5 ) == PchSendState::NEEDED_QUEUED );
            state.OnDelivered( 5 );
        }
        TEST_ASSERT( state.OnNeeded( 5 ) == PchSendState::NEEDED_LIMIT );
        TEST_ASSERT( state.IsOnWorker( 5 ) ); // Left as it was
        TEST_ASSERT( state.GetSending().IsEmpty() );
        TEST_ASSERT( state.OnNeeded( 6 ) == PchSendState::NEEDED_QUEUED ); // The count is kept for each
    }

    // A worker that loses a PCH again and again, but builds jobs with it in between, is not given up on:
    // only resends without progress count
    {
        PchSendState state;
        state.OnDelivered( 7 );
        for ( uint32_t i = 0; i < ( 3 * PchSendState::kMaxResends ); ++i )
        {
            TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_QUEUED );
            state.OnDelivered( 7 );
            state.OnJobCompleted( 7 );
        }

        // and the bound is back to the full number of resends after the last job
        for ( uint32_t i = 0; i < PchSendState::kMaxResends; ++i )
        {
            TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_QUEUED );
            state.OnDelivered( 7 );
        }
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_LIMIT );

        // A job for another PCH is no progress for this one
        state.OnJobCompleted( 8 );
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_LIMIT );
        state.OnJobCompleted( 7 );
        TEST_ASSERT( state.OnNeeded( 7 ) == PchSendState::NEEDED_QUEUED );
    }
}

//------------------------------------------------------------------------------
