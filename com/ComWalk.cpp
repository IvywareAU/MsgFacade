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
// ComWalk.cpp -- CMsgRecurs, the recursive subtree walker.
#include "stdafx.h"
#include "ComWalk.h"
#include "ComColl.h"
#include "ComField.h"

using namespace msgf;

static inline HRESULT Fail ( HRESULT hr, LPCWSTR wszCall )
{
    return ComFail ( IID_IMsgRecursCom, hr, wszCall, NULL );
}

CMsgRecurs::CMsgRecurs ( ) : m_pStore ( NULL ), m_pWalk ( NULL ) { }

void CMsgRecurs::FinalRelease ( )
{
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pWalk != NULL ) { m_pWalk->Release(); m_pWalk = NULL; }
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgRecurs::Make ( CMsgStore *pStore, IMsgNode *pRoot, IMsgRecursCom **ppOut )
{
    if ( ppOut == NULL ) return E_POINTER;
    *ppOut = NULL;
    if ( pStore == NULL || pRoot == NULL ) return E_POINTER;

    ATL::CComBSTR bsRoot;
    HRESULT hr = ReadString ( pRoot, &IMsgNode::GetPath, bsRoot );
    if ( FAILED(hr) ) return FromFacade ( hr );

    CWalkerPtr spWalk;
    hr = pRoot->OpenWalker ( MSGF_SCOPE_CHILD, &spWalk );
    if ( FAILED(hr) ) return FromFacade ( hr );

    ATL::CComObject<CMsgRecurs> *pObj = NULL;
    hr = ATL::CComObject<CMsgRecurs>::CreateInstance ( &pObj );
    if ( FAILED(hr) ) return hr;

    pObj->AddRef();
    pObj->m_pStore    = pStore;
    pObj->m_pWalk     = spWalk.Detach();
    pObj->m_bsRootPath = bsRoot;
    pObj->m_spStoreKeepAlive = pStore->GetUnknown();
    pObj->Track();                          // the walk starts ON the first stop
    hr = pObj->QueryInterface ( IID_IMsgRecursCom, (void**)ppOut );
    pObj->Release();
    return hr;
}

HRESULT CMsgRecurs::Bind ( ) const
{
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return MSGC_E_CLOSED;
    if ( m_pWalk  == NULL ) return MSGC_E_STALE;
    return S_OK;
}

// ---------------------------------------------------------------------------
// Keeping the ancestry, and why.
//
// The walker reports WHERE IT IS -- kind, name, depth -- and nothing else. It
// deliberately hands back no node: a node is a route, and re-deriving one per
// step would make the walk O(n*depth), which is the cost the walker exists to
// avoid.
//
// So the path is assembled here from what the walker does report. depth() is
// 0-based, so at a stop of depth d the first d entries of this vector are still
// the ancestry and the name goes on the end. Nine lines, once, and what comes
// out of it OUTLIVES the walk -- which the walker's own position does not.
// ---------------------------------------------------------------------------
void CMsgRecurs::Track ( )
{
    if ( m_pWalk == NULL ) return;

    int bEnd = 1;
    if ( FAILED ( m_pWalk->IsEnd ( &bEnd ) ) || bEnd )
        return;                             // finished: leave the last stop in place

    int nDepth = 0;
    if ( FAILED ( m_pWalk->GetDepth ( &nDepth ) ) || nDepth < 0 )
        return;

    unsigned int cch = 0;
    if ( FAILED ( m_pWalk->GetName ( NULL, &cch ) ) || cch <= 1 )
        return;

    std::vector<wchar_t> buf ( cch );
    if ( FAILED ( m_pWalk->GetName ( &buf[0], &cch ) ) )
        return;

    m_ancestry.resize ( (size_t)nDepth );
    m_ancestry.push_back ( &buf[0] );
}

HRESULT CMsgRecurs::CurrentPath ( ATL::CComBSTR& out ) const
{
    if ( m_ancestry.empty() ) return MSGC_E_NO_FIELD;

    std::wstring s ( ( m_bsRootPath != NULL ) ? (LPCWSTR)m_bsRootPath : L"" );
    for ( size_t i = 0; i < m_ancestry.size(); ++i )
    {
        s += L'.';
        s += m_ancestry[i];
    }
    out = s.c_str();
    return S_OK;
}

STDMETHODIMP CMsgRecurs::MoveNext ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"MoveNext" );

    // S_FALSE is "the walk is finished", which AtEnd then reports -- not a
    // failure, and not something to raise on.
    hr = m_pWalk->Next ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"MoveNext" );

    Track();
    return S_OK;
    MSGF_GUARD_END(L"MoveNext")
}

// ---------------------------------------------------------------------------
// Push has THREE answers, and the difference is the whole reason a generic
// walk can be written without knowing the shape of the tree:
//
//   S_OK        descended; the walker is now inside it
//   S_FALSE     an item, but it has nothing under it -- nothing happened
//   MSGF_E_TYPE not something that can be descended into at all (a list, a
//               vect, a bare value)
//
// The core's own Push THREW for the third case -- "Attempt to push non-
// P2PmsgItem environment" -- which is why the old loop here had to test
// IsField before every descent. Only S_OK moves the walker, so a caller that
// ignores the distinction still walks correctly, and the depth reported back
// says which happened.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgRecurs::Push ( LONG *pDepth )
{
    if ( pDepth != NULL ) *pDepth = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Push" );

    int nDepth = 0;
    hr = m_pWalk->Push ( &nDepth );
    if ( hr == MSGF_E_TYPE )
    {
        // Not descendable. Answer the depth unchanged rather than failing: a
        // caller pushing at every stop is the normal case.
        if ( SUCCEEDED ( m_pWalk->GetDepth ( &nDepth ) ) && pDepth != NULL )
            *pDepth = (LONG)nDepth;
        return S_OK;
    }
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Push" );

    if ( pDepth != NULL ) *pDepth = (LONG)nDepth;
    return S_OK;
    MSGF_GUARD_END(L"Push")
}

STDMETHODIMP CMsgRecurs::Pop ( LONG *pDepth )
{
    if ( pDepth != NULL ) *pDepth = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Pop" );

    int nDepth = 0;
    hr = m_pWalk->Pop ( &nDepth );
    if ( hr == MSGF_E_RANGE )
        return Fail ( MSGC_E_RANGE, L"Pop" );      // already at the outermost
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Pop" );

    if ( pDepth != NULL ) *pDepth = (LONG)nDepth;
    Track();
    return S_OK;
    MSGF_GUARD_END(L"Pop")
}

// Abandon every pushed level at once and resume at the outermost -- and the
// walk CONTINUES from there rather than ending, which is the one behavioural
// difference from the core's own Break (that one ran the top cursor off its end
// as well, so the walk was over).
STDMETHODIMP CMsgRecurs::Break ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Break" );

    hr = m_pWalk->Break ( );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Break" );

    Track();
    return S_OK;
    MSGF_GUARD_END(L"Break")
}

STDMETHODIMP CMsgRecurs::get_AtEnd ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_TRUE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"AtEnd" );

    int bEnd = 1;
    hr = m_pWalk->IsEnd ( &bEnd );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"AtEnd" );
    *pVal = bEnd ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"AtEnd")
}

STDMETHODIMP CMsgRecurs::get_Name ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Name" );

    unsigned int cch = 0;
    hr = m_pWalk->GetName ( NULL, &cch );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Name" );

    if ( cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    hr = m_pWalk->GetName ( &buf[0], &cch );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Name" );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"Name")
}

// New at this tier: how deep the walk currently is, 0 at the outermost level.
// The old walker had it internally and did not publish it.
STDMETHODIMP CMsgRecurs::get_Depth ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Depth" );

    int nDepth = 0;
    hr = m_pWalk->GetDepth ( &nDepth );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Depth" );
    *pVal = (LONG)nDepth;
    return S_OK;
    MSGF_GUARD_END(L"Depth")
}

static HRESULT WalkKindIs ( IMsgWalker *pWalk, unsigned int uWant, VARIANT_BOOL *pVal )
{
    unsigned int uKind = 0;
    HRESULT hr = pWalk->GetKind ( &uKind );
    if ( FAILED(hr) ) return FromFacade ( hr );
    *pVal = ( uKind == uWant ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgRecurs::get_IsField ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsField" );
    hr = WalkKindIs ( m_pWalk, MSGF_KIND_ITEM, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsField" ) : S_OK;
    MSGF_GUARD_END(L"IsField")
}

STDMETHODIMP CMsgRecurs::get_IsList ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsList" );
    hr = WalkKindIs ( m_pWalk, MSGF_KIND_LIST, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsList" ) : S_OK;
    MSGF_GUARD_END(L"IsList")
}

STDMETHODIMP CMsgRecurs::get_IsVect ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsVect" );
    hr = WalkKindIs ( m_pWalk, MSGF_KIND_VECT, pVal );
    return FAILED(hr) ? Fail ( hr, L"IsVect" ) : S_OK;
    MSGF_GUARD_END(L"IsVect")
}

// The current stop's path -- and, unlike everything else on this object, it
// stays true after the walk has moved on. New at this tier.
STDMETHODIMP CMsgRecurs::get_Path ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Path" );

    ATL::CComBSTR bs;
    hr = CurrentPath ( bs );
    if ( FAILED(hr) ) return Fail ( hr, L"Path" );
    *pVal = bs.Detach();
    return S_OK;
    MSGF_GUARD_END(L"Path")
}

STDMETHODIMP CMsgRecurs::get_Field ( IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Field" );

    ATL::CComBSTR bs;
    hr = CurrentPath ( bs );
    if ( FAILED(hr) ) return Fail ( hr, L"Field" );

    IMsgNode *pNode = NULL;
    hr = m_pStore->Store()->NodeFromPath ( ( bs != NULL ) ? bs : L"", &pNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Field" );

    return CMsgStore::MakeField ( m_pStore, pNode, ppField );
    MSGF_GUARD_END(L"Field")
}

STDMETHODIMP CMsgRecurs::get_List ( IMsgListCom **ppList )
{
    if ( ppList == NULL ) return E_POINTER;
    *ppList = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"List" );

    ATL::CComBSTR bs;
    hr = CurrentPath ( bs );
    if ( FAILED(hr) ) return Fail ( hr, L"List" );

    CNodePtr spNode;
    hr = m_pStore->Store()->NodeFromPath ( ( bs != NULL ) ? bs : L"", &spNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"List" );

    hr = CMsgList::MakeFromSelf ( m_pStore, spNode, ppList );
    return FAILED(hr) ? Fail ( hr, L"List" ) : S_OK;
    MSGF_GUARD_END(L"List")
}

STDMETHODIMP CMsgRecurs::get_Vector ( IMsgVectCom **ppVect )
{
    if ( ppVect == NULL ) return E_POINTER;
    *ppVect = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Vector" );

    ATL::CComBSTR bs;
    hr = CurrentPath ( bs );
    if ( FAILED(hr) ) return Fail ( hr, L"Vector" );

    CNodePtr spNode;
    hr = m_pStore->Store()->NodeFromPath ( ( bs != NULL ) ? bs : L"", &spNode );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Vector" );

    hr = CMsgVect::MakeFromSelf ( m_pStore, spNode, ppVect );
    return FAILED(hr) ? Fail ( hr, L"Vector" ) : S_OK;
    MSGF_GUARD_END(L"Vector")
}
