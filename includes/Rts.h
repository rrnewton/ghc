/* -----------------------------------------------------------------------------
 *
 * (c) The GHC Team, 1998-2009
 *
 * RTS external APIs.  This file declares everything that the GHC RTS
 * exposes externally.
 *
 * To understand the structure of the RTS headers, see the wiki:
 *   http://hackage.haskell.org/trac/ghc/wiki/Commentary/SourceTree/Includes
 *
 * ---------------------------------------------------------------------------*/

#ifndef RTS_H
#define RTS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IN_STG_CODE
#define IN_STG_CODE 0
#endif
#include "Stg.h"

#include "HsFFI.h"
#include "RtsAPI.h"

// Turn off inlining when debugging - it obfuscates things
#ifdef DEBUG
# undef  STATIC_INLINE
# define STATIC_INLINE static
#endif

#include "rts/Types.h"

#if __GNUC__ >= 3
/* Assume that a flexible array member at the end of a struct
 * can be defined thus: T arr[]; */
#define FLEXIBLE_ARRAY
#else
/* Assume that it must be defined thus: T arr[0]; */
#define FLEXIBLE_ARRAY 0
#endif

#if __GNUC__ >= 3
#define ATTRIBUTE_ALIGNED(n) __attribute__((aligned(n)))
#else
#define ATTRIBUTE_ALIGNED(n) /*nothing*/
#endif

/* Fix for mingw stat problem (done here so it's early enough) */
#ifdef mingw32_HOST_OS
#define __MSVCRT__ 1
#endif

/* Needed to get the macro version of errno on some OSs, and also to
   get prototypes for the _r versions of C library functions. */
#ifndef _REENTRANT
#define _REENTRANT 1
#endif

/*
 * We often want to know the size of something in units of an
 * StgWord... (rounded up, of course!)
 */
#define ROUNDUP_BYTES_TO_WDS(n) (((n) + sizeof(W_) - 1) / sizeof(W_))

#define sizeofW(t) ROUNDUP_BYTES_TO_WDS(sizeof(t))

/* -----------------------------------------------------------------------------
   Assertions and Debuggery

   CHECK(p)   evaluates p and terminates with an error if p is false
   ASSERT(p)  like CHECK(p) if DEBUG is on, otherwise a no-op
   -------------------------------------------------------------------------- */

void _assertFail(const char *filename, unsigned int linenum)
   GNUC3_ATTRIBUTE(__noreturn__);

#define CHECK(predicate)			\
	if (predicate)				\
	    /*null*/;				\
	else					\
	    _assertFail(__FILE__, __LINE__)

#ifndef DEBUG
#define ASSERT(predicate) /* nothing */
#else
#define ASSERT(predicate) CHECK(predicate)
#endif /* DEBUG */

/* 
 * Use this on the RHS of macros which expand to nothing
 * to make sure that the macro can be used in a context which
 * demands a non-empty statement.
 */

#define doNothing() do { } while (0)

#ifdef DEBUG
#define USED_IF_DEBUG
#define USED_IF_NOT_DEBUG STG_UNUSED
#else
#define USED_IF_DEBUG STG_UNUSED
#define USED_IF_NOT_DEBUG
#endif

#ifdef THREADED_RTS
#define USED_IF_THREADS
#define USED_IF_NOT_THREADS STG_UNUSED
#else
#define USED_IF_THREADS STG_UNUSED
#define USED_IF_NOT_THREADS
#endif

/*
 * Getting printf formats right for platform-dependent typedefs
 */
#if SIZEOF_LONG == 8
#define FMT_Word64 "lu"
#define FMT_Int64  "ld"
#else
#define FMT_Word64 "llu"
#define FMT_Int64  "lld"
#endif

/* -----------------------------------------------------------------------------
   Include everything STG-ish
   -------------------------------------------------------------------------- */

/* System headers: stdlib.h is eeded so that we can use NULL.  It must
 * come after MachRegs.h, because stdlib.h might define some inline
 * functions which may only be defined after register variables have
 * been declared.
 */
#include <stdlib.h>

#include "rts/Config.h"

/* Global constaints */
#include "rts/Constants.h"

/* Profiling information */
#include "rts/prof/CCS.h"
#include "rts/prof/LDV.h"

/* Parallel information */
#include "rts/OSThreads.h"
#include "rts/SpinLock.h"

#include "rts/Messages.h"

/* Storage format definitions */
#include "rts/storage/FunTypes.h"
#include "rts/storage/InfoTables.h"
#include "rts/storage/Closures.h"
#include "rts/storage/Liveness.h"
#include "rts/storage/ClosureTypes.h"
#include "rts/storage/TSO.h"
#include "stg/MiscClosures.h" /* InfoTables, closures etc. defined in the RTS */
#include "rts/storage/SMPClosureOps.h"
#include "rts/storage/Block.h"
#include "rts/storage/ClosureMacros.h"
#include "rts/storage/MBlock.h"
#include "rts/storage/GC.h"

/* Other RTS external APIs */
#include "rts/Parallel.h"
#include "rts/Hooks.h"
#include "rts/Signals.h"
#include "rts/BlockSignals.h"
#include "rts/Hpc.h"
#include "rts/Flags.h"
#include "rts/Adjustor.h"
#include "rts/FileLock.h"
#include "rts/Globals.h"
#include "rts/IOManager.h"
#include "rts/Linker.h"
#include "rts/Threads.h"
#include "rts/Ticky.h"
#include "rts/Timer.h"
#include "rts/Stable.h"
#include "rts/TTY.h"
#include "rts/Utils.h"
#include "rts/PrimFloat.h"

/* Misc stuff without a home */
DLL_IMPORT_RTS extern char **prog_argv;	/* so we can get at these from Haskell */
DLL_IMPORT_RTS extern int    prog_argc;
DLL_IMPORT_RTS extern char  *prog_name;

void stackOverflow(void);

void stg_exit(int n) GNU_ATTRIBUTE(__noreturn__);

#ifndef mingw32_HOST_OS
int stg_sig_install (int, int, void *);
#endif

/* -----------------------------------------------------------------------------
   RTS Exit codes
   -------------------------------------------------------------------------- */

/* 255 is allegedly used by dynamic linkers to report linking failure */
#define EXIT_INTERNAL_ERROR 254
#define EXIT_DEADLOCK       253
#define EXIT_INTERRUPTED    252
#define EXIT_HEAPOVERFLOW   251
#define EXIT_KILLED         250

/* -----------------------------------------------------------------------------
   Miscellaneous garbage
   -------------------------------------------------------------------------- */

/* declarations for runtime flags/values */
#define MAX_RTS_ARGS 32

#ifdef DEBUG
#define TICK_VAR(arity) \
  extern StgInt SLOW_CALLS_##arity; \
  extern StgInt RIGHT_ARITY_##arity; \
  extern StgInt TAGGED_PTR_##arity;

extern StgInt TOTAL_CALLS;

TICK_VAR(1)
TICK_VAR(2)
#endif

/* -----------------------------------------------------------------------------
   Assertions and Debuggery
   -------------------------------------------------------------------------- */

#define IF_RTSFLAGS(c,s)  if (RtsFlags.c) { s; }

#ifdef DEBUG
#if IN_STG_CODE
#define IF_DEBUG(c,s)  if (RtsFlags[0].DebugFlags.c) { s; }
#else
#define IF_DEBUG(c,s)  if (RtsFlags.DebugFlags.c) { s; }
#endif
#else
#define IF_DEBUG(c,s)  doNothing()
#endif

#ifdef DEBUG
#define DEBUG_ONLY(s) s
#else
#define DEBUG_ONLY(s) doNothing()
#endif

/* -----------------------------------------------------------------------------
   Useful macros and inline functions
   -------------------------------------------------------------------------- */

#if defined(__GNUC__)
#define SUPPORTS_TYPEOF
#endif

#if defined(SUPPORTS_TYPEOF)
#define stg_min(a,b) ({typeof(a) _a = (a), _b = (b); _a <= _b ? _a : _b; })
#define stg_max(a,b) ({typeof(a) _a = (a), _b = (b); _a <= _b ? _b : _a; })
#else
#define stg_min(a,b) ((a) <= (b) ? (a) : (b))
#define stg_max(a,b) ((a) <= (b) ? (b) : (a))
#endif

/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* RTS_H */
