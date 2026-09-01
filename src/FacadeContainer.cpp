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
// FacadeContainer.cpp -- IMsgList and IMsgVect over P3PmsgList / P3PmsgVect.
#include "stdafx.h"
#include "FacadeContainer.h"
#include "FacadeStore.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

// ===========================================================================
//  FacadeList
// ===========================================================================

FacadeList::FacadeList ( FacadeStore *pStore, const FacadeRoute& rRoute )
          : m_pStore ( pStore )
          , m_oRoute ( rRoute )
{
    if ( m_pStore )
      m_pStore -> AddRef ( );
}

FacadeList::~FacadeList ( )
{
    if ( m_pStore )
      m_pStore -> ReleaseRef ( );
}

HRESULT
FacadeList::Make ( FacadeStore *pStore, const FacadeRoute& rRoute
                 , IMsgList **outList )
{
    if ( !outList )
      return E_POINTER;
    *outList = nullptr;
    if ( !pStore )
      return MSGF_E_CLOSED;
    *outList = new FacadeList ( pStore, rRoute );
    return S_OK;
}

ULONG
FacadeList::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    delete this;
    return 0;
}

//
//  Route -> a live P3PmsgList
//  NOTES: Bound by ASSIGNING the P3PmsgObject, which is the one path that
//         aliases: P3PmsgList::operator=(const P3PmsgObject&) is a bare
//         `m_oObject = rhs`, and the constructor taking a P3PmsgObject is
//         written in terms of it
//       : NOT Connect(handle, address, size).  That looks like the lower-level
//         version of the same thing and is not: it forwards to
//         P3PmsgField::Connect -> P3PmsgObject::Connectx, which re-derives the
//         block layout from the size it is given.  Handing it a size taken
//         from an already-connected object seats the wrapper on a block header
//         it then disagrees with, and the core asserts its way out
//       : The kind is checked BEFORE the assignment, because operator= asserts
//         on a non-list rather than reporting one
//
HRESULT
FacadeList::Resolve ( P3PmsgList& rOut ) const
{
    P3PmsgObject oObject;
    HRESULT      hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;
    try
    {
      if ( !oObject.IsList ( ) )
        return MSGF_E_TYPE;
      rOut = oObject;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::GetCount ( unsigned int *outCount ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outCount ) return E_POINTER;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try { *outCount = (unsigned int)oList.GetCount ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  The type of one cell
//  NOTES: An index past the end answers MSGF_TYPE_NULL and S_OK rather than
//         MSGF_E_RANGE, which is what makes this the safe probe the header
//         advertises: a caller can walk until the type comes back NULL without
//         having to ask for the count first
//
HRESULT
FacadeList::GetTypeAt ( unsigned int index, unsigned char *outType ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outType ) return E_POINTER;
    *outType = MSGF_TYPE_NULL;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      P3PmsgData *pCell = ListCellAt ( oList, index );
      if ( pCell )
        *outType = pCell->DataType ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::GetIntAt ( unsigned int index
                     , long long *outValue, int *outUnsigned ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    return ReadInt ( *pCell, outValue, outUnsigned );
}

HRESULT
FacadeList::GetRealAt ( unsigned int index, double *outValue ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    return ReadReal ( *pCell, outValue );
}

HRESULT
FacadeList::GetTextAt ( unsigned int index, wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    return ReadText ( *pCell, buf, cch );
}

HRESULT
FacadeList::SetIntAt ( unsigned int index, long long value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    hr = WriteInt ( *pCell, value );
    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

HRESULT
FacadeList::SetRealAt ( unsigned int index, double value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    hr = WriteReal ( *pCell, value );
    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

HRESULT
FacadeList::SetTextAt ( unsigned int index, const wchar_t *value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgData *pCell = nullptr;
    try { pCell = ListCellAt ( oList, index ); }
    catch ( ... ) { return MSGF_E_CORE; }
    if ( !pCell ) return MSGF_E_RANGE;

    // false: a list cell cannot grow -- see WriteText.
    hr = WriteText ( *pCell, value, false );
    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

//
//  Append or prepend a new cell
//  NOTES: One switch arm per width, each passing the P3PmsgData straight into
//         AddListHead/AddListTail as a temporary -- the same rule the node's
//         declares follow, and for the same reason
//
HRESULT
FacadeList::AddInt ( long long value, unsigned char type, unsigned int flags )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( type == 0 )
      type = MSGF_TYPE_INT32;
    if ( !IsIntType ( type ) )
      return MSGF_E_TYPE;
    if ( !IntFits ( value, type ) )
      return MSGF_E_LIMIT;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    BOOL bHead = ( flags & MSGF_ADD_HEAD ) ? TRUE : FALSE;
    try
    {
      switch ( type )
      {
        case MSGF_TYPE_INT08:  if (bHead) oList.AddListHead(P3PmsgData((INT08 )value)); else oList.AddListTail(P3PmsgData((INT08 )value)); break;
        case MSGF_TYPE_UINT08: if (bHead) oList.AddListHead(P3PmsgData((UINT08)value)); else oList.AddListTail(P3PmsgData((UINT08)value)); break;
        case MSGF_TYPE_INT16:  if (bHead) oList.AddListHead(P3PmsgData((INT16 )value)); else oList.AddListTail(P3PmsgData((INT16 )value)); break;
        case MSGF_TYPE_UINT16: if (bHead) oList.AddListHead(P3PmsgData((UINT16)value)); else oList.AddListTail(P3PmsgData((UINT16)value)); break;
        case MSGF_TYPE_INT32:  if (bHead) oList.AddListHead(P3PmsgData((INT32 )value)); else oList.AddListTail(P3PmsgData((INT32 )value)); break;
        case MSGF_TYPE_UINT32: if (bHead) oList.AddListHead(P3PmsgData((UINT32)value)); else oList.AddListTail(P3PmsgData((UINT32)value)); break;
        case MSGF_TYPE_INT64:  if (bHead) oList.AddListHead(P3PmsgData((INT64 )value)); else oList.AddListTail(P3PmsgData((INT64 )value)); break;
        case MSGF_TYPE_UINT64: if (bHead) oList.AddListHead(P3PmsgData((UINT64)value)); else oList.AddListTail(P3PmsgData((UINT64)value)); break;
        case MSGF_TYPE_BOOL:   if (bHead) oList.AddListHead(P3PmsgData(value != 0   )); else oList.AddListTail(P3PmsgData(value != 0   )); break;
        default:               return MSGF_E_TYPE;
      }
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::AddReal ( double value, unsigned char type, unsigned int flags )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    if ( type == 0 )
      type = MSGF_TYPE_DOUBLE;
    if ( !IsRealType ( type ) )
      return MSGF_E_TYPE;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    BOOL bHead = ( flags & MSGF_ADD_HEAD ) ? TRUE : FALSE;
    try
    {
      if ( type == MSGF_TYPE_FLOAT )
      {
        if (bHead) oList.AddListHead ( P3PmsgData((float)value) );
        else       oList.AddListTail ( P3PmsgData((float)value) );
      }
      else
      {
        if (bHead) oList.AddListHead ( P3PmsgData(value) );
        else       oList.AddListTail ( P3PmsgData(value) );
      }
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::AddText ( const wchar_t *value, unsigned int flags )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !value ) return E_POINTER;
    if ( ::wcslen ( value ) > MAX_TEXT ) return MSGF_E_LIMIT;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( flags & MSGF_ADD_HEAD ) oList.AddListHead ( P3PmsgData(value) );
      else                         oList.AddListTail ( P3PmsgData(value) );
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::Drop ( unsigned int flags )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      if ( oList.GetCount ( ) <= 0 )
        return MSGF_E_RANGE;
      if ( flags & MSGF_DROP_HEAD ) oList.DropHead ( );
      else                          oList.DropTail ( );
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Remove the cell at one index
//  NOTES: The chain is walked to the index and the raw position handed to
//         P3PmsgList::Delete.  That position is a VBLaddr and is valid only
//         for as long as no allocation happens -- which is why it is obtained
//         and spent in the same expression rather than kept
//
HRESULT
FacadeList::DeleteAt ( unsigned int index )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      VBLaddr aPos = oList.GetHeadPos ( );
      for ( unsigned int i = 0; i < index && aPos; ++i )
        oList.GetNext ( aPos );
      if ( !aPos )
        return MSGF_E_RANGE;
      if ( !oList.Delete ( aPos ) )
        return MSGF_E_RANGE;
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeList::Truncate ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgList oList;
    hr = Resolve ( oList );
    if ( FAILED ( hr ) ) return hr;

    try { oList.Truncate ( ); m_pStore->MarkDirty ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ===========================================================================
//  FacadeVect
// ===========================================================================

FacadeVect::FacadeVect ( FacadeStore *pStore, const FacadeRoute& rRoute )
          : m_pStore ( pStore )
          , m_oRoute ( rRoute )
{
    if ( m_pStore )
      m_pStore -> AddRef ( );
}

FacadeVect::~FacadeVect ( )
{
    if ( m_pStore )
      m_pStore -> ReleaseRef ( );
}

HRESULT
FacadeVect::Make ( FacadeStore *pStore, const FacadeRoute& rRoute
                 , IMsgVect **outVect )
{
    if ( !outVect )
      return E_POINTER;
    *outVect = nullptr;
    if ( !pStore )
      return MSGF_E_CLOSED;
    *outVect = new FacadeVect ( pStore, rRoute );
    return S_OK;
}

ULONG
FacadeVect::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    delete this;
    return 0;
}

FacadeRoute
FacadeVect::RouteWith ( unsigned int uIndex ) const
{
    FacadeRoute oRoute ( m_oRoute );
    FacadeStep  oStep;
    oStep.uScope = SCOPE_ELEM;
    oStep.nIndex = uIndex;
    oRoute.push_back ( oStep );
    return oRoute;
}

HRESULT
FacadeVect::Resolve ( P3PmsgVect& rOut ) const
{
    P3PmsgObject oObject;
    HRESULT      hr = m_pStore -> ResolveObject ( m_oRoute, oObject );
    if ( FAILED ( hr ) ) return hr;
    try
    {
      if ( !oObject.IsVect ( ) )
        return MSGF_E_TYPE;
      rOut = oObject;                 // see the note on FacadeList::Resolve
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetCount ( unsigned int *outCount ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outCount ) return E_POINTER;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try { *outCount = (unsigned int)oVect.GetCount ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetKindAt ( unsigned int index, unsigned int *outKind ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outKind ) return E_POINTER;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      if      ( oVect.IsList  ( nElem ) ) *outKind = MSGF_KIND_LIST;
      else if ( oVect.IsVect  ( nElem ) ) *outKind = MSGF_KIND_VECT;
      else if ( oVect.IsField ( nElem ) ) *outKind = MSGF_KIND_ITEM;
      else                                *outKind = MSGF_KIND_DATA;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetTypeAt ( unsigned int index, unsigned char *outType ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outType ) return E_POINTER;
    *outType = MSGF_TYPE_NULL;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return S_OK;   // safe probe
      *outType = oVect.r_data ( nElem ).DataType ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetNameAt ( unsigned int index, wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      return CopyOut ( oVect.r_name ( nElem ).c_name ( ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetIntAt ( unsigned int index
                     , long long *outValue, int *outUnsigned ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      return ReadInt ( oVect.r_data ( nElem ), outValue, outUnsigned );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetRealAt ( unsigned int index, double *outValue ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      return ReadReal ( oVect.r_data ( nElem ), outValue );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::GetTextAt ( unsigned int index, wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      return ReadText ( oVect.r_data ( nElem ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::SetIntAt ( unsigned int index, long long value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      hr = WriteInt ( oVect.r_data ( nElem ), value );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

HRESULT
FacadeVect::SetRealAt ( unsigned int index, double value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      hr = WriteReal ( oVect.r_data ( nElem ), value );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

HRESULT
FacadeVect::SetTextAt ( unsigned int index, const wchar_t *value )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      hr = WriteText ( oVect.r_data ( nElem ), value );
    }
    catch ( ... ) { return MSGF_E_CORE; }

    if ( SUCCEEDED ( hr ) ) m_pStore->MarkDirty ( );
    return hr;
}

HRESULT
FacadeVect::GetListAt ( unsigned int index, IMsgList **outList )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outList ) return E_POINTER;
    *outList = nullptr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      if ( !oVect.IsList ( nElem ) )          return MSGF_E_TYPE;
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return FacadeList::Make ( m_pStore, RouteWith ( index ), outList );
}

HRESULT
FacadeVect::GetVectAt ( unsigned int index, IMsgVect **outVect )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;
    if ( !outVect ) return E_POINTER;
    *outVect = nullptr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      if ( !oVect.IsVect ( nElem ) )          return MSGF_E_TYPE;
    }
    catch ( ... ) { return MSGF_E_CORE; }

    return FacadeVect::Make ( m_pStore, RouteWith ( index ), outVect );
}

HRESULT
FacadeVect::DeleteAt ( unsigned int index )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try
    {
      int nElem = (int)index;
      if ( nElem >= (int)oVect.GetCount ( ) ) return MSGF_E_RANGE;
      if ( !oVect.Delete ( nElem ) )          return MSGF_E_RANGE;
      m_pStore->MarkDirty ( );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeVect::Truncate ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    StoreGuard oGuard ( m_pStore );
    HRESULT hr = oGuard.Failed ( );
    if ( FAILED ( hr ) ) return hr;

    P3PmsgVect oVect;
    hr = Resolve ( oVect );
    if ( FAILED ( hr ) ) return hr;

    try { oVect.Truncate ( ); m_pStore->MarkDirty ( ); return S_OK; }
    catch ( ... ) { return MSGF_E_CORE; }
}
