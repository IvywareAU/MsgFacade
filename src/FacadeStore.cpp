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
// FacadeStore.cpp -- the store, the lock, and the route resolver.
#include "stdafx.h"
#include "FacadeStore.h"
#include "FacadeNode.h"
#include "FacadeLibrary.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

FacadeStore::FacadeStore ( FacadeLibrary *pLibrary, P2PmsgMgr *pMgr )
           : m_pLibrary ( pLibrary )
           , m_pMgr     ( pMgr )
{
    if ( m_pLibrary )
      m_pLibrary -> AddRef ( );
}

FacadeStore::~FacadeStore ( )
{
    if ( m_pMgr )
    {
      // Clear the sinks BEFORE the manager goes: the core holds the raw
      // function pointer and the raw user pointer, and a destructor that fired
      // one would call into a half-dead store.
      try { m_pMgr->SetTriggerSink ( nullptr, nullptr ); } catch ( ... ) { }
      m_pPaging = nullptr;
      try
      {
        m_pMgr->PageRegistration ( (PINT_PTR)0, (P2PageinCBFnc)0, (P2PageoutCBFnc)0 );
      }
      catch ( ... ) { }
      delete m_pMgr;
      m_pMgr = nullptr;
    }
    if ( m_pLibrary )
      m_pLibrary -> ReleaseRef ( );
}

//
//  A new, empty store
//  NOTES: The default constructor and the three-argument one are NOT the same
//         call with different numbers: the three-argument one takes an
//         addressing width, and a store created at 16-bit addressing cannot
//         grow past 64 KB whatever it is later asked to hold.  0 selects the
//         core's own default rather than guessing on the caller's behalf
//
HRESULT
FacadeStore::Create ( FacadeLibrary *pLibrary, unsigned char uAddr
                    , unsigned int uInitial, unsigned int uMax
                    , IMsgStore **outStore )
{
    if ( !outStore )
      return E_POINTER;
    *outStore = nullptr;

    if ( uAddr != 0             &&
         uAddr != MSGF_ADDR_16  &&
         uAddr != MSGF_ADDR_32  &&
         uAddr != MSGF_ADDR_64     )
      return E_INVALIDARG;

    // A heap smaller than this DOES NOT COME BACK.  Measured, by
    // _Msgcore_UseExamplesLight\BstrWidthTest, across all three widths: an
    // initial request of 1..256 bytes hangs inside the core's construction
    // rather than failing, and 512 is the smallest that returns.  So a
    // non-zero request below the floor is raised to it -- silently, because
    // the alternative is refusing a store the caller can perfectly well have,
    // over a number that was only ever a hint about growth.
    const unsigned int kMinInitial = 512;
    if ( uInitial != 0 && uInitial < kMinInitial )
      uInitial = kMinInitial;

    P2PmsgMgr *pMgr = nullptr;
    try
    {
      if ( uAddr == 0 && uInitial == 0 && uMax == 0 )
        pMgr = new P2PmsgMgr ( );
      else
        pMgr = new P2PmsgMgr ( uAddr ? uAddr : (unsigned char)VBLock_Addrxx
                             , uInitial ? uInitial : 2024
                             , uMax );
    }
    catch ( ... )
    {
      delete pMgr;
      return MSGF_E_CORE;
    }
    if ( !pMgr )
      return E_OUTOFMEMORY;

    FacadeStore *pStore = new FacadeStore ( pLibrary, pMgr );
    // Remembered so Clear can build an EQUIVALENT store rather than a default
    // one -- a store asked for at 16-bit addressing must still be 16-bit after
    // it is emptied.
    pStore->m_uAddr    = uAddr;
    pStore->m_uInitial = uInitial;
    pStore->m_uMax     = uMax;

    *outStore = pStore;
    return S_OK;
}

HRESULT
FacadeStore::Open ( FacadeLibrary *pLibrary, const wchar_t *lpszFilename
                  , IMsgStore **outStore )
{
    if ( !outStore )
      return E_POINTER;
    *outStore = nullptr;
    if ( !lpszFilename || !*lpszFilename )
      return E_INVALIDARG;

    // Validate the FILE before building anything from it.  The filename
    // constructor does not report failure, and -- measured -- the manager it
    // leaves behind answers IsValid() TRUE for a file that does not exist: it
    // has a perfectly good empty heap, which is what IsValid is asking about.
    // P2PmsgMgr_IsValid(filename) is the overload that asks the other question.
    try
    {
      if ( !P2PmsgMgr_IsValid ( lpszFilename ) )
        return MSGF_E_FILE;
    }
    catch ( ... ) { return MSGF_E_FILE; }

    P2PmsgMgr *pMgr = nullptr;
    try
    {
      pMgr = new P2PmsgMgr ( lpszFilename );
      if ( !pMgr->IsValid ( ) )
      {
        delete pMgr;
        return MSGF_E_FILE;
      }
    }
    catch ( ... )
    {
      delete pMgr;
      return MSGF_E_FILE;
    }

    FacadeStore *pStore = new FacadeStore ( pLibrary, pMgr );
    pStore->m_strFile = lpszFilename;      // survives Clear; see the member
    *outStore = pStore;
    return S_OK;
}

// ---------------------------------------------------------------------------
//  Refcount
// ---------------------------------------------------------------------------

void
FacadeStore::AddRef ( )
{
    ::InterlockedIncrement ( &m_cRef );
}

ULONG
FacadeStore::ReleaseRef ( )
{
    LONG cRef = ::InterlockedDecrement ( &m_cRef );
    if ( cRef == 0 )
      delete this;
    return (ULONG)cRef;
}

ULONG
FacadeStore::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );

    // Close FIRST, under the lock, so that anything already inside a call on
    // another thread finishes against a live manager and everything after it
    // sees MSGF_E_CLOSED.
    {
      StoreGuard oGuard ( this );
      if ( !m_bClosed )
      {
        m_bClosed = TRUE;
        m_pEvents = nullptr;
        m_pPaging = nullptr;
        if ( m_pMgr )
        {
          try { m_pMgr->SetTriggerSink ( nullptr, nullptr ); } catch ( ... ) { }
          try
          {
            m_pMgr->PageRegistration ( (PINT_PTR)0, (P2PageinCBFnc)0, (P2PageoutCBFnc)0 );
          }
          catch ( ... ) { }
        }
      }
    }
    return ReleaseRef ( );
}

void
FacadeStore::MarkDirty ( )
{
    if ( m_pMgr )
      try { m_pMgr->SetDirty ( TRUE ); } catch ( ... ) { }
}

// ---------------------------------------------------------------------------
//  The route resolver
// ---------------------------------------------------------------------------

//
//  Resolve a route to the live object it names
//  NOTES: The caller must hold Section().  Every assignment binds to the live
//         heap node -- `x = y.r_Object()` -- rather than deep-copying, which
//         is what `P3PmsgField x = y;` would do
//       : Each step lifts its result out as a P3PmsgObject and lets the cursor
//         (and the field the cursor was built from) go BEFORE moving on.  A
//         P3PmsgCurs over the attribute scope keeps a raw pointer into the
//         field it came from, so overlapping the two lifetimes would leave it
//         pointing at a collection that had been re-seated underneath it
//
HRESULT
FacadeStore::ResolveObject ( const FacadeRoute& rRoute, P3PmsgObject& rOut ) const
{
    if ( m_bClosed || !m_pMgr )
      return MSGF_E_CLOSED;
    if ( rRoute.size ( ) > MAX_DEPTH )
      return MSGF_E_DEPTH;

    try
    {
      P3PmsgObject oCur = m_pMgr -> r_Object ( );

      for ( size_t i = 0; i < rRoute.size ( ); ++i )
      {
        const FacadeStep& rStep = rRoute[i];
        P3PmsgObject      oNext;

        if ( rStep.uScope == SCOPE_ELEM )
        {
          if ( !oCur.IsVect ( ) )
            return MSGF_E_TYPE;
          P3PmsgVect oVect ( oCur );
          int        nElem = (int)rStep.nIndex;
          if ( nElem < 0 || nElem >= (int)oVect.GetCount ( ) )
            return MSGF_E_RANGE;
          if      ( oVect.IsList  ( nElem ) ) oNext = oVect.r_list ( nElem ).r_Object ( );
          else if ( oVect.IsVect  ( nElem ) ) oNext = oVect.r_vect ( nElem ).r_Object ( );
          else if ( oVect.IsField ( nElem ) ) oNext = oVect.r_item ( nElem ).r_Object ( );
          else                                return MSGF_E_TYPE;
        }
        else
        {
          if ( !oCur.IsField ( ) )
            return MSGF_E_TYPE;             // a list/vect has no named children
          P3PmsgField oField ( oCur );
          ScopeCursor oCurs  ( oField, rStep.uScope );
          if ( !oCurs->Goto ( (LPCWSTR)rStep.strName ) )
            return MSGF_E_NO_ITEM;
          oNext = oCurs->r_Object ( );
        }

        oCur = oNext;
      }

      rOut = oCur;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::ResolveField ( const FacadeRoute& rRoute, P3PmsgField& rOut ) const
{
    P3PmsgObject oObject;
    HRESULT      hr = ResolveObject ( rRoute, oObject );
    if ( FAILED ( hr ) )
      return hr;
    try
    {
      if ( !oObject.IsField ( ) )
        return MSGF_E_TYPE;
      rOut = oObject;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  The path grammar
//
//  '.' before a descendant step, '@' before an attribute step, nothing at all
//  for the root.  Unambiguous because the core's own name validator refuses
//  both characters inside a name -- see IsUsableName.
// ---------------------------------------------------------------------------

HRESULT
FacadeStore::ParseRoute ( const wchar_t *lpszPath, FacadeRoute& rOut )
{
    rOut.clear ( );
    if ( !lpszPath )
      return E_POINTER;

    const wchar_t *p = lpszPath;
    while ( *p )
    {
      unsigned int uScope;
      if      ( *p == L'.' ) uScope = MSGF_SCOPE_CHILD;
      else if ( *p == L'@' ) uScope = MSGF_SCOPE_ATTR;
      else                   return MSGF_E_PATH;   // a step must be introduced
      ++p;

      CString strName;
      while ( *p && *p != L'.' && *p != L'@' )
        strName += *p++;

      if ( !IsUsableName ( (LPCWSTR)strName ) )
        return MSGF_E_PATH;
      if ( rOut.size ( ) >= MAX_DEPTH )
        return MSGF_E_DEPTH;

      FacadeStep oStep;
      oStep.uScope  = uScope;
      oStep.strName = strName;
      rOut.push_back ( oStep );
    }
    return S_OK;
}

void
FacadeStore::FormatRoute ( const FacadeRoute& rRoute, CString& rOut )
{
    rOut.Empty ( );
    for ( size_t i = 0; i < rRoute.size ( ); ++i )
    {
      const FacadeStep& rStep = rRoute[i];
      if ( rStep.uScope == SCOPE_ELEM )
      {
        // Internal only -- an element step never appears in a node's route, so
        // this arm exists so that a diagnostic dump of a list/vect route reads
        // sensibly rather than silently losing a level.
        CString strElem;
        strElem.Format ( L"[%u]", rStep.nIndex );
        rOut += strElem;
      }
      else
      {
        rOut += ( rStep.uScope == MSGF_SCOPE_ATTR ) ? L'@' : L'.';
        rOut += rStep.strName;
      }
    }
}

// ---------------------------------------------------------------------------
//  Position -> route
// ---------------------------------------------------------------------------

//
//  Depth-first search for the node at `pos`
//  NOTES: A position is a heap OFFSET, and the core can turn one back into a
//         field directly -- but not into a path, which is what a node here is.
//         P2PmsgMgr::P2Pos2Path exists and answers in the CORE's spelling,
//         built by walking parent links whose delimiters ( ':' for a root,
//         '.' for a descendant, '@' for an attribute ) do not round-trip
//         through this ABI's grammar and are not reversible into scopes
//       : So the route is searched for instead.  It costs a walk of the store,
//         which is why this is the ONE entry point that takes a position --
//         everything downstream of it holds the path it found
//
BOOL
FacadeStore::SearchPos ( P3PmsgField& rParent, unsigned long long pos
                       , FacadeRoute& rRoute, unsigned int uDepth ) const
{
    if ( uDepth >= MAX_DEPTH )
      return FALSE;

    static const unsigned int aScopes[2] = { MSGF_SCOPE_CHILD, MSGF_SCOPE_ATTR };
    for ( int s = 0; s < 2; ++s )
    {
      // The names first, then the recursion: the cursor must not be alive
      // while a child of it is being walked, for the reason ResolveObject
      // gives -- and a mutation cannot happen here, but a nested cursor over
      // the same field can still confuse the attribute pointer.
      std::vector<CString> aNames;
      {
        ScopeCursor oCurs ( rParent, aScopes[s] );
        oCurs->Seek ( );
        while ( !CursAtEnd ( *oCurs ) )
        {
          aNames.push_back ( CString ( oCurs->c_wstr ( ) ) );
          int nItem = oCurs->Item ( );
          if ( !oCurs->Goto ( nItem + 1 ) )
            break;
        }
      }

      for ( size_t i = 0; i < aNames.size ( ); ++i )
      {
        FacadeStep oStep;
        oStep.uScope  = aScopes[s];
        oStep.strName = aNames[i];

        P3PmsgObject oChild;
        {
          ScopeCursor oCurs ( rParent, aScopes[s] );
          if ( !oCurs->Goto ( (LPCWSTR)aNames[i] ) )
            continue;
          oChild = oCurs->r_Object ( );
        }

        P3PmsgField oField ( oChild );
        if ( (unsigned long long)oField.GetP2Pos ( ) == pos )
        {
          rRoute.push_back ( oStep );
          return TRUE;
        }
        if ( oChild.IsField ( ) )
        {
          rRoute.push_back ( oStep );
          if ( SearchPos ( oField, pos, rRoute, uDepth + 1 ) )
            return TRUE;
          rRoute.pop_back ( );
        }
      }
    }
    return FALSE;
}

HRESULT
FacadeStore::RouteOfPos ( unsigned long long pos, FacadeRoute& rOut ) const
{
    rOut.clear ( );
    if ( m_bClosed || !m_pMgr )
      return MSGF_E_CLOSED;
    if ( pos == 0 )
      return MSGF_E_NO_POS;

    try
    {
      P3PmsgField oRoot ( m_pMgr->r_Object ( ) );
      if ( (unsigned long long)oRoot.GetP2Pos ( ) == pos )
        return S_OK;                        // the root: an empty route
      if ( SearchPos ( oRoot, pos, rOut, 0 ) )
        return S_OK;
      rOut.clear ( );
      return MSGF_E_NO_POS;
    }
    catch ( ... ) { rOut.clear ( ); return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  msgf::IMsgStore -- persistence
// ---------------------------------------------------------------------------

HRESULT
FacadeStore::Save ( const wchar_t *filename, unsigned int flags )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    // Save-back comes from OUR record of the filename rather than the manager's.
    // They are normally the same; they differ after a Clear, which replaces the
    // manager and so loses the manager's copy.  Passing ours keeps "empty it,
    // then save it" working -- which is the only reading of Clear that makes
    // sense for a store that still reports the file it came from.
    const wchar_t *lpszTarget = ( filename && *filename ) ? filename
                              : ( !m_strFile.IsEmpty ( ) ? (LPCWSTR)m_strFile
                                                         : filename );
    try
    {
      BOOL bOk = m_pMgr -> Save ( lpszTarget
                                , ( flags & MSGF_SAVE_DEFRAGMENT ) ? true : false );
      if ( !bOk )
        return MSGF_E_FILE;
      if ( lpszTarget && *lpszTarget )
        m_strFile = lpszTarget;            // survives Clear; see the member
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_FILE; }
}

//
//  Replace this store's contents from a file
//  NOTES: This is the mutation every other design would have had to forbid.
//         A Load throws the whole heap away and builds a new one, so every
//         raw handle into the old one is dangling -- which is exactly why the
//         core's flat ABI lists a load among the calls after which no handle
//         survives.  A route survives it, because a route was never a handle
//
HRESULT
FacadeStore::Load ( const wchar_t *filename )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !filename || !*filename )
      return E_INVALIDARG;

    try
    {
      if ( !m_pMgr->Load ( filename ) )
        return MSGF_E_FILE;
      m_strFile = filename;                // survives Clear; see the member
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_FILE; }
}

HRESULT
FacadeStore::Clear ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    // NOT P2PmsgMgr::Nullify, which is what this obviously wants to be.  That
    // one does
    //
    //      if ( m_hMgr ) P2PmsgHeap_Close ( m_hMgr );  m_hMgr = 0;  ...
    //
    // -- it closes the heap and drops the handle, and the P2PmsgMgr OBJECT
    // survives pointing at nothing.  IsValid() still answers TRUE, and the very
    // next call through it -- GetRootname, Sizeof, any resolve -- dereferences
    // the closed heap.  Emptying a store must not arm that, so this REPLACES the
    // manager with an equivalent empty one, at the width and bounds this store
    // was created with.
    //
    // Every outstanding node stays valid, because a node is a route: it now
    // resolves against the new empty tree and answers MSGF_E_NO_ITEM, which is
    // exactly what "the store was emptied" should look like from a handle.
    P2PmsgMgr *pNew = nullptr;
    try
    {
      if ( m_uAddr == 0 && m_uInitial == 0 && m_uMax == 0 )
        pNew = new P2PmsgMgr ( );
      else
        pNew = new P2PmsgMgr ( m_uAddr ? m_uAddr : (unsigned char)VBLock_Addrxx
                             , m_uInitial ? m_uInitial : 2024
                             , m_uMax );
    }
    catch ( ... )
    {
      delete pNew;
      return MSGF_E_CORE;
    }
    if ( !pNew )
      return E_OUTOFMEMORY;

    // The old one goes only once the new one exists, so a failure leaves the
    // store exactly as it was rather than empty and closed.  The sinks are
    // re-hooked onto the new manager, because a client that installed one did
    // so on the STORE and never saw a manager at all.
    try { m_pMgr->SetTriggerSink ( nullptr, nullptr ); } catch ( ... ) { }
    try
    {
      m_pMgr->PageRegistration ( (PINT_PTR)0, (P2PageinCBFnc)0, (P2PageoutCBFnc)0 );
    }
    catch ( ... ) { }
    delete m_pMgr;
    m_pMgr = pNew;
    m_nPagingPushed = 0;                 // the saved registration went with it

    try
    {
      if ( m_pEvents )
        m_pMgr->SetTriggerSink ( &FacadeStore::TriggerTramp, this );
      if ( m_pPaging )
        m_pMgr->PageRegistration ( (PINT_PTR)this
                                 , &FacadeStore::PageInTramp
                                 , &FacadeStore::PageOutTramp );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return S_OK;
}

HRESULT
FacadeStore::Rename ( const wchar_t *newName )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !IsUsableName ( newName ) )
      return MSGF_E_NAME;

    // NOT P2PmsgMgr::Rename.  That one is named for the FILE: it MoveFileEx's
    // m_strFilename to the string it is given, and returns FALSE for a store
    // that has never been saved.  Calling it here would have answered
    // MSGF_E_CORE for an in-memory store and silently MOVED THE FILE of a
    // saved one -- measured by _Msgcore_UseExamplesLight\BstrWidthTest, which
    // is the first thing in this repo ever to call it.
    //
    // GetRootname is r_name().c_name(), so its inverse is the one below.
    try
    {
      m_pMgr->r_name ( ).c_name ( newName, 0 );
      MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::GetFilename ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    // From OUR record, not the manager's. Clear replaces the manager, and a
    // store that has been emptied is still the same document -- the core has no
    // filename setter to put the name back with, so this is where it lives.
    try { return CopyOut ( (LPCWSTR)m_strFile, buf, cch ); }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::GetRootname ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try { return CopyOut ( m_pMgr->GetRootname ( ), buf, cch ); }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::IsDirty ( int *outDirty ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !outDirty ) return E_POINTER;
    try { *outDirty = m_pMgr->IsDirty ( ) ? 1 : 0; return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::SetDirty ( int dirty )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try { m_pMgr->SetDirty ( dirty ? TRUE : FALSE ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::GetSize ( unsigned int *outSize ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !outSize ) return E_POINTER;
    try { *outSize = (unsigned int)m_pMgr->Sizeof ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::IsValid ( int *outValid ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !outValid ) return E_POINTER;
    try { *outValid = m_pMgr->IsValid ( ) ? 1 : 0; return S_OK; }
    catch ( ... ) { *outValid = 0; return S_OK; }
}

// ---------------------------------------------------------------------------
//  msgf::IMsgStore -- getting into the tree
// ---------------------------------------------------------------------------

HRESULT
FacadeStore::GetRoot ( IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    FacadeRoute oEmpty;
    return FacadeNode::Make ( this, oEmpty, outNode );
}

HRESULT
FacadeStore::NodeFromPath ( const wchar_t *path, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outNode ) return E_POINTER;
    *outNode = nullptr;

    FacadeRoute oRoute;
    hr = ParseRoute ( path, oRoute );
    if ( FAILED ( hr ) ) return hr;

    // A path that parses but names nothing is MSGF_E_NO_ITEM, not a node that
    // fails later: the caller asked to look something up, and this is the
    // answer to that question.
    P3PmsgObject oObject;
    hr = ResolveObject ( oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    return FacadeNode::Make ( this, oRoute, outNode );
}

HRESULT
FacadeStore::NodeFromPos ( unsigned long long pos, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outNode ) return E_POINTER;
    *outNode = nullptr;

    FacadeRoute oRoute;
    hr = RouteOfPos ( pos, oRoute );
    if ( FAILED ( hr ) ) return hr;

    return FacadeNode::Make ( this, oRoute, outNode );
}

// ---------------------------------------------------------------------------
//  msgf::IMsgStore -- change notification
// ---------------------------------------------------------------------------

//
//  The core's trigger callback
//  NOTES: Called on the thread that performed the mutation, from inside the
//         heap's own trigger sweep -- so the facade's lock is ALREADY held by
//         that thread, and the client's sink is called with it held.  That is
//         stated in the public header rather than worked around, because the
//         only alternatives are to queue (which loses the synchronous "this
//         node just changed" the facility exists for) or to drop the lock in
//         the middle of a mutation (which is worse than either)
//
void
FacadeStore::TriggerTramp ( void *pUser, UINT nType, unsigned long long pos )
{
    FacadeStore *pStore = (FacadeStore*)pUser;
    if ( !pStore || pStore->m_bClosed )
      return;
    IMsgStoreEvents *pEvents = pStore->m_pEvents;
    if ( !pEvents )
      return;
    // A sink that raises must not unwind through the core's heap sweep.
    try { pEvents->OnTrigger ( (unsigned int)nType, pos ); } catch ( ... ) { }
}

HRESULT
FacadeStore::SetEvents ( IMsgStoreEvents *events )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      m_pEvents = events;
      if ( events )
        m_pMgr->SetTriggerSink ( &FacadeStore::TriggerTramp, this );
      else
        m_pMgr->SetTriggerSink ( nullptr, nullptr );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::Arm ( unsigned int mask, unsigned long long pos )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( !( mask & MSGF_TRIG_ALL ) || pos == 0 )
      return E_INVALIDARG;

    try
    {
      m_pMgr->CreateTrigger ( mask, (HWND)nullptr, (P2Pos)pos );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::Disarm ( unsigned int mask, unsigned long long pos )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      m_pMgr->DropTriggers ( mask, (HWND)nullptr, (P2Pos)pos );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::Fire ( unsigned int mask, unsigned long long pos
                  , unsigned int *outFired )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      // Note the core's argument order here: FireTrigger takes the POSITION
      // first and the mask second, the opposite way round from CreateTrigger
      // and DropTriggers next to it.
      UINT uFired = m_pMgr->FireTrigger ( (P2Pos)pos, mask );
      if ( outFired )
        *outFired = (unsigned int)uFired;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  Demand paging (ABI 2)
//
//  NOTES: The core carries an opaque PINT_PTR "key" from PageRegistration
//         through to the callback, and uses it for nothing but an is-anything-
//         registered test.  So the key is this store, and one pair of static
//         functions serves every store in the process -- no per-registration
//         allocation, and no map to look the store up in
//       : Both trampolines run with our lock ALREADY HELD, on the thread that
//         touched the store.  A sink that raises must not unwind through the
//         core's heap walk, so both swallow
// ---------------------------------------------------------------------------

BOOL CALLBACK
FacadeStore::PageInTramp ( PINT_PTR nKey, P2Pos posItem )
{
    FacadeStore *pStore = (FacadeStore*)nKey;
    if ( !pStore || pStore->m_bClosed || !pStore->m_pPaging )
      return FALSE;
    try { return pStore->m_pPaging->OnPageIn ( (unsigned long long)posItem ) ? TRUE : FALSE; }
    catch ( ... ) { return FALSE; }
}

BOOL CALLBACK
FacadeStore::PageOutTramp ( PINT_PTR nKey, P2Pos posItem, BOOL bFlush )
{
    FacadeStore *pStore = (FacadeStore*)nKey;
    if ( !pStore || pStore->m_bClosed || !pStore->m_pPaging )
      return FALSE;
    try
    {
      return pStore->m_pPaging->OnPageOut ( (unsigned long long)posItem
                                          , bFlush ? 1 : 0 ) ? TRUE : FALSE;
    }
    catch ( ... ) { return FALSE; }
}

HRESULT
FacadeStore::SetPaging ( IMsgPagingEvents *events )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      m_pPaging = events;
      if ( events )
        m_pMgr->PageRegistration ( (PINT_PTR)this
                                 , &FacadeStore::PageInTramp
                                 , &FacadeStore::PageOutTramp );
      else
        m_pMgr->PageRegistration ( (PINT_PTR)0, (P2PageinCBFnc)0, (P2PageoutCBFnc)0 );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Drive a page-in explicitly
//  NOTES: S_FALSE, not an error, when nothing was paged -- which is what the
//         core answers with no registration installed.  "There was nothing to
//         do" is an answer here, exactly as it is for Exists
//
HRESULT
FacadeStore::PageIn ( unsigned long long pos )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( pos == 0 )
      return E_INVALIDARG;

    try { return m_pMgr->PageDatasetIn ( (P2Pos)pos ) ? S_OK : S_FALSE; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::PageOut ( unsigned long long pos, int flush )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( pos == 0 )
      return E_INVALIDARG;

    try
    {
      return m_pMgr->PageDatasetOut ( (P2Pos)pos, flush ? TRUE : FALSE ) ? S_OK : S_FALSE;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Save the whole paging registration
//  NOTES: The core's own push does NOT nest: a second one without an
//         intervening pop asserts in a debug build and loses the first saved
//         set in a release one.  Counting it here turns that into
//         MSGF_E_STATE, which is a return rather than an abort
//
HRESULT
FacadeStore::PushPaging ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( m_nPagingPushed != 0 )
      return MSGF_E_STATE;

    // And refused with nothing installed, which is the other half of the same
    // protection: the core's Pop asserts unless all three saved slots are
    // non-null, so a push made with no registration to save is an abort at the
    // matching pop.  There is nothing to suppress in that state anyway.
    if ( !m_pPaging )
      return MSGF_E_STATE;

    // AND THE CLEAR IS OURS, because the core's Push does not do it.
    //
    //      P2PmsgMgr::PageRegistrationPush ( )         // P2PmsgMgr.cpp:694
    //      {
    //        m_pfncP2PageinCBp  = m_pfncP2PageinCB;    // saved
    //        ...                                       // and left INSTALLED
    //      }
    //
    // It copies the live callbacks into the saved slots and leaves the live ones
    // exactly where they were, so a Push/Pop bracket around a section of work
    // saves and restores the same values and suspends nothing.  Measured, not
    // read: _Msgcore_UseExamplesLight\BstrWidthTest pushed, paged in, and the
    // sink still ran.
    //
    // That is defensible for a caller who means "save what is installed, put my
    // own sink in, and give me the old one back" -- but it is not what SUSPEND
    // means, and suspend is what the one documented use needs: a Save walks the
    // whole store and must not fault all of it in on the way past.  So this
    // clears the registration after saving it, and PopPaging's restore then has
    // something to restore.  Nothing else in the kernel calls Push, so there is
    // no caller to surprise.
    try
    {
      if ( !m_pMgr->PageRegistrationPush ( ) )
        return MSGF_E_CORE;
      m_pMgr->PageRegistration ( (PINT_PTR)0, (P2PageinCBFnc)0, (P2PageoutCBFnc)0 );
      ++m_nPagingPushed;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeStore::PopPaging ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( this );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( m_nPagingPushed == 0 )
      return MSGF_E_STATE;

    try
    {
      const BOOL bOk = m_pMgr->PageRegistrationPop ( );
      --m_nPagingPushed;
      return bOk ? S_OK : MSGF_E_CORE;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}
