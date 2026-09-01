// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
// MsgFacade.h
//
// The ONE public header of MsgFacade.dll -- a minimal, macro-free facade over
// Msgcore.dll's structured message store.
//
// Design rules (all deliberate, do not "improve" them away):
//
//  * NOTHING from Msgcore leaks through here: no P2PmsgMgr, no P3PmsgField,
//    no P3PmsgData, no P3PmsgCurs, no VBLock, no MFC, no CString, no
//    Msgexception.  A client includes only this header and links only
//    MsgFacade.lib.
//
//  * Pure-vtable interfaces + one extern "C" factory.  No C++ classes are
//    exported, so the facade is usable from any MSVC toolset (and any language
//    that can call a vtable) without name-mangling / CRT coupling.
//
//  * Every method returns HRESULT and takes only flat, automation-friendly
//    parameters (const wchar_t*, integers, void*+size).  That is intentional:
//    if/when an ATL layer with DUAL interfaces (IDispatch + vtable) is built on
//    top, each method here maps 1:1 onto a [dual] interface method
//    (LPCWSTR->BSTR, void*+size->SAFEARRAY(VT_UI1)), and IMsgStoreEvents maps
//    onto a connection-point event interface.
//
//  * No STL types and no exceptions cross the DLL boundary.  Msgcore throws
//    (Msgexception) from deep inside the heap on a great many ordinary
//    mistakes -- a strict-typed read of the wrong subtype, a SelectItem of a
//    container, an over-capacity string.  Every entry point below catches, and
//    answers MSGF_E_CORE.  A header-only std::function / RAII convenience layer
//    lives in MsgFacadeFn.hpp.
//
// Lifecycle in one sentence:
//    MSGF_CreateLibrary() -> IMsgLibrary::CreateStore() -> store->GetRoot()
//    -> node->Declare*/Get*/GetChild -> store->Save() -> library->Release().
//
// ---------------------------------------------------------------------------
// THE THREE THINGS THIS FACADE IS FOR
// ---------------------------------------------------------------------------
//
// Msgcore is a fast, packed, offset-addressed object store.  Its speed comes
// from properties that are hostile to a caller who is not the code that wrote
// it, and each of the three is fixed here rather than documented at the caller.
//
//  1. A NODE IS A PATH, NOT A POINTER.
//
//     Msgcore addresses blocks by offset and grows by reallocating its base
//     image, so ANY call that can allocate -- a declare, a rename, a retype, a
//     data write, a load -- may move every block in the store.  A raw handle
//     obtained before such a call is a dangling pointer after it, with no way
//     to tell.  That is why the kernel's own flat ABI tells callers that no
//     handle survives a mutation and to treat "obtain, use, discard" as the
//     unit of work.
//
//     An IMsgNode holds the ROUTE to a node -- the ordered (scope, name) steps
//     from the store root -- and re-walks that route on entry to every method.
//     So it survives every mutation, including a Load that replaces the whole
//     tree, and it never aliases freed memory.  When the node it names is gone
//     the walk fails at the missing step and the method answers MSGF_E_NO_ITEM,
//     which is a real answer rather than the kernel's "resolves to whatever now
//     occupies that block".
//
//     What it costs: O(depth) name lookups per call.  Depth is the nesting of
//     the store, not its size, and each step is a cursor seek over one
//     collection.  If that is ever the bottleneck, walk with IMsgCursor /
//     IMsgWalker, which hold a real position -- and which is the one place the
//     kernel's rule survives, because it cannot not: see "Cursors and walkers".
//
//  2. ONE PAIR OF VERBS FOR BOTH COLLECTIONS.
//
//     Every Msgcore node carries TWO child collections -- its descendants (the
//     tree proper) and its attributes (keyed by '@') -- and the kernel exposes
//     them through two parallel families of near-identical calls, plus a third
//     on the field itself.  They differ in one thing: which collection is
//     reached.  So here that is an ARGUMENT, not a family:
//
//         node->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"Hello", 0, 0 );
//         node->DeclareText ( MSGF_SCOPE_ATTR,  L"Lang",  L"en",    0, 0 );
//
//     A third collection would then be a case label rather than twenty more
//     vtable slots, twenty more IDL dispids and twenty more wrappers per
//     language binding.
//
//  3. READING A VALUE NEVER THROWS AND NEVER LIES.
//
//     Msgcore's scalar accessors are STRICT: c_int() on anything that is not
//     INT32, or c_double() on a FLOAT, raises rather than converts.  A caller
//     that does not already know a node's exact subtype cannot safely read it.
//     GetInt below reads the whole integer family (INT08..UINT64 and BOOL) at
//     any width and reports whether the subtype was unsigned; GetReal reads
//     FLOAT and DOUBLE.  Asking for a value the node does not hold is
//     MSGF_E_TYPE -- an answer, not an exception.
//
// ---------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------
//  * Msgcore's store has NO internal synchronisation of any kind: one writer,
//    or external locking around every call, and concurrent READERS are unsafe
//    too because a read can trigger the relocation above.
//  * This facade puts ONE critical section around each store and takes it in
//    every method of every object belonging to that store.  So a call is safe
//    from any thread, and two threads calling on one store are serialised
//    rather than racing.  Two stores never contend: they share no state.
//  * That makes each CALL atomic; it does not make a SEQUENCE atomic.  A walk
//    (IMsgCursor / IMsgWalker) interleaved with another thread's mutation is
//    still a logic error -- the lock is released between the two calls.  Finish
//    the walk, then mutate.
//  * IMsgStoreEvents callbacks arrive ON THE THREAD PERFORMING THE MUTATION,
//    synchronously, with the store's lock HELD.  Do not call back into the
//    store from one; copy what you need and post it to your own queue.
//
// ---------------------------------------------------------------------------
// Cursors and walkers
// ---------------------------------------------------------------------------
// IMsgCursor and IMsgWalker are the two objects that hold a real kernel
// position rather than a path, because holding a position is the whole point of
// them: re-walking a route per step would make an iteration O(n*depth) and
// would not iterate anything the caller had not already named.
//
// They therefore inherit the kernel's rule, and it is stated here once: a
// cursor or a walker must not outlive a MUTATION of the tree it is walking.
// Reads through it are fine.  Declare, Delete, Rename, Move, Retype, Truncate,
// SetText, Load and any list/vect write invalidate it -- finish the walk, then
// mutate, or collect the names first and act on them afterwards (which is what
// IMsgCursor::GetNode is for: it hands back a path-based IMsgNode that DOES
// survive the mutation).

#pragma once

#ifndef _WINDEF_
  #include <windows.h>   // HRESULT, wchar_t plumbing; clients all target Win32
#endif

#if defined(MSGFACADE_EXPORTS)
  #define MSGF_API __declspec(dllexport)
#else
  #define MSGF_API __declspec(dllimport)
#endif

namespace msgf {

// ---------------------------------------------------------------------------
// ABI version.
//
// The rule is the one TargetFacade arrived at the hard way: additions go at the
// END of an interface, or onto a NEW interface a client opts into -- never into
// the middle of a vtable, and never as a new pure virtual on an interface the
// CLIENT implements (IMsgStoreEvents, IMsgPagingEvents), because the DLL would
// then call a slot the client does not have.
//
//   ABI 1  the store, nodes, containers, cursors, walkers, triggers.
//   ABI 2  appends the two facilities the kernel has and ABI 1 did not reach:
//          DEMAND PAGING (IMsgPagingEvents + five methods at the end of
//          IMsgStore) and the NODE VALUE STACK (four methods at the end of
//          IMsgNode).  Both are appends, so ABI 1's vtables are still prefixes
//          of these and an ABI 1 client needs no rebuild.
// ---------------------------------------------------------------------------
const unsigned int ABI_VERSION     = 2;

// The oldest ABI whose vtable is still a prefix of this one.  Between this and
// ABI_VERSION inclusive, a client needs no rebuild.
const unsigned int ABI_VERSION_MIN = 1;

// ---------------------------------------------------------------------------
// Facade-specific HRESULTs (FACILITY_ITF, codes 0x0300+ as COM prescribes).
//
// Codes are only ever appended, never renumbered: they are compared
// NUMERICALLY by the smoke test and by any script client.
// ---------------------------------------------------------------------------
const HRESULT MSGF_E_ABI_MISMATCH = MAKE_HRESULT(1, FACILITY_ITF, 0x0300); // header/DLL ABI_VERSION differ
const HRESULT MSGF_E_CORE         = MAKE_HRESULT(1, FACILITY_ITF, 0x0301); // Msgcore raised or refused
const HRESULT MSGF_E_NO_ITEM      = MAKE_HRESULT(1, FACILITY_ITF, 0x0302); // no item of that name in that scope
const HRESULT MSGF_E_NAME         = MAKE_HRESULT(1, FACILITY_ITF, 0x0303); // name empty, over-long, or holding . @ : ^ / \ * ? | < > "
const HRESULT MSGF_E_TYPE         = MAKE_HRESULT(1, FACILITY_ITF, 0x0304); // the node is not of the kind/type asked for
const HRESULT MSGF_E_RANGE        = MAKE_HRESULT(1, FACILITY_ITF, 0x0305); // index outside the collection
const HRESULT MSGF_E_FILE         = MAKE_HRESULT(1, FACILITY_ITF, 0x0306); // Load/Save failed
const HRESULT MSGF_E_CLOSED       = MAKE_HRESULT(1, FACILITY_ITF, 0x0307); // the store this object belongs to is closed
const HRESULT MSGF_E_SCOPE        = MAKE_HRESULT(1, FACILITY_ITF, 0x0308); // scope is neither MSGF_SCOPE_CHILD nor _ATTR
const HRESULT MSGF_E_PATH         = MAKE_HRESULT(1, FACILITY_ITF, 0x0309); // path string does not parse
const HRESULT MSGF_E_EXISTS       = MAKE_HRESULT(1, FACILITY_ITF, 0x030A); // declare onto an existing name without MSGF_DECLARE_UPDATE
const HRESULT MSGF_E_NO_POS       = MAKE_HRESULT(1, FACILITY_ITF, 0x030B); // position does not resolve in this store
const HRESULT MSGF_E_LIMIT        = MAKE_HRESULT(1, FACILITY_ITF, 0x030C); // value past what the storage type can hold
const HRESULT MSGF_E_DEPTH        = MAKE_HRESULT(1, FACILITY_ITF, 0x030D); // node path deeper than MAX_DEPTH
const HRESULT MSGF_E_STATE        = MAKE_HRESULT(1, FACILITY_ITF, 0x030E); // the object is not in a state that call is legal in

// ---------------------------------------------------------------------------
// Scope -- WHICH of a node's two child collections a call means.
//
// Every Msgcore node has both, always.  Neither is created until something is
// put in it, and every Declare* below creates the collection it needs.
// ---------------------------------------------------------------------------
const unsigned int MSGF_SCOPE_CHILD = 0;   // descendants: the tree proper
const unsigned int MSGF_SCOPE_ATTR  = 1;   // attributes: the '@' collection

// ---------------------------------------------------------------------------
// Kind -- WHAT a node is, structurally.  Exactly one is reported.
// ---------------------------------------------------------------------------
const unsigned int MSGF_KIND_ITEM = 1;     // a name+value node, possibly with children
const unsigned int MSGF_KIND_LIST = 2;     // a linked list of values (IMsgList)
const unsigned int MSGF_KIND_VECT = 3;     // an indexed vector of elements (IMsgVect)
const unsigned int MSGF_KIND_DATA = 4;     // a bare value cell with no name of its own

// ---------------------------------------------------------------------------
// Data types.
//
// These ARE Msgcore's own VBLockData_* codes, repeated here so a client never
// needs P2PmsgVBLock.h.  The values are part of this ABI and match the kernel.
// Only the subset a value can actually be declared as appears; the kernel's
// var-length and narrow string/blob variants are reachable by reading a store
// somebody else wrote, and GetType will report them.
// ---------------------------------------------------------------------------
const unsigned char MSGF_TYPE_NULL   = 0;
const unsigned char MSGF_TYPE_INT08  = 1;
const unsigned char MSGF_TYPE_UINT08 = 2;
const unsigned char MSGF_TYPE_INT16  = 3;
const unsigned char MSGF_TYPE_UINT16 = 4;
const unsigned char MSGF_TYPE_INT32  = 5;
const unsigned char MSGF_TYPE_UINT32 = 6;
const unsigned char MSGF_TYPE_INT64  = 7;
const unsigned char MSGF_TYPE_UINT64 = 8;
const unsigned char MSGF_TYPE_FLOAT  = 9;
const unsigned char MSGF_TYPE_DOUBLE = 10;
const unsigned char MSGF_TYPE_BOOL   = 13;
const unsigned char MSGF_TYPE_BSTR16 = 18;
const unsigned char MSGF_TYPE_WSTR16 = 26;   // what DeclareText writes
const unsigned char MSGF_TYPE_BLOB16 = 34;   // what DeclareBlob writes
const unsigned char MSGF_TYPE_GUID   = 47;

// Addressing width of a new store.  This is the width of an internal heap
// OFFSET, so it caps how big the store can grow, and it is fixed at creation.
const unsigned char MSGF_ADDR_16 = 1;      // up to 64 KB
const unsigned char MSGF_ADDR_32 = 2;      // up to 4 GB
const unsigned char MSGF_ADDR_64 = 3;      // the default on a 64-bit build

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------
// Declare*: what to do when the name is already there.  Without it, a declare
// onto an existing name is MSGF_E_EXISTS -- the kernel's own default silently
// returns the existing node instead, which makes "create" and "assign"
// indistinguishable at the call site.
const unsigned int MSGF_DECLARE_UPDATE = 0x0001;

// IMsgList::Add: which end.  Tail is the default because that is append.
const unsigned int MSGF_ADD_TAIL = 0x0000;
const unsigned int MSGF_ADD_HEAD = 0x0001;

// IMsgList::Drop: which end.
const unsigned int MSGF_DROP_TAIL = 0x0000;
const unsigned int MSGF_DROP_HEAD = 0x0001;

// IMsgStore::Save: rebuild the heap image without its free holes on the way
// out.  Costs a full walk; shrinks the file.
const unsigned int MSGF_SAVE_DEFRAGMENT = 0x0001;

// IMsgStore trigger masks.  These ARE the kernel's TRIGGER_* bits.  A sink
// invocation carries exactly one of them.
const unsigned int MSGF_TRIG_INSERT = 1;
const unsigned int MSGF_TRIG_UPDATE = 2;
const unsigned int MSGF_TRIG_DELETE = 4;
const unsigned int MSGF_TRIG_ACTIVE = 8;
const unsigned int MSGF_TRIG_ALL    = MSGF_TRIG_INSERT | MSGF_TRIG_UPDATE
                                    | MSGF_TRIG_DELETE | MSGF_TRIG_ACTIVE;

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------
// An item name is at most this many UTF-16 UNITS -- so one astral code point
// costs two of them.
//
// This is the kernel's STORAGE bound, not its validator's: P3PmsgName holds
// its characters inline in a 63-unit array, while P3Pmsg_IsValidItemname
// answers TRUE for anything up to 127.  Trusting the validator was measured
// wrong (by _Msgcore_UseExamplesLight\DataFieldTest): a declare at 64..127
// units passed every check here, reached the core, and died inside
// P3PmsgName's copy with an MFC ASSERT -- which AfxAbort turns into a process
// exit, not a return.  So the bound has to be the one the storage actually
// has, and it is enforced HERE, before the core is called.
const unsigned int MAX_NAME = 63;

// How deep a node's path may be.  A path IS the node here (see "A node is a
// path"), so this is a real bound rather than a buffer size: the facade
// refuses to mint a node it could not re-walk.
const unsigned int MAX_DEPTH = 64;

// The largest text a node can hold.  A WSTR16 blob caps at 0xFFFF BYTES, so
// 32767 UTF-16 units plus a terminator.  Rejected HERE rather than in the
// kernel, where an over-capacity store raises from inside the heap copy and
// leaks the half-built value on the way out.
const unsigned int MAX_TEXT = 32767;

// The largest blob a node can hold, for the same reason: BLOB16 is 16-bit sized.
const unsigned int MAX_BLOB = 65535;

// ---------------------------------------------------------------------------
// The caller-sized buffer protocol, stated once for every string- and
// bytes-out method below.
//
//   *cch (or *size) IN  is the capacity of `buf`, in characters (or bytes).
//   *cch      OUT is what was NEEDED -- ALWAYS, whatever the result -- so one
//                 failed call tells you exactly how big to make the second.
//   buf == NULL   is a size query: S_OK, nothing written, *cch is the answer.
//   too small     is HRESULT_FROM_WIN32(ERROR_MORE_DATA), and NOTHING is
//                 written.  A partial string is worse than none: it looks like
//                 a value.
//
// For strings the count INCLUDES the terminating NUL, so a size query on an
// empty string answers 1.  For bytes a zero-length value is a real value and
// answers S_OK with *size 0 -- which is how "present and empty" is told apart
// from "absent", which is MSGF_E_NO_ITEM.
// ---------------------------------------------------------------------------

class IMsgNode;
class IMsgList;
class IMsgVect;
class IMsgCursor;
class IMsgWalker;

// ===========================================================================
//  IMsgStoreEvents -- CLIENT-implemented change sink (optional)
// ===========================================================================
//
// Msgcore's own trigger facility posts a Windows message to an HWND, which is
// unreachable from a service, a daemon, a script host or a unit test.  This is
// the windowless path: install one sink, arm the nodes you care about by
// position, and OnTrigger fires when one of them changes.
//
// DELIVERY CONTRACT, and it is not the one the rest of this ABI follows:
//   * called SYNCHRONOUSLY, on the thread that performed the mutation,
//   * with the store's lock HELD -- so calling back into the store from inside
//     it deadlocks.  Copy the position and post it to your own queue.
//   * INSERT and UPDATE fire when a writer asks for them (IMsgStore::Fire);
//     DELETE fires by itself when an armed node is freed.
//
// Implement it, keep it alive for as long as it is installed, and clear it
// with SetEvents(NULL) BEFORE destroying it -- nothing here can tell a freed
// sink from a live one.
class IMsgStoreEvents
{
    public:
      // `type` is exactly one MSGF_TRIG_* bit.  `pos` is the changed node's
      // position -- feed it to IMsgStore::NodeFromPos to get a node, AFTER you
      // are off this thread.
      virtual void OnTrigger ( unsigned int type, unsigned long long pos ) = 0;
};

// ===========================================================================
//  IMsgPagingEvents -- CLIENT-implemented demand-paging sink (optional, ABI 2)
// ===========================================================================
//
// A store can hand the decision "this subtree is not in memory yet" back to the
// application.  Install a sink with IMsgStore::SetPaging, and the core asks it
// to materialise a subtree before reading one, and to evict one afterwards.
//
// DELIVERY CONTRACT, and it is stricter than IMsgStoreEvents':
//   * called SYNCHRONOUSLY, on the thread performing the access, with the
//     store's lock HELD, and the core WAITS FOR THE ANSWER -- a page-in that
//     has not returned yet is data that is not there.  Do not call back into
//     the store from one, and do not block on anything slow.
//   * the answer is the whole point: return non-zero for "I dealt with it",
//     zero for "I did not", and the core propagates that to whoever asked.
//   * the core may call these DURING an ordinary read, not only from the
//     explicit IMsgStore::PageIn / PageOut below.
//
// Clear it with SetPaging(NULL) BEFORE destroying the sink -- nothing here can
// tell a freed sink from a live one.
class IMsgPagingEvents
{
    public:
      // Materialise the subtree rooted at `pos`.  Non-zero == handled.
      virtual int OnPageIn  ( unsigned long long pos ) = 0;
      // Evict it.  `flush` is non-zero when the core wants it written first.
      virtual int OnPageOut ( unsigned long long pos, int flush ) = 0;
};

// ===========================================================================
//  IMsgList -- a linked sequence of values under one name
// ===========================================================================
//
// A list holds VALUES, not named nodes: its cells have a type and a value and
// no name of their own.  That is what distinguishes it from a node's child
// collection, and why everything here is indexed rather than named.
//
// Indexing is 0-based and O(index): the kernel walks the chain from the head,
// because the only thing it could hand back instead is a raw heap address --
// exactly the thing a relocation invalidates.  For a full scan of a large list,
// that makes a loop of GetAt calls O(n^2); open an IMsgCursor instead, which
// holds its own position.
//
// A list handle is bound to the node it was opened from and survives mutation
// of the STORE, but not the deletion of that node.  Release it when done.
class IMsgList
{
    public:
      virtual HRESULT GetCount   ( unsigned int *outCount ) const = 0;

      // The MSGF_TYPE_* of one cell.  MSGF_TYPE_NULL for an index past the
      // end, which makes this the safe probe.
      virtual HRESULT GetTypeAt  ( unsigned int index, unsigned char *outType ) const = 0;

      // Width-agnostic read of any integer-family cell (INT08..UINT64, BOOL).
      // `outUnsigned` may be NULL if you do not care.  MSGF_E_TYPE when the
      // cell is not an integer.
      virtual HRESULT GetIntAt   ( unsigned int index
                                 , long long *outValue, int *outUnsigned ) const = 0;
      // FLOAT or DOUBLE.  MSGF_E_TYPE otherwise.
      virtual HRESULT GetRealAt  ( unsigned int index, double *outValue ) const = 0;
      // Any string cell.  See "the caller-sized buffer protocol".
      virtual HRESULT GetTextAt  ( unsigned int index
                                 , wchar_t *buf, unsigned int *cch ) const = 0;

      // Overwrite one cell IN PLACE, keeping its declared type.  MSGF_E_TYPE
      // when the cell is not of that family -- a list is typed per cell and
      // this is not the way to change a cell's type.
      virtual HRESULT SetIntAt   ( unsigned int index, long long value ) = 0;
      virtual HRESULT SetRealAt  ( unsigned int index, double value ) = 0;
      // A text cell can only be overwritten with a value that FITS the storage
      // it was created with; a longer one is MSGF_E_LIMIT.  A list cell lives
      // inside the list's own block and cannot be grown, unlike a node's value
      // (IMsgNode::SetText grows freely).  To lengthen one, delete it and add a
      // new cell.
      virtual HRESULT SetTextAt  ( unsigned int index, const wchar_t *value ) = 0;

      // Append (or prepend, with MSGF_ADD_HEAD) a new cell.  `type` is the
      // MSGF_TYPE_* to store at; 0 means "the natural one" -- INT32 for AddInt,
      // DOUBLE for AddReal.
      virtual HRESULT AddInt     ( long long value, unsigned char type
                                 , unsigned int flags ) = 0;
      virtual HRESULT AddReal    ( double value, unsigned char type
                                 , unsigned int flags ) = 0;
      virtual HRESULT AddText    ( const wchar_t *value, unsigned int flags ) = 0;

      // Remove the cell at one end (MSGF_DROP_HEAD / _TAIL), or at an index.
      virtual HRESULT Drop       ( unsigned int flags ) = 0;
      virtual HRESULT DeleteAt   ( unsigned int index ) = 0;
      // Empty the list, keeping the list itself.
      virtual HRESULT Truncate   ( ) = 0;

      virtual ULONG   Release    ( ) = 0;
};

// ===========================================================================
//  IMsgVect -- an indexed vector of ELEMENTS under one name
// ===========================================================================
//
// Where a list holds values, a vect holds ELEMENTS: each one is a named node in
// its own right and may itself be a list or another vect.  It is declared with
// a length and an element prototype, and it grows.
//
// Indexing here is genuine random access -- a vect is a slot table, not a
// chain -- so unlike IMsgList there is no reason to reach for a cursor.
class IMsgVect
{
    public:
      virtual HRESULT GetCount   ( unsigned int *outCount ) const = 0;

      // MSGF_KIND_* of one element: DATA, ITEM, LIST or VECT.
      virtual HRESULT GetKindAt  ( unsigned int index, unsigned int *outKind ) const = 0;
      // MSGF_TYPE_* of one element's value.
      virtual HRESULT GetTypeAt  ( unsigned int index, unsigned char *outType ) const = 0;
      // An element's own name.  Empty for an element that was never named.
      virtual HRESULT GetNameAt  ( unsigned int index
                                 , wchar_t *buf, unsigned int *cch ) const = 0;

      virtual HRESULT GetIntAt   ( unsigned int index
                                 , long long *outValue, int *outUnsigned ) const = 0;
      virtual HRESULT GetRealAt  ( unsigned int index, double *outValue ) const = 0;
      virtual HRESULT GetTextAt  ( unsigned int index
                                 , wchar_t *buf, unsigned int *cch ) const = 0;

      virtual HRESULT SetIntAt   ( unsigned int index, long long value ) = 0;
      virtual HRESULT SetRealAt  ( unsigned int index, double value ) = 0;
      virtual HRESULT SetTextAt  ( unsigned int index, const wchar_t *value ) = 0;

      // A nested container element.  MSGF_E_TYPE when the element is not one.
      virtual HRESULT GetListAt  ( unsigned int index, IMsgList **outList ) = 0;
      virtual HRESULT GetVectAt  ( unsigned int index, IMsgVect **outVect ) = 0;

      virtual HRESULT DeleteAt   ( unsigned int index ) = 0;
      virtual HRESULT Truncate   ( ) = 0;

      virtual ULONG   Release    ( ) = 0;
};

// ===========================================================================
//  IMsgCursor -- one collection, one position
// ===========================================================================
//
// Opened from a node over one of its scopes (or from a list).  It holds a real
// kernel position, so it is the efficient way to enumerate -- and it is one of
// the two objects the invalidation rule still governs: do not mutate the tree
// while one is open.  See "Cursors and walkers" at the top.
//
// The canonical loop:
//
//     IMsgCursor *c = 0;
//     node->OpenCursor ( msgf::MSGF_SCOPE_CHILD, &c );
//     for ( int end = 0; SUCCEEDED(c->IsEnd(&end)) && !end; c->Next() )
//     {
//         wchar_t name[msgf::MAX_NAME + 1]; unsigned int cch = msgf::MAX_NAME + 1;
//         c->GetName ( name, &cch );
//         ...
//     }
//     c->Release();
class IMsgCursor
{
    public:
      // Back to the first element.
      virtual HRESULT Rewind     ( ) = 0;
      // Forward one.  S_FALSE (a SUCCESS code) when that walked off the end.
      virtual HRESULT Next       ( ) = 0;
      // Position on a named / indexed element.  MSGF_E_NO_ITEM / MSGF_E_RANGE
      // when there is none, leaving the cursor where it was.
      virtual HRESULT GotoName   ( const wchar_t *name ) = 0;
      virtual HRESULT GotoIndex  ( unsigned int index ) = 0;

      // 1 when the cursor is past the last element -- every accessor below
      // fails from there.
      virtual HRESULT IsEnd      ( int *outEnd ) const = 0;
      virtual HRESULT GetCount   ( unsigned int *outCount ) const = 0;
      virtual HRESULT GetIndex   ( unsigned int *outIndex ) const = 0;
      // MSGF_KIND_* of the current element.
      virtual HRESULT GetKind    ( unsigned int *outKind ) const = 0;
      virtual HRESULT GetName    ( wchar_t *buf, unsigned int *cch ) const = 0;

      // A path-based node for the current element -- which DOES survive the
      // mutation that would invalidate this cursor.  Collect nodes on the
      // walk, act on them after it.
      virtual HRESULT GetNode    ( IMsgNode **outNode ) = 0;

      // Remove the current element.  This is a mutation: the cursor is the one
      // thing it does NOT invalidate -- it is left positioned on the FIRST
      // element, because the kernel's own delete leaves it on no element at all
      // and every accessor would then fail.  Any OTHER open cursor or walker on
      // this store IS now stale.
      virtual HRESULT Delete     ( ) = 0;

      virtual ULONG   Release    ( ) = 0;
};

// ===========================================================================
//  IMsgWalker -- a whole subtree from one flat loop
// ===========================================================================
//
// A walker owns a chain of cursors and splices descent into its advance -- but
// it descends only when the caller says Push.  That is what makes it a WALKER
// rather than an iterator, and what lets a caller prune a branch by simply not
// descending into it.
//
//     IMsgWalker *w = 0;
//     node->OpenWalker ( msgf::MSGF_SCOPE_CHILD, &w );
//     for ( int end = 0; SUCCEEDED(w->IsEnd(&end)) && !end; w->Next() )
//     {
//         unsigned int kind = 0; w->GetKind ( &kind );
//         if ( kind == msgf::MSGF_KIND_ITEM ) w->Push ( 0 );   // else prune
//     }
//     w->Release();
//
// Same invalidation rule as IMsgCursor, for the same reason.
class IMsgWalker
{
    public:
      // Advance.  At the end of a pushed level this ascends automatically.
      // S_FALSE when the whole walk is finished.
      virtual HRESULT Next       ( ) = 0;
      // Descend into the current element.  `outDepth` (may be NULL) receives
      // the new depth, 1-based.  Three outcomes, and the difference matters:
      //   S_OK          descended; the walker is now inside it
      //   S_FALSE       an item, but it has no children -- nothing happened
      //   MSGF_E_TYPE   not something that can be descended into at all
      //                 (a list, a vect, a bare value)
      // Only S_OK moves the walker, so treating S_FALSE as "pruned" is right.
      //
      // Push is a statement about where the NEXT Next goes -- into this
      // element's children rather than on to its next sibling.  It is not a
      // step in itself, and reading the walker between a Push and the Next
      // that follows it is not meaningful.
      virtual HRESULT Push       ( int *outDepth ) = 0;
      // Ascend one level.  MSGF_E_RANGE at the outermost.
      virtual HRESULT Pop        ( int *outDepth ) = 0;
      // Abandon every pushed level at once and resume at the outermost.
      virtual HRESULT Break      ( ) = 0;

      virtual HRESULT IsEnd      ( int *outEnd ) const = 0;
      virtual HRESULT GetDepth   ( int *outDepth ) const = 0;
      virtual HRESULT GetKind    ( unsigned int *outKind ) const = 0;
      virtual HRESULT GetName    ( wchar_t *buf, unsigned int *cch ) const = 0;

      virtual ULONG   Release    ( ) = 0;
};

// ===========================================================================
//  IMsgNode -- a position in a store's tree
// ===========================================================================
//
// Not a copy of a node and not a pointer to one: the ROUTE to one, re-walked on
// every call.  Read the top of this header before assuming otherwise; the
// difference is the reason this facade exists.
//
// A node keeps its store alive: releasing the store while nodes are outstanding
// is legal, and those nodes then answer MSGF_E_CLOSED.
class IMsgNode
{
    // --- what this node is ------------------------------------------------
    public:
      // The node's own name.  Empty (just a terminator) for the store root,
      // which has no name of its own -- use IMsgStore::GetRootname.
      virtual HRESULT GetName    ( wchar_t *buf, unsigned int *cch ) const = 0;

      // This node's path, in the facade's own spelling: a '.' before each
      // descendant step and an '@' before each attribute step, from the root.
      //
      //     L""                       the root
      //     L".Config"                a child of the root
      //     L".Config.Window@Colour"  an attribute of a grandchild
      //
      // This is NOT Msgcore's internal path spelling, deliberately: this one
      // round-trips exactly through IMsgStore::NodeFromPath, and item names
      // cannot contain '.' or '@' (the kernel's own validator rejects them),
      // so it is unambiguous.
      virtual HRESULT GetPath    ( wchar_t *buf, unsigned int *cch ) const = 0;

      // The node's stable position -- the store's natural inode number.  It is
      // what triggers report and what NodeFromPos consumes.  0 for a node that
      // is not addressable.
      //
      // It is NOT an identity: delete the node and the position can be handed
      // out again to a later allocation.  Hold the NODE across a mutation, not
      // the position -- holding a position is exactly what this facade exists
      // to stop you needing to do.
      virtual HRESULT GetPos     ( unsigned long long *outPos ) const = 0;

      // MSGF_KIND_* -- item, list or vect.
      virtual HRESULT GetKind    ( unsigned int *outKind ) const = 0;
      // MSGF_TYPE_* of this node's own value.
      virtual HRESULT GetType    ( unsigned char *outType ) const = 0;
      // 1 when the node holds no value at all (as opposed to a zero one).
      virtual HRESULT IsNull     ( int *outNull ) const = 0;

    // --- this node's own value --------------------------------------------
    public:
      // Any integer-family value (INT08..UINT64, BOOL) at any width, with
      // `outUnsigned` (may be NULL) reporting the subtype's signedness.
      // MSGF_E_TYPE when the node holds something else.
      virtual HRESULT GetInt     ( long long *outValue, int *outUnsigned ) const = 0;
      // Overwrite in place, keeping the node's declared width.  MSGF_E_LIMIT
      // when the value does not fit it.
      virtual HRESULT SetInt     ( long long value ) = 0;

      // FLOAT or DOUBLE.
      virtual HRESULT GetReal    ( double *outValue ) const = 0;
      virtual HRESULT SetReal    ( double value ) = 0;

      // Any string value.  MSGF_E_LIMIT past MAX_TEXT on the way in.
      virtual HRESULT GetText    ( wchar_t *buf, unsigned int *cch ) const = 0;
      virtual HRESULT SetText    ( const wchar_t *value ) = 0;

      // Raw bytes of a BLOB node.  A zero-length blob is a real value.
      virtual HRESULT GetBlob    ( void *buf, unsigned int *size ) const = 0;

      // A GUID node as canonical text, "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX",
      // upper hex, no braces.
      virtual HRESULT GetGuid    ( wchar_t *buf, unsigned int *cch ) const = 0;

      // The node's timestamp, seconds since the epoch, held as an attribute by
      // the kernel.  0 when never stamped.  SetTime(-1) means "now" and
      // reports back what was stored.
      virtual HRESULT GetTime    ( long long *outTime ) const = 0;
      virtual HRESULT SetTime    ( long long value, long long *outTime ) = 0;

    // --- this node's children, in either scope ----------------------------
    public:
      // How many items the scope holds.  0 for a scope that has never been
      // written to.
      virtual HRESULT GetCount   ( unsigned int scope, unsigned int *outCount ) const = 0;
      // S_OK / S_FALSE (both SUCCESS) rather than an error: absence is an
      // answer here, not a failure.
      virtual HRESULT Exists     ( unsigned int scope, const wchar_t *name ) const = 0;

      // A node for one child.  MSGF_E_NO_ITEM when there is none.  Works for
      // every kind of child, including lists and vects -- the kernel's own
      // SelectItem raises on those, which is a trap this closes.
      virtual HRESULT GetChild   ( unsigned int scope, const wchar_t *name
                                 , IMsgNode **outNode ) = 0;
      // The same by position in the collection, for enumeration without a
      // cursor.  MSGF_E_RANGE past the end.
      virtual HRESULT GetChildAt ( unsigned int scope, unsigned int index
                                 , IMsgNode **outNode ) = 0;

      // Create a child (or, with MSGF_DECLARE_UPDATE, create-or-assign).
      // `outNode` may be NULL when the caller does not want the child back.
      //
      // `type` selects the storage width: one of MSGF_TYPE_INT08..UINT64 or
      // _BOOL for DeclareInt, _FLOAT or _DOUBLE for DeclareReal, and 0 for the
      // natural one (INT32 / DOUBLE).  A width that cannot hold `value` is
      // MSGF_E_LIMIT -- the kernel would truncate silently.
      virtual HRESULT DeclareInt ( unsigned int scope, const wchar_t *name
                                 , long long value, unsigned char type
                                 , unsigned int flags, IMsgNode **outNode ) = 0;
      virtual HRESULT DeclareReal( unsigned int scope, const wchar_t *name
                                 , double value, unsigned char type
                                 , unsigned int flags, IMsgNode **outNode ) = 0;
      virtual HRESULT DeclareText( unsigned int scope, const wchar_t *name
                                 , const wchar_t *value
                                 , unsigned int flags, IMsgNode **outNode ) = 0;
      virtual HRESULT DeclareBlob( unsigned int scope, const wchar_t *name
                                 , const void *value, unsigned int size
                                 , unsigned int flags, IMsgNode **outNode ) = 0;
      // `guid` is canonical text; surrounding braces are tolerated, anything
      // else malformed is MSGF_E_NAME.
      virtual HRESULT DeclareGuid( unsigned int scope, const wchar_t *name
                                 , const wchar_t *guid
                                 , unsigned int flags, IMsgNode **outNode ) = 0;

      // Create a container child and hand back a handle to it.
      // `elems` is a vect's initial length (0 is legal -- a vect grows) and
      // `type` its element prototype.
      virtual HRESULT DeclareList( unsigned int scope, const wchar_t *name
                                 , IMsgList **outList ) = 0;
      virtual HRESULT DeclareVect( unsigned int scope, const wchar_t *name
                                 , unsigned int elems, unsigned char type
                                 , IMsgVect **outVect ) = 0;

      // Open an EXISTING container child.  MSGF_E_NO_ITEM when the name is not
      // there, MSGF_E_TYPE when it is there but is not a container of that
      // kind -- so a caller never has to pre-check with Exists.
      virtual HRESULT GetList    ( unsigned int scope, const wchar_t *name
                                 , IMsgList **outList ) = 0;
      virtual HRESULT GetVect    ( unsigned int scope, const wchar_t *name
                                 , IMsgVect **outVect ) = 0;

    // --- restructuring ----------------------------------------------------
    public:
      virtual HRESULT Delete     ( unsigned int scope, const wchar_t *name ) = 0;
      // Empty one scope, keeping this node.
      virtual HRESULT Truncate   ( unsigned int scope ) = 0;
      // Rename a child in place.  Renaming to its current name is S_FALSE.
      virtual HRESULT Rename     ( unsigned int scope, const wchar_t *name
                                 , const wchar_t *newName ) = 0;
      // Move a child of THIS node into `destin`'s CHILD scope.  Both nodes must
      // belong to the same store.
      virtual HRESULT Move       ( unsigned int scope, const wchar_t *name
                                 , IMsgNode *destin ) = 0;
      // Change a child's storage type, seeding a zero value of it.  The one way
      // to change a node's type: a Set* keeps the declared width.
      virtual HRESULT Retype     ( unsigned int scope, const wchar_t *name
                                 , unsigned char type ) = 0;

    // --- enumeration ------------------------------------------------------
    public:
      virtual HRESULT OpenCursor ( unsigned int scope, IMsgCursor **outCursor ) = 0;
      virtual HRESULT OpenWalker ( unsigned int scope, IMsgWalker **outWalker ) = 0;

      virtual ULONG   Release    ( ) = 0;

    // --- the value stack (ABI 2) ------------------------------------------
    //
    // A scoped override of this node's own name and value, held INSIDE the node
    // by the kernel rather than by the caller.  PushValue saves the pair,
    // PopValue puts it back, DropValue forgets it without restoring.
    //
    // A caller can do the same thing itself -- read the value, write a new one,
    // write the old one back -- and for one value it should.  What this buys is
    // the two things that hand-rolled version cannot: the NAME travels with the
    // value, and the saved pair lives in the store, so it survives everything a
    // node survives, including a Save and the process that wrote it.
    //
    // IT NESTS.  This comment used to say the kernel keeps ONE saved pair per
    // node and that pushing twice replaces the first; that was wrong, and it was
    // wrong in a way worth recording, because the kernel LOOKS like it says so:
    // MsgStck__AllocItem asserted the saved slot was empty, so a second push
    // tripped an assertion in a debug build.  MsgStck::Push itself is a linked
    // stack -- it reads the current head, allocates, and re-links the old head
    // onto the new item -- and push/push/pop/pop unwinds correctly.  The
    // assertion was the leftover; it is gone.
    //
    // Popping an unstacked node is S_FALSE -- a SUCCESS code meaning "there was
    // nothing to restore" -- so a drain loop tests for S_OK, or IsStacked, and
    // never merely for SUCCEEDED.
    public:
      virtual HRESULT PushValue  ( ) = 0;
      virtual HRESULT PopValue   ( ) = 0;
      virtual HRESULT DropValue  ( ) = 0;
      virtual HRESULT IsStacked  ( int *outStacked ) const = 0;
};

// ===========================================================================
//  IMsgStore -- one message store
// ===========================================================================
//
// A store is a tree with a root, a heap behind it and (optionally) a file.
// Every node, list, vect, cursor and walker reached from it holds it alive, so
// Release here means "I am finished with it", not "destroy it now": the store
// goes away when the last object rooted in it does.
//
// Release DOES close it immediately, though, in the sense that matters -- every
// outstanding object starts answering MSGF_E_CLOSED.  A half-alive store that
// still served reads would be a much worse contract than one that says so.
class IMsgStore
{
    // --- persistence -------------------------------------------------------
    public:
      // Save to `filename`, or to the name it was opened/last saved under when
      // `filename` is NULL.  MSGF_E_FILE when the kernel refuses.
      virtual HRESULT Save       ( const wchar_t *filename, unsigned int flags ) = 0;
      // Replace this store's whole contents from a file.  Every outstanding
      // node stays valid and re-resolves against the NEW tree -- which is the
      // clearest demonstration of what a path-based node buys.
      virtual HRESULT Load       ( const wchar_t *filename ) = 0;
      // Empty the store.
      virtual HRESULT Clear      ( ) = 0;
      // Rename the root.
      virtual HRESULT Rename     ( const wchar_t *newName ) = 0;

      virtual HRESULT GetFilename( wchar_t *buf, unsigned int *cch ) const = 0;
      virtual HRESULT GetRootname( wchar_t *buf, unsigned int *cch ) const = 0;

      // Unsaved changes?  SetDirty exists because a host that has just written
      // its own journal may want to say so.
      virtual HRESULT IsDirty    ( int *outDirty ) const = 0;
      virtual HRESULT SetDirty   ( int dirty ) = 0;

      // Bytes the store's heap currently occupies.
      virtual HRESULT GetSize    ( unsigned int *outSize ) const = 0;
      // Does the kernel still consider the heap structurally sound?
      virtual HRESULT IsValid    ( int *outValid ) const = 0;

    // --- getting into the tree ---------------------------------------------
    public:
      virtual HRESULT GetRoot    ( IMsgNode **outNode ) = 0;
      // Resolve a facade path (see IMsgNode::GetPath) -- L"" is the root.
      // MSGF_E_PATH when it does not parse, MSGF_E_NO_ITEM when it parses but
      // names nothing.
      virtual HRESULT NodeFromPath ( const wchar_t *path, IMsgNode **outNode ) = 0;
      // Resolve a position, as reported by IMsgNode::GetPos and by triggers.
      // MSGF_E_NO_POS when it does not resolve in this store.
      //
      // The node handed back is a PATH node like every other, so it is safe to
      // keep; the POSITION it came from is not, and stops being meaningful the
      // moment that node is deleted.
      virtual HRESULT NodeFromPos  ( unsigned long long pos, IMsgNode **outNode ) = 0;

    // --- change notification ------------------------------------------------
    public:
      // Install (NULL clears) the change sink.  Read IMsgStoreEvents before
      // using it: the delivery contract is not this ABI's usual one.
      virtual HRESULT SetEvents  ( IMsgStoreEvents *events ) = 0;
      // Arm / disarm a node, by position, for a mask of MSGF_TRIG_* bits.
      virtual HRESULT Arm        ( unsigned int mask, unsigned long long pos ) = 0;
      virtual HRESULT Disarm     ( unsigned int mask, unsigned long long pos ) = 0;
      // Fire a mask by hand, after a mutation the facade cannot see as one.
      // `outFired` (may be NULL) receives how many registrations answered.
      virtual HRESULT Fire       ( unsigned int mask, unsigned long long pos
                                 , unsigned int *outFired ) = 0;

      virtual ULONG   Release    ( ) = 0;

    // --- demand paging (ABI 2) ---------------------------------------------
    //
    // Read IMsgPagingEvents before using any of this: the sink's delivery
    // contract is the strictest in this ABI.
    //
    // With no sink installed, PageIn and PageOut are successful no-ops and
    // answer S_FALSE -- "nothing was paged" is an answer, not a failure, and
    // that is also what the core reports.
    //
    // PushPaging / PopPaging save and restore the whole registration, which is
    // how a section of code runs with paging SUPPRESSED (a Save, say, which
    // must not fault in every subtree it walks): push, install nothing or
    // something else, do the work, pop.
    //
    // Two states answer MSGF_E_STATE rather than reaching the core, and both
    // are protections rather than policy: a SECOND push (the core's save has
    // one slot, and a nested push loses the first set), and a push with NO sink
    // installed (the core's pop asserts unless it has something to restore, so
    // that push is an abort at the matching pop -- and there is nothing to
    // suppress in that state anyway).  PopPaging without a push is the same
    // answer.
    public:
      virtual HRESULT SetPaging  ( IMsgPagingEvents *events ) = 0;
      virtual HRESULT PageIn     ( unsigned long long pos ) = 0;
      virtual HRESULT PageOut    ( unsigned long long pos, int flush ) = 0;
      virtual HRESULT PushPaging ( ) = 0;
      virtual HRESULT PopPaging  ( ) = 0;
};

// ===========================================================================
//  IMsgLibrary -- the process-wide entry point
// ===========================================================================
//
// One refcounted singleton.  It owns nothing a client can see; what it is for
// is being the ONE place a store comes from, so that the facade has somewhere
// to put process-scoped work (and so a client never has to know whether there
// is any).
class IMsgLibrary
{
    public:
      // A new, empty store.  `addr` is one of MSGF_ADDR_*, 0 for the platform
      // default; `initialBytes` and `maxBytes` size its heap, 0 for the
      // kernel's own defaults (~2 KB initial, no ceiling).
      //
      // `initialBytes` is a hint about GROWTH, not a limit: the heap starts
      // there and grows towards `maxBytes` on demand.  A non-zero request
      // below 512 is raised to 512, because the core does not return at all
      // from a smaller one -- see the note at FacadeStore::Create.  `maxBytes`
      // IS a limit, and a declare that would pass it answers MSGF_E_CORE.
      virtual HRESULT CreateStore ( unsigned char addr
                                  , unsigned int initialBytes
                                  , unsigned int maxBytes
                                  , IMsgStore **outStore ) = 0;
      // A store loaded from a .p2p file.  MSGF_E_FILE when it will not open.
      virtual HRESULT OpenStore   ( const wchar_t *filename, IMsgStore **outStore ) = 0;

      // Msgcore's own wildcard match, the one its name lookups use.  S_OK /
      // S_FALSE, both SUCCESS.
      virtual HRESULT WildcardMatch ( const wchar_t *pattern, const wchar_t *name ) const = 0;

      // The stable ASCII-in-wide name of a MSGF_TYPE_* code ("INT32",
      // "WSTR16", "GUID", ...) and its inverse.  One vocabulary, so a caller
      // that writes a type out and reads it back never disagrees with itself.
      // TypeFromName answers MSGF_E_TYPE for an unrecognised name.
      virtual HRESULT TypeName     ( unsigned char type
                                   , wchar_t *buf, unsigned int *cch ) const = 0;
      virtual HRESULT TypeFromName ( const wchar_t *name, unsigned char *outType ) const = 0;

      // Is `name` usable as an item name in this store family?  S_OK / S_FALSE.
      virtual HRESULT IsValidName  ( const wchar_t *name ) const = 0;

      // "MsgFacade 1.0 (ABI 1) over Msgcore x.y.z" -- for a log line or an
      // about box.  Owned by the DLL and valid for its lifetime.
      virtual const wchar_t* VersionString ( ) const = 0;

      virtual ULONG   Release      ( ) = 0;
};

} // namespace msgf

// ---------------------------------------------------------------------------
// The one exported function.
//
// `abiVersion` MUST be msgf::ABI_VERSION as this header defines it.  The DLL
// compares it against the range it still serves and answers
// MSGF_E_ABI_MISMATCH rather than handing back an object whose vtable the
// caller would read at the wrong offsets.  That check is the entire reason the
// argument exists; pass the constant, never a literal.
//
// The library is a refcounted singleton: calling this twice hands back the same
// object with a second reference, and the last Release tears it down.
// ---------------------------------------------------------------------------
extern "C" MSGF_API HRESULT __stdcall
MSGF_CreateLibrary ( unsigned int abiVersion, msgf::IMsgLibrary **outLibrary );
