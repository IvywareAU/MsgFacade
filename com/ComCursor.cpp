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
// ComCursor.cpp -- CMsgCursor and the snapshot enumerators.
#include "stdafx.h"
#include "ComCursor.h"
#include "ComColl.h"
#include "ComField.h"

using namespace msgf;

static inline HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR bsArg = NULL )
{
    return ComFail ( IID_IMsgCursorCom, hr, wszCall, bsArg );
}

CMsgCursor::CMsgCursor ( )
    : m_pStore ( NULL ), m_pOwner ( NULL ), m_pCurs ( NULL )
    , m_uScope ( MSGF_SCOPE_CHILD ), m_lSeq ( 0 ), m_uIndex ( 0 )
{
}

void CMsgCursor::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pCurs  != NULL ) { m_pCurs->Release();  m_pCurs  = NULL; }
        if ( m_pOwner != NULL ) { m_pOwner->Release(); m_pOwner = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgCursor::Make ( CMsgStore *pStore, IMsgNode *pOwner
                         , unsigned int uScope, IMsgCursorCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pOwner == NULL || pStore->Store() == NULL ) return E_POINTER;

    // The owner is re-opened by path so this object owns its own reference and
    // outlives the field it was asked of.
    ATL::CComBSTR bsPath;
    HRESULT hr = ReadString ( pOwner, &IMsgNode::GetPath, bsPath );
    if ( FAILED(hr) ) return FromFacade ( hr );

    CNodePtr spOwn;
    hr = pStore->Store()->NodeFromPath ( ( bsPath != NULL ) ? bsPath : L"", &spOwn );
    if ( FAILED(hr) ) return FromFacade ( hr );

    CCursorPtr spCurs;
    hr = spOwn->OpenCursor ( uScope, &spCurs );
    if ( FAILED(hr) ) return FromFacade ( hr );

    ATL::CComObject<CMsgCursor> *pObj = NULL;
    hr = ATL::CComObject<CMsgCursor>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore = pStore;
    pObj->m_pOwner = spOwn.Detach();
    pObj->m_pCurs  = spCurs.Detach();
    pObj->m_uScope = uScope;
    pObj->m_lSeq   = pStore->MutationSeq();
    pObj->m_uIndex = 0;
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    hr = pObj->QueryInterface ( IID_IMsgCursorCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// The staleness check.
//
// A cursor holds a real position in the store, so a mutation anywhere in that
// store invalidates it -- the facade says so, and cannot do otherwise without
// making every step O(depth). What it CAN do is notice, which is what the
// store's mutation counter is for: rebuild the cursor and seek back to where
// this one was.
//
// The index is a position, not an identity: if the element this cursor was on
// has been deleted, the seek lands on whatever took its place. That is the
// honest behaviour for a positional object, and it is why For Each snapshots.
// ---------------------------------------------------------------------------
HRESULT CMsgCursor::Bind ( )
{
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return MSGC_E_CLOSED;
    if ( m_pOwner == NULL ) return MSGC_E_STALE;

    const LONG lNow = m_pStore->MutationSeq();
    if ( lNow == m_lSeq && m_pCurs != NULL )
        return S_OK;

    CCursorPtr spCurs;
    HRESULT hr = m_pOwner->OpenCursor ( m_uScope, &spCurs );
    if ( FAILED(hr) ) return FromFacade ( hr );

    // Seek back. Past the end after a delete is not a failure: the cursor is
    // simply at the end, which is what EndOfCursor will then report.
    if ( m_uIndex != 0 )
        spCurs->GotoIndex ( m_uIndex );

    if ( m_pCurs != NULL ) m_pCurs->Release();
    m_pCurs = spCurs.Detach();
    m_lSeq  = lNow;
    return S_OK;
}

STDMETHODIMP CMsgCursor::get_Field ( IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Field" );

    // The bridge out of a walk: a path-based node, which survives the mutation
    // that would invalidate this cursor. It used to be a DETACHED copy here --
    // readable and not writable -- because that was all the flat ABI could
    // hand back from a cursor.
    IMsgNode *pNode = NULL;
    hr = m_pCurs->GetNode ( &pNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Field" );

    return CMsgStore::MakeField ( m_pStore, pNode, ppField );
    MSGF_GUARD_END(L"Field")
}

STDMETHODIMP CMsgCursor::Next ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Next" );

    // S_FALSE is "that walked off the end" -- a success. The core's own
    // operator++ RAISES there, which is the trap the facade closed.
    //
    // AND IT IS PASSED ON, which it briefly was not: swallowing it into S_OK
    // costs a caller the one cheap way of learning that a walk has finished
    // without a second call to EndOfCursor, and it is the answer this method has
    // always given.
    hr = m_pCurs->Next ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Next" );

    unsigned int u = 0;
    if ( SUCCEEDED ( m_pCurs->GetIndex ( &u ) ) ) m_uIndex = u;
    return hr;                                  // S_OK, or S_FALSE at the end
    MSGF_GUARD_END(L"Next")
}

STDMETHODIMP CMsgCursor::Seek ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Seek" );

    hr = m_pCurs->Rewind ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Seek" );
    m_uIndex = 0;
    return S_OK;
    MSGF_GUARD_END(L"Seek")
}

STDMETHODIMP CMsgCursor::GotoName ( BSTR name, VARIANT_BOOL *pFound )
{
    if ( pFound == NULL ) return E_POINTER;
    *pFound = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"GotoName", name );

    // A miss leaves the cursor where it was, and is False rather than an error.
    hr = m_pCurs->GotoName ( Str ( name ) );
    if ( hr == MSGF_E_NO_ITEM ) return S_OK;
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"GotoName", name );

    unsigned int u = 0;
    if ( SUCCEEDED ( m_pCurs->GetIndex ( &u ) ) ) m_uIndex = u;
    *pFound = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"GotoName")
}

STDMETHODIMP CMsgCursor::GotoIndex ( LONG index, VARIANT_BOOL *pFound )
{
    if ( pFound == NULL ) return E_POINTER;
    *pFound = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"GotoIndex" );
    if ( index < 0 ) return S_OK;

    hr = m_pCurs->GotoIndex ( (unsigned int)index );
    if ( hr == MSGF_E_RANGE ) return S_OK;
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"GotoIndex" );

    m_uIndex = (unsigned int)index;
    *pFound  = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"GotoIndex")
}

STDMETHODIMP CMsgCursor::get_EndOfCursor ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_TRUE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"EndOfCursor" );

    int bEnd = 1;
    hr = m_pCurs->IsEnd ( &bEnd );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"EndOfCursor" );
    *pVal = bEnd ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"EndOfCursor")
}

// "On the first element", which is what a script means by it. Note the core's
// own IsEoCursor answers TRUE while standing ON the last element -- a loop
// driven by that visits every element but the last, and then raises. Neither
// end of this interface has that shape.
STDMETHODIMP CMsgCursor::get_StartOfCursor ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"StartOfCursor" );

    int bEnd = 1;
    if ( FAILED ( m_pCurs->IsEnd ( &bEnd ) ) ) return Fail ( MSGC_E_KERNEL, L"StartOfCursor" );

    unsigned int u = 0;
    if ( !bEnd && SUCCEEDED ( m_pCurs->GetIndex ( &u ) ) && u == 0 )
        *pVal = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"StartOfCursor")
}

STDMETHODIMP CMsgCursor::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Count" );

    unsigned int u = 0;
    hr = m_pCurs->GetCount ( &u );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Count" );
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"Count")
}

STDMETHODIMP CMsgCursor::get_Index ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Index" );

    // OFF THE END IS -1, NOT AN ERROR. The facade answers MSGF_E_RANGE for a
    // cursor that has walked past the last element, which is right for it --
    // there is no index to give. At this tier the position is a PROPERTY, and a
    // property that raises for a legitimate state is unusable from a scripting
    // host: `If c.Index = -1` is what a caller wants to write, and a -1 cannot
    // be mistaken for a position the way 0 or Count can.
    unsigned int u = 0;
    hr = m_pCurs->GetIndex ( &u );
    if ( hr == MSGF_E_RANGE )
    {
        *pVal = -1;
        return S_OK;
    }
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Index" );
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"Index")
}

STDMETHODIMP CMsgCursor::get_Name ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Name" );

    unsigned int cch = 0;
    hr = m_pCurs->GetName ( NULL, &cch );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Name" );

    if ( cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = m_pCurs->GetName ( &buf[0], &cch );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Name" );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"Name")
}

static HRESULT CursorKindIs ( IMsgCursor *pCurs, unsigned int uWant, VARIANT_BOOL *pVal )
{
    unsigned int uKind = 0;
    HRESULT hr = pCurs->GetKind ( &uKind );
    if ( FAILED(hr) ) return FromFacade ( hr );
    *pVal = ( uKind == uWant ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgCursor::get_IsItem ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsItem" );
    hr = CursorKindIs ( m_pCurs, MSGF_KIND_ITEM, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsItem" ) : S_OK;
    MSGF_GUARD_END(L"IsItem")
}

STDMETHODIMP CMsgCursor::get_IsList ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsList" );
    hr = CursorKindIs ( m_pCurs, MSGF_KIND_LIST, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsList" ) : S_OK;
    MSGF_GUARD_END(L"IsList")
}

STDMETHODIMP CMsgCursor::get_IsVect ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsVect" );
    hr = CursorKindIs ( m_pCurs, MSGF_KIND_VECT, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsVect" ) : S_OK;
    MSGF_GUARD_END(L"IsVect")
}

// Deleting through the cursor is the ONE mutation a cursor survives: the facade
// leaves it positioned on the first element rather than on nothing, because the
// core's own delete leaves it on no element at all and every accessor would
// then fail. Any OTHER cursor on this store is now stale -- and will notice,
// because this bumps the counter they all watch.
STDMETHODIMP CMsgCursor::Delete ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Delete" );

    hr = m_pCurs->Delete ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Delete" );

    m_pStore->BumpMutation();
    m_lSeq   = m_pStore->MutationSeq();   // this cursor is the one that survives
    m_uIndex = 0;
    return S_OK;
    MSGF_GUARD_END(L"Delete")
}

STDMETHODIMP CMsgCursor::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"_NewEnum" );

    return MakeFieldEnum ( m_pStore, m_pOwner, m_uScope, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}

// ===========================================================================
// The enumerators.
//
// Both are SNAPSHOTS. For Each's contract is one pass over a fixed set, and the
// cursor underneath is invalidated by any mutation -- so collecting first costs
// one walk and makes
//
//      For Each f In node.Descendants : node.Delete f.Name : Next
//
// legal, which is what a script author will write whether or not it is safe.
// ===========================================================================
namespace {

class ATL_NO_VTABLE CVariantEnum
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public IEnumVARIANT
{
  public:
    BEGIN_COM_MAP(CVariantEnum)
        COM_INTERFACE_ENTRY(IEnumVARIANT)
    END_COM_MAP()

    std::vector<ATL::CComVariant> m_values;
    size_t                        m_iNext{0};

    STDMETHOD(Next) ( ULONG celt, VARIANT *rgVar, ULONG *pCeltFetched )
    {
        if ( rgVar == NULL ) return E_POINTER;
        ULONG nFetched = 0;
        for ( ; nFetched < celt && m_iNext < m_values.size(); ++nFetched, ++m_iNext )
        {
            ::VariantInit ( &rgVar[nFetched] );
            ::VariantCopy ( &rgVar[nFetched], &m_values[m_iNext] );
        }
        if ( pCeltFetched != NULL ) *pCeltFetched = nFetched;
        return ( nFetched == celt ) ? S_OK : S_FALSE;
    }

    STDMETHOD(Skip) ( ULONG celt )
    {
        const size_t nLeft = m_values.size() - m_iNext;
        if ( (size_t)celt > nLeft ) { m_iNext = m_values.size(); return S_FALSE; }
        m_iNext += celt;
        return S_OK;
    }

    STDMETHOD(Reset) ( ) { m_iNext = 0; return S_OK; }

    STDMETHOD(Clone) ( IEnumVARIANT **ppEnum )
    {
        if ( ppEnum == NULL ) return E_POINTER;
        *ppEnum = NULL;

        ATL::CComObject<CVariantEnum> *pObj = NULL;
        HRESULT hr = ATL::CComObject<CVariantEnum>::CreateInstance ( &pObj );
        if ( FAILED(hr) ) return hr;

        pObj->AddRef();
        pObj->m_values = m_values;
        pObj->m_iNext  = m_iNext;
        hr = pObj->QueryInterface ( IID_IEnumVARIANT, (void**)ppEnum );
        pObj->Release();
        return hr;
    }
};

} // namespace

HRESULT MakeVariantEnum ( const std::vector<ATL::CComVariant>& values, IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    ATL::CComObject<CVariantEnum> *pObj = NULL;
    HRESULT hr = ATL::CComObject<CVariantEnum>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_values = values;
    hr = pObj->QueryInterface ( IID_IUnknown, (void**)ppUnk );
    pObj->Release();
    return hr;
}

// Every child of `pOwner` in `uScope`, as field objects, collected now.
HRESULT MakeFieldEnum ( CMsgStore *pStore, IMsgNode *pOwner
                      , unsigned int uScope, IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;
    if ( pStore == NULL || pOwner == NULL ) return E_POINTER;

    unsigned int uCount = 0;
    HRESULT hr = pOwner->GetCount ( uScope, &uCount );
    if ( hr == MSGF_E_TYPE ) uCount = 0;             // a container has no children
    else if ( FAILED(hr) ) return FromFacade ( hr );

    std::vector<ATL::CComVariant> values;
    values.reserve ( uCount );

    for ( unsigned int i = 0; i < uCount; ++i )
    {
        IMsgNode *pChild = NULL;
        if ( FAILED ( pOwner->GetChildAt ( uScope, i, &pChild ) ) )
            continue;

        ATL::CComPtr<IMsgFieldCom> spField;
        if ( FAILED ( CMsgStore::MakeField ( pStore, pChild, &spField ) ) )
            continue;

        ATL::CComVariant v;
        v.vt      = VT_DISPATCH;
        v.pdispVal = spField.Detach();      // the VARIANT owns it now
        values.push_back ( v );
    }

    return MakeVariantEnum ( values, ppUnk );
}
