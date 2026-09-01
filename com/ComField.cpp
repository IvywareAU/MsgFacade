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
// ComField.cpp -- implementation of coclass MsgField.
#include "stdafx.h"
#include "ComField.h"
#include "ComColl.h"
#include "ComCursor.h"
#include "ComWalk.h"

using namespace msgf;

// {9E4B7C21-0D53-4A8E-B6F1-72C0D4E58A31}
extern "C" const GUID IID_IMsgFieldPrivate =
    { 0x9e4b7c21, 0x0d53, 0x4a8e, { 0xb6, 0xf1, 0x72, 0xc0, 0xd4, 0xe5, 0x8a, 0x31 } };

static inline HRESULT Fail ( HRESULT hr, LPCWSTR wszCall, BSTR bsArg1 = NULL, BSTR bsArg2 = NULL )
{
    return ComFail ( IID_IMsgFieldCom, hr, wszCall, bsArg1, bsArg2 );
}

// A node that is not there answers MSGF_E_NO_ITEM, which at this tier is
// msgcStale when it is THIS node and msgcNoField when it is one being looked
// up. The two are different sentences, so the mapping is per call site rather
// than in FromFacade.
//
// TAKES A FACADE CODE, and every helper it is fed from answers one -- see the
// note on FromFacade in ComUtil.cpp, which is where the reason lives: the two
// code spaces collide numerically, so converting twice is not a no-op, it is a
// silent rewrite into a different error.
static inline HRESULT AsStale ( HRESULT hrFacade )
{
    return ( hrFacade == MSGF_E_NO_ITEM ) ? MSGC_E_STALE : FromFacade ( hrFacade );
}

CMsgField::CMsgField ( )
    : m_pStore ( NULL ), m_pNode ( NULL )
{
}

void CMsgField::Init ( CMsgStore *pStore, IMsgNode *pNode )
{
    m_pStore = pStore;
    m_pNode  = pNode;                   // taken
    if ( m_pStore != NULL )
        m_spStoreKeepAlive = m_pStore->GetUnknown();
}

void CMsgField::FinalRelease ( )
{
    // Under the lock: releasing a node is a call into the store, and the store
    // may be being torn down on another thread.
    if ( m_pStore != NULL )
    {
        CStoreLock lock ( m_pStore );
        if ( m_pNode != NULL ) { m_pNode->Release(); m_pNode = NULL; }
    }
    else if ( m_pNode != NULL )
    {
        m_pNode->Release();
        m_pNode = NULL;
    }
    m_pStore = NULL;
    m_spStoreKeepAlive.Release();
}

HRESULT CMsgField::Bind ( ) const
{
    if ( m_pStore == NULL || m_pStore->Store() == NULL ) return MSGC_E_CLOSED;
    if ( m_pNode  == NULL )                              return MSGC_E_STALE;
    return S_OK;
}

// ---------------------------------------------------------------------------
// value
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::get_Value ( VARIANT *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    ::VariantInit ( pVal );

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Value" );

    hr = ReadNodeValue ( m_pNode, pVal );
    return FAILED(hr) ? Fail ( AsStale ( hr ), L"Value" ) : S_OK;
    MSGF_GUARD_END(L"Value")
}

STDMETHODIMP CMsgField::put_Value ( VARIANT newVal )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Value" );

    hr = WriteNodeValue ( m_pNode, newVal );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Value" );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Value")
}

STDMETHODIMP CMsgField::get_Name ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Name" );

    ATL::CComBSTR bs;
    hr = ReadString ( m_pNode, &IMsgNode::GetName, bs );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Name" );
    *pVal = bs.Detach();
    return S_OK;
    MSGF_GUARD_END(L"Name")
}

// Text is the value AS A STRING whatever the node holds -- the spelling a
// script uses to print one. Value keeps the type; Text does not.
STDMETHODIMP CMsgField::get_Text ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Text" );

    ATL::CComVariant v;
    hr = ReadNodeValue ( m_pNode, &v );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Text" );

    if ( FAILED ( v.ChangeType ( VT_BSTR ) ) )
        return Fail ( MSGC_E_TYPE, L"Text" );

    *pVal = ::SysAllocString ( Str ( v.bstrVal ) );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"Text")
}

STDMETHODIMP CMsgField::put_Text ( BSTR newVal )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Text", newVal );

    hr = m_pNode->SetText ( Str ( newVal ) );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Text", newVal );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Text")
}

STDMETHODIMP CMsgField::get_DataType ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"DataType" );

    unsigned char uType = 0;
    hr = m_pNode->GetType ( &uType );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"DataType" );
    *pVal = (LONG)uType;
    return S_OK;
    MSGF_GUARD_END(L"DataType")
}

STDMETHODIMP CMsgField::get_TypeName ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"TypeName" );

    unsigned char uType = 0;
    hr = m_pNode->GetType ( &uType );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"TypeName" );

    IMsgLibrary *pLib = m_pStore->Library();
    unsigned int cch = 0;
    if ( pLib == NULL || FAILED ( pLib->TypeName ( uType, NULL, &cch ) ) || cch <= 1 )
    {
        *pVal = ::SysAllocString ( L"UNKNOWN" );
        return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    }

    std::vector<wchar_t> buf ( cch );
    if ( FAILED ( pLib->TypeName ( uType, &buf[0], &cch ) ) )
        return Fail ( MSGC_E_TYPE, L"TypeName" );

    *pVal = ::SysAllocString ( &buf[0] );
    return ( *pVal != NULL ) ? S_OK : E_OUTOFMEMORY;
    MSGF_GUARD_END(L"TypeName")
}

STDMETHODIMP CMsgField::get_IsNull ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsNull" );

    int bNull = 0;
    hr = m_pNode->IsNull ( &bNull );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"IsNull" );
    *pVal = bNull ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsNull")
}

// The path, in the grammar MsgStore.FieldAt takes: '.' before a child step,
// '@' before an attribute step, from the root. "" is the root itself.
STDMETHODIMP CMsgField::get_Path ( BSTR *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Path" );

    ATL::CComBSTR bs;
    hr = ReadString ( m_pNode, &IMsgNode::GetPath, bs );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Path" );
    *pVal = bs.Detach();
    return S_OK;
    MSGF_GUARD_END(L"Path")
}

STDMETHODIMP CMsgField::get_P2Pos ( LONGLONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"P2Pos" );

    unsigned long long pos = 0;
    hr = m_pNode->GetPos ( &pos );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"P2Pos" );
    *pVal = (LONGLONG)pos;
    return S_OK;
    MSGF_GUARD_END(L"P2Pos")
}

// ---------------------------------------------------------------------------
// children
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::Exists ( BSTR name, VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Exists", name );

    // Absence is an ANSWER, so this is the one lookup that does not fail for
    // it: S_FALSE from the facade, False here.
    hr = m_pNode->Exists ( MSGF_SCOPE_CHILD, Str ( name ) );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Exists", name );
    *pVal = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"Exists")
}

// ---------------------------------------------------------------------------
// Child REACHES A LIST AND A VECT NOW, which it could not before.
//
// It used to resolve through msgcore_field_child, which is SelectItem-based,
// and SelectItem THROWS on a container -- "a list is not an item" -- so
// field.Child("Samples") failed for a list named Samples that was plainly
// there, and ChildList / ChildVect existed for exactly that gap. The facade's
// GetChild is cursor-based and handles all three kinds, so Child is now the
// one lookup; ChildList and ChildVect remain as the TYPED spellings, which
// answer msgcNotList / msgcNotVect for a name of the wrong kind.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::Child ( BSTR name, IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Child", name );

    IMsgNode *pChild = NULL;
    hr = m_pNode->GetChild ( MSGF_SCOPE_CHILD, Str ( name ), &pChild );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Child", name );

    return CMsgStore::MakeField ( m_pStore, pChild, ppField );
    MSGF_GUARD_END(L"Child")
}

// By POSITION in the collection, for a caller walking children without a
// cursor. New at this tier: the flat ABI had no by-index child accessor.
STDMETHODIMP CMsgField::ChildAt ( LONG index, IMsgFieldCom **ppField )
{
    if ( ppField == NULL ) return E_POINTER;
    *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"ChildAt" );
    if ( index < 0 ) return Fail ( MSGC_E_RANGE, L"ChildAt" );

    IMsgNode *pChild = NULL;
    hr = m_pNode->GetChildAt ( MSGF_SCOPE_CHILD, (unsigned int)index, &pChild );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"ChildAt" );

    return CMsgStore::MakeField ( m_pStore, pChild, ppField );
    MSGF_GUARD_END(L"ChildAt")
}

STDMETHODIMP CMsgField::Declare ( BSTR name, VARIANT value, VARIANT_BOOL update,
                                  IMsgFieldCom **ppField )
{
    if ( ppField != NULL ) *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Declare", name );

    hr = CheckName ( name );
    if ( FAILED(hr) ) return Fail ( hr, L"Declare", name );

    IMsgNode *pChild = NULL;
    hr = DeclareVariant ( m_pNode, MSGF_SCOPE_CHILD, name, value
                        , ( update != VARIANT_FALSE ) ? 1 : 0, &pChild );
    if ( FAILED(hr) ) return Fail ( hr, L"Declare", name );

    m_pStore->BumpMutation();
    if ( ppField == NULL )
    {
        if ( pChild != NULL ) pChild->Release();
        return S_OK;
    }
    return CMsgStore::MakeField ( m_pStore, pChild, ppField );
    MSGF_GUARD_END(L"Declare")
}

STDMETHODIMP CMsgField::DeclareTyped ( BSTR name, VARIANT value, LONG dataType,
                                       VARIANT_BOOL update, IMsgFieldCom **ppField )
{
    if ( ppField != NULL ) *ppField = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"DeclareTyped", name );

    hr = CheckName ( name );
    if ( FAILED(hr) ) return Fail ( hr, L"DeclareTyped", name );
    if ( dataType < 0 || dataType > 255 ) return Fail ( E_INVALIDARG, L"DeclareTyped", name );

    IMsgNode *pChild = NULL;
    hr = DeclareTypedVariant ( m_pNode, MSGF_SCOPE_CHILD, name, value
                             , (unsigned char)dataType
                             , ( update != VARIANT_FALSE ) ? 1 : 0, &pChild );
    if ( FAILED(hr) ) return Fail ( hr, L"DeclareTyped", name );

    m_pStore->BumpMutation();
    if ( ppField == NULL )
    {
        if ( pChild != NULL ) pChild->Release();
        return S_OK;
    }
    return CMsgStore::MakeField ( m_pStore, pChild, ppField );
    MSGF_GUARD_END(L"DeclareTyped")
}

STDMETHODIMP CMsgField::Delete ( BSTR name, VARIANT_BOOL *pDeleted )
{
    if ( pDeleted != NULL ) *pDeleted = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Delete", name );

    hr = m_pNode->Delete ( MSGF_SCOPE_CHILD, Str ( name ) );
    if ( hr == MSGF_E_NO_ITEM )
        return S_OK;                    // "there was none" is an answer, not a failure
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"Delete", name );

    m_pStore->BumpMutation();
    if ( pDeleted != NULL ) *pDeleted = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"Delete")
}

STDMETHODIMP CMsgField::Truncate ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Truncate" );

    // SCOPED: this drops the children and leaves the attributes alone. The
    // core's own Truncate drops the descendants AND the attributes AND the
    // position stack, which is three effects for one verb.
    hr = m_pNode->Truncate ( MSGF_SCOPE_CHILD );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Truncate" );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Truncate")
}

STDMETHODIMP CMsgField::RenameChild ( BSTR oldName, BSTR newName, VARIANT_BOOL *pRenamed )
{
    if ( pRenamed != NULL ) *pRenamed = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"RenameChild", oldName, newName );

    hr = CheckName ( newName );
    if ( FAILED(hr) ) return Fail ( hr, L"RenameChild", oldName, newName );

    hr = m_pNode->Rename ( MSGF_SCOPE_CHILD, Str ( oldName ), Str ( newName ) );
    if ( hr == MSGF_E_NO_ITEM )
        return S_OK;                    // nothing of that name: False, not a failure
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"RenameChild", oldName, newName );

    m_pStore->BumpMutation();
    // S_FALSE is "renamed to the name it already had" -- a no-op, and still True
    // to a caller asking whether the child now has that name.
    if ( pRenamed != NULL ) *pRenamed = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"RenameChild")
}

STDMETHODIMP CMsgField::MoveChild ( IMsgFieldCom *destination, BSTR name, VARIANT_BOOL *pMoved )
{
    if ( pMoved != NULL ) *pMoved = VARIANT_FALSE;
    if ( destination == NULL ) return Fail ( E_POINTER, L"MoveChild", name );

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"MoveChild", name );

    // The destination must be provably one of OUR objects, in THIS store. Two
    // stores commonly have the same shape, so a foreign destination would not
    // fail -- it would move the child into the wrong heap.
    ATL::CComPtr<IMsgFieldPrivate> spPriv;
    if ( FAILED ( destination->QueryInterface ( IID_IMsgFieldPrivate, (void**)&spPriv ) ) ||
         spPriv->OwningStore() != m_pStore )
        return Fail ( MSGC_E_FOREIGN, L"MoveChild", name );

    IMsgNode *pDest = spPriv->BorrowNode();
    if ( pDest == NULL ) return Fail ( MSGC_E_STALE, L"MoveChild", name );

    // A MOVE TO WHERE THE CHILD ALREADY IS DOES NOTHING, and it has to be
    // stopped HERE rather than left to the layers below. The move is a
    // remove-and-re-add, and performing it with the same node as source and
    // destination does not churn the child's position -- measured, it LOSES the
    // child: the remove takes it out of the collection the re-add is about to
    // put it back into, and what comes back is not what went in.
    //
    // The old server over the flat ABI carried this guard and the IDL documented
    // the S_FALSE; the port dropped it. Restored, with the check made on the
    // node's POSITION, which is the only identity two references to one node are
    // guaranteed to agree on.
    unsigned long long posSelf = 0, posDest = 0;
    if ( SUCCEEDED ( m_pNode->GetPos ( &posSelf ) ) &&
         SUCCEEDED ( pDest  ->GetPos ( &posDest ) ) &&
         posSelf != 0 && posSelf == posDest )
        return S_FALSE;                 // pMoved stays False: nothing moved

    hr = m_pNode->Move ( MSGF_SCOPE_CHILD, Str ( name ), pDest );
    if ( hr == MSGF_E_NO_ITEM )
        return S_OK;
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"MoveChild", name );

    m_pStore->BumpMutation();
    if ( pMoved != NULL ) *pMoved = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"MoveChild")
}

// ---------------------------------------------------------------------------
// Retype takes a TYPE, not a value.
//
// It used to take a VARIANT and derive the type from it, because the flat ABI's
// retype entry points were one per type and a value was the only way to pick
// one. Retype seeds a zero value of the new type by definition -- there is no
// version of it that keeps the old one -- so a value argument was always a
// polite fiction, and passing 7 to make an INT32 read as though it stored 7.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::RetypeChild ( BSTR name, LONG dataType, VARIANT_BOOL *pRetyped )
{
    if ( pRetyped != NULL ) *pRetyped = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"RetypeChild", name );
    if ( dataType < 0 || dataType > 255 ) return Fail ( E_INVALIDARG, L"RetypeChild", name );

    hr = m_pNode->Retype ( MSGF_SCOPE_CHILD, Str ( name ), (unsigned char)dataType );
    if ( hr == MSGF_E_NO_ITEM )
        return S_OK;
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"RetypeChild", name );

    m_pStore->BumpMutation();
    if ( pRetyped != NULL ) *pRetyped = VARIANT_TRUE;
    return S_OK;
    MSGF_GUARD_END(L"RetypeChild")
}

// ---------------------------------------------------------------------------
// collections
//
// `create` is accepted and ignored, and the IDL says so: a collection is
// created by the first thing declared into it, one layer down, so there is no
// state here for the flag to select. It stays in the signature because every
// existing client passes it.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::Attributes ( VARIANT_BOOL /*create*/, IMsgAttrCom **ppAttr )
{
    if ( ppAttr == NULL ) return E_POINTER;
    *ppAttr = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Attributes" );

    return CMsgAttr::Make ( m_pStore, m_pNode, ppAttr );
    MSGF_GUARD_END(L"Attributes")
}

STDMETHODIMP CMsgField::Descendants ( VARIANT_BOOL /*create*/, IMsgDescCom **ppDesc )
{
    if ( ppDesc == NULL ) return E_POINTER;
    *ppDesc = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Descendants" );

    return CMsgDesc::Make ( m_pStore, m_pNode, ppDesc );
    MSGF_GUARD_END(L"Descendants")
}

STDMETHODIMP CMsgField::get_Cursor ( IMsgCursorCom **ppCurs )
{
    if ( ppCurs == NULL ) return E_POINTER;
    *ppCurs = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Cursor" );

    return CMsgCursor::Make ( m_pStore, m_pNode, MSGF_SCOPE_CHILD, ppCurs );
    MSGF_GUARD_END(L"Cursor")
}

STDMETHODIMP CMsgField::get_List ( IMsgListCom **ppList )
{
    if ( ppList == NULL ) return E_POINTER;
    *ppList = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"List" );

    // THIS node as a list, which only works when it IS one. A node's own
    // container-ness is asked with IsList first.
    ATL::CComBSTR bsName;
    hr = ReadString ( m_pNode, &IMsgNode::GetName, bsName );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"List" );

    // Wrapped in Fail(), which it was not: MakeFromSelf answers a code and sets
    // no IErrorInfo, so `Set l = f.List` on a node that is not a list arrived at
    // a scripting host as Err.Number = msgcNotList with an EMPTY
    // Err.Description. A failure a client cannot read is half a failure.
    hr = CMsgList::MakeFromSelf ( m_pStore, m_pNode, ppList );
    return FAILED(hr) ? Fail ( hr, L"List" ) : hr;
    MSGF_GUARD_END(L"List")
}

STDMETHODIMP CMsgField::get_Vector ( IMsgVectCom **ppVect )
{
    if ( ppVect == NULL ) return E_POINTER;
    *ppVect = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Vector" );

    hr = CMsgVect::MakeFromSelf ( m_pStore, m_pNode, ppVect );
    return FAILED(hr) ? Fail ( hr, L"Vector" ) : hr;
    MSGF_GUARD_END(L"Vector")
}

STDMETHODIMP CMsgField::DeclareList ( BSTR name, IMsgListCom **ppList )
{
    if ( ppList == NULL ) return E_POINTER;
    *ppList = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"DeclareList", name );

    hr = CheckName ( name );
    if ( FAILED(hr) ) return Fail ( hr, L"DeclareList", name );

    IMsgList *pList = NULL;
    hr = m_pNode->DeclareList ( MSGF_SCOPE_CHILD, name, &pList );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"DeclareList", name );

    m_pStore->BumpMutation();
    return CMsgList::Make ( m_pStore, pList, ppList );
    MSGF_GUARD_END(L"DeclareList")
}

STDMETHODIMP CMsgField::DeclareVect ( BSTR name, LONG count, LONG dataType,
                                      IMsgVectCom **ppVect )
{
    if ( ppVect == NULL ) return E_POINTER;
    *ppVect = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"DeclareVect", name );

    hr = CheckName ( name );
    if ( FAILED(hr) ) return Fail ( hr, L"DeclareVect", name );
    if ( count < 0 ) return Fail ( MSGC_E_RANGE, L"DeclareVect", name );
    if ( dataType < 0 || dataType > 255 ) return Fail ( E_INVALIDARG, L"DeclareVect", name );

    IMsgVect *pVect = NULL;
    hr = m_pNode->DeclareVect ( MSGF_SCOPE_CHILD, name, (unsigned int)count
                              , (unsigned char)dataType, &pVect );
    if ( FAILED(hr) ) return Fail ( FromFacade ( hr ), L"DeclareVect", name );

    m_pStore->BumpMutation();
    return CMsgVect::Make ( m_pStore, pVect, ppVect );
    MSGF_GUARD_END(L"DeclareVect")
}

STDMETHODIMP CMsgField::ChildList ( BSTR name, IMsgListCom **ppList )
{
    if ( ppList == NULL ) return E_POINTER;
    *ppList = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"ChildList", name );

    IMsgList *pList = NULL;
    hr = m_pNode->GetList ( MSGF_SCOPE_CHILD, Str ( name ), &pList );
    if ( hr == MSGF_E_TYPE ) return Fail ( MSGC_E_NOT_LIST, L"ChildList", name );
    if ( FAILED(hr) )        return Fail ( FromFacade ( hr ), L"ChildList", name );

    return CMsgList::Make ( m_pStore, pList, ppList );
    MSGF_GUARD_END(L"ChildList")
}

STDMETHODIMP CMsgField::ChildVect ( BSTR name, IMsgVectCom **ppVect )
{
    if ( ppVect == NULL ) return E_POINTER;
    *ppVect = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"ChildVect", name );

    IMsgVect *pVect = NULL;
    hr = m_pNode->GetVect ( MSGF_SCOPE_CHILD, Str ( name ), &pVect );
    if ( hr == MSGF_E_TYPE ) return Fail ( MSGC_E_NOT_VECT, L"ChildVect", name );
    if ( FAILED(hr) )        return Fail ( FromFacade ( hr ), L"ChildVect", name );

    return CMsgVect::Make ( m_pStore, pVect, ppVect );
    MSGF_GUARD_END(L"ChildVect")
}

STDMETHODIMP CMsgField::get_Walker ( IMsgRecursCom **ppWalker )
{
    if ( ppWalker == NULL ) return E_POINTER;
    *ppWalker = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Walker" );

    return CMsgRecurs::Make ( m_pStore, m_pNode, ppWalker );
    MSGF_GUARD_END(L"Walker")
}

// ---------------------------------------------------------------------------
// timestamp
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::get_Timestamp ( DATE *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = (DATE)0.0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Timestamp" );

    long long ts = 0;
    hr = m_pNode->GetTime ( &ts );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Timestamp" );
    *pVal = EpochToDate ( ts );
    return S_OK;
    MSGF_GUARD_END(L"Timestamp")
}

STDMETHODIMP CMsgField::put_Timestamp ( DATE newVal )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Timestamp" );

    long long out = 0;
    hr = m_pNode->SetTime ( DateToEpoch ( newVal ), &out );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Timestamp" );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Timestamp")
}

// Stamp with NOW, which is the whole reason a caller wants a timestamp at all.
STDMETHODIMP CMsgField::Touch ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Touch" );

    long long out = 0;
    hr = m_pNode->SetTime ( -1, &out );          // -1 == now
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Touch" );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"Touch")
}

// ---------------------------------------------------------------------------
// shape
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::get_Count ( LONG *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = 0;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"Count" );

    unsigned int u = 0;
    hr = m_pNode->GetCount ( MSGF_SCOPE_CHILD, &u );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"Count" );
    *pVal = (LONG)u;
    return S_OK;
    MSGF_GUARD_END(L"Count")
}

// One helper for the four kind questions: they differ only in what they compare
// the answer against.
static HRESULT KindIs ( IMsgNode *pNode, unsigned int uWant, VARIANT_BOOL *pVal )
{
    unsigned int uKind = 0;
    HRESULT hr = pNode->GetKind ( &uKind );
    if ( FAILED(hr) ) return hr;
    *pVal = ( uKind == uWant ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

STDMETHODIMP CMsgField::get_IsList ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsList" );
    hr = KindIs ( m_pNode, MSGF_KIND_LIST, pVal );
    return FAILED(hr) ? Fail ( AsStale ( hr ), L"IsList" ) : S_OK;
    MSGF_GUARD_END(L"IsList")
}

STDMETHODIMP CMsgField::get_IsVect ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsVect" );
    hr = KindIs ( m_pNode, MSGF_KIND_VECT, pVal );
    return FAILED(hr) ? Fail ( AsStale ( hr ), L"IsVect" ) : S_OK;
    MSGF_GUARD_END(L"IsVect")
}

// "Does this node carry any attributes / any children" -- which is a count
// question, and the two collections answer it separately.
STDMETHODIMP CMsgField::get_IsAttributed ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsAttributed" );

    unsigned int u = 0;
    hr = m_pNode->GetCount ( MSGF_SCOPE_ATTR, &u );
    // A container has no attribute collection at all, which is False rather
    // than an error to a caller asking whether it has any.
    if ( hr == MSGF_E_TYPE ) return S_OK;
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"IsAttributed" );
    *pVal = ( u != 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsAttributed")
}

STDMETHODIMP CMsgField::get_IsDescendant ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsDescendant" );

    unsigned int u = 0;
    hr = m_pNode->GetCount ( MSGF_SCOPE_CHILD, &u );
    if ( hr == MSGF_E_TYPE ) return S_OK;
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"IsDescendant" );
    *pVal = ( u != 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsDescendant")
}

// ---------------------------------------------------------------------------
// the value stack
//
// A separate MsgStack object before, over MsgStck, with its own coclass and its
// own IID. It is four methods on the node now, because that is what the facade
// exposes and what the thing actually is: one saved (name, value) pair, held
// inside the node.
//
// Pop and Drop answer False for "there was nothing stacked" rather than
// failing, which is the same normalisation the old MsgStack made by hand -- the
// core's Pop is a silent no-op there, so a drain loop driven by success would
// never end.
// ---------------------------------------------------------------------------
STDMETHODIMP CMsgField::PushValue ( )
{
    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"PushValue" );

    hr = m_pNode->PushValue ( );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"PushValue" );

    m_pStore->BumpMutation();
    return S_OK;
    MSGF_GUARD_END(L"PushValue")
}

STDMETHODIMP CMsgField::PopValue ( VARIANT_BOOL *pRestored )
{
    if ( pRestored != NULL ) *pRestored = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"PopValue" );

    hr = m_pNode->PopValue ( );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"PopValue" );

    m_pStore->BumpMutation();
    if ( pRestored != NULL ) *pRestored = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"PopValue")
}

STDMETHODIMP CMsgField::DropValue ( VARIANT_BOOL *pDropped )
{
    if ( pDropped != NULL ) *pDropped = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"DropValue" );

    hr = m_pNode->DropValue ( );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"DropValue" );

    m_pStore->BumpMutation();
    if ( pDropped != NULL ) *pDropped = ( hr == S_OK ) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"DropValue")
}

STDMETHODIMP CMsgField::get_IsStacked ( VARIANT_BOOL *pVal )
{
    if ( pVal == NULL ) return E_POINTER;
    *pVal = VARIANT_FALSE;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"IsStacked" );

    int bStacked = 0;
    hr = m_pNode->IsStacked ( &bStacked );
    if ( FAILED(hr) ) return Fail ( AsStale ( hr ), L"IsStacked" );
    *pVal = bStacked ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
    MSGF_GUARD_END(L"IsStacked")
}

// For Each over a node walks its CHILDREN, which is what a script means by
// iterating a node. The enumerator is a snapshot, as everywhere else here.
STDMETHODIMP CMsgField::get__NewEnum ( IUnknown **ppUnk )
{
    if ( ppUnk == NULL ) return E_POINTER;
    *ppUnk = NULL;

    MSGF_GUARD_BEGIN
    CStoreLock lock ( m_pStore );
    HRESULT hr = Bind(); if ( FAILED(hr) ) return Fail ( hr, L"_NewEnum" );

    return MakeFieldEnum ( m_pStore, m_pNode, MSGF_SCOPE_CHILD, ppUnk );
    MSGF_GUARD_END(L"_NewEnum")
}
