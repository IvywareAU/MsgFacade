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
// ComCursor.h -- CMsgCursor, and the two snapshot enumerators.
//
// THE ONE OBJECT HERE THAT CARRIES STATE, and therefore the one that cannot
// simply hold a route the way CMsgField does: a POSITION is what it holds, and
// re-resolving a position per call is a contradiction in terms.
//
// So it inherits the rule the facade states once and cannot remove: a cursor
// must not outlive a mutation of the tree it is walking. What this class adds
// is that the rule stops being the CLIENT's problem -- the store carries a
// mutation counter, and a cursor whose counter is stale silently rebuilds
// itself and seeks back to its index before answering.
//
// That is not the same as never having been invalidated: an element deleted
// meanwhile shifts everything after it, so the index is a position and not an
// identity. Which is why _NewEnum, whose contract is one pass over a fixed set,
// snapshots instead.
#pragma once

#include "MsgcoreCom_h.h"
#include "ComUtil.h"
#include "ComStore.h"

class ATL_NO_VTABLE CMsgCursor
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgCursor, &CLSID_MsgCursor>
    , public ATL::IDispatchImpl<IMsgCursorCom, &IID_IMsgCursorCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgCursorCom>
{
  public:
    CMsgCursor ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgCursor)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgCursor)
        COM_INTERFACE_ENTRY(IMsgCursorCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgNode *pOwner
                        , unsigned int uScope, IMsgCursorCom **ppOut );

    STDMETHOD(get_Field)         ( IMsgFieldCom **ppField );
    STDMETHOD(Next)              ( );
    STDMETHOD(Seek)              ( );
    STDMETHOD(GotoName)          ( BSTR name, VARIANT_BOOL *pFound );
    STDMETHOD(GotoIndex)         ( LONG index, VARIANT_BOOL *pFound );
    STDMETHOD(get_EndOfCursor)   ( VARIANT_BOOL *pVal );
    STDMETHOD(get_StartOfCursor) ( VARIANT_BOOL *pVal );
    STDMETHOD(get_Count)         ( LONG *pVal );
    STDMETHOD(get_Index)         ( LONG *pVal );
    STDMETHOD(get_Name)          ( BSTR *pVal );
    STDMETHOD(get_IsItem)        ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsList)        ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsVect)        ( VARIANT_BOOL *pVal );
    STDMETHOD(Delete)            ( );
    STDMETHOD(get__NewEnum)      ( IUnknown **ppUnk );

  private:
    // The staleness check every method opens with: rebuild and re-seek when the
    // store has been mutated since this cursor was last touched.
    HRESULT Bind ( );

    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;
    CMsgStore             *m_pStore;
    msgf::IMsgNode        *m_pOwner;        // owned
    msgf::IMsgCursor      *m_pCurs;         // owned; rebuilt on staleness
    unsigned int           m_uScope;
    LONG                   m_lSeq;          // the store's mutation count at last bind
    unsigned int           m_uIndex;        // what to seek back to after a rebuild
};
