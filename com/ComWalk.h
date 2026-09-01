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
// ComWalk.h -- CMsgRecurs, the recursive subtree walker.
//
// A whole subtree from one flat loop. It owns a chain of cursors and splices
// descent into its own advance, but it descends only when the caller says Push
// -- which is what makes it a WALKER rather than an iterator, and what lets a
// caller PRUNE a branch by simply not descending into it. That is the one thing
// For Each over Descendants cannot express.
//
// It holds live state, so it must not outlive a mutation of the tree it is
// walking: finish the walk, then mutate. Unlike the enumerators elsewhere in
// this server it is NOT a snapshot, because snapshotting a subtree of unknown
// size is the thing it exists to avoid.
//
// ---------------------------------------------------------------------------
// MsgStack IS GONE FROM THIS FILE
// ---------------------------------------------------------------------------
// It used to hold a second class, CMsgStack, over the core's MsgStck. What that
// object was is a saved (name, value) pair living inside a node, so it is four
// methods on IMsgFieldCom now -- PushValue, PopValue, DropValue, IsStacked --
// rather than a coclass with a lifetime of its own to get wrong.
#pragma once

#include "MsgcoreCom_h.h"
#include "ComUtil.h"
#include "ComStore.h"

class ATL_NO_VTABLE CMsgRecurs
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgRecurs, &CLSID_MsgRecurs>
    , public ATL::IDispatchImpl<IMsgRecursCom, &IID_IMsgRecursCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgRecursCom>
{
  public:
    CMsgRecurs ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgRecurs)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgRecurs)
        COM_INTERFACE_ENTRY(IMsgRecursCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgNode *pRoot, IMsgRecursCom **ppOut );

    STDMETHOD(MoveNext)     ( );
    STDMETHOD(Push)         ( LONG *pDepth );
    STDMETHOD(Pop)          ( LONG *pDepth );
    STDMETHOD(Break)        ( );
    STDMETHOD(get_AtEnd)    ( VARIANT_BOOL *pVal );
    STDMETHOD(get_Name)     ( BSTR *pVal );
    STDMETHOD(get_Depth)    ( LONG *pVal );
    STDMETHOD(get_IsField)  ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsList)   ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsVect)   ( VARIANT_BOOL *pVal );
    STDMETHOD(get_Path)     ( BSTR *pVal );
    STDMETHOD(get_Field)    ( IMsgFieldCom **ppField );
    STDMETHOD(get_List)     ( IMsgListCom **ppList );
    STDMETHOD(get_Vector)   ( IMsgVectCom **ppVect );

  private:
    HRESULT Bind ( ) const;

    // The ancestry, kept as the walk moves, so that the current stop has a
    // PATH -- see the note in the .cpp. Called after every advance.
    void    Track ( );
    // The current stop's path, from the root the walker was opened on.
    HRESULT CurrentPath ( ATL::CComBSTR& out ) const;

    ATL::CComPtr<IUnknown>     m_spStoreKeepAlive;
    CMsgStore                 *m_pStore;
    msgf::IMsgWalker          *m_pWalk;         // owned
    ATL::CComBSTR              m_bsRootPath;    // where the walk started
    std::vector<std::wstring>  m_ancestry;      // one name per level, current last
};
