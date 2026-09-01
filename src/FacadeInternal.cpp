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
// FacadeInternal.cpp -- the value layer: types, reads, writes and declares
// against ONE live Msgcore data cell.
//
// Everything here is deliberately free of routes, stores and locks: it works on
// a P3PmsgData or a P3PmsgField that the caller has already resolved and is
// already holding the store's lock for.  That keeps the three places a value
// can live -- a node, a list cell, a vect element -- on one implementation
// instead of three near-copies.
#include "stdafx.h"
#include "FacadeInternal.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

// ---------------------------------------------------------------------------
//  Type predicates
// ---------------------------------------------------------------------------

//
//  Is this one of the integer-family types ReadAnyInt understands?
//  NOTES: TIME32/TIME64 are integers in the union but not in ReadAnyInt's
//         switch, so they are read as neither int nor real.  Following the
//         core rather than second-guessing it: a timestamp has its own pair of
//         accessors (GetTime/SetTime) and reading one as a plain integer would
//         make the two disagree
//
bool
IsIntType ( unsigned char uType )
{
    switch ( uType )
    {
      case VBLockData_INT08:  case VBLockData_UINT08:
      case VBLockData_INT16:  case VBLockData_UINT16:
      case VBLockData_INT32:  case VBLockData_UINT32:
      case VBLockData_INT64:  case VBLockData_UINT64:
      case VBLockData_BOOL:
        return true;
      default:
        return false;
    }
}

bool
IsRealType ( unsigned char uType )
{
    return uType == VBLockData_FLOAT || uType == VBLockData_DOUBLE;
}

//
//  Is this a WIDE string type?
//  NOTES: The BSTR family is deliberately excluded.  P3PmsgData::c_wstr
//         refuses anything but a WSTR, so a BSTR node could be declared here
//         and then never read back -- better to call it "not text" in one
//         place than to have GetText raise a type error in another
//
bool
IsTextType ( unsigned char uType )
{
    return uType >= VBLockData_WSTR08 && uType <= VBLockData_WSTR32var;
}

//
//  Does iValue survive storage at uType?
//  NOTES: This exists because the core does NOT check.  P3PmsgData((INT08)v)
//         narrows in the cast, so 300 declared as an INT08 is stored as 44 and
//         reads back as 44, with nothing anywhere reporting it
//
bool
IntFits ( long long iValue, unsigned char uType )
{
    switch ( uType )
    {
      case VBLockData_INT08:   return iValue >= -128LL      && iValue <= 127LL;
      case VBLockData_UINT08:  return iValue >=  0LL        && iValue <= 255LL;
      case VBLockData_INT16:   return iValue >= -32768LL    && iValue <= 32767LL;
      case VBLockData_UINT16:  return iValue >=  0LL        && iValue <= 65535LL;
      case VBLockData_INT32:   return iValue >= -2147483648LL && iValue <= 2147483647LL;
      case VBLockData_UINT32:  return iValue >=  0LL        && iValue <= 4294967295LL;
      case VBLockData_INT64:   return true;
      // A UINT64 cell holds every non-negative long long; the values it can
      // hold that a long long cannot are simply unreachable through this ABI,
      // which is a documented consequence of taking the value as signed.
      case VBLockData_UINT64:  return iValue >= 0LL;
      case VBLockData_BOOL:    return true;      // any non-zero is true
      default:                 return false;
    }
}

// ---------------------------------------------------------------------------
//  Type names -- one vocabulary, shared by TypeToName and TypeFromName so a
//  caller that writes a type out and reads it back never disagrees with itself
// ---------------------------------------------------------------------------
namespace {

struct TypeNameEntry { unsigned char uType; const wchar_t *lpszName; };

const TypeNameEntry g_aTypeNames[] =
{
    { VBLockData_NULL,   L"NULL"   },
    { VBLockData_INT08,  L"INT08"  }, { VBLockData_UINT08, L"UINT08" },
    { VBLockData_INT16,  L"INT16"  }, { VBLockData_UINT16, L"UINT16" },
    { VBLockData_INT32,  L"INT32"  }, { VBLockData_UINT32, L"UINT32" },
    { VBLockData_INT64,  L"INT64"  }, { VBLockData_UINT64, L"UINT64" },
    { VBLockData_FLOAT,  L"FLOAT"  }, { VBLockData_DOUBLE, L"DOUBLE" },
    { VBLockData_BOOL,   L"BOOL"   },
    { VBLockData_BSTR16, L"BSTR16" }, { VBLockData_WSTR16, L"WSTR16" },
    { VBLockData_BLOB16, L"BLOB16" }, { VBLockData_GUID,   L"GUID"   },
};

} // namespace

const wchar_t*
TypeToName ( unsigned char uType )
{
    for ( int i = 0; i < _countof(g_aTypeNames); ++i )
      if ( g_aTypeNames[i].uType == uType )
        return g_aTypeNames[i].lpszName;
    return L"UNKNOWN";
}

unsigned char
TypeFromName ( const wchar_t *lpszName )
{
    if ( !lpszName )
      return 0xFF;
    for ( int i = 0; i < _countof(g_aTypeNames); ++i )
      if ( ::wcscmp ( lpszName, g_aTypeNames[i].lpszName ) == 0 )
        return g_aTypeNames[i].uType;
    return 0xFF;
}

//
//  Is lpszName usable as an item name?
//  NOTES: The CHARACTER rule is delegated to the core's own validator rather
//         than restated, so the facade and the store can never disagree about
//         what a name is.  It refuses '.', '@', ':' and '^' among others --
//         which is exactly what makes the facade's path grammar unambiguous.
//
//         The LENGTH rule is not delegated, because the core's two answers
//         disagree with each other: P3Pmsg_IsValidItemname accepts up to 127
//         units while P3PmsgName can store 63, and a name between the two
//         reaches an ASSERT inside the core's own copy.  MAX_NAME is therefore
//         the storage bound, checked here, before the core is called
//
bool
IsUsableName ( const wchar_t *lpszName )
{
    if ( !lpszName || !*lpszName )
      return false;
    if ( ::wcslen ( lpszName ) > MAX_NAME )
      return false;
    return P3Pmsg_IsValidItemname ( lpszName ) ? true : false;
}

unsigned int
KindOf ( const P3PmsgObject& rObject )
{
    if ( rObject.IsList  ( ) ) return MSGF_KIND_LIST;
    if ( rObject.IsVect  ( ) ) return MSGF_KIND_VECT;
    if ( rObject.IsField ( ) ) return MSGF_KIND_ITEM;
    return MSGF_KIND_DATA;
}

// ---------------------------------------------------------------------------
//  GUID text
// ---------------------------------------------------------------------------
namespace {

bool
ReadHex ( const wchar_t*& p, int nDigits, unsigned long long& rOut )
{
    rOut = 0;
    for ( int i = 0; i < nDigits; ++i )
    {
      wchar_t c = *p++;
      unsigned d;
      if      ( c >= L'0' && c <= L'9' ) d =        (unsigned)( c - L'0' );
      else if ( c >= L'a' && c <= L'f' ) d = 10u + (unsigned)( c - L'a' );
      else if ( c >= L'A' && c <= L'F' ) d = 10u + (unsigned)( c - L'A' );
      else return false;
      rOut = ( rOut << 4 ) | d;
    }
    return true;
}

} // namespace

bool
ParseGuid ( const wchar_t *lpszGuid, GUID& rGuid )
{
    if ( !lpszGuid )
      return false;

    std::wstring str ( lpszGuid );
    size_t a = 0, b = str.size();
    const wchar_t *ws = L" \t\r\n";
    while ( a < b && ::wcschr ( ws, str[a]     ) ) ++a;
    while ( b > a && ::wcschr ( ws, str[b - 1] ) ) --b;
    str = str.substr ( a, b - a );
    if ( str.size() >= 2 && str.front() == L'{' && str.back() == L'}' )
      str = str.substr ( 1, str.size() - 2 );
    if ( str.size() != 36 )
      return false;

    const wchar_t *p = str.c_str();
    unsigned long long v = 0;
    if ( !ReadHex ( p, 8, v ) ) return false;  rGuid.Data1 = (unsigned long)v;
    if ( *p++ != L'-' ) return false;
    if ( !ReadHex ( p, 4, v ) ) return false;  rGuid.Data2 = (unsigned short)v;
    if ( *p++ != L'-' ) return false;
    if ( !ReadHex ( p, 4, v ) ) return false;  rGuid.Data3 = (unsigned short)v;
    if ( *p++ != L'-' ) return false;
    for ( int i = 0; i < 2; ++i )
    {
      if ( !ReadHex ( p, 2, v ) ) return false;
      rGuid.Data4[i] = (unsigned char)v;
    }
    if ( *p++ != L'-' ) return false;
    for ( int i = 2; i < 8; ++i )
    {
      if ( !ReadHex ( p, 2, v ) ) return false;
      rGuid.Data4[i] = (unsigned char)v;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Reads
// ---------------------------------------------------------------------------

//
//  Read any integer-family value at any width
//  NOTES: ReadAnyInt is the core's one width-agnostic reader.  Every other
//         integer accessor it has (c_int, c_int64, c_uint, ...) raises unless
//         the node is that EXACT subtype, which makes them unusable from a
//         caller who did not write the node
//
HRESULT
ReadInt ( const P3PmsgData& rData, long long *outValue, int *outUnsigned )
{
    if ( !outValue )
      return E_POINTER;
    try
    {
      INT64 iValue    = 0;
      bool  bUnsigned = false;
      if ( !rData.ReadAnyInt ( iValue, bUnsigned ) )
        return MSGF_E_TYPE;
      *outValue = (long long)iValue;
      if ( outUnsigned )
        *outUnsigned = bUnsigned ? 1 : 0;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
ReadReal ( const P3PmsgData& rData, double *outValue )
{
    if ( !outValue )
      return E_POINTER;
    try
    {
      switch ( rData.DataType ( ) )
      {
        case VBLockData_FLOAT:  *outValue = (double)rData.c_float  ( ); return S_OK;
        case VBLockData_DOUBLE: *outValue =         rData.c_double ( ); return S_OK;
        default:                return MSGF_E_TYPE;
      }
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
ReadText ( const P3PmsgData& rData, wchar_t *buf, unsigned int *cch )
{
    if ( !cch )
      return E_POINTER;
    try
    {
      if ( !IsTextType ( rData.DataType ( ) ) )
        return MSGF_E_TYPE;
      return CopyOut ( rData.c_wstr ( ), buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
ReadBlob ( const P3PmsgData& rData, void *buf, unsigned int *size )
{
    if ( !size )
      return E_POINTER;
    try
    {
      if ( rData.DataType ( ) != VBLockData_BLOB16 )
        return MSGF_E_TYPE;
      const void  *pv    = rData.c_vBlob ( );
      unsigned int uSize = (unsigned int)rData.c_size ( );
      return CopyOutBytes ( pv, uSize, buf, size );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
ReadGuid ( const P3PmsgData& rData, wchar_t *buf, unsigned int *cch )
{
    if ( !cch )
      return E_POINTER;
    try
    {
      if ( rData.DataType ( ) != VBLockData_GUID )
        return MSGF_E_TYPE;
      const GUID *pGuid = (const GUID*)rData.c_vGUID ( );
      wchar_t     szText[40] = { 0 };
      ::swprintf_s ( szText, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"
                   , (unsigned long)pGuid->Data1
                   , (unsigned)pGuid->Data2, (unsigned)pGuid->Data3
                   , pGuid->Data4[0], pGuid->Data4[1], pGuid->Data4[2]
                   , pGuid->Data4[3], pGuid->Data4[4], pGuid->Data4[5]
                   , pGuid->Data4[6], pGuid->Data4[7] );
      return CopyOut ( szText, buf, cch );
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  Writes
// ---------------------------------------------------------------------------

//
//  Overwrite an integer cell IN PLACE, keeping its declared width
//  NOTES: Recreate rather than the c_* accessors, and the reason is not
//         stylistic: the accessors cannot express two of the nine widths this
//         ABI accepts.  P3PmsgData offers c_char/c_short/c_int/c_uint/
//         c_int64/c_uint64 and NO unsigned narrow pair, so a UINT08 or UINT16
//         node has no accessor at all.  Recreate writes the union member for
//         every type in the family
//       : Recreate does NOT clear VBLockAttr_NULL (only the accessors do), so
//         a value written into a node that had never been assigned would read
//         back as still-null.  SetAttr closes that, and marks the store dirty
//         on the way through
//
HRESULT
WriteInt ( P3PmsgData& rData, long long iValue )
{
    try
    {
      unsigned char uType = rData.DataType ( );
      if ( !IsIntType ( uType ) )
        return MSGF_E_TYPE;
      if ( !IntFits ( iValue, uType ) )
        return MSGF_E_LIMIT;

      switch ( uType )
      {
        case VBLockData_INT08:  { INT08  v = (INT08)iValue;   rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_UINT08: { UINT08 v = (UINT08)iValue;  rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_INT16:  { INT16  v = (INT16)iValue;   rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_UINT16: { UINT16 v = (UINT16)iValue;  rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_INT32:  { INT32  v = (INT32)iValue;   rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_UINT32: { UINT32 v = (UINT32)iValue;  rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_INT64:  { INT64  v = (INT64)iValue;   rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_UINT64: { UINT64 v = (UINT64)iValue;  rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_BOOL:   { bool   v = iValue != 0;     rData.Recreate ( uType, &v, 0 ); break; }
        default:                return MSGF_E_TYPE;
      }
      rData.SetAttr ( 0, VBLockAttr_NULL );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
WriteReal ( P3PmsgData& rData, double dValue )
{
    try
    {
      unsigned char uType = rData.DataType ( );
      switch ( uType )
      {
        case VBLockData_FLOAT:  { float  v = (float)dValue; rData.Recreate ( uType, &v, 0 ); break; }
        case VBLockData_DOUBLE: {                           rData.Recreate ( uType, &dValue, 0 ); break; }
        default:                return MSGF_E_TYPE;
      }
      rData.SetAttr ( 0, VBLockAttr_NULL );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Overwrite a text cell
//  NOTES: c_wcscpy is the one write path that GROWS the stored blob -- up to
//         the cell's BlobMax, past which it raises.  That raise is the only
//         signal there is, so it is caught and reported as MSGF_E_LIMIT rather
//         than left to escape as a core exception
//       : The over-cap case is refused BEFORE the call as well, because the
//         core's over-capacity path raises from inside the heap copy after it
//         has already begun, and unwinding out of there is a documented
//         exception-safety gap in the core rather than a clean rejection
//
HRESULT
WriteText ( P3PmsgData& rData, const wchar_t *lpszValue, bool bMayGrow )
{
    if ( !lpszValue )
      return E_POINTER;
    if ( ::wcslen ( lpszValue ) > MAX_TEXT )
      return MSGF_E_LIMIT;
    try
    {
      if ( !IsTextType ( rData.DataType ( ) ) )
        return MSGF_E_TYPE;

      if ( !bMayGrow )
      {
        // c_size() answers the bytes CURRENTLY used, which is the only size
        // this ABI can see: the capacity accessor the core declares
        // (c_size_max) has no definition anywhere, and the free functions that
        // would answer it (VBLockData_BlobSize / _BlobMax) are not exported.
        // Used-size is therefore the bound, which is exact for a cell created
        // at its value's length -- every cell this facade creates -- and
        // conservative for one that was not.
        //
        // The store's wide unit is 16 bits by definition of the format, not by
        // sizeof(wchar_t): that is what makes a saved store readable on a
        // platform whose wchar_t is 32.
        size_t nNeed = ::wcslen ( lpszValue ) * 2;
        if ( nNeed > rData.c_size ( ) )
          return MSGF_E_LIMIT;
      }

      rData.c_wcscpy ( lpszValue );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_LIMIT; }
}

// ---------------------------------------------------------------------------
//  Declares
// ---------------------------------------------------------------------------
namespace {

//
//  Declare one value into one scope of one live field
//  NOTES: The scope switch lives HERE and nowhere else, which is what makes
//         "one pair of verbs for both collections" a one-line cost rather than
//         a parallel family
//       : AttrCMD_Create on the way in: a scope that has never been written to
//         has no collection block at all, and DeclareItem into the descendant
//         scope creates one itself while the attribute scope's does not
//
P3PmsgField*
DoDeclare ( P3PmsgField& rParent, unsigned int uScope
          , const wchar_t *lpszName, const P3PmsgData& rData, BOOL bUpdate )
{
    if ( uScope == MSGF_SCOPE_ATTR )
      return &rParent.r_Attr ( P3PmsgField::AttrCMD_Create )
                     .DeclareItem ( lpszName, rData, bUpdate ? true : false );
    return &rParent.r_Desc ( P3PmsgField::AttrCMD_Create )
                   .DeclareItem ( lpszName, rData, bUpdate );
}

} // namespace

//
//  Declare an integer child at an EXPLICIT width
//  NOTES: One switch arm per type, each building the P3PmsgData as a temporary
//         passed straight into DoDeclare.  It is never built into a local and
//         assigned: P3PmsgData::operator= resizes and copies THROUGH THE HEAP,
//         which is a store mutation rather than a value construction, and it
//         would run against whatever the local happened to be connected to
//
HRESULT
DeclareInt ( P3PmsgField& rParent, unsigned int uScope
           , const wchar_t *lpszName, long long iValue
           , unsigned char uType, BOOL bUpdate )
{
    if ( uType == 0 )
      uType = VBLockData_INT32;
    if ( !IsIntType ( uType ) )
      return MSGF_E_TYPE;
    if ( !IntFits ( iValue, uType ) )
      return MSGF_E_LIMIT;

    try
    {
      switch ( uType )
      {
        case VBLockData_INT08:  DoDeclare ( rParent, uScope, lpszName, P3PmsgData((INT08 )iValue), bUpdate ); break;
        case VBLockData_UINT08: DoDeclare ( rParent, uScope, lpszName, P3PmsgData((UINT08)iValue), bUpdate ); break;
        case VBLockData_INT16:  DoDeclare ( rParent, uScope, lpszName, P3PmsgData((INT16 )iValue), bUpdate ); break;
        case VBLockData_UINT16: DoDeclare ( rParent, uScope, lpszName, P3PmsgData((UINT16)iValue), bUpdate ); break;
        case VBLockData_INT32:  DoDeclare ( rParent, uScope, lpszName, P3PmsgData((INT32 )iValue), bUpdate ); break;
        case VBLockData_UINT32: DoDeclare ( rParent, uScope, lpszName, P3PmsgData((UINT32)iValue), bUpdate ); break;
        case VBLockData_INT64:  DoDeclare ( rParent, uScope, lpszName, P3PmsgData((INT64 )iValue), bUpdate ); break;
        case VBLockData_UINT64: DoDeclare ( rParent, uScope, lpszName, P3PmsgData((UINT64)iValue), bUpdate ); break;
        case VBLockData_BOOL:   DoDeclare ( rParent, uScope, lpszName, P3PmsgData(iValue != 0   ), bUpdate ); break;
        default:                return MSGF_E_TYPE;
      }
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
DeclareReal ( P3PmsgField& rParent, unsigned int uScope
            , const wchar_t *lpszName, double dValue
            , unsigned char uType, BOOL bUpdate )
{
    if ( uType == 0 )
      uType = VBLockData_DOUBLE;
    if ( !IsRealType ( uType ) )
      return MSGF_E_TYPE;

    try
    {
      if ( uType == VBLockData_FLOAT )
        DoDeclare ( rParent, uScope, lpszName, P3PmsgData((float)dValue), bUpdate );
      else
        DoDeclare ( rParent, uScope, lpszName, P3PmsgData(dValue), bUpdate );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
DeclareText ( P3PmsgField& rParent, unsigned int uScope
            , const wchar_t *lpszName, const wchar_t *lpszValue, BOOL bUpdate )
{
    if ( !lpszValue )
      return E_POINTER;
    if ( ::wcslen ( lpszValue ) > MAX_TEXT )
      return MSGF_E_LIMIT;

    try
    {
      DoDeclare ( rParent, uScope, lpszName, P3PmsgData(lpszValue), bUpdate );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
DeclareBlob ( P3PmsgField& rParent, unsigned int uScope
            , const wchar_t *lpszName, const void *pvValue
            , unsigned int uSize, BOOL bUpdate )
{
    if ( uSize && !pvValue )
      return E_POINTER;
    if ( uSize > MAX_BLOB )
      return MSGF_E_LIMIT;

    try
    {
      DoDeclare ( rParent, uScope, lpszName
                , P3PmsgData ( pvValue, (VBLsize)uSize, VBLockData_BLOB16 ), bUpdate );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
DeclareGuid ( P3PmsgField& rParent, unsigned int uScope
            , const wchar_t *lpszName, const wchar_t *lpszGuid, BOOL bUpdate )
{
    GUID oGuid = { 0 };
    if ( !ParseGuid ( lpszGuid, oGuid ) )
      return MSGF_E_NAME;

    try
    {
      DoDeclare ( rParent, uScope, lpszName, P3PmsgData(oGuid), bUpdate );
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

//
//  Declare a ZERO value of uType -- what Retype writes
//  NOTES: DeclareItem's update path assigns the whole P3PmsgData, and
//         P3PmsgData::operator= copies the VBLockData INCLUDING its type word.
//         So this retypes an existing child in place, keeping its name, its
//         position and its children, and it works in either scope
//
HRESULT
DeclareZero ( P3PmsgField& rParent, unsigned int uScope
            , const wchar_t *lpszName, unsigned char uType )
{
    if ( IsIntType  ( uType ) )
      return DeclareInt  ( rParent, uScope, lpszName, 0, uType, TRUE );
    if ( IsRealType ( uType ) )
      return DeclareReal ( rParent, uScope, lpszName, 0.0, uType, TRUE );

    try
    {
      switch ( uType )
      {
        case VBLockData_WSTR16:
        case VBLockData_BSTR16:
          DoDeclare ( rParent, uScope, lpszName, P3PmsgData(L""), TRUE );
          return S_OK;
        case VBLockData_BLOB16:
        {
          // A one-byte zero rather than a zero-length allocation: the core's
          // blob constructor sizes the block from this count, and a block with
          // no room at all is not something the rest of the store expects to
          // meet.  A retype seeds a value; it does not promise an empty one.
          const unsigned char uZero = 0;
          DoDeclare ( rParent, uScope, lpszName
                    , P3PmsgData ( &uZero, (VBLsize)1, VBLockData_BLOB16 ), TRUE );
          return S_OK;
        }
        case VBLockData_GUID:
        {
          GUID oGuid = { 0 };
          DoDeclare ( rParent, uScope, lpszName, P3PmsgData(oGuid), TRUE );
          return S_OK;
        }
        default:
          return MSGF_E_TYPE;
      }
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

// ---------------------------------------------------------------------------
//  Containers
// ---------------------------------------------------------------------------
namespace {

//
//  A zero-valued P3PmsgData of uType, for a vect's element prototype
//  NOTES: Returned BY VALUE, which is safe for exactly the reason a
//         P3PmsgData built from a C++ literal is safe anywhere: it is a
//         standalone value with its own P3PmsgObject and no connection to any
//         heap.  It is the CONNECTED ones that must never be copied about
//
P3PmsgData
MakeProto ( unsigned char uType )
{
    switch ( uType )
    {
      case VBLockData_INT08:  return P3PmsgData ( (INT08 )0 );
      case VBLockData_UINT08: return P3PmsgData ( (UINT08)0 );
      case VBLockData_INT16:  return P3PmsgData ( (INT16 )0 );
      case VBLockData_UINT16: return P3PmsgData ( (UINT16)0 );
      case VBLockData_UINT32: return P3PmsgData ( (UINT32)0 );
      case VBLockData_INT64:  return P3PmsgData ( (INT64 )0 );
      case VBLockData_UINT64: return P3PmsgData ( (UINT64)0 );
      case VBLockData_FLOAT:  return P3PmsgData ( 0.0f );
      case VBLockData_DOUBLE: return P3PmsgData ( 0.0 );
      case VBLockData_BOOL:   return P3PmsgData ( false );
      case VBLockData_WSTR16:
      case VBLockData_BSTR16: return P3PmsgData ( L"" );
      case VBLockData_INT32:
      default:                return P3PmsgData ( (INT32)0 );
    }
}

} // namespace

HRESULT
AttachList ( P3PmsgField& rParent, unsigned int uScope, const wchar_t *lpszName )
{
    try
    {
      P3PmsgList oList ( lpszName, P3PmsgData ( (INT32)0 ) );
      if ( uScope == MSGF_SCOPE_ATTR )
        rParent.r_Attr ( P3PmsgField::AttrCMD_Create ) += oList;
      else
        rParent.r_Desc ( P3PmsgField::AttrCMD_Create ) += oList;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
AttachVect ( P3PmsgField& rParent, unsigned int uScope, const wchar_t *lpszName
           , unsigned int uElems, unsigned char uType )
{
    try
    {
      P3PmsgVect oVect ( (int)uElems, lpszName, MakeProto ( uType ) );
      if ( uScope == MSGF_SCOPE_ATTR )
        rParent.r_Attr ( P3PmsgField::AttrCMD_Create ) += oVect;
      else
        rParent.r_Desc ( P3PmsgField::AttrCMD_Create ) += oVect;
      return S_OK;
    }
    catch ( ... ) { return MSGF_E_CORE; }
}

P3PmsgData*
ListCellAt ( P3PmsgList& rList, unsigned int nIndex )
{
    VBLaddr aPos = rList.GetHeadPos ( );
    for ( unsigned int i = 0; i < nIndex && aPos; ++i )
      rList.GetNext ( aPos );
    if ( !aPos )
      return nullptr;
    return &rList.GetNext ( aPos );
}
