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
// FacadeInternal.h -- internal implementation surface of MsgFacade.dll.
//
// ---------------------------------------------------------------------------
// IMPLEMENTATION MAP (facade -> Msgcore)
// ---------------------------------------------------------------------------
//  MSGF_CreateLibrary        -> FacadeLibrary::Acquire (refcounted singleton)
//  IMsgLibrary::CreateStore  -> new P2PmsgMgr / P2PmsgMgr(addr,init,max)
//  IMsgLibrary::OpenStore    -> new P2PmsgMgr(filename), then IsValid
//  IMsgLibrary::WildcardMatch-> P2Pmsg_wcsicmpWC
//
//  IMsgStore::Save/Load      -> P2PmsgMgr::Save/Load
//  IMsgStore::Clear          -> P2PmsgMgr::Nullify
//  IMsgStore::Rename         -> P2PmsgMgr::Rename
//  IMsgStore::GetRoot        -> a FacadeNode with an EMPTY route
//  IMsgStore::NodeFromPos    -> P2PmsgMgr::P2Pos2Field, then the route is
//                               rebuilt by walking UP from that node (see
//                               FacadeStore::RouteOfPos) -- a position is
//                               turned into a path once, here, so that
//                               everything downstream is a path
//  IMsgStore::SetEvents      -> P2PmsgMgr::SetTriggerSink + a static trampoline
//  IMsgStore::Arm/Disarm     -> P2PmsgMgr::CreateTrigger/DropTriggers, HWND 0
//  IMsgStore::Fire           -> P2PmsgMgr::FireTrigger
//
//  ROUTE RESOLUTION -- the one operation everything else is built on:
//
//    ResolveRoute ( mgr, route, out )
//        out = mgr->r_Object()                  // alias the live root
//        for each step:
//            P3PmsgCurs over the step's scope   // r_Desc() or r_Attr()
//            Goto ( step name )                 // false -> MSGF_E_NO_ITEM
//            out = cursor's r_Object()          // alias the live child
//
//    Three details of that loop are load-bearing:
//
//    a) ALIASING, not copying.  `P3PmsgField x = y;` deep-copies into a
//       DETACHED field whose writes never reach the store; `x = y.r_Object()`
//       binds to the live heap node.  Every assignment in the resolver is the
//       second form.  (This is the same distinction Msgcore's own headers draw
//       between their detached and live handle families.)
//
//    b) A CURSOR, not SelectItem.  P3PmsgField::SelectItem RAISES on a child
//       that is a list or a vect -- "a list is not an item" -- so a resolver
//       built on it could not walk to, or through, any container.  P3PmsgCurs
//       ::Goto handles all three kinds, and reports absence with `false`
//       rather than an exception.  It is also how Msgcore's own flat layer had
//       to implement its container selectors, for the same reason.
//
//    c) The cursor is DESTROYED before the assignment.  P3PmsgCurs(P3PmsgAttr&)
//       keeps a raw pointer to the attr collection cached inside the field it
//       was built from; assigning to that field while the cursor is alive
//       would leave the cursor pointing at a re-seated collection.  So each
//       step lifts the P3PmsgObject out, closes the cursor, and only then
//       moves `out` on.
//
//  IMsgNode::GetChild        -> ResolveRoute + one more step appended
//  IMsgNode::Declare*        -> ResolveRoute, then
//                                 CHILD: field.DeclareItem(name,data,bUpdate)
//                                 ATTR : field.r_Attr(AttrCMD_Create)
//                                             .DeclareItem(name,data,bUpdate)
//     ... the P3PmsgData is built as a TEMPORARY of the exact requested width
//         and passed straight in, one switch arm per type.  It is never
//         assigned into a local: P3PmsgData's assignment operator resizes and
//         copies through the heap, which is the wrong operation entirely for
//         "make me a value of this type"
//  IMsgNode::Retype          -> the same Declare with a ZERO value of the new
//                               type and bUpdate TRUE.  DeclareItem's update
//                               path is `r_item().r_data() = oData`, and
//                               P3PmsgData::operator= copies the whole
//                               VBLockData INCLUDING its type word -- so a
//                               declare-with-update IS a retype, and one that
//                               keeps the node's name, position and children.
//                               No P3PmsgRefactor_DataType needed, and it
//                               works in the attribute scope, which that
//                               helper has no overload for
//  IMsgNode::SetInt/SetReal  -> P3PmsgData::Recreate(type,&value,0) at the
//                               node's EXISTING type, then SetAttr(0,
//                               VBLockAttr_NULL) to clear the null flag
//                               Recreate does not touch.
//     ... Recreate rather than the c_* accessors because the accessors are
//         strict AND INCOMPLETE: c_int() raises on anything but INT32, and
//         there is no accessor at all for UINT08 or UINT16 -- P3PmsgData
//         declares c_char/c_short/c_int/c_uint/c_int64/c_uint64 and no
//         unsigned narrow pair.  A switch over the accessors could not write
//         two of the nine integer widths this ABI accepts.  Recreate writes
//         the union member for every one of them
//  IMsgNode::SetText         -> P3PmsgData::c_wcscpy, which grows the blob up
//                               to its BlobMax and raises past it -> MSGF_E_LIMIT
//  IMsgNode::GetInt          -> P3PmsgData::ReadAnyInt (the one width-agnostic
//                               reader the core has; it reports the subtype's
//                               signedness and returns false rather than
//                               raising off-family)
//  IMsgNode::GetTime/SetTime -> P3Pmsg_GetTStamp / P3Pmsg_SetTStamp
//  IMsgNode::Rename/Move     -> P3PmsgRefactor_Rename / P3PmsgRefactor_Move,
//                               the field/field and attr/field overloads
//
//  IMsgList/IMsgVect         -> FacadeList / FacadeVect hold a ROUTE, exactly
//                               like a node, and re-resolve to a P3PmsgList /
//                               P3PmsgVect per call.  So a container handle
//                               survives a mutation for the same reason a node
//                               does.  A route step may also be an ELEMENT
//                               INDEX (SCOPE_ELEM), which is how a nested
//                               container inside a vect is named -- vect
//                               elements have no reliable name
//
//  IMsgCursor                -> FacadeCursor holds a live P3PmsgCurs and the
//                               owner's resolved field, and is therefore the
//                               one thing here a mutation invalidates
//     ... its end test is NOT P3PmsgCurs::IsEoCursor.  That answers TRUE while
//         positioned ON the last element (`m_nItem >= nItems-1`), so a loop
//         driven by it skips the last item; and operator++ RAISES rather than
//         saturating when advanced from the end.  FacadeCursor tests
//         Item()/GetCount() itself (CursAtEnd below) and refuses to advance
//         past the end, so IsEnd means "there is no current element" and Next
//         answers S_FALSE instead of throwing
//
//  IMsgWalker                -> FacadeWalker owns an explicit STACK of
//                               P3PmsgCurs, not P2PmsgRecurs.
//     ... P2PmsgRecurs is the core's own walker and would have been the
//         obvious choice, but two of its properties are not expressible
//         through this ABI: its depth counter is protected and its advance
//         POPS LEVELS SILENTLY, so a caller-side depth count drifts and
//         GetDepth could not be implemented honestly; and its Push RAISES on
//         an element it cannot descend into rather than reporting it.  A stack
//         of cursors gives exact depth, an explicit prune, and a Push that
//         returns MSGF_E_TYPE.  Each level's cursor is built from the level
//         above's current item, and P3PmsgCurs(P3PmsgItem&) copies the parent
//         it is given -- so no level depends on another level's storage
//
//  MsgStck                   -> deliberately NOT exposed.  It is the core's
//                               "remember where I was" stack, and its whole
//                               purpose is to let a walker return to a
//                               position it can no longer name.  Here a node
//                               IS a name, so holding one is already the saved
//                               position, and a stack of them is a std::vector
//                               in the client
//
//  Threading note: every exported entry point (the factory + every vtable
//  method) must open with AFX_MANAGE_STATE(AfxGetStaticModuleState()), and
//  every method that touches a store must take that store's lock -- regular
//  MFC DLL, and a core with no internal synchronisation.  StoreGuard below
//  does both in one line.
// ---------------------------------------------------------------------------
#pragma once

#include "MsgFacade.h"

#include <vector>

class FacadeStore;

// ---------------------------------------------------------------------------
//  One step of a route
//
//  NOTES: The two public scopes are msgf::MSGF_SCOPE_CHILD / _ATTR.  SCOPE_ELEM
//         is internal and never crosses the ABI: it names an element of a vect
//         by index, which is the only way to reach a container nested inside
//         one (vect elements need not carry a usable name)
// ---------------------------------------------------------------------------
const unsigned int SCOPE_ELEM = 2;

struct FacadeStep
{
    unsigned int uScope{msgf::MSGF_SCOPE_CHILD};
    CString      strName;
    unsigned int nIndex{0};
};

typedef std::vector<FacadeStep> FacadeRoute;

// ---------------------------------------------------------------------------
//  Hand one string back through the caller's buffer
//  NOTES: The plain Win32 in/out protocol: *cch in is capacity, *cch out is
//         what was needed -- ALWAYS, so one failed call tells you exactly how
//         big to make the second one
//       : Nothing is written on a short buffer.  A partial string is worse
//         than none: it looks like a value
// ---------------------------------------------------------------------------
inline HRESULT
CopyOut ( const wchar_t *lpszValue, wchar_t *buf, unsigned int *cch )
{
    if ( !cch )
      return E_POINTER;
    if ( !lpszValue )
      lpszValue = L"";

    unsigned int uNeed = (unsigned int)::wcslen ( lpszValue ) + 1;
    unsigned int uHave = buf ? *cch : 0;
    *cch = uNeed;                       // reported whatever happens next

    if ( !buf )
      return S_OK;                      // size query
    if ( uHave < uNeed )
      return HRESULT_FROM_WIN32 ( ERROR_MORE_DATA );

    ::wcscpy_s ( buf, uHave, lpszValue );
    return S_OK;
}

// ---------------------------------------------------------------------------
//  Hand one BLOB back through the caller's buffer
//  NOTES: CopyOut's twin for the one thing in this ABI that is bytes rather
//         than characters.  Same protocol exactly
//       : A zero-length value is a real value and answers S_OK with *size 0.
//         That is the difference between a node that is present and empty and
//         one that is absent, which answers MSGF_E_NO_ITEM -- and the reason
//         this cannot simply reuse the string version
// ---------------------------------------------------------------------------
inline HRESULT
CopyOutBytes ( const void *pvValue, unsigned int uSize
             , void *buf, unsigned int *size )
{
    if ( !size )
      return E_POINTER;

    unsigned int uHave = buf ? *size : 0;
    *size = uSize;                      // reported whatever happens next

    if ( !buf )
      return S_OK;                      // size query
    if ( uHave < uSize )
      return HRESULT_FROM_WIN32 ( ERROR_MORE_DATA );

    if ( uSize )
      ::memcpy ( buf, pvValue, uSize );
    return S_OK;
}

// ---------------------------------------------------------------------------
//  ScopeCursor -- a P3PmsgCurs over one scope of one field
//
//  NOTES: Heap-held so that no P3PmsgCurs is ever copied or assigned.  Its
//         copy operator calls RecycleThis and then copies only three of its
//         seven members, leaving the copy pointing at nothing; the class is
//         usable but not value-like, so this wrapper simply never treats it
//         as a value
//       : For the ATTRIBUTE scope the cursor keeps a RAW POINTER to the attr
//         collection cached inside rField, so rField MUST outlive the cursor.
//         Every use below keeps both in one block for exactly that reason
//       : For the CHILD scope P3PmsgCurs(P3PmsgItem&) takes its own copy of
//         the parent, so there is no such coupling -- but the rule is stated
//         once for both rather than per call site
// ---------------------------------------------------------------------------
class ScopeCursor
{
    public:
        ScopeCursor ( P3PmsgField& rField, unsigned int uScope )
        {
          if ( uScope == msgf::MSGF_SCOPE_ATTR )
            m_pCurs = new P3PmsgCurs ( rField.r_Attr() );
          else
            m_pCurs = new P3PmsgCurs ( rField );
        }
       ~ScopeCursor ( ) { delete m_pCurs; }

      P3PmsgCurs* operator -> ( ) const { return  m_pCurs; }
      P3PmsgCurs& operator *  ( ) const { return *m_pCurs; }

    private:
        ScopeCursor ( const ScopeCursor& );
        ScopeCursor& operator = ( const ScopeCursor& );
        P3PmsgCurs *m_pCurs{nullptr};
};

//
//  Is a cursor past its last element?
//  NOTES: NOT P3PmsgCurs::IsEoCursor, which answers TRUE while positioned ON
//         the last element -- see the note on IMsgCursor in the map above
//       : Item() is INT_MAX-1 after an advance off the end, and GetCount() is
//         0 for a collection that was never created, so both ends are covered
//         by the one comparison
//
inline bool
CursAtEnd ( P3PmsgCurs& rCurs )
{
    int nCount = (int)rCurs.GetCount ( );
    int nItem  =      rCurs.Item     ( );
    return nCount <= 0 || nItem < 0 || nItem >= nCount;
}

// ---------------------------------------------------------------------------
//  Type predicates and conversions
// ---------------------------------------------------------------------------
bool    IsIntType   ( unsigned char uType );
bool    IsRealType  ( unsigned char uType );
bool    IsTextType  ( unsigned char uType );
// Does `iValue` survive being stored at `uType`?  MSGF_E_LIMIT hangs off this:
// the core would truncate silently.
bool    IntFits     ( long long iValue, unsigned char uType );
// The facade's spelling of a type code, and its inverse.  0 / MSGF_TYPE_NULL
// mean "not recognised".
const wchar_t*
        TypeToName  ( unsigned char uType );
unsigned char
        TypeFromName( const wchar_t *lpszName );

// A name usable as an item name -- non-empty, within MAX_NAME, and free of the
// characters the core's own validator refuses (which include '.' and '@', the
// two the facade's path grammar is built on).
bool    IsUsableName ( const wchar_t *lpszName );

// ---------------------------------------------------------------------------
//  Value read / write against ONE live data cell
//
//  NOTES: These take P3PmsgData& because a node, a list cell and a vect
//         element are all one -- P3PmsgField derives from P3PmsgData, and a
//         list cell IS a P3PmsgData.  One implementation serves all three
//       : Every one of them returns an HRESULT rather than raising, and none
//         of them lets a Msgexception escape
// ---------------------------------------------------------------------------
HRESULT ReadInt   ( const P3PmsgData& rData, long long *outValue, int *outUnsigned );
HRESULT ReadReal  ( const P3PmsgData& rData, double *outValue );
HRESULT ReadText  ( const P3PmsgData& rData, wchar_t *buf, unsigned int *cch );
HRESULT ReadBlob  ( const P3PmsgData& rData, void *buf, unsigned int *size );
HRESULT ReadGuid  ( const P3PmsgData& rData, wchar_t *buf, unsigned int *cch );

HRESULT WriteInt  ( P3PmsgData& rData, long long iValue );
HRESULT WriteReal ( P3PmsgData& rData, double dValue );
//
//  Overwrite a text cell
//  NOTES: `bMayGrow` is not a preference, it is a property of WHERE the cell
//         lives.  A field's value sits in an item block the heap can
//         reallocate, so a longer string simply grows it.  A LIST CELL sits
//         inside the list's own block, and the core's grow path reallocates
//         through the cell's owning object -- which for a list cell is not a
//         block of its own.  It asserts and aborts rather than failing, so the
//         over-long write has to be refused here, before the call
//
HRESULT WriteText ( P3PmsgData& rData, const wchar_t *lpszValue
                  , bool bMayGrow = true );

// Parse canonical GUID text (braces tolerated).  false on any malformation.
bool    ParseGuid ( const wchar_t *lpszGuid, GUID& rGuid );

// ---------------------------------------------------------------------------
//  Declaring into one scope of one live field
//
//  NOTES: One function per value family rather than one taking a P3PmsgData,
//         because building the P3PmsgData is the part that has to happen at
//         the exact requested width and must not pass through an assignment
//       : `pOutName` (optional) receives nothing -- the caller already knows
//         the name; the functions return only success, and the caller builds
//         the node route itself
// ---------------------------------------------------------------------------
HRESULT DeclareInt  ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, long long iValue
                    , unsigned char uType, BOOL bUpdate );
HRESULT DeclareReal ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, double dValue
                    , unsigned char uType, BOOL bUpdate );
HRESULT DeclareText ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, const wchar_t *lpszValue
                    , BOOL bUpdate );
HRESULT DeclareBlob ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, const void *pvValue
                    , unsigned int uSize, BOOL bUpdate );
HRESULT DeclareGuid ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, const wchar_t *lpszGuid
                    , BOOL bUpdate );
// A zero value of `uType`, which is what Retype writes.
HRESULT DeclareZero ( P3PmsgField& rParent, unsigned int uScope
                    , const wchar_t *lpszName, unsigned char uType );

// ---------------------------------------------------------------------------
//  Containers
// ---------------------------------------------------------------------------
// Build a container and attach it to one scope of rParent.  Attaching is
// `collection += container`, which DEEP-COPIES the built object into the heap
// -- so the caller must read the attached node back rather than keep the
// temporary, which still refers to its own caller-side storage.  Here that
// falls out for free: the caller keeps a ROUTE and resolves it.
HRESULT AttachList ( P3PmsgField& rParent, unsigned int uScope
                   , const wchar_t *lpszName );
HRESULT AttachVect ( P3PmsgField& rParent, unsigned int uScope
                   , const wchar_t *lpszName
                   , unsigned int uElems, unsigned char uType );

//
//  The nIndex'th cell of a list, or NULL when the index is past the end
//  NOTES: O(nIndex).  P3PmsgList is a chain walked by VBLaddr, and a VBLaddr
//         is precisely the heap address a relocation invalidates -- so an
//         index is the only thing that can safely name a cell across a call,
//         and a full scan of a long list is quadratic.  IMsgCursor is the
//         answer for that; the header says so
//
P3PmsgData* ListCellAt ( P3PmsgList& rList, unsigned int nIndex );

// The structural kind of one live object.
unsigned int KindOf ( const P3PmsgObject& rObject );
