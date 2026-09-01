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
// ComStore.cpp -- implementation of coclass MsgStore.
#include "stdafx.h"
#include "ComStore.h"
#include "ComField.h"

using namespace msgf;

// Bound on the pending-event queue. The mutating thread must never block on a
// slow client handler, so a backlog is dropped rather than accumulated; the
// drop count is reported as an OnError once the queue drains, because a client
// that is silently not seeing everything is worse than one that is told.
static const size_t kMaxQueued = 4096;

// How long FinalRelease waits for the dispatch thread. Only ever approached
// when a client handler is still running as the last reference goes away --
// which it cannot be, since a running handler implies a reference.
static const DWORD kJoinMs = 10000;

static inline HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR bsArg1 = NULL, BSTR bsArg2 = NULL )
{
    return ComFail ( IID_IMsgStoreCom, hr, wszCall, bsArg1, bsArg2 );
}

// ---------------------------------------------------------------------------
// The process-wide Global Interface Table, created on first use.
// ---------------------------------------------------------------------------
static IGlobalInterfaceTable *g_pGit = NULL;

IGlobalInterfaceTable* MsgcoreGit ( )
{
    if ( g_pGit == NULL )
    {
        IGlobalInterfaceTable *pGit = NULL;
        HRESULT hr = ::CoCreateInstance ( CLSID_StdGlobalInterfaceTable, NULL
                                        , CLSCTX_INPROC_SERVER
                                        , IID_IGlobalInterfaceTable
                                        , (void**)&pGit );
        if ( SUCCEEDED(hr) )
        {
            if ( ::InterlockedCompareExchangePointer ( (PVOID*)&g_pGit, pGit, NULL ) != NULL )
                pGit->Release();                    // lost the race
        }
    }
    return g_pGit;
}

// A store's own string properties come back through the caller-sized buffer
// protocol, like a node's. One helper rather than the same six lines per
// property.
static HRESULT ReadStoreString ( IMsgStore *pStore
                               , HRESULT (IMsgStore::*pfn)(wchar_t*,unsigned int*) const
                               , BSTR *pVal )
{
    *pVal = NULL;

    unsigned int cch = 0;
    HRESULT hr = ( pStore->*pfn ) ( NULL, &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );

    if ( cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = ( pStore->*pfn ) ( &buf[0], &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
}

// ---------------------------------------------------------------------------
// construction / teardown
// ---------------------------------------------------------------------------
CMsgStore::CMsgStore ( )
    : m_pLib ( NULL ), m_pStore ( NULL )
    , m_uAddrMode ( 0 ), m_nInitial ( 0 ), m_nMax ( 0 )
    , m_lMutations ( 0 )
    , m_pPagingSink ( NULL ), m_dwPagingApartment ( 0 )
    , m_hThread ( NULL ), m_hWake ( NULL ), m_uThreadId ( 0 ), m_lQuit ( 0 )
    , m_cDropped ( 0 ), m_dwNextCookie ( 1 )
{
    m_events.pOwner = this;
    m_paging.pOwner = this;
}

HRESULT CMsgStore::FinalConstruct ( )
{
    m_hWake = ::CreateEvent ( NULL, FALSE, FALSE, NULL );
    if ( m_hWake == NULL )
        return HRESULT_FROM_WIN32 ( ::GetLastError() );

    // Started BEFORE the store, so no trigger can fire into a queue whose
    // reader does not exist yet.
    //
    // The thread holds NO reference to this object, unlike TargetCom's hub
    // dispatch thread -- and it must not, because a store is what the CLIENT
    // creates and releases. A self-reference would mean a store that is never
    // destroyed unless Close() is called explicitly. FinalRelease joining the
    // thread is safe instead: a handler that is running holds a reference by
    // definition, so FinalRelease cannot be racing one.
    m_hThread = (HANDLE)::_beginthreadex ( NULL, 0, DispatchThunk, this, 0, &m_uThreadId );
    if ( m_hThread == NULL )
    {
        ::CloseHandle ( m_hWake );
        m_hWake = NULL;
        return HRESULT_FROM_WIN32 ( ERROR_NOT_ENOUGH_MEMORY );
    }

    // The library is a refcounted singleton in the DLL below, so every store
    // taking one is a refcount rather than an allocation. The ABI it is asked
    // for is the constant from the header this server was COMPILED against --
    // never a literal, which is the entire reason the argument exists.
    HRESULT hr = ::MSGF_CreateLibrary ( ABI_VERSION, &m_pLib );
    if ( FAILED(hr) || m_pLib == NULL )
    {
        ::InterlockedExchange ( &m_lQuit, 1 );
        ::SetEvent ( m_hWake );
        ::WaitForSingleObject ( m_hThread, kJoinMs );
        ::CloseHandle ( m_hThread ); m_hThread = NULL;
        ::CloseHandle ( m_hWake );   m_hWake   = NULL;
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }

    // CoCreateInstance gives an EMPTY store with default addressing; CreateNew
    // replaces it when a client wants a specific width.
    IMsgStore *pNew = NULL;
    hr = m_pLib->CreateStore ( 0, 0, 0, &pNew );
    if ( FAILED(hr) || pNew == NULL )
    {
        m_pLib->Release(); m_pLib = NULL;
        ::InterlockedExchange ( &m_lQuit, 1 );
        ::SetEvent ( m_hWake );
        ::WaitForSingleObject ( m_hThread, kJoinMs );
        ::CloseHandle ( m_hThread ); m_hThread = NULL;
        ::CloseHandle ( m_hWake );   m_hWake   = NULL;
        return E_OUTOFMEMORY;
    }

    return AttachStore ( pNew );
}

HRESULT CMsgStore::AttachStore ( IMsgStore *pNew )
{
    m_pStore = pNew;
    m_pStore->SetEvents ( &m_events );
    BumpMutation();
    return S_OK;
}

void CMsgStore::ReleaseStore ( )
{
    IMsgStore *pStore = m_pStore;
    if ( pStore == NULL ) return;

    m_pStore = NULL;                    // nothing may resolve through it again

    // Cleared FIRST: a trigger or a page fault arriving out of the release
    // would reach a half-dead object. The facade clears both itself on
    // Release, but doing it here as well makes the order explicit rather than
    // depending on the DLL below to have got it right.
    pStore->SetEvents ( NULL );
    pStore->SetPaging ( NULL );
    pStore->Release   ( );

    if ( m_pPagingSink != NULL ) { m_pPagingSink->Release(); m_pPagingSink = NULL; }
    m_dwPagingApartment = 0;

    BumpMutation();
}

void CMsgStore::FinalRelease ( )
{
    {
        CStoreLock lock ( this );
        ReleaseStore();
        if ( m_pLib != NULL ) { m_pLib->Release(); m_pLib = NULL; }
    }

    if ( m_hThread != NULL )
    {
        ::InterlockedExchange ( &m_lQuit, 1 );
        ::SetEvent ( m_hWake );
        ::WaitForSingleObject ( m_hThread, kJoinMs );
        ::CloseHandle ( m_hThread );
        m_hThread = NULL;
    }
    if ( m_hWake != NULL ) { ::CloseHandle ( m_hWake ); m_hWake = NULL; }

    IGlobalInterfaceTable *pGit = MsgcoreGit();
    m_csSinks.Lock();
    if ( pGit != NULL )
        for ( std::map<DWORD,DWORD>::iterator it = m_sinks.begin(); it != m_sinks.end(); ++it )
            pGit->RevokeInterfaceFromGlobal ( it->second );
    m_sinks.clear();
    m_csSinks.Unlock();
}

// ---------------------------------------------------------------------------
// The one field factory.
// ---------------------------------------------------------------------------
HRESULT CMsgStore::MakeField ( CMsgStore *pStore, IMsgNode *pNode
                             , IMsgFieldCom **ppOut )
{
    CNodePtr spNode ( pNode );          // owned from here, on every path

    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pNode == NULL ) return E_POINTER;

    ATL::CComObject<CMsgField> *pObj = NULL;
    HRESULT hr = ATL::CComObject<CMsgField>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->Init ( pStore, spNode.Detach() );     // takes the node
    hr = pObj->QueryInterface ( IID_IMsgFieldCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

// ===========================================================================
// IMsgStoreCom
// ===========================================================================
STDMETHODIMP CMsgStore::Open ( BSTR path )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );

    if ( m_pStore == NULL )               return Fail ( MSGC_E_CLOSED, L"Open", path );
    if ( path == NULL || *path == L'\0' ) return Fail ( MSGC_E_LOAD, L"Open", path );

    // Load INTO the existing store rather than opening a new one: the change
    // sink, the armed nodes and this object's identity all hang off it, and
    // replacing it would silently discard them. Every node the client is
    // holding stays valid and re-resolves against the loaded tree, which is
    // what a path-based node buys and is checked in the facade's own tests.
    HRESULT hr = m_pStore->Load ( path );
    if ( FAILED(hr) )
        return Fail ( ( hr == MSGF_E_FILE ) ? MSGC_E_LOAD : FromFacade ( hr ), L"Open", path );

    BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Open")
}

STDMETHODIMP CMsgStore::Save ( BSTR path )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );

    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Save", path );

    // A NULL filename means "the name it was opened or last saved under", and
    // the facade answers MSGF_E_FILE for a store that has neither -- so the
    // read-Filename-first dance this method used to do is one argument now.
    const bool bNamed = ( path != NULL && *path != L'\0' );
    HRESULT hr = m_pStore->Save ( bNamed ? path : NULL, 0 );
    if ( FAILED(hr) )
        return Fail ( MSGC_E_SAVE, L"Save", path );

    return S_OK;
    MSGF_GUARD_END(L"Save")
}

STDMETHODIMP CMsgStore::Close ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    ReleaseStore();                     // idempotent
    return S_OK;
    MSGF_GUARD_END(L"Close")
}

// ---------------------------------------------------------------------------
// Empty the store, keeping it usable.
//
// This used to be Nullify, and used to be implemented by destroying the manager
// and building another, because the core's own Nullify closes the heap and
// leaves the object pointing at nothing -- IsValid still TRUE, the next call an
// access violation. IMsgStore::Clear does that rebuild now, one layer down and
// once for every client of the facade, so this is the call it looks like.
//
// The observable difference from the old method: Filename SURVIVES, because the
// store is the same store. The old one cleared it, and said so as a wart.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::Clear ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Clear" );

    HRESULT hr = m_pStore->Clear ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Clear" );

    BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Clear")
}

// ---------------------------------------------------------------------------
// Rename the ROOT NODE -- which this tier could not do at all until now.
//
// The old method here was RenameFile, over msgcore_mgr_rename, and it was named
// that way because P2PmsgMgr::Rename is a MoveFileEx on m_strFilename: it
// renames the FILE. The facade found that (its own IMsgStore::Rename was
// calling it, and answering an error for every in-memory store) and now sets
// the root item's name, which is the inverse of RootName.
//
// So the file rename is gone from this tier and the root rename has arrived.
// A client that wants the file moved has its host's own file API for that; a
// client that wants the root named had nothing before.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::RenameRoot ( BSTR newName )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"RenameRoot", newName );

    HRESULT hr = CheckName ( newName );
    if ( FAILED(hr) ) return Fail ( hr, L"RenameRoot", newName );

    hr = m_pStore->Rename ( newName );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"RenameRoot", newName );

    BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"RenameRoot")
}

STDMETHODIMP CMsgStore::get_Filename ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Filename" );
    return ReadStoreString ( m_pStore, &IMsgStore::GetFilename, pVal );
    MSGF_GUARD_END(L"Filename")
}

STDMETHODIMP CMsgStore::get_RootName ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"RootName" );
    return ReadStoreString ( m_pStore, &IMsgStore::GetRootname, pVal );
    MSGF_GUARD_END(L"RootName")
}

STDMETHODIMP CMsgStore::get_Dirty ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Dirty" );

    int bDirty = 0;
    HRESULT hr = m_pStore->IsDirty ( &bDirty );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Dirty" );
    *pVal = bDirty ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"Dirty")
}

STDMETHODIMP CMsgStore::put_Dirty ( VARIANT_BOOL newVal )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Dirty" );
    return FromFacade ( m_pStore->SetDirty ( ( newVal != VARIANT_FALSE ) ? 1 : 0 ) );
    MSGF_GUARD_END(L"Dirty")
}

// The store's Size is the heap's current ALLOCATION, not bytes in use: it
// starts at the initial request and only moves when the tree outgrows it.
// Reported as it is rather than reinterpreted, because a store's footprint is
// the honest answer to "how big is this".
STDMETHODIMP CMsgStore::get_Size ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Size" );

    unsigned int uSize = 0;
    HRESULT hr = m_pStore->GetSize ( &uSize );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Size" );
    *pVal = (LONG)uSize;
    return S_OK;
    MSGF_GUARD_END(L"Size")
}

// The one property that must NOT fail after Close: it is the question "is this
// store usable", and answering it with msgcClosed would be a riddle.
STDMETHODIMP CMsgStore::get_IsValid ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return S_OK;

    int bValid = 0;
    if ( SUCCEEDED ( m_pStore->IsValid ( &bValid ) ) )
        *pVal = bValid ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsValid")
}

STDMETHODIMP CMsgStore::get_Root ( IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"Root" );

    IMsgNode *pNode = NULL;
    HRESULT hr = m_pStore->GetRoot ( &pNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Root" );

    return MakeField ( this, pNode, ppField );
    MSGF_GUARD_END(L"Root")
}

// ---------------------------------------------------------------------------
// A STRING is a path; a NUMBER is a P2Pos. Both now yield a LIVE, writable
// node.
//
// The asymmetry this method used to carry -- a path gave a live node and a
// position gave a detached copy whose writes vanished -- was the flat ABI's:
// msgcore_mgr_p2pos2field deep-copies. IMsgStore::NodeFromPos searches the tree
// for the position once and hands back a route, so both spellings arrive at the
// same kind of thing and IsLive is True for both.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::FieldAt ( VARIANT pathOrPos, IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"FieldAt" );

    const VARIANT *pv = UnwrapVariant ( pathOrPos );
    if ( pv == NULL ) return E_POINTER;

    IMsgNode *pNode = NULL;

    if ( pv->vt == VT_BSTR )
    {
        HRESULT hr = m_pStore->NodeFromPath ( Str ( pv->bstrVal ), &pNode );
        if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"FieldAt", pv->bstrVal );
        return MakeField ( this, pNode, ppField );
    }

    ATL::CComVariant v;
    if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) )
        return Fail ( DISP_E_TYPEMISMATCH, L"FieldAt" );

    HRESULT hr = m_pStore->NodeFromPos ( (unsigned long long)v.llVal, &pNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"FieldAt" );

    return MakeField ( this, pNode, ppField );
    MSGF_GUARD_END(L"FieldAt")
}

// ---------------------------------------------------------------------------
// The path of a position -- IN THE SAME SPELLING FieldAt TAKES.
//
// It was not, before. msgcore_mgr_p2pos2path answers the core's own path
// grammar: a leading '.' and the root's NAME as the first segment, built by
// walking parent links. Feeding that back to FieldAt found nothing, and the
// help string had to say so. Both ends are the facade's grammar now, so a path
// handed out here can be handed straight back.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::PathOf ( LONGLONG p2pos, BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"PathOf" );

    CNodePtr spNode;
    HRESULT hr = m_pStore->NodeFromPos ( (unsigned long long)p2pos, &spNode );
    if ( FAILED(hr) ) return Fail ( MSGC_E_NO_POS, L"PathOf" );

    ATL::CComBSTR bs;
    hr = ReadString ( spNode, &IMsgNode::GetPath, bs );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"PathOf" );

    *pVal = bs.Detach();
    return S_OK;
    MSGF_GUARD_END(L"PathOf")
}

STDMETHODIMP CMsgStore::CreateNew ( LONG addrMode, LONG initialBytes, LONG maxBytes )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pLib == NULL ) return Fail ( MSGC_E_CLOSED, L"CreateNew" );

    if ( addrMode != MSGF_ADDR_16 && addrMode != MSGF_ADDR_32 && addrMode != MSGF_ADDR_64 )
        return Fail ( E_INVALIDARG, L"CreateNew" );
    if ( initialBytes < 0 || maxBytes < 0 )
        return Fail ( E_INVALIDARG, L"CreateNew" );

    // Zero means "the default" here and one layer down, and the facade also
    // floors a small non-zero request -- an initial size under 512 bytes does
    // not fail in the core, it does not RETURN. That measurement is why a
    // number the client is free to invent is safe to pass through.
    IMsgStore *pNew = NULL;
    HRESULT hr = m_pLib->CreateStore ( (unsigned char)addrMode
                                     , (unsigned int)initialBytes
                                     , (unsigned int)maxBytes, &pNew );
    if ( FAILED(hr) || pNew == NULL ) return Fail ( FromFacade ( hr ), L"CreateNew" );

    // The old store only goes once the new one exists, so a failure leaves this
    // object exactly as it was rather than empty and closed.
    ReleaseStore();

    m_uAddrMode = (unsigned char)addrMode;
    m_nInitial  = (unsigned int)initialBytes;
    m_nMax      = (unsigned int)maxBytes;

    return AttachStore ( pNew );
    MSGF_GUARD_END(L"CreateNew")
}

// ---------------------------------------------------------------------------
// change notification
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::ArmTrigger ( LONG mask, LONGLONG p2pos )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"ArmTrigger" );
    return FromFacade ( m_pStore->Arm ( (unsigned int)mask, (unsigned long long)p2pos ) );
    MSGF_GUARD_END(L"ArmTrigger")
}

STDMETHODIMP CMsgStore::DisarmTrigger ( LONG mask, LONGLONG p2pos )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"DisarmTrigger" );
    return FromFacade ( m_pStore->Disarm ( (unsigned int)mask, (unsigned long long)p2pos ) );
    MSGF_GUARD_END(L"DisarmTrigger")
}

STDMETHODIMP CMsgStore::FireTrigger ( LONG mask, LONGLONG p2pos, LONG *pFired )
{
    if ( pFired == NULL ) return E_POINTER;
    *pFired = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"FireTrigger" );

    unsigned int uFired = 0;
    HRESULT hr = m_pStore->Fire ( (unsigned int)mask, (unsigned long long)p2pos, &uFired );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"FireTrigger" );
    *pFired = (LONG)uFired;
    return S_OK;
    MSGF_GUARD_END(L"FireTrigger")
}

// The facade's change sink. Runs on the MUTATING thread, holding that store's
// lock and this one's: it may only copy and enqueue.
void CMsgStore::EventsAdapter::OnTrigger ( unsigned int type, unsigned long long pos )
{
    if ( pOwner != NULL )
        pOwner->OnTrigger ( type, pos );
}

void CMsgStore::OnTrigger ( unsigned int nTriggerType, unsigned long long p2pos )
{
    StoreEvent ev;
    ev.kind         = StoreEvent::evChange;
    ev.nTriggerKind = (LONG)nTriggerType;
    ev.p2pos        = (LONGLONG)p2pos;

    // NOTHING IS RESOLVED HERE, and that is a correction rather than a
    // simplification.
    //
    // This runs on the mutating thread, INSIDE the core's own mutation, with
    // the store's lock held -- and the facade's header says in as many words
    // not to call back into the store from a sink. The previous version of
    // this server did anyway, because against the flat ABI the path lookup was
    // a cheap direct call (msgcore_mgr_p2pos2path) and it got away with it.
    //
    // IMsgStore::NodeFromPos is a depth-first SEARCH of the tree. Running one
    // from here, while an allocation is part-way through relocating the heap,
    // reads blocks that are being moved: it access-violates, reliably, as soon
    // as the store is full enough for the allocation to grow the heap. Measured
    // 2026-08-15, through PushValue, which allocates.
    //
    // So the position is queued bare and the PATH is resolved on the dispatch
    // thread, after the mutation has finished. What that costs is accuracy for
    // a node that is deleted or moved between the two moments: the path comes
    // back empty. For a DELETE it was always empty -- the trigger fires after
    // the block is freed -- and the event's own p2pos is what identifies which
    // node went.
    Push ( ev );
}

// ---------------------------------------------------------------------------
// utilities
// ---------------------------------------------------------------------------

// One vocabulary, both directions, and it is the facade's -- so a type name
// written out by TypeName is one TypeFromName accepts. It is also WIDE at
// every layer now: the flat ABI's was ASCII, and this method used to widen it
// a character at a time.
STDMETHODIMP CMsgStore::TypeName ( LONG dataType, BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pLib == NULL ) return Fail ( MSGC_E_CLOSED, L"TypeName" );
    if ( dataType < 0 || dataType > 255 ) return Fail ( E_INVALIDARG, L"TypeName" );

    unsigned int cch = 0;
    HRESULT hr = m_pLib->TypeName ( (unsigned char)dataType, NULL, &cch );
    if ( FAILED(hr) || cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"UNKNOWN" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = m_pLib->TypeName ( (unsigned char)dataType, &buf[0], &cch );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"TypeName" );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"TypeName")
}

STDMETHODIMP CMsgStore::TypeFromName ( BSTR typeName, LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = -1;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pLib == NULL ) return Fail ( MSGC_E_CLOSED, L"TypeFromName" );

    unsigned char u = 0;
    // An unrecognised name is MSGF_E_TYPE below and -1 here, because a script
    // comparing against 255 would be comparing against a plausible type code
    // rather than against a sentinel.
    if ( FAILED ( m_pLib->TypeFromName ( Str ( typeName ), &u ) ) )
        return S_OK;
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"TypeFromName")
}

STDMETHODIMP CMsgStore::WildcardMatch ( BSTR pattern, BSTR name, VARIANT_BOOL *pMatched )
{
    if ( pMatched == NULL ) return E_POINTER;
    *pMatched = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pLib == NULL ) return Fail ( MSGC_E_CLOSED, L"WildcardMatch" );

    *pMatched = ( m_pLib->WildcardMatch ( Str ( pattern ), Str ( name ) ) == S_OK )
              ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"WildcardMatch")
}

// New at this tier: ask whether a name is usable BEFORE declaring it, rather
// than declaring and reading the error. The rule is the store's own -- 1 to 63
// UTF-16 units, and none of . @ : ^ / \ * ? | < > " -- and a client that builds
// names from user input now has somewhere to check them.
STDMETHODIMP CMsgStore::IsValidName ( BSTR name, VARIANT_BOOL *pValid )
{
    if ( pValid == NULL ) return E_POINTER;
    *pValid = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pLib == NULL ) return Fail ( MSGC_E_CLOSED, L"IsValidName" );

    *pValid = ( m_pLib->IsValidName ( Str ( name ) ) == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsValidName")
}

// This COM SERVER's build tag AND the facade's, which carries the kernel's --
// so one string answers "what am I talking to" all the way down.
STDMETHODIMP CMsgStore::get_VersionString ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );

    ATL::CComBSTR bs (
#if defined(_WIN64)
        L"MsgcoreCom 2.0 (x64) built " _CRT_WIDE(__DATE__) L" " _CRT_WIDE(__TIME__)
#else
        L"MsgcoreCom 2.0 (x86) built " _CRT_WIDE(__DATE__) L" " _CRT_WIDE(__TIME__)
#endif
    );

    if ( m_pLib != NULL )
    {
        const wchar_t *wsz = m_pLib->VersionString ( );
        if ( wsz != NULL ) { bs += L" over "; bs += wsz; }
    }

    *pVal = bs.Detach();
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"VersionString")
}

// ---------------------------------------------------------------------------
// Paging.
//
// Read the contract table in the .idl before changing anything here. In one
// line: these run on the accessing thread, under the store lock, with the store
// blocked waiting for an answer -- which is why the sink is called directly
// rather than queued and replayed like an OnChange event, and why it has to
// live in the registering apartment.
// ---------------------------------------------------------------------------

// A P2Pos is 64-bit and VARIANT's portable integer is 32-bit, so it crosses as
// a VARIANT and is widened here. VT_I8 travels through automation, but a script
// that produced the value from Field.Position may hand back a VT_R8 or a
// VT_BSTR, so anything convertible is accepted.
static HRESULT VariantToP2Pos ( const VARIANT& v, unsigned long long *pOut )
{
    *pOut = 0;
    ATL::CComVariant conv;
    if ( SUCCEEDED ( conv.ChangeType ( VT_UI8, &v ) ) ) { *pOut = conv.ullVal; return S_OK; }
    if ( SUCCEEDED ( conv.ChangeType ( VT_I8,  &v ) ) )
    {
        if ( conv.llVal < 0 ) return MSGC_E_RANGE;
        *pOut = (unsigned long long)conv.llVal;
        return S_OK;
    }
    return MSGC_E_TYPE;
}

static ATL::CComVariant P2PosToVariant ( unsigned long long pos )
{
    ATL::CComVariant v;
    v.vt     = VT_UI8;
    v.ullVal = pos;
    return v;
}

int CMsgStore::PagingAdapter::OnPageIn ( unsigned long long pos )
{
    CMsgStore *pThis = pOwner;
    if ( pThis == NULL || pThis->m_pPagingSink == NULL ) return 0;

    VARIANT_BOOL bHandled = VARIANT_FALSE;
    // No lock: the thread that got here already holds it. A sink that throws a
    // structured exception must not unwind into the store mid-page-fault.
    try
    {
        ATL::CComVariant v = P2PosToVariant ( pos );
        if ( FAILED ( pThis->m_pPagingSink->OnPageIn ( v, &bHandled ) ) ) return 0;
    }
    catch ( ... ) { return 0; }
    return ( bHandled != VARIANT_FALSE ) ? 1 : 0;
}

int CMsgStore::PagingAdapter::OnPageOut ( unsigned long long pos, int flush )
{
    CMsgStore *pThis = pOwner;
    if ( pThis == NULL || pThis->m_pPagingSink == NULL ) return 0;

    VARIANT_BOOL bHandled = VARIANT_FALSE;
    try
    {
        ATL::CComVariant v = P2PosToVariant ( pos );
        if ( FAILED ( pThis->m_pPagingSink->OnPageOut ( v,
                          flush ? VARIANT_TRUE : VARIANT_FALSE, &bHandled ) ) ) return 0;
    }
    catch ( ... ) { return 0; }
    return ( bHandled != VARIANT_FALSE ) ? 1 : 0;
}

STDMETHODIMP CMsgStore::SetPagingSink ( IMsgPagingSink *sink )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"SetPagingSink" );

    if ( sink == NULL )
    {
        m_pStore->SetPaging ( NULL );
        if ( m_pPagingSink != NULL ) { m_pPagingSink->Release(); m_pPagingSink = NULL; }
        m_dwPagingApartment = 0;
        return S_OK;
    }

    // Same-apartment only. A cross-apartment sink would have to be marshalled,
    // and a marshalled call made while the store is blocked under its own lock
    // is a deadlock waiting for a specific interleaving to find it. Refusing it
    // outright is the only honest option, because there is no version of this
    // that both defers and answers in time.
    if ( m_pPagingSink != NULL ) { m_pPagingSink->Release(); m_pPagingSink = NULL; }
    m_pPagingSink       = sink;
    m_pPagingSink->AddRef();
    m_dwPagingApartment = ::GetCurrentThreadId();

    HRESULT hr = m_pStore->SetPaging ( &m_paging );
    if ( FAILED(hr) )
    {
        m_pPagingSink->Release(); m_pPagingSink = NULL;
        m_dwPagingApartment = 0;
        return Fail ( FromFacade ( hr ), L"SetPagingSink" );
    }
    return S_OK;
    MSGF_GUARD_END(L"SetPagingSink")
}

STDMETHODIMP CMsgStore::PageIn ( VARIANT p2pos, VARIANT_BOOL *pOk )
{
    if ( pOk == NULL ) return E_POINTER;
    *pOk = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"PageIn" );

    unsigned long long pos = 0;
    HRESULT hr = VariantToP2Pos ( p2pos, &pos );
    if ( FAILED(hr) ) return Fail ( hr, L"PageIn" );

    // S_FALSE is "nothing was paged" -- no sink, or a sink that declined -- and
    // is not a failure. It is the answer this method reports as False.
    hr = m_pStore->PageIn ( pos );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"PageIn" );
    *pOk = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"PageIn")
}

STDMETHODIMP CMsgStore::PageOut ( VARIANT p2pos, VARIANT_BOOL flush, VARIANT_BOOL *pOk )
{
    if ( pOk == NULL ) return E_POINTER;
    *pOk = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"PageOut" );

    unsigned long long pos = 0;
    HRESULT hr = VariantToP2Pos ( p2pos, &pos );
    if ( FAILED(hr) ) return Fail ( hr, L"PageOut" );

    hr = m_pStore->PageOut ( pos, ( flush == VARIANT_FALSE ) ? 0 : 1 );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"PageOut" );
    *pOk = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"PageOut")
}

STDMETHODIMP CMsgStore::PushPaging ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"PushPaging" );

    // The refusals -- a second push, and a push with no sink to save -- are the
    // facade's now, and both arrive as MSGF_E_STATE. They used to be counted
    // here because the flat ABI passed a nesting push straight to a core that
    // asserts on it.
    HRESULT hr = m_pStore->PushPaging ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"PushPaging" );
    return S_OK;
    MSGF_GUARD_END(L"PushPaging")
}

STDMETHODIMP CMsgStore::PopPaging ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( this );
    if ( m_pStore == NULL ) return Fail ( MSGC_E_CLOSED, L"PopPaging" );

    HRESULT hr = m_pStore->PopPaging ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"PopPaging" );
    return S_OK;
    MSGF_GUARD_END(L"PopPaging")
}

// ---------------------------------------------------------------------------
// queue
// ---------------------------------------------------------------------------
void CMsgStore::Push ( const StoreEvent& ev )
{
    bool bWake = false;

    m_csQueue.Lock();
    if ( m_queue.size() >= kMaxQueued )
        ++m_cDropped;                   // the sink cannot keep up; say so later
    else
    {
        m_queue.push_back ( ev );
        bWake = true;
    }
    m_csQueue.Unlock();

    if ( bWake && m_hWake != NULL )
        ::SetEvent ( m_hWake );
}

bool CMsgStore::Pop ( StoreEvent& ev )
{
    bool bGot = false;

    m_csQueue.Lock();
    if ( !m_queue.empty() )
    {
        ev = m_queue.front();
        m_queue.pop_front();
        bGot = true;
    }
    else if ( m_cDropped != 0 )
    {
        WCHAR wsz[128];
        ::swprintf_s ( wsz, L"%ld change event(s) dropped: the event sink could not keep up", m_cDropped );
        m_cDropped = 0;
        ev      = StoreEvent();
        ev.kind = StoreEvent::evError;
        ev.what = wsz;
        bGot    = true;
    }
    m_csQueue.Unlock();

    return bGot;
}

// ---------------------------------------------------------------------------
// dispatch thread
// ---------------------------------------------------------------------------
unsigned __stdcall CMsgStore::DispatchThunk ( void *pThis )
{
    ((CMsgStore*)pThis)->DispatchLoop();
    return 0;
}

void CMsgStore::DispatchLoop ( )
{
    ::CoInitializeEx ( NULL, COINIT_MULTITHREADED );

    for ( ;; )
    {
        ::WaitForSingleObject ( m_hWake, INFINITE );

        StoreEvent ev;
        while ( Pop ( ev ) )
            Fire ( ev );                // may block for as long as the client likes

        if ( ::InterlockedCompareExchange ( &m_lQuit, 0, 0 ) != 0 )
            break;
    }

    ::CoUninitialize();
}

void CMsgStore::Fire ( const StoreEvent& ev )
{
    std::vector<DWORD> git;

    // The path, resolved HERE -- on the dispatch thread, after the mutation
    // that fired this has finished, and under the store lock, which is released
    // again before any sink is invoked. A node that has gone in the meantime
    // simply has no path, which is the same answer a DELETE has always given.
    ATL::CComBSTR bsPath ( ev.path );
    if ( ev.kind == StoreEvent::evChange && ev.p2pos != 0 && bsPath.Length() == 0 )
    {
        CStoreLock lock ( this );
        if ( m_pStore != NULL )
        {
            CNodePtr spNode;
            if ( SUCCEEDED ( m_pStore->NodeFromPos ( (unsigned long long)ev.p2pos, &spNode ) ) )
                ReadString ( spNode, &IMsgNode::GetPath, bsPath );
        }
    }

    m_csSinks.Lock();
    for ( std::map<DWORD,DWORD>::iterator it = m_sinks.begin(); it != m_sinks.end(); ++it )
        git.push_back ( it->second );
    m_csSinks.Unlock();

    if ( git.empty() ) return;

    IGlobalInterfaceTable *pGit = MsgcoreGit();
    if ( pGit == NULL ) return;

    // DISPPARAMS carries arguments in REVERSE declaration order.
    ATL::CComVariant args[3];
    DISPID           dispid = 0;
    UINT             cArgs  = 0;

    switch ( ev.kind )
    {
      case StoreEvent::evChange:
        dispid  = 1;
        args[0] = ATL::CComVariant ( bsPath );           // path
        args[1].vt = VT_I8; args[1].llVal = ev.p2pos;    // p2pos
        args[2] = ATL::CComVariant ( ev.nTriggerKind );  // kind
        cArgs   = 3;
        break;

      case StoreEvent::evError:
        dispid  = 2;
        args[0] = ATL::CComVariant ( ev.what );
        cArgs   = 1;
        break;

      default:
        return;
    }

    DISPPARAMS dp;
    ::ZeroMemory ( &dp, sizeof(dp) );
    dp.rgvarg = args;
    dp.cArgs  = cArgs;

    for ( size_t i = 0; i < git.size(); ++i )
    {
        ATL::CComPtr<IDispatch> spSink;
        // Re-fetched per fire, which is what produces a proxy valid in THIS
        // apartment; a cached raw pointer would be an illegal cross-apartment
        // call for every STA client.
        if ( FAILED ( pGit->GetInterfaceFromGlobal ( git[i], IID_IDispatch, (void**)&spSink ) ) )
            continue;
        if ( spSink == NULL )
            continue;

        // NOTE the store lock is NOT held here, deliberately: that is what lets
        // a handler call back into the store without deadlocking the writer.
        spSink->Invoke ( dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD
                       , &dp, NULL, NULL, NULL );   // a sink's failure is its own business
    }
}

// ---------------------------------------------------------------------------
// IConnectionPointContainer / IConnectionPoint
//
// There is exactly one connection point, so this object IS its own connection
// point. Sinks live in the GIT rather than as raw pointers -- see the header.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgStore::EnumConnectionPoints ( IEnumConnectionPoints **ppEnum )
{
    if ( ppEnum == NULL ) return E_POINTER;
    *ppEnum = NULL;
    return E_NOTIMPL;      // FindConnectionPoint is what every real client uses
}

STDMETHODIMP CMsgStore::FindConnectionPoint ( REFIID riid, IConnectionPoint **ppCP )
{
    if ( ppCP == NULL ) return E_POINTER;
    *ppCP = NULL;
    if ( !::InlineIsEqualGUID ( riid, DIID__IMsgStoreEvents ) )
        return CONNECT_E_NOCONNECTION;
    return GetUnknown()->QueryInterface ( IID_IConnectionPoint, (void**)ppCP );
}

STDMETHODIMP CMsgStore::GetConnectionInterface ( IID *pIID )
{
    if ( pIID == NULL ) return E_POINTER;
    *pIID = DIID__IMsgStoreEvents;
    return S_OK;
}

STDMETHODIMP CMsgStore::GetConnectionPointContainer ( IConnectionPointContainer **ppCPC )
{
    if ( ppCPC == NULL ) return E_POINTER;
    return GetUnknown()->QueryInterface ( IID_IConnectionPointContainer, (void**)ppCPC );
}

STDMETHODIMP CMsgStore::Advise ( IUnknown *pUnkSink, DWORD *pdwCookie )
{
    if ( pdwCookie == NULL ) return E_POINTER;
    *pdwCookie = 0;
    if ( pUnkSink == NULL ) return E_POINTER;

    // The DIID is tried AFTER IDispatch and the order matters: a .NET sink
    // declared [ClassInterface(None)] answers E_NOINTERFACE for IID_IDispatch
    // on its CCW, and a connection point that asked for IDispatch alone would
    // refuse every managed client.
    ATL::CComPtr<IDispatch> spSink;
    if ( FAILED ( pUnkSink->QueryInterface ( IID_IDispatch,        (void**)&spSink ) ) &&
         FAILED ( pUnkSink->QueryInterface ( DIID__IMsgStoreEvents, (void**)&spSink ) ) )
        return CONNECT_E_CANNOTCONNECT;

    IGlobalInterfaceTable *pGit = MsgcoreGit();
    if ( pGit == NULL ) return E_UNEXPECTED;

    DWORD dwGit = 0;
    HRESULT hr = pGit->RegisterInterfaceInGlobal ( spSink, IID_IDispatch, &dwGit );
    if ( FAILED(hr) ) return hr;

    m_csSinks.Lock();
    DWORD dwCookie = m_dwNextCookie++;
    m_sinks[dwCookie] = dwGit;
    m_csSinks.Unlock();

    *pdwCookie = dwCookie;
    return S_OK;
}

STDMETHODIMP CMsgStore::Unadvise ( DWORD dwCookie )
{
    DWORD dwGit = 0;

    m_csSinks.Lock();
    std::map<DWORD,DWORD>::iterator it = m_sinks.find ( dwCookie );
    bool bFound = ( it != m_sinks.end() );
    if ( bFound ) { dwGit = it->second; m_sinks.erase ( it ); }
    m_csSinks.Unlock();

    if ( !bFound ) return CONNECT_E_NOCONNECTION;

    IGlobalInterfaceTable *pGit = MsgcoreGit();
    if ( pGit != NULL ) pGit->RevokeInterfaceFromGlobal ( dwGit );
    return S_OK;
}

STDMETHODIMP CMsgStore::EnumConnections ( IEnumConnections **ppEnum )
{
    if ( ppEnum == NULL ) return E_POINTER;
    *ppEnum = NULL;
    return E_NOTIMPL;      // sinks are GIT cookies, not raw pointers to hand out
}
