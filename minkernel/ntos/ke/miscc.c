/*++

Copyright (c) 1989-1992  Microsoft Corporation

Module Name:

    miscc.c

Abstract:

    This module implements machine independent miscellaneous kernel functions.

Author:

    David N. Cutler (davec) 13-May-1989

Environment:

    Kernel mode only.

Revision History:

--*/

#include "ki.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, KeAddSystemServiceTable)
#pragma alloc_text(PAGE, KeSetSwapContextNotifyRoutine)
#pragma alloc_text(PAGE, KeSetTimeUpdateNotifyRoutine)
#pragma alloc_text(PAGE, KeSetThreadSelectNotifyRoutine)
#endif


#undef KeEnterCriticalRegion
VOID
KeEnterCriticalRegion (
   VOID
   )

/*++

Routine Description:

   This function disables kernel APC's.

   N.B. The following code does not require any interlocks. There are
        two cases of interest: 1) On an MP system, the thread cannot
        be running on two processors as once, and 2) if the thread is
        is interrupted to deliver a kernel mode APC which also calls
        this routine, the values read and stored will stack and unstack
        properly.

Arguments:

   None.

Return Value:

   None.

--*/

{
    //
    // Simply directly disable kernel APCs.
    //

    KeGetCurrentThread()->KernelApcDisable -= 1;
    return;
}


#undef KeLeaveCriticalRegion
VOID
KeLeaveCriticalRegion (
    VOID
    )

/*++

Routine Description:

    This function enables kernel APC's and requests an APC interrupt if
    appropriate.

Arguments:

    None.

Return Value:

    None.

--*/

{

    //
    // Increment the kernel APC disable count. If the resultant count is
    // zero and the thread's kernel APC List is not empty, then request an
    // APC interrupt.
    //
    // For multiprocessor performance, the following code utilizes the fact
    // that queuing an APC is done by first queuing the APC, then checking
    // the AST disable count. The following code increments the disable
    // count first, checks to determine if it is zero, and then checks the
    // kernel AST queue.
    //
    // See also KiInsertQueueApc().
    //

    KiLeaveCriticalRegion();
    return;
}

VOID
KeQuerySystemTime (
    OUT PLARGE_INTEGER CurrentTime
    )

/*++

Routine Description:

    This function returns the current system time by determining when the
    time is stable and then returning its value.

Arguments:

    CurrentTime - Supplies a pointer to a variable that will receive the
        current system time.

Return Value:

    None.

--*/

{

    KiQuerySystemTime(CurrentTime);
    return;
}

VOID
KeQueryTickCount (
    OUT PLARGE_INTEGER CurrentCount
    )

/*++

Routine Description:

    This function returns the current tick count by determining when the
    count is stable and then returning its value.

Arguments:

    CurrentCount - Supplies a pointer to a variable that will receive the
        current tick count.

Return Value:

    None.

--*/

{

    KiQueryTickCount(CurrentCount);
    return;
}

ULONG
KeQueryTimeIncrement (
    VOID
    )

/*++

Routine Description:

    This function returns the time increment value in 100ns units. This
    is the value that is added to the system time at each interval clock
    interrupt.

Arguments:

    None.

Return Value:

    The time increment value is returned as the function value.

--*/

{

    return KeMaximumIncrement;
}

VOID
KeSetDmaIoCoherency (
    IN ULONG Attributes
    )

/*++

Routine Description:

    This function sets (enables/disables) DMA I/O coherency with data
    caches.

Arguments:

    Attributes - Supplies the set of DMA I/O coherency attributes for
        the host system.

Return Value:

    None.

--*/

{

    KiDmaIoCoherency = Attributes;
}

#if defined(i386)
VOID
KeSetProfileIrql (
    IN KIRQL ProfileIrql
    )

/*++

Routine Description:

    This function sets the profile IRQL.

    N.B. There are only two valid values for synchronization IRQL:
        PROFILE_LEVEL and HIGH_LEVEL.

Arguments:

    Irql - Supplies the synchronization IRQL value.

Return Value:

    None.

--*/

{

    ASSERT((ProfileIrql == PROFILE_LEVEL) || (ProfileIrql == HIGH_LEVEL));
    KiProfileIrql = ProfileIrql;
}

#endif

#if defined(_MIPS_) || defined(_ALPHA_)
VOID
KeSetSynchIrql (
    IN KIRQL SynchIrql
    )

/*++

Routine Description:

    This function sets the synchronization IRQL.

    N.B. Synchronization IRQL may be any value between DISPATCH_LEVEL
         and SYNCH_LEVEL.

Arguments:

    Irql - Supplies the synchronization IRQL value.

Return Value:

    None.

--*/

{

    ASSERT((SynchIrql >= DISPATCH_LEVEL) && (SynchIrql <= SYNCH_LEVEL));

    KiSynchIrql = SynchIrql;
}

#endif


VOID
KeSetSystemTime (
    IN PLARGE_INTEGER NewTime,
    OUT PLARGE_INTEGER OldTime,
    IN PLARGE_INTEGER HalTimeToSet OPTIONAL
    )

/*++

Routine Description:

    This function sets the system time to the specified value and updates
    timer queue entries to reflect the difference between the old system
    time and the new system time.

Arguments:

    NewTime - Supplies a pointer to a variable that specifies the new system
        time.

    OldTime - Supplies a pointer to a variable that will receive the previous
        system time.

    HalTimeToSet - Supplies an optional time that if specified is to be used
        to set the time in the realtime clock.

Return Value:

    None.

--*/

{

    LIST_ENTRY AbsoluteListHead;
    LIST_ENTRY ExpiredListHead;
    ULONG Index;
    PLIST_ENTRY ListHead;
    PLIST_ENTRY NextEntry;
    KIRQL OldIrql1;
    KIRQL OldIrql2;
    LARGE_INTEGER TimeDelta;
    TIME_FIELDS TimeFields;
    PKTIMER Timer;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    //
    // If a realtime clock value is specified, then convert the time value
    // to time fields.
    //

    if (ARGUMENT_PRESENT(HalTimeToSet)) {
        RtlTimeToTimeFields(HalTimeToSet, &TimeFields);
    }

    //
    // Set affinity to the processor that keeps the system time, raise IRQL
    // to dispatcher level and lock the dispatcher database, then raise IRQL
    // to HIGH_LEVEL to synchronize with the clock interrupt routine.
    //

    KeSetSystemAffinityThread((KAFFINITY)1);
    KiLockDispatcherDatabase(&OldIrql1);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql2);

    //
    // Save the previous system time, set the new system time, and set
    // the realtime clock, if a time value is specified.
    //

    KiQuerySystemTime(OldTime);

#ifdef ALPHA

    SharedUserData->SystemTime = *(PULONGLONG)NewTime;

#elif defined(_MIPS_)

    *((DOUBLE *)(&SharedUserData->SystemTime)) = *((DOUBLE *)(NewTime));

#else

    SharedUserData->SystemTime.High2Time = NewTime->HighPart;
    SharedUserData->SystemTime.LowPart = NewTime->LowPart;
    SharedUserData->SystemTime.High1Time = NewTime->HighPart;

#endif

    if (ARGUMENT_PRESENT(HalTimeToSet)) {
        HalSetRealTimeClock(&TimeFields);
    }

    //
    // Compute the difference between the previous system time and the new
    // system time.
    //

    TimeDelta.QuadPart = NewTime->QuadPart - OldTime->QuadPart;

    //
    // Update the boot time to reflect the delta. This keeps time based
    // on boot time constant
    //

    KeBootTime.QuadPart = KeBootTime.QuadPart + TimeDelta.QuadPart;

    //
    // Lower IRQL to dispatch level and remove all absolute timers from the
    // timer queue so their due time can be recomputed.
    //

    KeLowerIrql(OldIrql2);
    InitializeListHead(&AbsoluteListHead);
    for (Index = 0; Index < TIMER_TABLE_SIZE; Index += 1) {
        ListHead = &KiTimerTableListHead[Index];
        NextEntry = ListHead->Flink;
        while (NextEntry != ListHead) {
            Timer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);
            NextEntry = NextEntry->Flink;
            if (Timer->Header.Absolute != FALSE) {
                RemoveEntryList(&Timer->TimerListEntry);
                InsertTailList(&AbsoluteListHead, &Timer->TimerListEntry);
            }
        }
    }

    //
    // Recompute the due time and reinsert all absolute timers in the timer
    // tree. If a timer has already expired, then insert the timer in the
    // expired timer list.
    //

    InitializeListHead(&ExpiredListHead);
    while (AbsoluteListHead.Flink != &AbsoluteListHead) {
        Timer = CONTAINING_RECORD(AbsoluteListHead.Flink, KTIMER, TimerListEntry);
        KiRemoveTreeTimer(Timer);
        Timer->DueTime.QuadPart -= TimeDelta.QuadPart;
        if (KiReinsertTreeTimer(Timer, Timer->DueTime) == FALSE) {
            Timer->Header.Inserted = TRUE;
            InsertTailList(&ExpiredListHead, &Timer->TimerListEntry);
        }
    }

    //
    // If any of the attempts to reinsert a timer failed, then timers have
    // already expired and must be processed.
    //
    // N.B. The following function returns with the dispatcher database
    //      unlocked.
    //

    KiTimerListExpire(&ExpiredListHead, OldIrql1);

    //
    // Notify registered components of the time change.
    //

#ifdef _PNP_POWER_

    ExNotifyCallback(ExCbSetSystemTime, (PVOID)OldTime, (PVOID)NewTime);

#endif

    //
    // Set affinity back to its original value.
    //

    KeRevertToUserAffinityThread();
    return;
}

VOID
KeSetTimeIncrement (
    IN ULONG MaximumIncrement,
    IN ULONG MinimumIncrement
    )

/*++

Routine Description:

    This function sets the time increment value in 100ns units. This
    value is added to the system time at each interval clock interrupt.

Arguments:

    MaximumIncrement - Supplies the maximum time between clock interrupts
        in 100ns units supported by the host HAL.

    MinimumIncrement - Supplies the minimum time between clock interrupts
        in 100ns units supported by the host HAL.

Return Value:

    None.

--*/

{

    KeMaximumIncrement = MaximumIncrement;
    KeMinimumIncrement = max(MinimumIncrement, 10 * 1000);
    KeTimeAdjustment = MaximumIncrement;
    KeTimeIncrement = MaximumIncrement;
    KiTickOffset = MaximumIncrement;
}

BOOLEAN
KeAddSystemServiceTable(
    IN PULONG Base,
    IN PULONG Count OPTIONAL,
    IN ULONG Limit,
    IN PUCHAR Number,
    IN ULONG Index
    )

/*++

Routine Description:

    This function allows the caller to add a system service table
    to the system

Arguments:

    Base - Supplies the address of the system service table dispatch
        table.

    Count - Supplies an optional pointer to a table of per system service
        counters.

    Limit - Supplies the limit of the service table. Services greater
        than or equal to this limit will fail.

    Arguments - Supplies the address of the argument count table.

    Index - Supplies index of the service table.

Return Value:

    TRUE - The operation was successful.

    FALSE - the operation failed. A service table is already bound to
        the specified location, or the specified index is larger than
        the maximum allowed index.

--*/

{

    PAGED_CODE();

    //
    // If a system service table is already defined for the specified
    // index, then return FALSE. Otherwise, establish the new system
    // service table.
    //

    if ((Index > NUMBER_SERVICE_TABLES - 1) ||
        (KeServiceDescriptorTable[Index].Base != NULL) ||
        (KeServiceDescriptorTableShadow[Index].Base != NULL)) {
        return FALSE;

    } else {

        //
        // If the service table index is equal to the Win32 table, then
        // only update the shadow system service table. Otherwise, both
        // the shadow and static system service tables are updated.
        //

        KeServiceDescriptorTableShadow[Index].Base = Base;
        KeServiceDescriptorTableShadow[Index].Count = Count;
        KeServiceDescriptorTableShadow[Index].Limit = Limit;
        KeServiceDescriptorTableShadow[Index].Number = Number;
        if (Index != 1) {
            KeServiceDescriptorTable[Index].Base = Base;
            KeServiceDescriptorTable[Index].Count = Count;
            KeServiceDescriptorTable[Index].Limit = Limit;
            KeServiceDescriptorTable[Index].Number = Number;
        }

        return TRUE;
    }
}

VOID
FASTCALL
KeSetSwapContextNotifyRoutine(
    IN PSWAP_CONTEXT_NOTIFY_ROUTINE NotifyRoutine
    )
/*++

Routine Description:

    This function sets the address of a callout routine which will be called
    at each context swtich.

Arguments:

    NotifyRoutine - Supplies the address of the swap context notify callout
        routine.

Return Value:

    None.

--*/

{

    PAGED_CODE();

    KiSwapContextNotifyRoutine = NotifyRoutine;
    return;
}

VOID
FASTCALL
KeSetThreadSelectNotifyRoutine(
    IN PTHREAD_SELECT_NOTIFY_ROUTINE NotifyRoutine
    )

/*++

Routine Description:

    This function sets the address of a callout routine which will be called
    when a thread is being selected for execution.

Arguments:

    NotifyRoutine - Supplies the address of the thread select notify callout
        routine.

Return Value:

    None.

--*/

{

    PAGED_CODE();

    KiThreadSelectNotifyRoutine = NotifyRoutine;
    return;
}

VOID
FASTCALL
KeSetTimeUpdateNotifyRoutine(
    IN PTIME_UPDATE_NOTIFY_ROUTINE NotifyRoutine
    )

/*++

Routine Description:

    This function sets the address of a callout routine which will be called
    each time the runtime for a thread is updated.

Arguments:

    RoutineNotify - Supplies the address of the time update notify callout
        routine.

Return Value:

    None.

--*/

{

    PAGED_CODE();

    KiTimeUpdateNotifyRoutine = NotifyRoutine;
    return;
}
