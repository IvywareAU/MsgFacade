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
// FacadeCursor.cpp -- the two objects that hold a position rather than a route.
#include "stdafx.h"
#include "FacadeCursor.h"
#include "FacadeStore.h"
#include "FacadeNode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

// ===========================================================================
//  FacadeCursor
// ===========================================================================

FacadeCursor::FacadeCursor ( FacadeStore *pStore, const FacadeRoute& rOwner
                           , unsigned int uScope )
            : m_pStore ( pStore )
            , m_oOwner ( rOwner )
            , m_uScope ( uScope )
{
    if ( m_pStore )
      m_pStore -> AddRef ( );
}

//
//  Teardown
//  NOTES: The cursor goes FIRST.  A P3PmsgCurs over the attribute scope holds
//         a raw pointer to the attribute collection cached inside m_pField, so
//         releasing the field first would leave the cursor's destructor
//         reading freed storage
//
FacadeCursor::~FacadeCursor ( )
{
    delete m_pCurs;
    delete m_pField;
    if ( m_pStore )
      m_pStore -> ReleaseRef ( );
}

HRESULT
FacadeCursor::Make ( FacadeStore *pStore, const FacadeRoute& rOwner
                   , unsigned int uScope, IMsgCursor **outCursor )
{
    if ( !outCursor )
      return E_POINTER;
    *outCursor = nullptr;
    if ( !pStore )
      return MSGF_E_CLOSED;

    P3PmsgField oOwner;
    HRESULT     hr = pStore -> ResolveField ( rOwner, oOwner );
    if ( FAILED ( hr ) ) return hr;

    FacadeCursor *pCursor = new FacadeCursor ( pStore, rOwner, uScope );
    try
    {
      // Its OWN alias of the owner, kept for the cursor's whole life: the
      // caller's `oOwner` is about to go out of scope, and the attribute
      // cursor would be left pointing into it.
      pCursor->m_pField = new P3PmsgField ( oOwner.r_Object ( ) );
      pCursor->m_pCurs  = ( uScope == MSGF_SCOPE_ATTR )
                            ? new P3PmsgCurs ( pCursor->m_pField->r_Attr ( ) )
                            : new P3PmsgCurs ( *pCursor->m_pField );
    }
    catch ( ... )
    {
      pCursor->Release ( );
      return MSGF_E_CORE;
    }

    *outCursor = pCursor;
    return S_OK;
}

ULONG
FacadeCursor::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    delete this;
    return 0;
}

HRESULT
FacadeCursor::Rewind ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try { m_pCurs->Seek ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Forward one
//  NOTES: The end is tested BEFORE the advance, not after, because
//         P3PmsgCurs::operator++ RAISES when it is already past the last
//         element rather than saturating.  A loop that simply advanced until
//         something said stop would therefore end in an exception rather than
//         at the end of the collection
//
HRESULT
FacadeCursor::Next ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return S_FALSE;
      ++(*m_pCurs);
      return CursAtEnd ( *m_pCurs ) ? S_FALSE : S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Position on a named element
//  NOTES: A failed Goto(name) leaves the core cursor with its index reset to 0
//         and NO current element -- a state in which the index says "on the
//         first item" and every accessor raises.  So the previous position is
//         restored on failure, which is also what the header promises
//
HRESULT
FacadeCursor::GotoName ( const wchar_t *name )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !name ) return E_POINTER;

    try
    {
      int nWas   = m_pCurs->Item ( );
      int nCount = (int)m_pCurs->GetCount ( );
      if ( m_pCurs->Goto ( name ) )
        return S_OK;
      m_pCurs->Goto ( ( nWas < 0 || nWas >= nCount ) ? nCount : nWas );
      return MSGF_E_NO_ITEM;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeCursor::GotoIndex ( unsigned int index )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nWas   = m_pCurs->Item ( );
      int nCount = (int)m_pCurs->GetCount ( );
      if ( (int)index < nCount && m_pCurs->Goto ( (int)index ) )
        return S_OK;
      m_pCurs->Goto ( ( nWas < 0 || nWas >= nCount ) ? nCount : nWas );
      return MSGF_E_RANGE;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Is there a current element?
//  NOTES: NOT P3PmsgCurs::IsEoCursor, which answers TRUE while positioned ON
//         the last element -- a loop driven by it visits every element but the
//         last.  See CursAtEnd in FacadeInternal.h
//
HRESULT
FacadeCursor::IsEnd ( int *outEnd ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outEnd ) return E_POINTER;

    try { *outEnd = CursAtEnd ( *m_pCurs ) ? 1 : 0; return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeCursor::GetCount ( unsigned int *outCount ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outCount ) return E_POINTER;

    try { *outCount = (unsigned int)m_pCurs->GetCount ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeCursor::GetIndex ( unsigned int *outIndex ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outIndex ) return E_POINTER;

    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return MSGF_E_RANGE;
      *outIndex = (unsigned int)m_pCurs->Item ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeCursor::GetKind ( unsigned int *outKind ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outKind ) return E_POINTER;

    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return MSGF_E_RANGE;
      *outKind = KindOf ( m_pCurs->r_Object ( ) );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeCursor::GetName ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return MSGF_E_RANGE;
      return CopyOut ( m_pCurs->c_wstr ( ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  A node for the current element
//  NOTES: The one bridge back from a position to a route, and the reason a
//         walk can safely be followed by a round of mutations: collect nodes
//         here, release the cursor, then act on them
//
HRESULT
FacadeCursor::GetNode ( IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outNode ) return E_POINTER;
    *outNode = nullptr;

    CString strName;
    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return MSGF_E_RANGE;
      strName = m_pCurs->c_wstr ( );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    FacadeRoute oRoute ( m_oOwner );
    FacadeStep  oStep;
    oStep.uScope  = m_uScope;
    oStep.strName = strName;
    oRoute.push_back ( oStep );

    return FacadeNode::Make ( m_pStore, oRoute, outNode );
}

//
//  Remove the current element
//  NOTES: The core's Delete leaves the cursor with no current element and its
//         index at 0 -- a state whose accessors raise -- so the cursor is
//         re-seated at the first element afterwards.  That is what the header
//         promises, and it is why Delete does not simply invalidate the thing
//         it was called on
//
HRESULT
FacadeCursor::Delete ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( CursAtEnd ( *m_pCurs ) )
        return MSGF_E_RANGE;
      m_pCurs->Delete ( );
      m_pCurs->Seek   ( );
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ===========================================================================
//  FacadeWalker
// ===========================================================================

FacadeWalker::FacadeWalker ( FacadeStore *pStore, const FacadeRoute& rOwner
                           , unsigned int uScope )
            : m_pStore ( pStore )
            , m_oOwner ( rOwner )
            , m_uScope ( uScope )
{
    if ( m_pStore )
      m_pStore -> AddRef ( );
}

FacadeWalker::~FacadeWalker ( )
{
    for ( size_t i = m_aLevels.size ( ); i > 0; --i )
      delete m_aLevels[i - 1].pCurs;
    m_aLevels.clear ( );
    delete m_pField;
    if ( m_pStore )
      m_pStore -> ReleaseRef ( );
}

HRESULT
FacadeWalker::Make ( FacadeStore *pStore, const FacadeRoute& rOwner
                   , unsigned int uScope, IMsgWalker **outWalker )
{
    if ( !outWalker )
      return E_POINTER;
    *outWalker = nullptr;
    if ( !pStore )
      return MSGF_E_CLOSED;

    P3PmsgField oOwner;
    HRESULT     hr = pStore -> ResolveField ( rOwner, oOwner );
    if ( FAILED ( hr ) ) return hr;

    FacadeWalker *pWalker = new FacadeWalker ( pStore, rOwner, uScope );
    try
    {
      pWalker->m_pField = new P3PmsgField ( oOwner.r_Object ( ) );
      Level oLevel;
      oLevel.pCurs  = ( uScope == MSGF_SCOPE_ATTR )
                        ? new P3PmsgCurs ( pWalker->m_pField->r_Attr ( ) )
                        : new P3PmsgCurs ( *pWalker->m_pField );
      // NOT fresh: the caller reads the outermost level's first element before
      // it ever calls Next, so the first Next must advance.
      oLevel.bFresh = false;
      pWalker->m_aLevels.push_back ( oLevel );
    }
    catch ( ... )
    {
      pWalker->Release ( );
      return MSGF_E_CORE;
    }

    *outWalker = pWalker;
    return S_OK;
}

ULONG
FacadeWalker::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    delete this;
    return 0;
}

P3PmsgCurs*
FacadeWalker::Top ( ) const
{
    return m_aLevels.empty ( ) ? nullptr : m_aLevels.back ( ).pCurs;
}

//
//  Advance, ascending out of a level that is finished
//  NOTES: The auto-ascend is the whole reason a walk is one loop rather than
//         two.  It is bounded: the outermost level is never popped, so a walk
//         that runs out answers S_FALSE from there for ever rather than
//         emptying its own stack
//
HRESULT
FacadeWalker::Next ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      while ( !m_aLevels.empty ( ) )
      {
        Level&      rTop = m_aLevels.back ( );
        P3PmsgCurs *pTop = rTop.pCurs;

        if ( rTop.bFresh )
        {
          // Just descended: the level is already standing on its first child,
          // so this Next steps ONTO it rather than over it.
          rTop.bFresh = false;
          if ( !CursAtEnd ( *pTop ) )
            return S_OK;
        }
        else
        {
          if ( !CursAtEnd ( *pTop ) )
            ++(*pTop);
          if ( !CursAtEnd ( *pTop ) )
            return S_OK;
        }

        if ( m_aLevels.size ( ) == 1 )
          return S_FALSE;                    // the walk is over
        delete pTop;
        m_aLevels.pop_back ( );              // ascend and try the parent again
      }
      return S_FALSE;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Descend into the current element
//  NOTES: Three outcomes rather than two, and the distinction is worth having:
//         MSGF_E_TYPE for an element that CANNOT be descended into (a list, a
//         vect, a bare value), S_FALSE for an item that simply has no
//         children, and S_OK for a descent that happened.  Only the last one
//         changes the walker, so a caller that treats S_FALSE as "pruned" is
//         right
//       : An empty level is never pushed, which is what keeps IsEnd honest:
//         every open level below the outermost is positioned on something
//
HRESULT
FacadeWalker::Push ( int *outDepth )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgCurs *pTop = Top ( );
    if ( !pTop )
      return MSGF_E_RANGE;
    if ( m_aLevels.size ( ) > MAX_DEPTH )
      return MSGF_E_DEPTH;

    try
    {
      if ( CursAtEnd ( *pTop ) )
        return MSGF_E_RANGE;
      if ( !pTop->IsItem ( ) )
        return MSGF_E_TYPE;

      P3PmsgCurs *pNew = new P3PmsgCurs ( pTop->r_item ( ) );
      if ( CursAtEnd ( *pNew ) )
      {
        delete pNew;
        return S_FALSE;                      // nothing under it
      }
      Level oLevel;
      oLevel.pCurs  = pNew;
      oLevel.bFresh = true;                  // the next Next lands ON its first child
      m_aLevels.push_back ( oLevel );
      if ( outDepth )
        *outDepth = (int)m_aLevels.size ( ) - 1;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeWalker::Pop ( int *outDepth )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( m_aLevels.size ( ) <= 1 )
      return MSGF_E_RANGE;

    delete m_aLevels.back ( ).pCurs;
    m_aLevels.pop_back ( );
    if ( outDepth )
      *outDepth = (int)m_aLevels.size ( ) - 1;
    return S_OK;
}

//
//  Abandon every pushed level
//  NOTES: Resumes at the outermost, positioned where it was -- so the next
//         Next continues with the SIBLINGS of the branch that was abandoned.
//         The core's own Break ends the outermost level too, which turns
//         "stop descending" into "stop walking"
//
HRESULT
FacadeWalker::Break ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    while ( m_aLevels.size ( ) > 1 )
    {
      delete m_aLevels.back ( ).pCurs;
      m_aLevels.pop_back ( );
    }
    return S_OK;
}

HRESULT
FacadeWalker::IsEnd ( int *outEnd ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outEnd ) return E_POINTER;

    P3PmsgCurs *pTop = Top ( );
    if ( !pTop ) { *outEnd = 1; return S_OK; }

    try { *outEnd = CursAtEnd ( *pTop ) ? 1 : 0; return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeWalker::GetDepth ( int *outDepth ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outDepth ) return E_POINTER;

    *outDepth = m_aLevels.empty ( ) ? 0 : (int)m_aLevels.size ( ) - 1;
    return S_OK;
}

HRESULT
FacadeWalker::GetKind ( unsigned int *outKind ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outKind ) return E_POINTER;

    P3PmsgCurs *pTop = Top ( );
    if ( !pTop ) return MSGF_E_RANGE;

    try
    {
      if ( CursAtEnd ( *pTop ) )
        return MSGF_E_RANGE;
      *outKind = KindOf ( pTop->r_Object ( ) );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeWalker::GetName ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgCurs *pTop = Top ( );
    if ( !pTop ) return MSGF_E_RANGE;

    try
    {
      if ( CursAtEnd ( *pTop ) )
        return MSGF_E_RANGE;
      return CopyOut ( pTop->c_wstr ( ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}
