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
// FacadeStore.h -- FacadeStore, the object behind msgf::IMsgStore.
//
// It owns three things: the P2PmsgMgr, the critical section that makes the
// manager safe to touch from more than one thread, and the ROUTE RESOLVER that
// every other object in this DLL is built on.
//
// Lifetime is a refcount, not an owner pointer.  A node, list, vect, cursor or
// walker rooted in this store holds a reference, so the client releasing the
// store does not free the manager out from under them -- it CLOSES it, and
// they all start answering MSGF_E_CLOSED.  The manager goes away when the last
// of them does.
#pragma once

#include "FacadeInternal.h"

class FacadeLibrary;

class FacadeStore : public msgf::IMsgStore
{
    // Construction -- through the factories only
    public:
        static HRESULT
          Create ( FacadeLibrary *pLibrary, unsigned char uAddr
                 , unsigned int uInitial, unsigned int uMax
                 , msgf::IMsgStore **outStore );
        static HRESULT
          Open   ( FacadeLibrary *pLibrary, const wchar_t *lpszFilename
                 , msgf::IMsgStore **outStore );

    // msgf::IMsgStore
    public:
      virtual HRESULT Save        ( const wchar_t *filename, unsigned int flags );
      virtual HRESULT Load        ( const wchar_t *filename );
      virtual HRESULT Clear       ( );
      virtual HRESULT Rename      ( const wchar_t *newName );
      virtual HRESULT GetFilename ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetRootname ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT IsDirty     ( int *outDirty ) const;
      virtual HRESULT SetDirty    ( int dirty );
      virtual HRESULT GetSize     ( unsigned int *outSize ) const;
      virtual HRESULT IsValid     ( int *outValid ) const;
      virtual HRESULT GetRoot     ( msgf::IMsgNode **outNode );
      virtual HRESULT NodeFromPath( const wchar_t *path, msgf::IMsgNode **outNode );
      virtual HRESULT NodeFromPos ( unsigned long long pos, msgf::IMsgNode **outNode );
      virtual HRESULT SetEvents   ( msgf::IMsgStoreEvents *events );
      virtual HRESULT Arm         ( unsigned int mask, unsigned long long pos );
      virtual HRESULT Disarm      ( unsigned int mask, unsigned long long pos );
      virtual HRESULT Fire        ( unsigned int mask, unsigned long long pos
                                  , unsigned int *outFired );
      virtual ULONG   Release     ( );

      // ABI 2 -- demand paging, appended after Release so every slot above
      // keeps the index an ABI 1 client compiled against.
      virtual HRESULT SetPaging   ( msgf::IMsgPagingEvents *events );
      virtual HRESULT PageIn      ( unsigned long long pos );
      virtual HRESULT PageOut     ( unsigned long long pos, int flush );
      virtual HRESULT PushPaging  ( );
      virtual HRESULT PopPaging   ( );

    // What every other object in this DLL needs from a store
    public:
        // Refcount.  Every node/list/vect/cursor/walker rooted here holds one.
        void   AddRef     ( );
        ULONG  ReleaseRef ( );

        CCriticalSection& Section ( ) const { return m_oCSection; }
        BOOL              IsClosed( ) const { return m_bClosed;   }
        P2PmsgMgr*        Mgr     ( ) const { return m_pMgr;      }

        // Flag the store modified.  Called after every successful mutation the
        // core does not flag itself -- the in-place value writes go through
        // VBLockData directly and set no dirty bit of their own.
        void   MarkDirty  ( );

        //
        //  Resolve a route to the live object it names
        //  NOTES: THE central operation of this DLL.  See the resolver notes in
        //         FacadeInternal.h for why it is a cursor walk with the object
        //         lifted out before each assignment
        //       : The caller MUST hold Section()
        //
        HRESULT ResolveObject ( const FacadeRoute& rRoute, P3PmsgObject& rOut ) const;
        //  The same, refusing anything that is not an item.  What every node
        //  method opens with.
        HRESULT ResolveField  ( const FacadeRoute& rRoute, P3PmsgField& rOut ) const;

        // The route of the node at `pos`, found by search from the root.
        HRESULT RouteOfPos    ( unsigned long long pos, FacadeRoute& rOut ) const;

        // The facade's path grammar, in both directions.  Static: neither
        // needs a store, and the smoke test exercises them against GetPath.
        static HRESULT ParseRoute  ( const wchar_t *lpszPath, FacadeRoute& rOut );
        static void    FormatRoute ( const FacadeRoute& rRoute, CString& rOut );

    // Internals
    private:
        FacadeStore ( FacadeLibrary *pLibrary, P2PmsgMgr *pMgr );
       ~FacadeStore ( );

        // The one callback the core's trigger facility calls, on the thread
        // that performed the mutation, with our lock already held.
        static void
          TriggerTramp ( void *pUser, UINT nType, unsigned long long pos );

        // The two the core's paging facility calls, on the thread performing
        // the access, with our lock already held -- and it WAITS for the
        // answer.  The core carries a PINT_PTR "key" through for the
        // application; this store passes itself as that key, which is what
        // makes one pair of static functions serve every store in the process.
        static BOOL CALLBACK
          PageInTramp  ( PINT_PTR nKey, P2Pos posItem );
        static BOOL CALLBACK
          PageOutTramp ( PINT_PTR nKey, P2Pos posItem, BOOL bFlush );

        // Depth-first search for `pos`, appending steps to rRoute.
        BOOL
          SearchPos ( P3PmsgField& rParent, unsigned long long pos
                    , FacadeRoute& rRoute, unsigned int uDepth ) const;

    // Attributes
    private:
        FacadeLibrary          *m_pLibrary{nullptr};
        P2PmsgMgr              *m_pMgr{nullptr};
        // What this store was created with, so Clear can build an equivalent
        // empty one rather than a default one.  All three zero for a store
        // opened from a file, whose width the core does not publish.
        unsigned char           m_uAddr{0};
        unsigned int            m_uInitial{0};
        unsigned int            m_uMax{0};
        // The file this store is OF, kept here rather than read back from the
        // manager -- because Clear replaces the manager, and a store that is
        // emptied is still the same document.  P2PmsgMgr has no filename setter
        // to put it back with, so this is the only place it can survive.
        // Written by Open and Save; GetFilename answers it.
        CString                 m_strFile;
        LONG                    m_cRef{1};
        BOOL                    m_bClosed{FALSE};
        msgf::IMsgStoreEvents  *m_pEvents{nullptr};
        msgf::IMsgPagingEvents *m_pPaging{nullptr};
        // The core's own push/pop of a paging registration does not nest, so
        // this counts it and refuses the second push rather than passing one on.
        int                     m_nPagingPushed{0};
        mutable CCriticalSection m_oCSection;
};

// ---------------------------------------------------------------------------
//  StoreGuard -- the two lines every entry point in this DLL opens with
//
//  NOTES: AFX_MANAGE_STATE cannot live in here -- it declares a local object
//         whose scope IS the protection, so it stays at the top of each method
//       : Everything else does: the closed test and the lock, in one line, so
//         a method that forgets one cannot compile into something that looks
//         right.  `Failed()` is the whole guard
// ---------------------------------------------------------------------------
class StoreGuard
{
    public:
        StoreGuard ( const FacadeStore *pStore )
          : m_pStore ( pStore )
        {
          if ( m_pStore )
            m_pStore -> Section ( ).Lock ( );
        }
       ~StoreGuard ( )
        {
          if ( m_pStore )
            m_pStore -> Section ( ).Unlock ( );
        }

      // MSGF_E_CLOSED when there is nothing to work on, S_OK otherwise.
      HRESULT
        Failed ( ) const
        {
          if ( !m_pStore || m_pStore->IsClosed() || !m_pStore->Mgr() )
            return msgf::MSGF_E_CLOSED;
          return S_OK;
        }

    private:
        StoreGuard ( const StoreGuard& );
        StoreGuard& operator = ( const StoreGuard& );
        const FacadeStore *m_pStore;
};
