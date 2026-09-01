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
// ComColl.h -- the four collection objects: MsgAttr, MsgDesc, MsgList, MsgVect.
//
// MsgAttr and MsgDesc are THE SAME CODE with a different scope constant, and
// that is the visible shape of the facade's second design rule: a node's two
// child collections differ in one thing, which is which collection is reached,
// so it is an argument rather than two families of calls. The two classes below
// exist only because the type library publishes two interfaces; every method of
// both forwards to the same file-local function with a scope.
//
// MsgList and MsgVect hold an msgf::IMsgList / msgf::IMsgVect, which survive
// every mutation of the store -- so, unlike the cursor next door, they need no
// staleness check and no rebuild.
#pragma once

#include "MsgcoreCom_h.h"
#include "ComUtil.h"
#include "ComStore.h"

// ---------------------------------------------------------------------------
// A node's attribute collection.
// ---------------------------------------------------------------------------
class ATL_NO_VTABLE CMsgAttr
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgAttr, &CLSID_MsgAttr>
    , public ATL::IDispatchImpl<IMsgAttrCom, &IID_IMsgAttrCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgAttrCom>
{
  public:
    CMsgAttr ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgAttr)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgAttr)
        COM_INTERFACE_ENTRY(IMsgAttrCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    // Borrows the owner node and takes its own reference on it.
    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgNode *pOwner, IMsgAttrCom **ppOut );

    // What MSGF_GUARD_END expands to. A static member rather than a file-local
    // function because this file holds FOUR classes, each of which has to
    // report against its OWN interface IID -- and class scope is looked up
    // first, so each class's methods find its own.
    static HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a = NULL, BSTR b = NULL );

    STDMETHOD(get_Item)     ( BSTR name, IMsgFieldCom **ppField );
    STDMETHOD(get_Count)    ( LONG *pVal );
    STDMETHOD(get_IsEmpty)  ( VARIANT_BOOL *pVal );
    STDMETHOD(Exists)       ( BSTR name, VARIANT_BOOL *pVal );
    STDMETHOD(Declare)      ( BSTR name, VARIANT value, VARIANT_BOOL update,
                              IMsgFieldCom **ppField );
    STDMETHOD(Delete)       ( BSTR name, VARIANT_BOOL *pDeleted );
    STDMETHOD(Truncate)     ( );
    STDMETHOD(get_Cursor)   ( IMsgCursorCom **ppCurs );
    STDMETHOD(get__NewEnum) ( IUnknown **ppUnk );

  private:
    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;
    CMsgStore             *m_pStore;
    msgf::IMsgNode        *m_pOwner;        // owned
};

// ---------------------------------------------------------------------------
// A node's descendant collection. The same, one constant apart.
// ---------------------------------------------------------------------------
class ATL_NO_VTABLE CMsgDesc
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgDesc, &CLSID_MsgDesc>
    , public ATL::IDispatchImpl<IMsgDescCom, &IID_IMsgDescCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgDescCom>
{
  public:
    CMsgDesc ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgDesc)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgDesc)
        COM_INTERFACE_ENTRY(IMsgDescCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgNode *pOwner, IMsgDescCom **ppOut );
    static HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a = NULL, BSTR b = NULL );

    STDMETHOD(get_Item)     ( BSTR name, IMsgFieldCom **ppField );
    STDMETHOD(get_Count)    ( LONG *pVal );
    STDMETHOD(get_IsEmpty)  ( VARIANT_BOOL *pVal );
    STDMETHOD(Exists)       ( BSTR name, VARIANT_BOOL *pVal );
    STDMETHOD(Declare)      ( BSTR name, VARIANT value, VARIANT_BOOL update,
                              IMsgFieldCom **ppField );
    STDMETHOD(Delete)       ( BSTR name, VARIANT_BOOL *pDeleted );
    STDMETHOD(Truncate)     ( );
    STDMETHOD(get_Cursor)   ( IMsgCursorCom **ppCurs );
    STDMETHOD(get__NewEnum) ( IUnknown **ppUnk );

  private:
    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;
    CMsgStore             *m_pStore;
    msgf::IMsgNode        *m_pOwner;        // owned
};

// ---------------------------------------------------------------------------
// A node's linked list of values.
// ---------------------------------------------------------------------------
class ATL_NO_VTABLE CMsgList
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgList, &CLSID_MsgList>
    , public ATL::IDispatchImpl<IMsgListCom, &IID_IMsgListCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgListCom>
{
  public:
    CMsgList ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgList)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgList)
        COM_INTERFACE_ENTRY(IMsgListCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    // TAKES the list handle.
    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgList *pList, IMsgListCom **ppOut );
    // A node that IS a list, reached through its own parent -- see the note in
    // the .cpp on why that is a path parse rather than a call.
    static HRESULT MakeFromSelf ( CMsgStore *pStore, msgf::IMsgNode *pSelf,
                                  IMsgListCom **ppOut );
    static HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a = NULL, BSTR b = NULL );

    STDMETHOD(get_Count)    ( LONG *pVal );
    STDMETHOD(AddHead)      ( VARIANT value );
    STDMETHOD(AddTail)      ( VARIANT value );
    STDMETHOD(DropHead)     ( );
    STDMETHOD(DropTail)     ( );
    STDMETHOD(Truncate)     ( );
    STDMETHOD(get_Item)     ( LONG index, VARIANT *pVal );
    STDMETHOD(put_Item)     ( LONG index, VARIANT value );
    STDMETHOD(get_TypeAt)   ( LONG index, LONG *pVal );
    STDMETHOD(RemoveAt)     ( LONG index, VARIANT_BOOL *pRemoved );
    STDMETHOD(get__NewEnum) ( IUnknown **ppUnk );

  private:
    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;
    CMsgStore             *m_pStore;
    msgf::IMsgList        *m_pList;         // owned
};

// ---------------------------------------------------------------------------
// A node's indexed vector.
// ---------------------------------------------------------------------------
class ATL_NO_VTABLE CMsgVect
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CMsgVect, &CLSID_MsgVect>
    , public ATL::IDispatchImpl<IMsgVectCom, &IID_IMsgVectCom, &LIBID_MsgcoreComLib, 1, 0>
    , public ATL::ISupportErrorInfoImpl<&IID_IMsgVectCom>
{
  public:
    CMsgVect ( );

    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(CMsgVect)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(CMsgVect)
        COM_INTERFACE_ENTRY(IMsgVectCom)
        COM_INTERFACE_ENTRY(IDispatch)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
    END_COM_MAP()

    void FinalRelease ( );

    static HRESULT Make ( CMsgStore *pStore, msgf::IMsgVect *pVect, IMsgVectCom **ppOut );
    static HRESULT MakeFromSelf ( CMsgStore *pStore, msgf::IMsgNode *pSelf,
                                  IMsgVectCom **ppOut );
    static HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a = NULL, BSTR b = NULL );

    STDMETHOD(get_Item)     ( LONG index, VARIANT *pVal );
    STDMETHOD(put_Item)     ( LONG index, VARIANT value );
    STDMETHOD(IsData)       ( LONG index, VARIANT_BOOL *pVal );
    STDMETHOD(IsField)      ( LONG index, VARIANT_BOOL *pVal );
    STDMETHOD(IsList)       ( LONG index, VARIANT_BOOL *pVal );
    STDMETHOD(IsVect)       ( LONG index, VARIANT_BOOL *pVal );
    STDMETHOD(Truncate)     ( );
    STDMETHOD(get_Count)    ( LONG *pVal );
    STDMETHOD(get_TypeAt)   ( LONG index, LONG *pVal );
    STDMETHOD(get_NameAt)   ( LONG index, BSTR *pVal );
    STDMETHOD(ListAt)       ( LONG index, IMsgListCom **ppList );
    STDMETHOD(VectAt)       ( LONG index, IMsgVectCom **ppVect );
    STDMETHOD(RemoveAt)     ( LONG index, VARIANT_BOOL *pRemoved );
    STDMETHOD(get__NewEnum) ( IUnknown **ppUnk );

  private:
    ATL::CComPtr<IUnknown> m_spStoreKeepAlive;
    CMsgStore             *m_pStore;
    msgf::IMsgVect        *m_pVect;         // owned
};

// ---------------------------------------------------------------------------
// The two snapshot enumerators, defined in ComCursor.cpp.
//
// SNAPSHOT, everywhere, for the reason For Each needs: its contract is one pass
// over a fixed set, and the underlying cursor is invalidated by any mutation.
// Collecting first costs one walk and makes `For Each x In coll : coll.Delete
// x.Name : Next` legal, which is what a script author will write.
// ---------------------------------------------------------------------------
HRESULT MakeFieldEnum ( CMsgStore *pStore, msgf::IMsgNode *pOwner,
                        unsigned int uScope, IUnknown **ppUnk );
HRESULT MakeVariantEnum ( const std::vector<ATL::CComVariant>& values, IUnknown **ppUnk );
