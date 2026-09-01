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
// ComStore.h -- CMsgStore, the coclass MsgStore and the one creatable object.
//
// It owns four things, and every other object in this server borrows all four
// from it:
//
//  1. THE STORE.  One msgf::IMsgStore, created at FinalConstruct and released
//     at Close() or FinalRelease, whichever comes first.  Every field,
//     collection and cursor holds a counted reference to THIS object, so the
//     store cannot be released underneath one however careless the client is
//     about release order -- and after Close() they all answer msgcClosed.
//
//  2. THE LOCK.  MsgFacade takes its own lock in every method, so a single CALL
//     is already safe from any thread.  This one is still here because a
//     SEQUENCE is not: Exists-then-Child, or the read-modify-write inside
//     Increment, must not interleave with another thread's Declare.  Two
//     different stores never contend.
//
//  3. THE MUTATION COUNTER.  Bumped by every write.  A cursor is the one object
//     here that cannot be re-resolved per call -- a position is what it holds --
//     so it compares this counter and rebuilds itself when it is stale.
//
//  4. THE EVENT PATH.  The facade's change sink runs on the mutating thread,
//     with the store's lock held; firing a client's handler from there would
//     deadlock the first one that touched the store.  So the sink copies into a
//     queue and a dedicated MTA dispatch thread replays it, with sinks held as
//     GIT cookies so COM marshals into the sink's own apartment.  A handler may
//     block, and may call back in.
//
// ---------------------------------------------------------------------------
// WHAT THE MOVE ONTO MsgFacade TOOK OUT OF THIS FILE
// ---------------------------------------------------------------------------
// NamePath and ResolveLive.  A node used to be kept here as a chain of names
// and walked from the root on entry to every method, because a flat kernel
// handle does not survive the next heap relocation.  msgf::IMsgNode is that,
// one layer down, so a node reference is now an interface pointer that the
// server simply holds -- and the ~90 lines that walked the chain, verified each
// hop against the hollow handle msgcore_field_child answers for a miss, and
// told a live handle from a detached copy are all gone.
#pragma once

#include "MsgcoreCom_h.h"
#include "ComUtil.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// One queued trigger, owning its own strings.
//
// The PATH travels with the copy because it is resolvable only while the
// mutation is still in progress: by the time the dispatch thread replays this,
// the node may be gone -- and for msgcTriggerDelete it always is.
// ---------------------------------------------------------------------------
struct StoreEvent
{
    enum Kind { evChange, evError };

    Kind          kind;
    LONG          nTriggerKind;         // one MSGCORE_TRIGGER_* bit (evChange)
    LONGLONG      p2pos;                // the node (evChange)
    ATL::CComBSTR path;                 // its path at mutation time (evChange)
    ATL::CComBSTR what;                 // the sentence (evError)

    StoreEvent ( ) : kind ( evError ), nTriggerKind ( 0 ), p2pos ( 0 ) { }
};

class ATL_NO_VTABLE CMsgStore
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgStore, &CLSID_MsgStore>
    , public ATL::IDispatchImpl<IMsgStoreCom, &IID_IMsgStoreCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgStoreCom>
    , public ATL::IProvideClassInfo2Impl<&CLSID_MsgStore, &DIID__IMsgStoreEvents, &LIBID_MsgcoreComLib>
    , public IConnectionPointContainer
    , public IConnectionPoint
{
  public:
    CMsgStore ( );

    DECLARE_REGISTRY_RESOURCEID(IDR_MSGSTORE)
    DECLARE_NOT_AGGREGATABLE(CMsgStore)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgStore)
        COM_INTERFACE_ENTRY(IMsgStoreCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IConnectionPointContainer)
        COM_INTERFACE_ENTRY(IConnectionPoint)
        COM_INTERFACE_ENTRY(IProvideClassInfo)
        COM_INTERFACE_ENTRY(IProvideClassInfo2)
    END_COM_MAP()

    HRESULT FinalConstruct ( );
    void    FinalRelease   ( );

    // --- IMsgStoreCom ------------------------------------------------------
    STDMETHOD(Open)                ( BSTR path );
    STDMETHOD(Save)                ( BSTR path );
    STDMETHOD(Close)               ( );
    STDMETHOD(Clear)               ( );
    STDMETHOD(RenameRoot)          ( BSTR newName );
    STDMETHOD(get_Filename)        ( BSTR *pVal );
    STDMETHOD(get_RootName)        ( BSTR *pVal );
    STDMETHOD(get_Dirty)           ( VARIANT_BOOL *pVal );
    STDMETHOD(put_Dirty)           ( VARIANT_BOOL newVal );
    STDMETHOD(get_Size)            ( LONG *pVal );
    STDMETHOD(get_IsValid)         ( VARIANT_BOOL *pVal );
    STDMETHOD(get_Root)            ( IMsgFieldCom **ppField );
    STDMETHOD(FieldAt)             ( VARIANT pathOrPos, IMsgFieldCom **ppField );
    STDMETHOD(PathOf)              ( LONGLONG p2pos, BSTR *pVal );
    STDMETHOD(CreateNew)           ( LONG addrMode, LONG initialBytes, LONG maxBytes );
    STDMETHOD(ArmTrigger)          ( LONG mask, LONGLONG p2pos );
    STDMETHOD(DisarmTrigger)       ( LONG mask, LONGLONG p2pos );
    STDMETHOD(FireTrigger)         ( LONG mask, LONGLONG p2pos, LONG *pFired );
    STDMETHOD(TypeName)            ( LONG dataType, BSTR *pVal );
    STDMETHOD(TypeFromName)        ( BSTR typeName, LONG *pVal );
    STDMETHOD(WildcardMatch)       ( BSTR pattern, BSTR name, VARIANT_BOOL *pMatched );
    STDMETHOD(IsValidName)         ( BSTR name, VARIANT_BOOL *pValid );
    STDMETHOD(get_VersionString)   ( BSTR *pVal );

    STDMETHOD(SetPagingSink)       ( IMsgPagingSink *sink );
    STDMETHOD(PageIn)              ( VARIANT p2pos, VARIANT_BOOL *pOk );
    STDMETHOD(PageOut)             ( VARIANT p2pos, VARIANT_BOOL flush, VARIANT_BOOL *pOk );
    STDMETHOD(PushPaging)          ( );
    STDMETHOD(PopPaging)           ( );

    // --- IConnectionPointContainer -----------------------------------------
    STDMETHOD(EnumConnectionPoints) ( IEnumConnectionPoints **ppEnum );
    STDMETHOD(FindConnectionPoint)  ( REFIID riid, IConnectionPoint **ppCP );

    // --- IConnectionPoint ---------------------------------------------------
    STDMETHOD(GetConnectionInterface)      ( IID *pIID );
    STDMETHOD(GetConnectionPointContainer) ( IConnectionPointContainer **ppCPC );
    STDMETHOD(Advise)                      ( IUnknown *pUnkSink, DWORD *pdwCookie );
    STDMETHOD(Unadvise)                    ( DWORD dwCookie );
    STDMETHOD(EnumConnections)             ( IEnumConnections **ppEnum );

    // =======================================================================
    // What the other objects of this store use. All of it assumes the caller
    // already holds the lock (CStoreLock), except Lock/Unlock themselves.
    // =======================================================================
    msgf::IMsgStore*   Store   ( ) const { return m_pStore; }
    msgf::IMsgLibrary* Library ( ) const { return m_pLib;   }

    void Lock   ( ) { m_cs.Lock(); }
    void Unlock ( ) { m_cs.Unlock(); }

    // A cursor's staleness test. Bumped by every write anywhere in this store.
    LONG MutationSeq ( ) const { return m_lMutations; }
    void BumpMutation ( )      { ::InterlockedIncrement ( &m_lMutations ); }

    // The one field factory. There used to be two -- live and detached -- and
    // the distinction is gone with the flat ABI that forced it: every
    // msgf::IMsgNode is a route into the live store.
    //
    // TAKES OWNERSHIP of pNode, including on failure, so a caller can write
    //     return CMsgStore::MakeField ( this, pNode.Detach(), ppOut );
    // without a leak on any path.
    static HRESULT MakeField ( CMsgStore *pStore, msgf::IMsgNode *pNode
                             , IMsgFieldCom **ppOut );

  private:
    // The two facade sinks, as members rather than as base classes: CMsgStore
    // is already six ATL bases deep, and a sink that is an adapter object
    // cannot be confused with a COM interface by anything reading the COM map.
    struct EventsAdapter : public msgf::IMsgStoreEvents
    {
        CMsgStore *pOwner;
        EventsAdapter ( ) : pOwner ( NULL ) { }
        virtual void OnTrigger ( unsigned int type, unsigned long long pos );
    };
    struct PagingAdapter : public msgf::IMsgPagingEvents
    {
        CMsgStore *pOwner;
        PagingAdapter ( ) : pOwner ( NULL ) { }
        virtual int OnPageIn  ( unsigned long long pos );
        virtual int OnPageOut ( unsigned long long pos, int flush );
    };

    void  OnTrigger ( unsigned int nTriggerType, unsigned long long p2pos );

    static unsigned __stdcall DispatchThunk ( void *pThis );
    void  DispatchLoop ( );
    void  Push ( const StoreEvent& ev );
    bool  Pop  ( StoreEvent& ev );
    void  Fire ( const StoreEvent& ev );

    void    ReleaseStore ( );           // idempotent; clears the sinks first
    HRESULT AttachStore  ( msgf::IMsgStore *pNew );

    msgf::IMsgLibrary           *m_pLib;
    msgf::IMsgStore             *m_pStore;

    // What CreateNew was last given, so a client can ask for a width. Zero
    // means "whatever CreateStore makes by default", which is the state a
    // CoCreateInstance leaves this in.
    unsigned char                m_uAddrMode;
    unsigned int                 m_nInitial;
    unsigned int                 m_nMax;

    ATL::CComAutoCriticalSection m_cs;          // THE store lock
    volatile LONG                m_lMutations;

    EventsAdapter                m_events;
    PagingAdapter                m_paging;

    // The paging sink, held as a RAW pointer with a reference, not through the
    // GIT the event sinks use. That is deliberate and it is why SetPagingSink
    // insists the sink live in this apartment: a GIT round trip marshals, and
    // marshalling a call that the store is synchronously blocked on -- while
    // holding its lock -- is how you deadlock. Same-apartment means the
    // adapter is a direct call.
    IMsgPagingSink              *m_pPagingSink;
    DWORD                        m_dwPagingApartment;   // thread that registered

    // dispatch thread
    HANDLE                       m_hThread;
    HANDLE                       m_hWake;
    unsigned                     m_uThreadId;
    volatile LONG                m_lQuit;

    // queue (mutating thread writes, dispatch thread reads)
    ATL::CComAutoCriticalSection m_csQueue;
    std::deque<StoreEvent>       m_queue;
    LONG                         m_cDropped;

    // sinks, held as GIT cookies: our cookie -> GIT cookie
    ATL::CComAutoCriticalSection m_csSinks;
    std::map<DWORD, DWORD>       m_sinks;
    DWORD                        m_dwNextCookie;
};

OBJECT_ENTRY_AUTO(__uuidof(MsgStore), CMsgStore)

// ---------------------------------------------------------------------------
// RAII for the store lock. Every entry point of every object opens with one.
// ---------------------------------------------------------------------------
class CStoreLock
{
  public:
    explicit CStoreLock ( CMsgStore *p ) : m_p ( p ) { if ( m_p ) m_p->Lock(); }
   ~CStoreLock ( )                                   { if ( m_p ) m_p->Unlock(); }
  private:
    CStoreLock ( const CStoreLock& );
    CStoreLock& operator= ( const CStoreLock& );
    CMsgStore *m_p;
};

// ---------------------------------------------------------------------------
// The process-wide Global Interface Table, created on first use. Shared with
// nothing else: this server has exactly one connection point, on the store.
// ---------------------------------------------------------------------------
IGlobalInterfaceTable* MsgcoreGit ( );
