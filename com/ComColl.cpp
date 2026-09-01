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
// ComColl.cpp -- MsgAttr, MsgDesc, MsgList and MsgVect.
#include "stdafx.h"
#include "ComColl.h"
#include "ComField.h"
#include "ComCursor.h"

using namespace msgf;

// ===========================================================================
// The collection body, once, with the scope as an argument.
//
// Every method of CMsgAttr and CMsgDesc is a forward to one of these. That is
// not tidiness: the two collections differ in exactly one constant, and writing
// them twice is how the two drift apart.
// ===========================================================================
namespace {

HRESULT CollItem ( CMsgStore *pStore, IMsgNode *pOwner, unsigned int uScope
                 , BSTR name, IMsgFieldCom **ppField )
{
    IMsgNode *pChild = NULL;
    HRESULT hr = pOwner->GetChild ( uScope, Str ( name ), &pChild );
    if ( FAILED(hr) ) return FromFacade ( hr );
    return CMsgStore::MakeField ( pStore, pChild, ppField );
}

HRESULT CollCount ( IMsgNode *pOwner, unsigned int uScope, LONG *pVal )
{
    unsigned int u = 0;
    HRESULT hr = pOwner->GetCount ( uScope, &u );
    // A list or a vect has no collection of either kind, and "how many" is 0
    // for a caller asking rather than an error -- the typed accessors are where
    // that distinction has to bite.
    if ( hr == MSGF_E_TYPE ) { *pVal = 0; return S_OK; }
    if ( FAILED(hr) ) return FromFacade ( hr );
    *pVal = (LONG)u;
    return S_OK;
}

HRESULT CollExists ( IMsgNode *pOwner, unsigned int uScope, BSTR name, VARIANT_BOOL *pVal )
{
    HRESULT hr = pOwner->Exists ( uScope, Str ( name ) );
    if ( FAILED(hr) ) return FromFacade ( hr );
    *pVal = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

HRESULT CollDeclare ( CMsgStore *pStore, IMsgNode *pOwner, unsigned int uScope
                    , BSTR name, VARIANT value, VARIANT_BOOL update
                    , IMsgFieldCom **ppField )
{
    HRESULT hr = CheckName ( name );
    if ( FAILED(hr) ) return hr;

    IMsgNode *pChild = NULL;
    hr = DeclareVariant ( pOwner, uScope, name, value
                        , ( update != VARIANT_FALSE ) ? 1 : 0, &pChild );
    if ( FAILED(hr) ) return hr;

    pStore->BumpMutation();
    if ( ppField == NULL )
    {
        if ( pChild != NULL ) pChild->Release();
        return S_OK;
    }
    return CMsgStore::MakeField ( pStore, pChild, ppField );
}

HRESULT CollDelete ( CMsgStore *pStore, IMsgNode *pOwner, unsigned int uScope
                   , BSTR name, VARIANT_BOOL *pDeleted )
{
    HRESULT hr = pOwner->Delete ( uScope, Str ( name ) );
    if ( hr == MSGF_E_NO_ITEM ) return S_OK;         // there was none: False
    if ( FAILED(hr) ) return FromFacade ( hr );

    pStore->BumpMutation();
    if ( pDeleted != NULL ) *pDeleted = VARIANT_TRUE;
    return S_OK;
}

HRESULT CollTruncate ( CMsgStore *pStore, IMsgNode *pOwner, unsigned int uScope )
{
    HRESULT hr = pOwner->Truncate ( uScope );
    if ( FAILED(hr) ) return FromFacade ( hr );
    pStore->BumpMutation();
    return S_OK;
}

} // namespace

// ===========================================================================
// CMsgAttr
// ===========================================================================
static inline HRESULT FailAttr ( HRESULT hr, LPCWSTR wszCall, BSTR bsArg = NULL )
{
    return ComFail ( IID_IMsgAttrCom, hr, wszCall, bsArg );
}

HRESULT CMsgAttr::Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a, BSTR b )
{
    return ComFail ( IID_IMsgAttrCom, hr, wszCall, a, b );
}

CMsgAttr::CMsgAttr ( ) : m_pStore ( NULL ), m_pOwner ( NULL ) { }

void CMsgAttr::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pOwner != NULL ) { m_pOwner->Release(); m_pOwner = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgAttr::Make ( CMsgStore *pStore, IMsgNode *pOwner, IMsgAttrCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pOwner == NULL ) return E_POINTER;

    // The owner node is re-opened by path rather than shared, so this object's
    // lifetime is its own: a caller may release the field it came from.
    ATL::CComBSTR bsPath;
    HRESULT hr = ReadString ( pOwner, &IMsgNode::GetPath, bsPath );
    if ( FAILED(hr) ) return FromFacade ( hr );

    CNodePtr spOwn;
    hr = pStore->Store()->NodeFromPath ( ( bsPath != NULL ) ? bsPath : L"", &spOwn );
    if ( FAILED(hr) ) return FromFacade ( hr );

    ATL::CComObject<CMsgAttr> *pObj = NULL;
    hr = ATL::CComObject<CMsgAttr>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore = pStore;
    pObj->m_pOwner = spOwn.Detach();
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    hr = pObj->QueryInterface ( IID_IMsgAttrCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

STDMETHODIMP CMsgAttr::get_Item ( BSTR name, IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Item", name );

    HRESULT hr = CollItem ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR, name, ppField );
    return FAILED(hr) ? FailAttr ( hr, L"Item", name ) : hr;
    MSGF_GUARD_END(L"Item")
}

STDMETHODIMP CMsgAttr::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Count" );

    HRESULT hr = CollCount ( m_pOwner, MSGF_SCOPE_ATTR, pVal );
    return FAILED(hr) ? FailAttr ( hr, L"Count" ) : S_OK;
    MSGF_GUARD_END(L"Count")
}

STDMETHODIMP CMsgAttr::get_IsEmpty ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_TRUE;

    LONG n = 0;
    HRESULT hr = get_Count ( &n );
    if ( FAILED(hr) ) return hr;
    *pVal = ( n == 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgAttr::Exists ( BSTR name, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Exists", name );

    HRESULT hr = CollExists ( m_pOwner, MSGF_SCOPE_ATTR, name, pVal );
    return FAILED(hr) ? FailAttr ( hr, L"Exists", name ) : S_OK;
    MSGF_GUARD_END(L"Exists")
}

STDMETHODIMP CMsgAttr::Declare ( BSTR name, VARIANT value, VARIANT_BOOL update,
                                 IMsgFieldCom **ppField )
{
    if ( ppField != NULL ) *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Declare", name );

    HRESULT hr = CollDeclare ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR, name, value, update, ppField );
    return FAILED(hr) ? FailAttr ( hr, L"Declare", name ) : hr;
    MSGF_GUARD_END(L"Declare")
}

STDMETHODIMP CMsgAttr::Delete ( BSTR name, VARIANT_BOOL *pDeleted )
{
    if ( pDeleted != NULL ) *pDeleted = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Delete", name );

    HRESULT hr = CollDelete ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR, name, pDeleted );
    return FAILED(hr) ? FailAttr ( hr, L"Delete", name ) : S_OK;
    MSGF_GUARD_END(L"Delete")
}

STDMETHODIMP CMsgAttr::Truncate ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Truncate" );

    HRESULT hr = CollTruncate ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR );
    return FAILED(hr) ? FailAttr ( hr, L"Truncate" ) : S_OK;
    MSGF_GUARD_END(L"Truncate")
}

STDMETHODIMP CMsgAttr::get_Cursor ( IMsgCursorCom **ppCurs )
{
    if ( ppCurs == NULL ) return E_POINTER;
    *ppCurs = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"Cursor" );

    return CMsgCursor::Make ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR, ppCurs );
    MSGF_GUARD_END(L"Cursor")
}

STDMETHODIMP CMsgAttr::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailAttr ( MSGC_E_CLOSED, L"_NewEnum" );

    return MakeFieldEnum ( m_pStore, m_pOwner, MSGF_SCOPE_ATTR, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}

// ===========================================================================
// CMsgDesc -- the same, one constant apart.
// ===========================================================================
static inline HRESULT FailDesc ( HRESULT hr, LPCWSTR wszCall, BSTR bsArg = NULL )
{
    return ComFail ( IID_IMsgDescCom, hr, wszCall, bsArg );
}

HRESULT CMsgDesc::Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a, BSTR b )
{
    return ComFail ( IID_IMsgDescCom, hr, wszCall, a, b );
}

CMsgDesc::CMsgDesc ( ) : m_pStore ( NULL ), m_pOwner ( NULL ) { }

void CMsgDesc::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pOwner != NULL ) { m_pOwner->Release(); m_pOwner = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgDesc::Make ( CMsgStore *pStore, IMsgNode *pOwner, IMsgDescCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pOwner == NULL ) return E_POINTER;

    ATL::CComBSTR bsPath;
    HRESULT hr = ReadString ( pOwner, &IMsgNode::GetPath, bsPath );
    if ( FAILED(hr) ) return FromFacade ( hr );

    CNodePtr spOwn;
    hr = pStore->Store()->NodeFromPath ( ( bsPath != NULL ) ? bsPath : L"", &spOwn );
    if ( FAILED(hr) ) return FromFacade ( hr );

    ATL::CComObject<CMsgDesc> *pObj = NULL;
    hr = ATL::CComObject<CMsgDesc>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore = pStore;
    pObj->m_pOwner = spOwn.Detach();
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    hr = pObj->QueryInterface ( IID_IMsgDescCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

STDMETHODIMP CMsgDesc::get_Item ( BSTR name, IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Item", name );

    HRESULT hr = CollItem ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD, name, ppField );
    return FAILED(hr) ? FailDesc ( hr, L"Item", name ) : hr;
    MSGF_GUARD_END(L"Item")
}

STDMETHODIMP CMsgDesc::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Count" );

    HRESULT hr = CollCount ( m_pOwner, MSGF_SCOPE_CHILD, pVal );
    return FAILED(hr) ? FailDesc ( hr, L"Count" ) : S_OK;
    MSGF_GUARD_END(L"Count")
}

STDMETHODIMP CMsgDesc::get_IsEmpty ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_TRUE;

    LONG n = 0;
    HRESULT hr = get_Count ( &n );
    if ( FAILED(hr) ) return hr;
    *pVal = ( n == 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgDesc::Exists ( BSTR name, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Exists", name );

    HRESULT hr = CollExists ( m_pOwner, MSGF_SCOPE_CHILD, name, pVal );
    return FAILED(hr) ? FailDesc ( hr, L"Exists", name ) : S_OK;
    MSGF_GUARD_END(L"Exists")
}

STDMETHODIMP CMsgDesc::Declare ( BSTR name, VARIANT value, VARIANT_BOOL update,
                                 IMsgFieldCom **ppField )
{
    if ( ppField != NULL ) *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Declare", name );

    HRESULT hr = CollDeclare ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD, name, value, update, ppField );
    return FAILED(hr) ? FailDesc ( hr, L"Declare", name ) : hr;
    MSGF_GUARD_END(L"Declare")
}

STDMETHODIMP CMsgDesc::Delete ( BSTR name, VARIANT_BOOL *pDeleted )
{
    if ( pDeleted != NULL ) *pDeleted = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Delete", name );

    HRESULT hr = CollDelete ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD, name, pDeleted );
    return FAILED(hr) ? FailDesc ( hr, L"Delete", name ) : S_OK;
    MSGF_GUARD_END(L"Delete")
}

STDMETHODIMP CMsgDesc::Truncate ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Truncate" );

    HRESULT hr = CollTruncate ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD );
    return FAILED(hr) ? FailDesc ( hr, L"Truncate" ) : S_OK;
    MSGF_GUARD_END(L"Truncate")
}

STDMETHODIMP CMsgDesc::get_Cursor ( IMsgCursorCom **ppCurs )
{
    if ( ppCurs == NULL ) return E_POINTER;
    *ppCurs = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"Cursor" );

    return CMsgCursor::Make ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD, ppCurs );
    MSGF_GUARD_END(L"Cursor")
}

STDMETHODIMP CMsgDesc::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return FailDesc ( MSGC_E_CLOSED, L"_NewEnum" );

    return MakeFieldEnum ( m_pStore, m_pOwner, MSGF_SCOPE_CHILD, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}

// ===========================================================================
// A node that IS a container, reached through its own parent.
//
// The facade opens a container from its PARENT by name -- GetList(scope,name)
// -- because that is where a container lives; there is no "this node as a
// list" call, and a node does not carry a parent pointer.
//
// So this splits the node's own PATH, which is the documented, reversible
// spelling of exactly that: the last '.' or '@' separates the parent's path
// from the step that reached this node, and the separator IS the scope. A name
// cannot contain either character -- the store's own validator refuses them,
// which is what makes the grammar unambiguous -- so the split is exact.
// ===========================================================================
namespace {

HRESULT SplitPath ( IMsgNode *pSelf, ATL::CComBSTR& bsParent
                  , unsigned int *puScope, ATL::CComBSTR& bsName )
{
    ATL::CComBSTR bsPath;
    HRESULT hr = ReadString ( pSelf, &IMsgNode::GetPath, bsPath );
    if ( FAILED(hr) ) return FromFacade ( hr );

    const std::wstring strPath ( ( bsPath != NULL ) ? (LPCWSTR)bsPath : L"" );
    const size_t nDot = strPath.find_last_of ( L'.' );
    const size_t nAt  = strPath.find_last_of ( L'@' );

    size_t nCut = std::wstring::npos;
    if ( nDot == std::wstring::npos )      nCut = nAt;
    else if ( nAt == std::wstring::npos )  nCut = nDot;
    else                                   nCut = ( nDot > nAt ) ? nDot : nAt;

    if ( nCut == std::wstring::npos )
        return MSGC_E_NO_FIELD;             // the root: it has no parent

    *puScope = ( strPath[nCut] == L'@' ) ? MSGF_SCOPE_ATTR : MSGF_SCOPE_CHILD;
    bsParent = strPath.substr ( 0, nCut ).c_str();
    bsName   = strPath.substr ( nCut + 1 ).c_str();
    return S_OK;
}

} // namespace

// ===========================================================================
// CMsgList
// ===========================================================================
static inline HRESULT FailList ( HRESULT hr, LPCWSTR wszCall )
{
    return ComFail ( IID_IMsgListCom, hr, wszCall, NULL );
}

HRESULT CMsgList::Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a, BSTR b )
{
    return ComFail ( IID_IMsgListCom, hr, wszCall, a, b );
}

CMsgList::CMsgList ( ) : m_pStore ( NULL ), m_pList ( NULL ) { }

void CMsgList::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pList != NULL ) { m_pList->Release(); m_pList = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgList::Make ( CMsgStore *pStore, IMsgList *pList, IMsgListCom **ppOut )
{
    CListPtr spList ( pList );

    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pList == NULL ) return E_POINTER;

    ATL::CComObject<CMsgList> *pObj = NULL;
    HRESULT hr = ATL::CComObject<CMsgList>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore = pStore;
    pObj->m_pList  = spList.Detach();
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    hr = pObj->QueryInterface ( IID_IMsgListCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

HRESULT CMsgList::MakeFromSelf ( CMsgStore *pStore, IMsgNode *pSelf, IMsgListCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;

    ATL::CComBSTR bsParent, bsName;
    unsigned int  uScope = MSGF_SCOPE_CHILD;
    HRESULT hr = SplitPath ( pSelf, bsParent, &uScope, bsName );
    if ( FAILED(hr) ) return MSGC_E_NOT_LIST;

    CNodePtr spParent;
    hr = pStore->Store()->NodeFromPath ( ( bsParent != NULL ) ? bsParent : L"", &spParent );
    if ( FAILED(hr) ) return FromFacade ( hr );

    IMsgList *pList = NULL;
    hr = spParent->GetList ( uScope, ( bsName != NULL ) ? bsName : L"", &pList );
    if ( hr == MSGF_E_TYPE ) return MSGC_E_NOT_LIST;
    if ( FAILED(hr) )        return FromFacade ( hr );

    return Make ( pStore, pList, ppOut );
}

STDMETHODIMP CMsgList::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"Count" );

    unsigned int u = 0;
    HRESULT hr = m_pList->GetCount ( &u );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"Count" );
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"Count")
}

// Add takes a VARIANT and picks the entry point from its type. A type the list
// cannot hold answers msgcType rather than being coerced: silently storing 3.7
// as 3 is worse than refusing it.
static HRESULT AddValue ( IMsgList *pList, const VARIANT& vIn, unsigned int uFlags )
{
    const VARIANT *pv = UnwrapVariant ( vIn );
    if ( pv == NULL ) return E_POINTER;

    ATL::CComVariant v;

    switch ( pv->vt )
    {
      case VT_EMPTY:
      case VT_NULL:
        return MSGC_E_TYPE;

      case VT_BOOL:
        return FromFacade ( pList->AddInt ( ( pv->boolVal != VARIANT_FALSE ) ? 1 : 0
                                          , MSGF_TYPE_BOOL, uFlags ) );

      case VT_I1: case VT_UI1: case VT_I2: case VT_UI2:
      case VT_I4: case VT_UI4: case VT_INT: case VT_UINT:
        if ( FAILED ( v.ChangeType ( VT_I4, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pList->AddInt ( v.lVal, 0, uFlags ) );

      case VT_I8: case VT_UI8:
        if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pList->AddInt ( v.llVal, MSGF_TYPE_INT64, uFlags ) );

      case VT_DATE:
      case VT_R4: case VT_R8: case VT_CY: case VT_DECIMAL:
        if ( FAILED ( v.ChangeType ( VT_R8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pList->AddReal ( v.dblVal, 0, uFlags ) );

      case VT_BSTR:
        return FromFacade ( pList->AddText ( Str ( pv->bstrVal ), uFlags ) );

      default:
        break;
    }

    if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return MSGC_E_TYPE;
    return FromFacade ( pList->AddText ( Str ( v.bstrVal ), uFlags ) );
}

STDMETHODIMP CMsgList::AddHead ( VARIANT value )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"AddHead" );

    HRESULT hr = AddValue ( m_pList, value, MSGF_ADD_HEAD );
    if ( FAILED(hr) ) return FailList ( hr, L"AddHead" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"AddHead")
}

STDMETHODIMP CMsgList::AddTail ( VARIANT value )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"AddTail" );

    HRESULT hr = AddValue ( m_pList, value, MSGF_ADD_TAIL );
    if ( FAILED(hr) ) return FailList ( hr, L"AddTail" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"AddTail")
}

STDMETHODIMP CMsgList::DropHead ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"DropHead" );

    HRESULT hr = m_pList->Drop ( MSGF_DROP_HEAD );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"DropHead" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"DropHead")
}

STDMETHODIMP CMsgList::DropTail ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"DropTail" );

    HRESULT hr = m_pList->Drop ( MSGF_DROP_TAIL );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"DropTail" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"DropTail")
}

STDMETHODIMP CMsgList::Truncate ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"Truncate" );

    HRESULT hr = m_pList->Truncate ( );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"Truncate" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Truncate")
}

// One cell, read at whatever type it was stored as.
static HRESULT ReadCell ( IMsgList *pList, unsigned int uIndex, VARIANT *pOut )
{
    ::VariantInit ( pOut );

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = pList->GetTypeAt ( uIndex, &uType );
    if ( FAILED(hr) ) return FromFacade ( hr );

    switch ( uType )
    {
      case MSGF_TYPE_NULL:
        // The safe probe: past the end is NULL. Reading past the end is a
        // range error, so ask the count rather than guessing from this.
        {
            unsigned int uCount = 0;
            if ( SUCCEEDED ( pList->GetCount ( &uCount ) ) && uIndex >= uCount )
                return MSGC_E_RANGE;
        }
        return S_OK;

      case MSGF_TYPE_BOOL:
      {
        long long ll = 0;
        hr = pList->GetIntAt ( uIndex, &ll, NULL );
        if ( FAILED(hr) ) return FromFacade ( hr );
        pOut->vt      = VT_BOOL;
        pOut->boolVal = ( ll != 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
      }

      case MSGF_TYPE_INT08: case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16: case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32: case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64: case MSGF_TYPE_UINT64:
      {
        long long ll = 0; int bUnsigned = 0;
        hr = pList->GetIntAt ( uIndex, &ll, &bUnsigned );
        if ( FAILED(hr) ) return FromFacade ( hr );

        const bool bFitsLong = ( bUnsigned != 0 )
                             ? ( (unsigned long long)ll <= 0x7FFFFFFFULL )
                             : ( ll >= -2147483647LL - 1 && ll <= 2147483647LL );
        if ( bFitsLong ) { pOut->vt = VT_I4; pOut->lVal  = (LONG)ll; }
        else             { pOut->vt = VT_I8; pOut->llVal = ll;       }
        return S_OK;
      }

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
      {
        double d = 0.0;
        hr = pList->GetRealAt ( uIndex, &d );
        if ( FAILED(hr) ) return FromFacade ( hr );
        if ( uType == MSGF_TYPE_FLOAT ) { pOut->vt = VT_R4; pOut->fltVal = (float)d; }
        else                            { pOut->vt = VT_R8; pOut->dblVal = d;        }
        return S_OK;
      }

      default:
        break;
    }

    unsigned int cch = 0;
    hr = pList->GetTextAt ( uIndex, NULL, &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );
    if ( cch <= 1 )
    {
        pOut->vt      = VT_BSTR;
        pOut->bstrVal = ::SysAllocString ( L"" );
        return ( pOut->bstrVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = pList->GetTextAt ( uIndex, &buf[0], &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );
    pOut->vt      = VT_BSTR;
    pOut->bstrVal = ::SysAllocString ( &buf[0] );
    return ( pOut->bstrVal != NULL ) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CMsgList::get_Item ( LONG index, VARIANT *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    ::VariantInit ( pVal );

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"Item" );
    if ( index < 0 ) return FailList ( MSGC_E_RANGE, L"Item" );

    HRESULT hr = ReadCell ( m_pList, (unsigned int)index, pVal );
    return FAILED(hr) ? FailList ( hr, L"Item" ) : S_OK;
    MSGF_GUARD_END(L"Item")
}

STDMETHODIMP CMsgList::put_Item ( LONG index, VARIANT value )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"Item" );
    if ( index < 0 ) return FailList ( MSGC_E_RANGE, L"Item" );

    const VARIANT *pv = UnwrapVariant ( value );
    if ( pv == NULL ) return E_POINTER;

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = m_pList->GetTypeAt ( (unsigned int)index, &uType );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"Item" );

    ATL::CComVariant v;

    switch ( uType )
    {
      case MSGF_TYPE_BOOL:
        if ( FAILED ( v.ChangeType ( VT_BOOL, pv ) ) ) return FailList ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pList->SetIntAt ( (unsigned int)index, ( v.boolVal != VARIANT_FALSE ) ? 1 : 0 );
        break;

      case MSGF_TYPE_INT08: case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16: case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32: case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64: case MSGF_TYPE_UINT64:
        if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) ) return FailList ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pList->SetIntAt ( (unsigned int)index, v.llVal );
        break;

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
        if ( FAILED ( v.ChangeType ( VT_R8, pv ) ) ) return FailList ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pList->SetRealAt ( (unsigned int)index, v.dblVal );
        break;

      default:
        // A text cell -- and it cannot GROW: a list cell lives inside the
        // list's own block, so a longer value is msgcLimit rather than a
        // reallocation. Delete it and add a new one to lengthen it.
        if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return FailList ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pList->SetTextAt ( (unsigned int)index, Str ( v.bstrVal ) );
        break;
    }

    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"Item" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Item")
}

STDMETHODIMP CMsgList::get_TypeAt ( LONG index, LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"TypeAt" );
    if ( index < 0 ) return FailList ( MSGC_E_RANGE, L"TypeAt" );

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = m_pList->GetTypeAt ( (unsigned int)index, &uType );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"TypeAt" );
    *pVal = (LONG)uType;
    return S_OK;
    MSGF_GUARD_END(L"TypeAt")
}

STDMETHODIMP CMsgList::RemoveAt ( LONG index, VARIANT_BOOL *pRemoved )
{
    if ( pRemoved != NULL ) *pRemoved = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"RemoveAt" );
    if ( index < 0 ) return FailList ( MSGC_E_RANGE, L"RemoveAt" );

    HRESULT hr = m_pList->DeleteAt ( (unsigned int)index );
    if ( hr == MSGF_E_RANGE ) return S_OK;           // nothing there: False
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"RemoveAt" );

    m_pStore->BumpMutation();
    if ( pRemoved != NULL ) *pRemoved = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"RemoveAt")
}

STDMETHODIMP CMsgList::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pList == NULL ) return FailList ( MSGC_E_CLOSED, L"_NewEnum" );

    unsigned int uCount = 0;
    HRESULT hr = m_pList->GetCount ( &uCount );
    if ( FAILED(hr) ) return FailList ( FromFacade ( hr ), L"_NewEnum" );

    std::vector<ATL::CComVariant> values;
    values.reserve ( uCount );
    for ( unsigned int i = 0; i < uCount; ++i )
    {
        ATL::CComVariant v;
        if ( FAILED ( ReadCell ( m_pList, i, &v ) ) ) break;
        values.push_back ( v );
    }

    return MakeVariantEnum ( values, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}

// ===========================================================================
// CMsgVect
// ===========================================================================
static inline HRESULT FailVect ( HRESULT hr, LPCWSTR wszCall )
{
    return ComFail ( IID_IMsgVectCom, hr, wszCall, NULL );
}

HRESULT CMsgVect::Fail ( HRESULT hr, LPCWSTR wszCall, BSTR a, BSTR b )
{
    return ComFail ( IID_IMsgVectCom, hr, wszCall, a, b );
}

CMsgVect::CMsgVect ( ) : m_pStore ( NULL ), m_pVect ( NULL ) { }

void CMsgVect::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pVect != NULL ) { m_pVect->Release(); m_pVect = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgVect::Make ( CMsgStore *pStore, IMsgVect *pVect, IMsgVectCom **ppOut )
{
    CVectPtr spVect ( pVect );

    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pVect == NULL ) return E_POINTER;

    ATL::CComObject<CMsgVect> *pObj = NULL;
    HRESULT hr = ATL::CComObject<CMsgVect>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore = pStore;
    pObj->m_pVect  = spVect.Detach();
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    hr = pObj->QueryInterface ( IID_IMsgVectCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

HRESULT CMsgVect::MakeFromSelf ( CMsgStore *pStore, IMsgNode *pSelf, IMsgVectCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;

    ATL::CComBSTR bsParent, bsName;
    unsigned int  uScope = MSGF_SCOPE_CHILD;
    HRESULT hr = SplitPath ( pSelf, bsParent, &uScope, bsName );
    if ( FAILED(hr) ) return MSGC_E_NOT_VECT;

    CNodePtr spParent;
    hr = pStore->Store()->NodeFromPath ( ( bsParent != NULL ) ? bsParent : L"", &spParent );
    if ( FAILED(hr) ) return FromFacade ( hr );

    IMsgVect *pVect = NULL;
    hr = spParent->GetVect ( uScope, ( bsName != NULL ) ? bsName : L"", &pVect );
    if ( hr == MSGF_E_TYPE ) return MSGC_E_NOT_VECT;
    if ( FAILED(hr) )        return FromFacade ( hr );

    return Make ( pStore, pVect, ppOut );
}

// One element, read at whatever type it holds.
static HRESULT ReadElem ( IMsgVect *pVect, unsigned int uIndex, VARIANT *pOut )
{
    ::VariantInit ( pOut );

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = pVect->GetTypeAt ( uIndex, &uType );
    if ( FAILED(hr) ) return FromFacade ( hr );

    switch ( uType )
    {
      case MSGF_TYPE_NULL:
        return S_OK;

      case MSGF_TYPE_BOOL:
      {
        long long ll = 0;
        hr = pVect->GetIntAt ( uIndex, &ll, NULL );
        if ( FAILED(hr) ) return FromFacade ( hr );
        pOut->vt      = VT_BOOL;
        pOut->boolVal = ( ll != 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
      }

      case MSGF_TYPE_INT08: case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16: case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32: case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64: case MSGF_TYPE_UINT64:
      {
        long long ll = 0; int bUnsigned = 0;
        hr = pVect->GetIntAt ( uIndex, &ll, &bUnsigned );
        if ( FAILED(hr) ) return FromFacade ( hr );

        const bool bFitsLong = ( bUnsigned != 0 )
                             ? ( (unsigned long long)ll <= 0x7FFFFFFFULL )
                             : ( ll >= -2147483647LL - 1 && ll <= 2147483647LL );
        if ( bFitsLong ) { pOut->vt = VT_I4; pOut->lVal  = (LONG)ll; }
        else             { pOut->vt = VT_I8; pOut->llVal = ll;       }
        return S_OK;
      }

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
      {
        double d = 0.0;
        hr = pVect->GetRealAt ( uIndex, &d );
        if ( FAILED(hr) ) return FromFacade ( hr );
        if ( uType == MSGF_TYPE_FLOAT ) { pOut->vt = VT_R4; pOut->fltVal = (float)d; }
        else                            { pOut->vt = VT_R8; pOut->dblVal = d;        }
        return S_OK;
      }

      default:
        break;
    }

    unsigned int cch = 0;
    hr = pVect->GetTextAt ( uIndex, NULL, &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );
    if ( cch <= 1 )
    {
        pOut->vt      = VT_BSTR;
        pOut->bstrVal = ::SysAllocString ( L"" );
        return ( pOut->bstrVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = pVect->GetTextAt ( uIndex, &buf[0], &cch );
    if ( FAILED(hr) ) return FromFacade ( hr );
    pOut->vt      = VT_BSTR;
    pOut->bstrVal = ::SysAllocString ( &buf[0] );
    return ( pOut->bstrVal != NULL ) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CMsgVect::get_Item ( LONG index, VARIANT *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    ::VariantInit ( pVal );

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"Item" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"Item" );

    HRESULT hr = ReadElem ( m_pVect, (unsigned int)index, pVal );
    return FAILED(hr) ? FailVect ( hr, L"Item" ) : S_OK;
    MSGF_GUARD_END(L"Item")
}

STDMETHODIMP CMsgVect::put_Item ( LONG index, VARIANT value )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"Item" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"Item" );

    const VARIANT *pv = UnwrapVariant ( value );
    if ( pv == NULL ) return E_POINTER;

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = m_pVect->GetTypeAt ( (unsigned int)index, &uType );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"Item" );

    ATL::CComVariant v;

    switch ( uType )
    {
      case MSGF_TYPE_BOOL:
      case MSGF_TYPE_INT08: case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16: case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32: case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64: case MSGF_TYPE_UINT64:
        if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) ) return FailVect ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pVect->SetIntAt ( (unsigned int)index, v.llVal );
        break;

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
        if ( FAILED ( v.ChangeType ( VT_R8, pv ) ) ) return FailVect ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pVect->SetRealAt ( (unsigned int)index, v.dblVal );
        break;

      default:
        if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return FailVect ( DISP_E_TYPEMISMATCH, L"Item" );
        hr = m_pVect->SetTextAt ( (unsigned int)index, Str ( v.bstrVal ) );
        break;
    }

    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"Item" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Item")
}

// The four kind questions, one helper apart.
static HRESULT ElemKindIs ( CMsgStore *pStore, IMsgVect *pVect, LONG index
                          , unsigned int uWant, VARIANT_BOOL *pVal )
{
    if ( pStore == NULL || pVect == NULL ) return MSGC_E_CLOSED;
    if ( index < 0 ) return MSGC_E_RANGE;

    unsigned int uKind = 0;
    HRESULT hr = pVect->GetKindAt ( (unsigned int)index, &uKind );
    if ( FAILED(hr) ) return FromFacade ( hr );
    *pVal = ( uKind == uWant ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgVect::IsData ( LONG index, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = ElemKindIs ( m_pStore, m_pVect, index, MSGF_KIND_DATA, pVal );
    return FAILED(hr) ? FailVect ( hr, L"IsData" ) : S_OK;
    MSGF_GUARD_END(L"IsData")
}

STDMETHODIMP CMsgVect::IsField ( LONG index, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = ElemKindIs ( m_pStore, m_pVect, index, MSGF_KIND_ITEM, pVal );
    return FAILED(hr) ? FailVect ( hr, L"IsField" ) : S_OK;
    MSGF_GUARD_END(L"IsField")
}

STDMETHODIMP CMsgVect::IsList ( LONG index, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = ElemKindIs ( m_pStore, m_pVect, index, MSGF_KIND_LIST, pVal );
    return FAILED(hr) ? FailVect ( hr, L"IsList" ) : S_OK;
    MSGF_GUARD_END(L"IsList")
}

STDMETHODIMP CMsgVect::IsVect ( LONG index, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = ElemKindIs ( m_pStore, m_pVect, index, MSGF_KIND_VECT, pVal );
    return FAILED(hr) ? FailVect ( hr, L"IsVect" ) : S_OK;
    MSGF_GUARD_END(L"IsVect")
}

STDMETHODIMP CMsgVect::Truncate ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"Truncate" );

    HRESULT hr = m_pVect->Truncate ( );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"Truncate" );
    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Truncate")
}

STDMETHODIMP CMsgVect::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"Count" );

    unsigned int u = 0;
    HRESULT hr = m_pVect->GetCount ( &u );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"Count" );
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"Count")
}

STDMETHODIMP CMsgVect::get_TypeAt ( LONG index, LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"TypeAt" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"TypeAt" );

    // THE RANGE CHECK IS THIS TIER'S. The facade's GetTypeAt is a SAFE PROBE:
    // an index past the end answers MSGF_TYPE_NULL and S_OK, which is a sensible
    // thing for a C++ caller that wants to ask without branching first. Up here
    // it would make "past the end" and "an element holding no value" the same
    // answer, and every other indexed member of this interface -- Item, IsData,
    // NameAt -- reports msgcRange. So the count is asked first.
    unsigned int uCount = 0;
    HRESULT hr = m_pVect->GetCount ( &uCount );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"TypeAt" );
    if ( (unsigned int)index >= uCount ) return FailVect ( MSGC_E_RANGE, L"TypeAt" );

    unsigned char uType = MSGF_TYPE_NULL;
    hr = m_pVect->GetTypeAt ( (unsigned int)index, &uType );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"TypeAt" );
    *pVal = (LONG)uType;
    return S_OK;
    MSGF_GUARD_END(L"TypeAt")
}

STDMETHODIMP CMsgVect::get_NameAt ( LONG index, BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"NameAt" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"NameAt" );

    unsigned int cch = 0;
    HRESULT hr = m_pVect->GetNameAt ( (unsigned int)index, NULL, &cch );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"NameAt" );

    if ( cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = m_pVect->GetNameAt ( (unsigned int)index, &buf[0], &cch );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"NameAt" );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"NameAt")
}

STDMETHODIMP CMsgVect::ListAt ( LONG index, IMsgListCom **ppList )
{
    if ( ppList == NULL ) return E_POINTER;
    *ppList = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"ListAt" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"ListAt" );

    IMsgList *pList = NULL;
    HRESULT hr = m_pVect->GetListAt ( (unsigned int)index, &pList );
    if ( hr == MSGF_E_TYPE ) return FailVect ( MSGC_E_NOT_LIST, L"ListAt" );
    if ( FAILED(hr) )        return FailVect ( FromFacade ( hr ), L"ListAt" );

    return CMsgList::Make ( m_pStore, pList, ppList );
    MSGF_GUARD_END(L"ListAt")
}

STDMETHODIMP CMsgVect::VectAt ( LONG index, IMsgVectCom **ppVect )
{
    if ( ppVect == NULL ) return E_POINTER;
    *ppVect = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"VectAt" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"VectAt" );

    IMsgVect *pInner = NULL;
    HRESULT hr = m_pVect->GetVectAt ( (unsigned int)index, &pInner );
    if ( hr == MSGF_E_TYPE ) return FailVect ( MSGC_E_NOT_VECT, L"VectAt" );
    if ( FAILED(hr) )        return FailVect ( FromFacade ( hr ), L"VectAt" );

    return CMsgVect::Make ( m_pStore, pInner, ppVect );
    MSGF_GUARD_END(L"VectAt")
}

STDMETHODIMP CMsgVect::RemoveAt ( LONG index, VARIANT_BOOL *pRemoved )
{
    if ( pRemoved != NULL ) *pRemoved = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"RemoveAt" );
    if ( index < 0 ) return FailVect ( MSGC_E_RANGE, L"RemoveAt" );

    HRESULT hr = m_pVect->DeleteAt ( (unsigned int)index );
    if ( hr == MSGF_E_RANGE ) return S_OK;
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"RemoveAt" );

    m_pStore->BumpMutation();
    if ( pRemoved != NULL ) *pRemoved = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"RemoveAt")
}

STDMETHODIMP CMsgVect::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    if ( m_pStore == NULL || m_pVect == NULL ) return FailVect ( MSGC_E_CLOSED, L"_NewEnum" );

    unsigned int uCount = 0;
    HRESULT hr = m_pVect->GetCount ( &uCount );
    if ( FAILED(hr) ) return FailVect ( FromFacade ( hr ), L"_NewEnum" );

    std::vector<ATL::CComVariant> values;
    values.reserve ( uCount );
    for ( unsigned int i = 0; i < uCount; ++i )
    {
        ATL::CComVariant v;
        if ( FAILED ( ReadElem ( m_pVect, i, &v ) ) ) break;
        values.push_back ( v );
    }

    return MakeVariantEnum ( values, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}
