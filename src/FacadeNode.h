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
// FacadeNode.h -- FacadeNode, the object behind msgf::IMsgNode.
//
// It holds a store reference and a ROUTE: the ordered (scope, name) steps from
// the store root to the node it names.  It holds no Msgcore object at all
// between calls, which is the entire point -- see "A node is a path, not a
// pointer" at the top of MsgFacade.h.
//
// Every method therefore opens the same way: manage the module state, take the
// store's lock, resolve the route to a live P3PmsgField, and work through that
// field for the duration of the call only.
#pragma once

#include "FacadeInternal.h"

class FacadeStore;

class FacadeNode : public msgf::IMsgNode
{
    public:
        // Mint a node for `rRoute`.  Does NOT verify that the route resolves:
        // a node is allowed to name something that is not there yet or is no
        // longer there, and says so when a method is called on it.  The one
        // thing checked here is depth, because a route past MAX_DEPTH is one
        // the resolver would refuse forever.
        static HRESULT
          Make ( FacadeStore *pStore, const FacadeRoute& rRoute
               , msgf::IMsgNode **outNode );

    // msgf::IMsgNode
    public:
      virtual HRESULT GetName    ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetPath    ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetPos     ( unsigned long long *outPos ) const;
      virtual HRESULT GetKind    ( unsigned int *outKind ) const;
      virtual HRESULT GetType    ( unsigned char *outType ) const;
      virtual HRESULT IsNull     ( int *outNull ) const;

      virtual HRESULT GetInt     ( long long *outValue, int *outUnsigned ) const;
      virtual HRESULT SetInt     ( long long value );
      virtual HRESULT GetReal    ( double *outValue ) const;
      virtual HRESULT SetReal    ( double value );
      virtual HRESULT GetText    ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT SetText    ( const wchar_t *value );
      virtual HRESULT GetBlob    ( void *buf, unsigned int *size ) const;
      virtual HRESULT GetGuid    ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetTime    ( long long *outTime ) const;
      virtual HRESULT SetTime    ( long long value, long long *outTime );

      virtual HRESULT GetCount   ( unsigned int scope, unsigned int *outCount ) const;
      virtual HRESULT Exists     ( unsigned int scope, const wchar_t *name ) const;
      virtual HRESULT GetChild   ( unsigned int scope, const wchar_t *name
                                 , msgf::IMsgNode **outNode );
      virtual HRESULT GetChildAt ( unsigned int scope, unsigned int index
                                 , msgf::IMsgNode **outNode );

      virtual HRESULT DeclareInt ( unsigned int scope, const wchar_t *name
                                 , long long value, unsigned char type
                                 , unsigned int flags, msgf::IMsgNode **outNode );
      virtual HRESULT DeclareReal( unsigned int scope, const wchar_t *name
                                 , double value, unsigned char type
                                 , unsigned int flags, msgf::IMsgNode **outNode );
      virtual HRESULT DeclareText( unsigned int scope, const wchar_t *name
                                 , const wchar_t *value
                                 , unsigned int flags, msgf::IMsgNode **outNode );
      virtual HRESULT DeclareBlob( unsigned int scope, const wchar_t *name
                                 , const void *value, unsigned int size
                                 , unsigned int flags, msgf::IMsgNode **outNode );
      virtual HRESULT DeclareGuid( unsigned int scope, const wchar_t *name
                                 , const wchar_t *guid
                                 , unsigned int flags, msgf::IMsgNode **outNode );
      virtual HRESULT DeclareList( unsigned int scope, const wchar_t *name
                                 , msgf::IMsgList **outList );
      virtual HRESULT DeclareVect( unsigned int scope, const wchar_t *name
                                 , unsigned int elems, unsigned char type
                                 , msgf::IMsgVect **outVect );
      virtual HRESULT GetList    ( unsigned int scope, const wchar_t *name
                                 , msgf::IMsgList **outList );
      virtual HRESULT GetVect    ( unsigned int scope, const wchar_t *name
                                 , msgf::IMsgVect **outVect );

      virtual HRESULT Delete     ( unsigned int scope, const wchar_t *name );
      virtual HRESULT Truncate   ( unsigned int scope );
      virtual HRESULT Rename     ( unsigned int scope, const wchar_t *name
                                 , const wchar_t *newName );
      virtual HRESULT Move       ( unsigned int scope, const wchar_t *name
                                 , msgf::IMsgNode *destin );
      virtual HRESULT Retype     ( unsigned int scope, const wchar_t *name
                                 , unsigned char type );

      virtual HRESULT OpenCursor ( unsigned int scope, msgf::IMsgCursor **outCursor );
      virtual HRESULT OpenWalker ( unsigned int scope, msgf::IMsgWalker **outWalker );

      virtual ULONG   Release    ( );

      // ABI 2 -- the value stack, appended after Release so every slot above
      // keeps the index an ABI 1 client compiled against.
      virtual HRESULT PushValue  ( );
      virtual HRESULT PopValue   ( );
      virtual HRESULT DropValue  ( );
      virtual HRESULT IsStacked  ( int *outStacked ) const;

    // What Move needs of the other node, and what the cursor needs of its owner
    public:
        const FacadeRoute& Route ( ) const { return m_oRoute; }
        FacadeStore*       Store ( ) const { return m_pStore; }

    private:
        FacadeNode ( FacadeStore *pStore, const FacadeRoute& rRoute );
       ~FacadeNode ( );

        // The shared opening of every declare: validate, refuse a collision
        // when MSGF_DECLARE_UPDATE was not asked for, and resolve the parent.
        HRESULT
          BeginDeclare ( unsigned int uScope, const wchar_t *lpszName
                       , unsigned int uFlags, P3PmsgField& rField
                       , BOOL& rbUpdate ) const;
        // The shared close: hand back a node for the child just declared.
        HRESULT
          EndDeclare   ( unsigned int uScope, const wchar_t *lpszName
                       , msgf::IMsgNode **outNode );
        // A route with one more step on the end.
        FacadeRoute
          RouteWith    ( unsigned int uScope, const wchar_t *lpszName ) const;

    // Attributes
    private:
        FacadeStore *m_pStore{nullptr};
        FacadeRoute  m_oRoute;
};
