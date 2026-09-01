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
// ComField.h -- CMsgField, the coclass MsgField.
//
// THE OBJECT HOLDS ONE msgf::IMsgNode, and that is the whole of it now.
//
// It used to hold a chain of names and re-resolve them from the root on entry
// to every method, because a raw kernel handle does not survive the next heap
// relocation and a script has nowhere to put a re-resolve:
//
//      Set cfg = store.Root.Child("config")
//      cfg.Declare "width", 1024
//      cfg.Declare "height", 768
//
// An IMsgNode IS that chain, one layer down, so this class keeps the node and
// calls through it. A node deleted under a held reference still answers
// msgcStale rather than reading a dangling address -- the facade answers
// MSGF_E_NO_ITEM and this file maps it.
//
// THE DETACHED HALF IS GONE. There used to be a second kind of field object,
// owning a deep copy, answering msgcDetached to every write, because
// msgcore_mgr_p2pos2field and msgcore_field_select_item deep-copied while
// msgcore_field_child aliased -- all three returning the same handle type.
// Every IMsgNode is a route into the live store, so there is one kind.
#pragma once

#include "MsgcoreCom_h.h"
#include "ComUtil.h"
#include "ComStore.h"

class CMsgStore;

// ---------------------------------------------------------------------------
// A PRIVATE interface, deliberately absent from the IDL and from the type
// library. Its only job is to answer "are you one of mine, and whose?".
//
// MoveChild needs that: a destination handed in from ANOTHER store would move
// the child into a different heap, and two stores commonly have the same shape,
// so it would not fail -- it would quietly do the wrong thing. Comparing store
// pointers is the only way to tell, and QI for an unpublished IID is the
// standard way to discover whether an interface pointer really is one of your
// own objects rather than a proxy or a foreign implementation.
//
// It returns C++ pointers, which is legal precisely BECAUSE it is unpublished:
// nothing can marshal it, and a cross-apartment proxy will not answer this IID
// at all -- which MoveChild treats as "cannot prove it is mine" and refuses.
// ---------------------------------------------------------------------------
// {9E4B7C21-0D53-4A8E-B6F1-72C0D4E58A31}
extern "C" const GUID IID_IMsgFieldPrivate;

struct IMsgFieldPrivate : public IUnknown
{
    virtual CMsgStore* STDMETHODCALLTYPE OwningStore ( ) = 0;

    // The node this object names. NOT reference-counted for the caller: it is
    // borrowed for the duration of a call the caller is making under the store
    // lock, which is the only context this interface is used in.
    virtual msgf::IMsgNode* STDMETHODCALLTYPE BorrowNode ( ) = 0;
};

class ATL_NO_VTABLE CMsgField
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgField, &CLSID_MsgField>
    , public ATL::IDispatchImpl<IMsgFieldCom, &IID_IMsgFieldCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgFieldCom>
    , public IMsgFieldPrivate
{
  public:
    CMsgField ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgField)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgField)
        COM_INTERFACE_ENTRY(IMsgFieldCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY_IID(IID_IMsgFieldPrivate, IMsgFieldPrivate)
    END_COM_MAP()

    void FinalRelease ( );

    // Called by CMsgStore::MakeField, never by a client. TAKES the node.
    void Init ( CMsgStore *pStore, msgf::IMsgNode *pNode );

    // --- IMsgFieldCom -------------------------------------------------------
    STDMETHOD(get_Value)        ( VARIANT *pVal );
    STDMETHOD(put_Value)        ( VARIANT newVal );
    STDMETHOD(get_Name)         ( BSTR *pVal );
    STDMETHOD(get_Text)         ( BSTR *pVal );
    STDMETHOD(put_Text)         ( BSTR newVal );
    STDMETHOD(get_DataType)     ( LONG *pVal );
    STDMETHOD(get_TypeName)     ( BSTR *pVal );
    STDMETHOD(get_IsNull)       ( VARIANT_BOOL *pVal );
    STDMETHOD(get_Path)         ( BSTR *pVal );
    STDMETHOD(get_P2Pos)        ( LONGLONG *pVal );
    STDMETHOD(Exists)           ( BSTR name, VARIANT_BOOL *pVal );
    STDMETHOD(Child)            ( BSTR name, IMsgFieldCom **ppField );
    STDMETHOD(ChildAt)          ( LONG index, IMsgFieldCom **ppField );
    STDMETHOD(Declare)          ( BSTR name, VARIANT value, VARIANT_BOOL update,
                                  IMsgFieldCom **ppField );
    STDMETHOD(DeclareTyped)     ( BSTR name, VARIANT value, LONG dataType,
                                  VARIANT_BOOL update, IMsgFieldCom **ppField );
    STDMETHOD(Delete)           ( BSTR name, VARIANT_BOOL *pDeleted );
    STDMETHOD(Truncate)         ( );
    STDMETHOD(RenameChild)      ( BSTR oldName, BSTR newName, VARIANT_BOOL *pRenamed );
    STDMETHOD(MoveChild)        ( IMsgFieldCom *destination, BSTR name, VARIANT_BOOL *pMoved );
    STDMETHOD(RetypeChild)      ( BSTR name, LONG dataType, VARIANT_BOOL *pRetyped );
    STDMETHOD(Attributes)       ( VARIANT_BOOL create, IMsgAttrCom **ppAttr );
    STDMETHOD(Descendants)      ( VARIANT_BOOL create, IMsgDescCom **ppDesc );
    STDMETHOD(get_Cursor)       ( IMsgCursorCom **ppCurs );
    STDMETHOD(get_List)         ( IMsgListCom **ppList );
    STDMETHOD(get_Vector)       ( IMsgVectCom **ppVect );

    STDMETHOD(DeclareList)      ( BSTR name, IMsgListCom **ppList );
    STDMETHOD(DeclareVect)      ( BSTR name, LONG count, LONG dataType,
                                  IMsgVectCom **ppVect );
    STDMETHOD(ChildList)        ( BSTR name, IMsgListCom **ppList );
    STDMETHOD(ChildVect)        ( BSTR name, IMsgVectCom **ppVect );
    STDMETHOD(get_Walker)       ( IMsgRecursCom **ppWalker );
    STDMETHOD(get_Timestamp)    ( DATE *pVal );
    STDMETHOD(put_Timestamp)    ( DATE newVal );
    STDMETHOD(Touch)            ( );
    STDMETHOD(get_Count)        ( LONG *pVal );
    STDMETHOD(get_IsList)       ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsVect)       ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsAttributed) ( VARIANT_BOOL *pVal );
    STDMETHOD(get_IsDescendant) ( VARIANT_BOOL *pVal );

    // The value stack, which is a node's own state one layer down and was a
    // separate MsgStack object here before.
    STDMETHOD(PushValue)        ( );
    STDMETHOD(PopValue)         ( VARIANT_BOOL *pRestored );
    STDMETHOD(DropValue)        ( VARIANT_BOOL *pDropped );
    STDMETHOD(get_IsStacked)    ( VARIANT_BOOL *pVal );

    STDMETHOD(get__NewEnum)     ( IUnknown **ppUnk );

    // --- IMsgFieldPrivate ---------------------------------------------------
    virtual CMsgStore*      STDMETHODCALLTYPE OwningStore ( ) { return m_pStore; }
    virtual msgf::IMsgNode* STDMETHODCALLTYPE BorrowNode  ( ) { return m_pNode; }

    CMsgStore*      Store ( ) const { return m_pStore; }
    msgf::IMsgNode* Node  ( ) const { return m_pNode;  }

  private:
    // The two lines every entry point opens with: the store is still open, and
    // this object still names something.
    HRESULT Bind ( ) const;

    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;   // the store outlives us
    CMsgStore             *m_pStore;
    msgf::IMsgNode        *m_pNode;              // owned
};
