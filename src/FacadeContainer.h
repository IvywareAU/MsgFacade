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
// FacadeContainer.h -- FacadeList and FacadeVect, the objects behind
// msgf::IMsgList and msgf::IMsgVect.
//
// Both hold a ROUTE, exactly as FacadeNode does, and re-resolve to a live
// P3PmsgList / P3PmsgVect on entry to every method.  So a container handle
// survives every mutation of the store for the same reason a node does, and
// there is ONE invalidation rule in this ABI rather than one per object kind.
//
// A route step may be an ELEMENT INDEX (SCOPE_ELEM) as well as a name, which
// is how a container nested inside a vect is reached: vect elements are
// addressed by position, and need not carry a name worth resolving.
#pragma once

#include "FacadeInternal.h"

class FacadeStore;

class FacadeList : public msgf::IMsgList
{
    public:
        static HRESULT
          Make ( FacadeStore *pStore, const FacadeRoute& rRoute
               , msgf::IMsgList **outList );

    // msgf::IMsgList
    public:
      virtual HRESULT GetCount  ( unsigned int *outCount ) const;
      virtual HRESULT GetTypeAt ( unsigned int index, unsigned char *outType ) const;
      virtual HRESULT GetIntAt  ( unsigned int index
                                , long long *outValue, int *outUnsigned ) const;
      virtual HRESULT GetRealAt ( unsigned int index, double *outValue ) const;
      virtual HRESULT GetTextAt ( unsigned int index
                                , wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT SetIntAt  ( unsigned int index, long long value );
      virtual HRESULT SetRealAt ( unsigned int index, double value );
      virtual HRESULT SetTextAt ( unsigned int index, const wchar_t *value );
      virtual HRESULT AddInt    ( long long value, unsigned char type
                                , unsigned int flags );
      virtual HRESULT AddReal   ( double value, unsigned char type
                                , unsigned int flags );
      virtual HRESULT AddText   ( const wchar_t *value, unsigned int flags );
      virtual HRESULT Drop      ( unsigned int flags );
      virtual HRESULT DeleteAt  ( unsigned int index );
      virtual HRESULT Truncate  ( );
      virtual ULONG   Release   ( );

    private:
        FacadeList ( FacadeStore *pStore, const FacadeRoute& rRoute );
       ~FacadeList ( );

        // Route -> a live P3PmsgList.  MSGF_E_TYPE when the route resolves to
        // something that is no longer a list.
        HRESULT Resolve ( P3PmsgList& rOut ) const;

    private:
        FacadeStore *m_pStore{nullptr};
        FacadeRoute  m_oRoute;
};

class FacadeVect : public msgf::IMsgVect
{
    public:
        static HRESULT
          Make ( FacadeStore *pStore, const FacadeRoute& rRoute
               , msgf::IMsgVect **outVect );

    // msgf::IMsgVect
    public:
      virtual HRESULT GetCount  ( unsigned int *outCount ) const;
      virtual HRESULT GetKindAt ( unsigned int index, unsigned int *outKind ) const;
      virtual HRESULT GetTypeAt ( unsigned int index, unsigned char *outType ) const;
      virtual HRESULT GetNameAt ( unsigned int index
                                , wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetIntAt  ( unsigned int index
                                , long long *outValue, int *outUnsigned ) const;
      virtual HRESULT GetRealAt ( unsigned int index, double *outValue ) const;
      virtual HRESULT GetTextAt ( unsigned int index
                                , wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT SetIntAt  ( unsigned int index, long long value );
      virtual HRESULT SetRealAt ( unsigned int index, double value );
      virtual HRESULT SetTextAt ( unsigned int index, const wchar_t *value );
      virtual HRESULT GetListAt ( unsigned int index, msgf::IMsgList **outList );
      virtual HRESULT GetVectAt ( unsigned int index, msgf::IMsgVect **outVect );
      virtual HRESULT DeleteAt  ( unsigned int index );
      virtual HRESULT Truncate  ( );
      virtual ULONG   Release   ( );

    private:
        FacadeVect ( FacadeStore *pStore, const FacadeRoute& rRoute );
       ~FacadeVect ( );

        HRESULT Resolve ( P3PmsgVect& rOut ) const;
        // A route with one element step appended.
        FacadeRoute RouteWith ( unsigned int uIndex ) const;

    private:
        FacadeStore *m_pStore{nullptr};
        FacadeRoute  m_oRoute;
};
