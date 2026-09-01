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
// FacadeNode.cpp -- a position in a store's tree, as a route.
#include "stdafx.h"
#include "FacadeNode.h"
#include "FacadeStore.h"
#include "FacadeContainer.h"
#include "FacadeCursor.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

FacadeNode::FacadeNode ( FacadeStore *pStore, const FacadeRoute& rRoute )
          : m_pStore ( pStore )
          , m_oRoute ( rRoute )
{
    if ( m_pStore )
      m_pStore -> AddRef ( );
}

FacadeNode::~FacadeNode ( )
{
    if ( m_pStore )
      m_pStore -> ReleaseRef ( );
}

HRESULT
FacadeNode::Make ( FacadeStore *pStore, const FacadeRoute& rRoute
                 , IMsgNode **outNode )
{
    if ( !outNode )
      return E_POINTER;
    *outNode = nullptr;
    if ( !pStore )
      return MSGF_E_CLOSED;
    if ( rRoute.size ( ) > MAX_DEPTH )
      return MSGF_E_DEPTH;

    *outNode = new FacadeNode ( pStore, rRoute );
    return S_OK;
}

ULONG
FacadeNode::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    delete this;
    return 0;
}

FacadeRoute
FacadeNode::RouteWith ( unsigned int uScope, const wchar_t *lpszName ) const
{
    FacadeRoute oRoute ( m_oRoute );
    FacadeStep  oStep;
    oStep.uScope  = uScope;
    oStep.strName = lpszName;
    oRoute.push_back ( oStep );
    return oRoute;
}

// ---------------------------------------------------------------------------
//  What this node is
// ---------------------------------------------------------------------------

//
//  This node's own name
//  NOTES: Answered from the RESOLVED node rather than from the route, and the
//         difference is visible: P3PmsgCurs::Goto matches case-INSENSITIVELY,
//         so a route built from L"title" resolves a node stored as L"Title"
//         and this reports the stored spelling.  Reporting the route's would
//         hand back the caller's own guess dressed as a fact
//
HRESULT
FacadeNode::GetName ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    if ( m_oRoute.empty ( ) )
      return CopyOut ( L"", buf, cch );      // the root: see IMsgStore::GetRootname

    try
    {
      P3PmsgField oField ( oObject );
      return CopyOut ( oField.c_name ( ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::GetPath ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    CString strPath;
    FacadeStore::FormatRoute ( m_oRoute, strPath );
    return CopyOut ( (LPCWSTR)strPath, buf, cch );
}

HRESULT
FacadeNode::GetPos ( unsigned long long *outPos ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outPos ) return E_POINTER;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      P3PmsgField oField ( oObject );
      *outPos = (unsigned long long)oField.GetP2Pos ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::GetKind ( unsigned int *outKind ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outKind ) return E_POINTER;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    try { *outKind = KindOf ( oObject ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::GetType ( unsigned char *outType ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outType ) return E_POINTER;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      P3PmsgField oField ( oObject );
      *outType = oField.DataType ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::IsNull ( int *outNull ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outNull ) return E_POINTER;

    P3PmsgObject oObject;
    hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      P3PmsgField oField ( oObject );
      *outNull = oField.IsNull ( ) ? 1 : 0;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  This node's own value
// ---------------------------------------------------------------------------

HRESULT
FacadeNode::GetInt ( long long *outValue, int *outUnsigned ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    return ReadInt ( oField, outValue, outUnsigned );
}

HRESULT
FacadeNode::SetInt ( long long value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    hr = WriteInt ( oField, value );
    if ( SUCCEEDED ( hr ) )
      m_pStore -> MarkDirty ( );
    return hr;
}

HRESULT
FacadeNode::GetReal ( double *outValue ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    return ReadReal ( oField, outValue );
}

HRESULT
FacadeNode::SetReal ( double value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    hr = WriteReal ( oField, value );
    if ( SUCCEEDED ( hr ) )
      m_pStore -> MarkDirty ( );
    return hr;
}

HRESULT
FacadeNode::GetText ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    return ReadText ( oField, buf, cch );
}

HRESULT
FacadeNode::SetText ( const wchar_t *value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    hr = WriteText ( oField, value );
    if ( SUCCEEDED ( hr ) )
      m_pStore -> MarkDirty ( );
    return hr;
}

HRESULT
FacadeNode::GetBlob ( void *buf, unsigned int *size ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    return ReadBlob ( oField, buf, size );
}

HRESULT
FacadeNode::GetGuid ( wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    return ReadGuid ( oField, buf, cch );
}

//
//  The node's timestamp
//  NOTES: Held by the core as a "$TStamp$" ATTRIBUTE of the node rather than
//         as a property of it, which is why reading one on a node with no
//         attribute collection answers 0 rather than failing
//
HRESULT
FacadeNode::GetTime ( long long *outTime ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outTime ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try { *outTime = (long long)P3Pmsg_GetTStamp ( oField ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::SetTime ( long long value, long long *outTime )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      INT64 iStored = P3Pmsg_SetTStamp ( oField, (INT64)value, FALSE );
      if ( outTime )
        *outTime = (long long)iStored;
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  Children, in either scope
// ---------------------------------------------------------------------------
namespace {

inline bool
IsPublicScope ( unsigned int uScope )
{
    return uScope == MSGF_SCOPE_CHILD || uScope == MSGF_SCOPE_ATTR;
}

} // namespace

HRESULT
FacadeNode::GetCount ( unsigned int scope, unsigned int *outCount ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !outCount ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      *outCount = ( scope == MSGF_SCOPE_ATTR )
                    ? (unsigned int)oField.r_Attr ( ).GetCount ( )
                    : (unsigned int)oField.r_Desc ( ).GetCount ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Is there a child of that name in that scope?
//  NOTES: S_OK / S_FALSE, both SUCCESS.  Absence is an ANSWER to this
//         question, not a failure to answer it -- which is not true of
//         GetChild, where absence means the caller does not get the thing it
//         asked for
//       : Through a cursor rather than P3PmsgField::Exists, which routes
//         through the core's path resolver and would therefore answer for a
//         SCOPE the caller did not ask about
//
HRESULT
FacadeNode::Exists ( unsigned int scope, const wchar_t *name ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      ScopeCursor oCurs ( oField, scope );
      return oCurs->Goto ( name ) ? S_OK : S_FALSE;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::GetChild ( unsigned int scope, const wchar_t *name
                     , IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !outNode ) return E_POINTER;
    *outNode = nullptr;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    CString strFound;
    try
    {
      ScopeCursor oCurs ( oField, scope );
      if ( !oCurs->Goto ( name ) )
        return MSGF_E_NO_ITEM;
      strFound = oCurs->c_wstr ( );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    // The STORED spelling goes into the route, not the caller's: Goto matches
    // without case, and a route that records the caller's guess would render a
    // path GetPath could not have produced.
    return Make ( m_pStore, RouteWith ( scope, (LPCWSTR)strFound ), outNode );
}

HRESULT
FacadeNode::GetChildAt ( unsigned int scope, unsigned int index
                       , IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !outNode ) return E_POINTER;
    *outNode = nullptr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    CString strFound;
    try
    {
      ScopeCursor oCurs ( oField, scope );
      if ( index >= (unsigned int)oCurs->GetCount ( ) )
        return MSGF_E_RANGE;
      if ( !oCurs->Goto ( (int)index ) )
        return MSGF_E_RANGE;
      strFound = oCurs->c_wstr ( );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return Make ( m_pStore, RouteWith ( scope, (LPCWSTR)strFound ), outNode );
}

// ---------------------------------------------------------------------------
//  Declares
// ---------------------------------------------------------------------------

//
//  The shared opening of every declare
//  NOTES: The collision test is the facade's, not the core's.  DeclareItem
//         without bUpdate silently hands back the EXISTING node, so "create"
//         and "look up" are the same call there and a caller cannot tell which
//         one happened.  MSGF_E_EXISTS makes it tellable, and asking for
//         MSGF_DECLARE_UPDATE makes it explicit that assignment was intended
//
HRESULT
FacadeNode::BeginDeclare ( unsigned int uScope, const wchar_t *lpszName
                         , unsigned int uFlags, P3PmsgField& rField
                         , BOOL& rbUpdate ) const
{
    if ( !IsPublicScope ( uScope ) )
      return MSGF_E_SCOPE;
    if ( !IsUsableName ( lpszName ) )
      return MSGF_E_NAME;

    HRESULT hr = m_pStore -> ResolveField ( m_oRoute, rField );
    if ( FAILED ( hr ) ) return hr;

    rbUpdate = ( uFlags & MSGF_DECLARE_UPDATE ) ? TRUE : FALSE;
    if ( !rbUpdate )
    {
      try
      {
        ScopeCursor oCurs ( rField, uScope );
        if ( oCurs->Goto ( lpszName ) )
          return MSGF_E_EXISTS;
      }
      catch ( ... ) { return MSGF_E_CORE; }
    }
    return S_OK;
}

HRESULT
FacadeNode::EndDeclare ( unsigned int uScope, const wchar_t *lpszName
                       , IMsgNode **outNode )
{
    m_pStore -> MarkDirty ( );
    if ( !outNode )
      return S_OK;
    return Make ( m_pStore, RouteWith ( uScope, lpszName ), outNode );
}

HRESULT
FacadeNode::DeclareInt ( unsigned int scope, const wchar_t *name
                       , long long value, unsigned char type
                       , unsigned int flags, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( outNode ) *outNode = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, flags, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = ::DeclareInt ( oField, scope, name, value, type, bUpdate );
    if ( FAILED ( hr ) ) return hr;
    return EndDeclare ( scope, name, outNode );
}

HRESULT
FacadeNode::DeclareReal ( unsigned int scope, const wchar_t *name
                        , double value, unsigned char type
                        , unsigned int flags, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( outNode ) *outNode = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, flags, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = ::DeclareReal ( oField, scope, name, value, type, bUpdate );
    if ( FAILED ( hr ) ) return hr;
    return EndDeclare ( scope, name, outNode );
}

HRESULT
FacadeNode::DeclareText ( unsigned int scope, const wchar_t *name
                        , const wchar_t *value
                        , unsigned int flags, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( outNode ) *outNode = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, flags, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = ::DeclareText ( oField, scope, name, value, bUpdate );
    if ( FAILED ( hr ) ) return hr;
    return EndDeclare ( scope, name, outNode );
}

HRESULT
FacadeNode::DeclareBlob ( unsigned int scope, const wchar_t *name
                        , const void *value, unsigned int size
                        , unsigned int flags, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( outNode ) *outNode = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, flags, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = ::DeclareBlob ( oField, scope, name, value, size, bUpdate );
    if ( FAILED ( hr ) ) return hr;
    return EndDeclare ( scope, name, outNode );
}

HRESULT
FacadeNode::DeclareGuid ( unsigned int scope, const wchar_t *name
                        , const wchar_t *guid
                        , unsigned int flags, IMsgNode **outNode )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( outNode ) *outNode = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, flags, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = ::DeclareGuid ( oField, scope, name, guid, bUpdate );
    if ( FAILED ( hr ) ) return hr;
    return EndDeclare ( scope, name, outNode );
}

HRESULT
FacadeNode::DeclareList ( unsigned int scope, const wchar_t *name
                        , IMsgList **outList )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outList ) return E_POINTER;
    *outList = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, 0, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = AttachList ( oField, scope, name );
    if ( FAILED ( hr ) ) return hr;

    m_pStore -> MarkDirty ( );
    return FacadeList::Make ( m_pStore, RouteWith ( scope, name ), outList );
}

HRESULT
FacadeNode::DeclareVect ( unsigned int scope, const wchar_t *name
                        , unsigned int elems, unsigned char type
                        , IMsgVect **outVect )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outVect ) return E_POINTER;
    *outVect = nullptr;

    P3PmsgField oField;
    BOOL        bUpdate = FALSE;
    hr = BeginDeclare ( scope, name, 0, oField, bUpdate );
    if ( FAILED ( hr ) ) return hr;

    hr = AttachVect ( oField, scope, name, elems, type ? type : MSGF_TYPE_INT32 );
    if ( FAILED ( hr ) ) return hr;

    m_pStore -> MarkDirty ( );
    return FacadeVect::Make ( m_pStore, RouteWith ( scope, name ), outVect );
}

//
//  Open an existing container child
//  NOTES: Absent and present-but-wrong-kind are DIFFERENT answers, so a caller
//         never has to pre-check with Exists and never mistakes "there is no
//         such list" for "that name holds something else"
//
HRESULT
FacadeNode::GetList ( unsigned int scope, const wchar_t *name, IMsgList **outList )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !outList ) return E_POINTER;
    *outList = nullptr;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    CString strFound;
    try
    {
      ScopeCursor oCurs ( oField, scope );
      if ( !oCurs->Goto ( name ) )
        return MSGF_E_NO_ITEM;
      if ( !oCurs->IsList ( ) )
        return MSGF_E_TYPE;
      strFound = oCurs->c_wstr ( );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return FacadeList::Make ( m_pStore, RouteWith ( scope, (LPCWSTR)strFound ), outList );
}

HRESULT
FacadeNode::GetVect ( unsigned int scope, const wchar_t *name, IMsgVect **outVect )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !outVect ) return E_POINTER;
    *outVect = nullptr;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    CString strFound;
    try
    {
      ScopeCursor oCurs ( oField, scope );
      if ( !oCurs->Goto ( name ) )
        return MSGF_E_NO_ITEM;
      if ( !oCurs->IsVect ( ) )
        return MSGF_E_TYPE;
      strFound = oCurs->c_wstr ( );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return FacadeVect::Make ( m_pStore, RouteWith ( scope, (LPCWSTR)strFound ), outVect );
}

// ---------------------------------------------------------------------------
//  Restructuring
// ---------------------------------------------------------------------------

HRESULT
FacadeNode::Delete ( unsigned int scope, const wchar_t *name )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      bool bGone = ( scope == MSGF_SCOPE_ATTR )
                     ? oField.r_Attr ( ).Delete ( name )
                     : oField.r_Desc ( ).Delete ( name );
      if ( !bGone )
        return MSGF_E_NO_ITEM;
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Empty one scope
//  NOTES: The collection's own Truncate, NOT P3PmsgField::Truncate -- that one
//         drops the descendants AND the attributes AND the position stack,
//         which is not a scoped operation at all
//
HRESULT
FacadeNode::Truncate ( unsigned int scope )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( scope == MSGF_SCOPE_ATTR ) oField.r_Attr ( ).Truncate ( );
      else                            oField.r_Desc ( ).Truncate ( );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::Rename ( unsigned int scope, const wchar_t *name
                   , const wchar_t *newName )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !name || !newName ) return E_POINTER;
    if ( !IsUsableName ( newName ) ) return MSGF_E_NAME;

    if ( ::_wcsicmp ( name, newName ) == 0 )
      return S_FALSE;                       // nothing to do, and not an error

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      {
        ScopeCursor oCurs ( oField, scope );
        if ( !oCurs->Goto ( name ) )
          return MSGF_E_NO_ITEM;
      }
      if ( scope == MSGF_SCOPE_ATTR )
        P3PmsgRefactor_Rename ( oField.r_Attr ( ), name, newName );
      else
        P3PmsgRefactor_Rename ( oField, name, newName );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Move a child into another node's CHILD scope
//  NOTES: `destin` must belong to the same store, and that is checked rather
//         than assumed: the core's move works on raw heap addresses and two
//         stores are two heaps, so a cross-store move would relink a block
//         into an image that does not contain it
//
HRESULT
FacadeNode::Move ( unsigned int scope, const wchar_t *name, IMsgNode *destin )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !name || !destin ) return E_POINTER;

    FacadeNode *pDestin = (FacadeNode*)destin;
    if ( pDestin->Store ( ) != m_pStore )
      return E_INVALIDARG;

    P3PmsgField oField, oDestin;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;
    hr = m_pStore -> ResolveField ( pDestin->Route ( ), oDestin );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      {
        ScopeCursor oCurs ( oField, scope );
        if ( !oCurs->Goto ( name ) )
          return MSGF_E_NO_ITEM;
      }
      if ( scope == MSGF_SCOPE_ATTR )
        P3PmsgRefactor_Move ( oField.r_Attr ( ), oDestin, name );
      else
        P3PmsgRefactor_Move ( oField, oDestin, name );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Change a child's storage type
//  NOTES: A declare-with-update carrying a ZERO value of the new type.  That
//         is a retype rather than an assignment because DeclareItem's update
//         path assigns the whole P3PmsgData and P3PmsgData::operator= copies
//         the type word with the bytes -- so the node keeps its name, its
//         position and its children and changes only what it is
//
HRESULT
FacadeNode::Retype ( unsigned int scope, const wchar_t *name, unsigned char type )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;
    if ( !name ) return E_POINTER;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      ScopeCursor oCurs ( oField, scope );
      if ( !oCurs->Goto ( name ) )
        return MSGF_E_NO_ITEM;
    }
    catch ( ... ) { return MSGF_E_CORE; }

    hr = DeclareZero ( oField, scope, name, type );
    if ( SUCCEEDED ( hr ) )
      m_pStore -> MarkDirty ( );
    return hr;
}

// ---------------------------------------------------------------------------
//  Enumeration
// ---------------------------------------------------------------------------

HRESULT
FacadeNode::OpenCursor ( unsigned int scope, IMsgCursor **outCursor )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;

    return FacadeCursor::Make ( m_pStore, m_oRoute, scope, outCursor );
}

HRESULT
FacadeNode::OpenWalker ( unsigned int scope, IMsgWalker **outWalker )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !IsPublicScope ( scope ) ) return MSGF_E_SCOPE;

    return FacadeWalker::Make ( m_pStore, m_oRoute, scope, outWalker );
}

// ---------------------------------------------------------------------------
//  The value stack (ABI 2)
//
//  NOTES: MsgStck is the one piece of per-node state the kernel keeps that a
//         path-based node can still reach, and the reason is worth writing
//         down: the saved pair lives INSIDE the field's own block, so it is
//         found by resolving the route like everything else.  A stack that
//         lived in the wrapper would not survive the wrapper, and a stack that
//         lived in a handle would not survive a relocation
//       : r_Stck() hands back a MsgStck already connected to this field.  Push
//         and Pop dereference that field without a null check, which is why
//         every one of these resolves first and calls second
// ---------------------------------------------------------------------------

HRESULT
FacadeNode::PushValue ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      oField.r_Stck ( ).Push ( );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Restore the saved pair
//  NOTES: S_FALSE rather than an error for an unstacked node, because the
//         core's own Pop is a SILENT NO-OP there -- so a caller draining a
//         stack with `while (SUCCEEDED(PopValue()))` would never stop.  The
//         distinction is the whole reason this returns two success codes
//
HRESULT
FacadeNode::PopValue ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( !oField.IsStacked ( ) )
        return S_FALSE;
      oField.r_Stck ( ).Pop ( );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::DropValue ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( !oField.IsStacked ( ) )
        return S_FALSE;
      oField.r_Stck ( ).Drop ( );
      m_pStore -> MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeNode::IsStacked ( int *outStacked ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outStacked ) return E_POINTER;
    *outStacked = 0;

    P3PmsgField oField;
    hr = m_pStore -> ResolveField ( m_oRoute, oField );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      *outStacked = oField.IsStacked ( ) ? 1 : 0;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}
