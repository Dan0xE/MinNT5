/*++

Copyright (c) 1987-92  Microsoft Corporation

Module Name:

    at.h

Abstract:

    Local include file for the SCHEDULE service.

Author:

    Vladimir Z. Vulovic     (vladimv)       06 - November - 1992

Revision History:

    06-Nov-1992     vladimv
        Created

--*/

#include <nt.h>                 //  NT definitions
#include <ntrtl.h>              //  NT runtime library definitions
#include <nturtl.h>

#include <netevent.h>
#include <windef.h>             //  Win32 type definitions
#include <winbase.h>            //  Win32 base API prototypes
#include <winsvc.h>             //  Win32 service control APIs
#include <winreg.h>             //  HKEY

#include <lmcons.h>             //  LAN Manager common definitions
#include <lmerr.h>              //  LAN Manager network error definitions
#include <lmsname.h>            //  LAN Manager service names
#include <lmapibuf.h>           //  NetApiBufferFree

#include <netlib.h>             //  LAN Man utility routines
#include <netlibnt.h>           //  NetpNtStatusToApiStatus
#include <netdebug.h>           //  NetpDbgPrint
#include <tstring.h>            //  Transitional string functions
#include <icanon.h>             //  I_Net canonicalize functions
#include <align.h>              //  ROUND_UP_COUNT macro

#include <services.h>           //  LMSVCS_GLOBAL_DATA
#include <apperr.h>             //  APE_AT_ID_NOT_FOUND

#include <rpc.h>                //  DataTypes and runtime APIs
#include <rpcutil.h>            //  Prototypes for MIDL user functions
#include <atsvc.h>              //  Generated by the MIDL complier


#if DBG
#define AT_DEBUG
#endif // DBG


//
//  atmain.c will #include this file with ATDATA_ALLOCATE defined.
//  That will cause each of these variables to be allocated.
//
#ifdef ATDATA_ALLOCATE
#define EXTERN
#define INIT( _x) = _x
#else
#define EXTERN extern
#define INIT( _x)
#endif

//
//      Server side data structures.
//

typedef struct _AT_RECORD {
    LIST_ENTRY      RuntimeList;    //  queue this on a doubly linked list
    LIST_ENTRY      JobIdList;      //  queue this on a doubly linked list
    LARGE_INTEGER   Runtime;        //  next time to run (secs from 01.Jan.1970)
    DWORD           JobId;          //  unique job id
    PWCHAR          Name;           //  name of this task in registry
    WORD            CommandSize;    //  in bytes, including NULL terminator
    WORD            NameSize;       //  not really needed, but fill in to DWORD

    DWORD       JobTime;        //  time of day to run, in miliseconds from midnight
    DWORD       DaysOfMonth;    //  bitmask for days of month to run
    UCHAR       DaysOfWeek;     //  bitmask for days of week to run
    UCHAR       Flags;          //  see lmat.h
    UCHAR       JobDay;         //  index of WeekDay or MonthDay when to run
#ifdef AT_DEBUG
    UCHAR       Debug;          //  for debugging purposes
#endif // AT_DEBUG
    PWCHAR      Command;        //  command & data to execute
} AT_RECORD, *PAT_RECORD, *LPAT_RECORD;

typedef struct _AT_SCHEDULE {
    DWORD       JobTime;        //  time of day to run, in seconds from midnight
    DWORD       DaysOfMonth;    //  bitmask for days of month to run
    UCHAR       DaysOfWeek;     //  bitmask for days of week to run
    UCHAR       Flags;          //  see lmat.h
    WORD        Reserved;       //  padding, since registry pads them as well
} AT_SCHEDULE, *PAT_SCHEDULE, *LPAT_SCHEDULE;


typedef struct _AT_TIME {
    LARGE_INTEGER   LargeInteger;   //  time since Jan.01,1970 (in NT_TICK-s)
    DWORD           TickCount;      //  time since most recent boot (in WINDOWS_TICK-s)
    DWORD           CurrentTime;    //  time in 24h day, (in WINDOWS_TICK-s)
    WORD            CurrentYear;
    WORD            CurrentMonth;       //  January=1, February=2, ...
    WORD            CurrentDayOfWeek;   //  Monday=0, Tuesday=1, ...
    WORD            CurrentDay;         //  first=1, second=2, ...
} AT_TIME, *PAT_TIME, *LPAT_TIME;



//
//      Functions exported by       atmain.c
//
VOID AtReportEvent(
    IN  WORD        EventType,
    IN  DWORD       MessageId,
    IN  WORD        StringsCount,
    IN  LPWSTR *    StringArray,
    IN  DWORD       RawDataBufferLength    OPTIONAL,
    IN  LPVOID      RawDataBuffer
    );


//
//      Functions exported by       atenv.c
//
VOID AtSetEnvironment( LPSTARTUPINFO pStartupInfo);

//
//      Functions exported by       attime.c
//
VOID AtCalculateRuntime(
    IN OUT  PAT_RECORD          pRecord,
    IN      PAT_TIME            pTime
    );
VOID AtTimeGetCurrents( IN OUT PAT_TIME pTime);
VOID AtTimeGet( OUT PAT_TIME pTime);


//
//      Functions exported by       atreg.c
//
DWORD AtCreateKey( PAT_RECORD pRecord);
BOOL AtDeleteKey( PAT_RECORD pRecord);
VOID AtInsertRecord(
    PAT_RECORD      pNewRecord,
    DWORD           QueueMask
    );
NET_API_STATUS AtMakeDataFromRegistry( IN PAT_TIME pTime);
BOOL AtPermitServerOperators( VOID);
VOID AtRemoveRecord(
    PAT_RECORD      pNewRecord,
    DWORD           QueueMask
    );
BOOL AtSystemInteractive( VOID);


//
//      Functions exported by       atsec.c
//
NET_API_STATUS
AtCheckSecurity(
    ACCESS_MASK     DesiredAccess
    );
NET_API_STATUS
AtCreateSecurityObject(
    VOID
    );
NET_API_STATUS
AtDeleteSecurityObject(
    VOID
    );

#ifdef NOT_YET
//
//  Is job enabled or disabled.
//
//  This flag is not supported yet - i.e. code to disable (and then re-enable)
//  jobs is missing.
//
#define JOB_IS_ENABLED                  0x40    //  set if enabled
#endif // NOT_YET



//
//  Bitmask used with AtGlobalTasks.  They describe outstanding tasks to be
//  done by the main schedule service thread once it wakes up.
//

#define AT_SERVICE_SHUTDOWN         0x0004


//
//  Bitmask describing on what global queues we should operate on.
//

#define RUNTIME_QUEUE       0x0001
#define JOBID_QUEUE         0x0002
#define BOTH_QUEUES         (RUNTIME_QUEUE | JOBID_QUEUE)


//
//  For a /NEXT type of program, JOB_CLEAR_WEEKDAY flag determines whether
//  to clear WEEKLY or MONTHLY schedule.
//
#define JOB_CLEAR_WEEKDAY               0x80    //  set if WEEKLY schedule


#define JOB_INVALID_DAY                 0xFF



#define MAXIMUM_COMMAND_LENGTH  (MAX_PATH - 1)  // == 259, cmd.exe uses this

#define AT_REGISTRY_PATH        L"System\\CurrentControlSet\\Services\\Schedule"
#define WINDOWS_REGISTRY_PATH   L"System\\CurrentControlSet\\Control\\Windows"
#define WINDOWS_VALUE_NAME      L"NoInteractiveServices"
#define INTERACTIVE_DESKTOP     L"WinSta0\\Default"
#define LSA_REGISTRY_PATH       L"System\\CurrentControlSet\\Control\\Lsa"
#define LSA_SUBMIT_CONTROL      L"SubmitControl"
#define LSA_SERVER_OPERATORS    0x00000001  //  can they submit jobs, etc.

#define MAXIMUM_JOB_TIME    (24 * 60 * 60 * 1000 - 1)
#define DAYS_OF_WEEK        0x7F                        // 7 bits for 7 days
#define DAYS_OF_MONTH       0x7FFFFFFF                  // 31 bits for 31 days

#define AT_SCHEDULE_NAME        L"Schedule"
#define AT_COMMAND_NAME         L"Command"

//
//  WINDOWS_TICK is one milisecond, i.e.    1e-3 seconds
//  NT_TICK is hundred nanoseconds, i.e.    1e-7 seconds
//
#define NT_TICKS_IN_WINDOWS_TICK    10000L
#define ONE_MINUTE_IN_NT_TICKS      (60000L * NT_TICKS_IN_WINDOWS_TICK)
#define MAXIMUM_FINITE_SLEEP_TIME   ((DWORD)-2)         //  in windows ticks
#define MAX_BUSY_TIMEOUT            (300*1000L)         //  5 MIN in WINDOWS_TICK-s
#define MAX_LAZY_TIMEOUT            (3600*1000L);       //  60 MIN in WINDOWS_TICK-s

#define AT_WAIT_HINT_TIME           5000L


//
//  The following are used only when we create new keys via NetrJobAdd.
//  A user can edit registry directly and define key names larger than this.
//
#define AT_KEY_NAME_MAX_LEN     8
#define AT_KEY_NAME_SIZE        ((AT_KEY_NAME_MAX_LEN + 1) * sizeof( WCHAR))



//
//  BUGBUG  Need to define ERROR_SERVICE_PAUSED in one of common include
//  BUGBUG  files.  This error means that request is denied because service
//  BUGBUG  is paused.  For now, set it to 65535.
//
#define ERROR_SERVICE_PAUSED    0xFFFF



//
//  Object specific access masks
//

#define AT_JOB_ADD          0x0001
#define AT_JOB_DEL          0x0002
#define AT_JOB_ENUM         0x0004
#define AT_JOB_GET_INFO     0x0008


//
//  Defines used to indicate how far we managed to initialize the Schedule
//  service before an error is encountered and the extent of clean up needed
//

#define AT_EVENT_CREATED            0x00000001
#define AT_QUEUES_CREATED           0x00000002
#define AT_RPC_SERVER_STARTED       0x00000004
#define AT_SECURITY_OBJECT_CREATED  0x00000008


//-------------------------------------------------------------------//
//                                                                   //
// Global variables                                                  //
//                                                                   //
//-------------------------------------------------------------------//


EXTERN CRITICAL_SECTION         AtGlobalCriticalSection;
EXTERN CRITICAL_SECTION         AtGlobalProtectLogFile;
EXTERN DWORD                    AtGlobalTasks;
EXTERN SERVICE_STATUS           AtGlobalServiceStatus;
EXTERN SERVICE_STATUS_HANDLE    AtGlobalServiceStatusHandle;
EXTERN DWORD                    AtGlobalJobId;
EXTERN LIST_ENTRY               AtGlobalRuntimeListHead;
EXTERN LIST_ENTRY               AtGlobalJobIdListHead;
EXTERN HANDLE                   AtGlobalEvent;
EXTERN HANDLE                   AtGlobalLogFile;
EXTERN DWORD                    AtGlobalSeed;
EXTERN AT_TIME                  AtGlobalSleepTime;
EXTERN AT_TIME                  AtGlobalGetupTime;
EXTERN HANDLE                   AtGlobalEvent;
EXTERN DWORD                    AtGlobalSeed;
EXTERN HKEY                     AtGlobalKey;
EXTERN BOOL                     AtGlobalHaveWindowsKey;
EXTERN BOOL                     AtGlobalPermitServerOperators;
EXTERN HKEY                     AtGlobalWindowsKey;
EXTERN STARTUPINFO              AtGlobalStartupInfo;
EXTERN CHAR                     AtGlobalDebugBuffer[ 1024];     //  arbitrary


#ifdef AT_DEBUG

EXTERN DWORD                AtGlobalDebug;

////////////////////////////////////////////////////////////////////////
//
// Debug Definititions
//
////////////////////////////////////////////////////////////////////////

#define AT_DEBUG_MAIN           0x00000001
#define AT_DEBUG_UTIL           0x00000002
#define AT_DEBUG_CRITICAL       0x00000004

//
// Control bits.
//

#define AT_TIMESTAMP            0x10000000 // TimeStamp each output line

#define IF_DEBUG(Function) \
     if (AtGlobalDebug & AT_ ## Function)

VOID AtDebugCreate( VOID);
VOID AtDebugDelete( VOID);

VOID AtAssertFailed(
    IN PVOID FailedAssertion,
    IN PVOID FileName,
    IN ULONG LineNumber,
    IN PCHAR Message OPTIONAL
    );

#define AtAssert( Predicate) \
    { \
        if (!(Predicate)) \
            AtAssertFailed( #Predicate, __FILE__, __LINE__, NULL ); \
    }

VOID AtLogRoutine(
    IN      DWORD       DebugFlag,
    IN      LPSTR       Format,         // PRINTF()-STYLE FORMAT STRING. 
    ...                                 // OTHER ARGUMENTS ARE POSSIBLE. 
    );
VOID AtLogRuntimeList( IN PCHAR Comment);
VOID AtLogTimeout( IN DWORD timeout);

#define AtLog( _x_)     AtLogRoutine _x_

#else //  AT_DEBUG

#define AtAssert( condition)

#define AtLog( _x_)
#define AtLogRuntimeList( _x_)
#define AtLogTimeout( _x_)

#endif //  AT_DEBUG



//
//  SCHEDULE_EVENTLOG_NAME is the name of registry key (under EventLog service)
//  used to interpret events for schedule service.
//

#define SCHEDULE_EVENTLOG_NAME    TEXT( "Schedule")
