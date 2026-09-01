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
// FacadeSmokeTest.cpp -- the facade's own client.
//
// It includes ONLY the facade's public headers.  No <afx*>, no MFC, no
// Msgcore, no P2Pmsg, no VBLock -- so this file stops compiling the moment the
// facade starts leaking its internals, which is the whole point of building it
// as a separate executable rather than as another source file in the DLL.
//
// Exit code 0 = every check passed.

#include <windows.h>
#include <stdio.h>

#include "MsgFacade.h"
#include "MsgFacadeFn.hpp"

using namespace msgf;

// ---------------------------------------------------------------------------
//  Harness
// ---------------------------------------------------------------------------
static int g_nPass = 0;
static int g_nFail = 0;

static void
Check ( bool bOk, const wchar_t *lpszWhat )
{
    if ( bOk ) { ++g_nPass; return; }
    ++g_nFail;
    wprintf ( L"  FAIL: %s\n", lpszWhat );
}

static void
CheckHr ( HRESULT hr, HRESULT hrWant, const wchar_t *lpszWhat )
{
    if ( hr == hrWant ) { ++g_nPass; return; }
    ++g_nFail;
    wprintf ( L"  FAIL: %s (hr=0x%08X, wanted 0x%08X)\n"
            , lpszWhat, (unsigned)hr, (unsigned)hrWant );
}

static void
Section ( const wchar_t *lpszName )
{
    wprintf ( L"\n== %s ==\n", lpszName );
}

// The caller-sized buffer protocol, once, for the tests that use it directly.
template <class T>
static bool
Text ( T *p, HRESULT (T::*pfn)(wchar_t*,unsigned int*) const
     , wchar_t *buf, unsigned int cchBuf )
{
    unsigned int cch = cchBuf;
    return SUCCEEDED ( (p->*pfn) ( buf, &cch ) );
}

static const wchar_t *g_szFile  = L"FacadeSmokeTest.p2p";
static const wchar_t *g_szFile2 = L"FacadeSmokeTest2.p2p";

// ---------------------------------------------------------------------------
//  1 -- the library and the ABI gate
// ---------------------------------------------------------------------------
static void
Test01_Library ( IMsgLibrary **outLib )
{
    Section ( L"1  library and the ABI gate" );

    IMsgLibrary *pBad = (IMsgLibrary*)(void*)1;
    CheckHr ( MSGF_CreateLibrary ( 0, &pBad ), MSGF_E_ABI_MISMATCH
            , L"ABI 0 is refused" );
    Check ( pBad == 0, L"a refused create nulls the out pointer" );
    CheckHr ( MSGF_CreateLibrary ( ABI_VERSION + 1, &pBad ), MSGF_E_ABI_MISMATCH
            , L"a future ABI is refused" );
    CheckHr ( MSGF_CreateLibrary ( ABI_VERSION, 0 ), E_POINTER
            , L"a null out pointer is refused" );

    IMsgLibrary *pLib = 0;
    CheckHr ( MSGF_CreateLibrary ( ABI_VERSION, &pLib ), S_OK, L"create at this ABI" );
    Check ( pLib != 0, L"the library came back" );

    // The singleton really is one: a second create hands back the same object.
    IMsgLibrary *pLib2 = 0;
    CheckHr ( MSGF_CreateLibrary ( ABI_VERSION, &pLib2 ), S_OK, L"a second create" );
    Check ( pLib2 == pLib, L"the library is a refcounted singleton" );
    pLib2->Release ( );

    const wchar_t *lpszVer = pLib->VersionString ( );
    Check ( lpszVer && ::wcsstr ( lpszVer, L"MsgFacade" ) != 0
          , L"the version string names the component" );
    Check ( lpszVer && ::wcsstr ( lpszVer, L"Msgcore" ) != 0
          , L"the version string names the kernel under it" );
    wprintf ( L"  %s\n", lpszVer );

    // Type vocabulary, both directions.
    wchar_t szType[32]; unsigned int cch = 32;
    CheckHr ( pLib->TypeName ( MSGF_TYPE_WSTR16, szType, &cch ), S_OK, L"TypeName" );
    Check ( ::wcscmp ( szType, L"WSTR16" ) == 0, L"WSTR16 spells itself" );
    unsigned char uType = 0;
    CheckHr ( pLib->TypeFromName ( L"UINT64", &uType ), S_OK, L"TypeFromName" );
    Check ( uType == MSGF_TYPE_UINT64, L"the vocabulary round-trips" );
    CheckHr ( pLib->TypeFromName ( L"NOSUCH", &uType ), MSGF_E_TYPE
            , L"an unknown type name is refused" );

    // Name validation is the core's, and it is what makes the path grammar work.
    CheckHr ( pLib->IsValidName ( L"Title" ),   S_OK,    L"a plain name is valid" );
    CheckHr ( pLib->IsValidName ( L"a.b" ),     S_FALSE, L"a name holding '.' is not" );
    CheckHr ( pLib->IsValidName ( L"a@b" ),     S_FALSE, L"a name holding '@' is not" );
    CheckHr ( pLib->IsValidName ( L"" ),        S_FALSE, L"an empty name is not" );

    CheckHr ( pLib->WildcardMatch ( L"Con*", L"Config" ), S_OK,    L"a wildcard hit" );
    CheckHr ( pLib->WildcardMatch ( L"Con*", L"Window" ), S_FALSE, L"a wildcard miss" );

    *outLib = pLib;
}

// ---------------------------------------------------------------------------
//  2 -- a store, its root, and the two scopes
// ---------------------------------------------------------------------------
static void
Test02_StoreAndScopes ( IMsgLibrary *pLib )
{
    Section ( L"2  a store, its root, and the two scopes" );

    IMsgStore *pStore = 0;
    CheckHr ( pLib->CreateStore ( 0, 0, 0, &pStore ), S_OK, L"CreateStore" );

    int nValid = 0;
    CheckHr ( pStore->IsValid ( &nValid ), S_OK, L"IsValid" );
    Check ( nValid != 0, L"a fresh store is structurally sound" );

    unsigned int uSize = 0;
    CheckHr ( pStore->GetSize ( &uSize ), S_OK, L"GetSize" );
    Check ( uSize > 0, L"a fresh store already occupies a heap" );

    IMsgNode *pRoot = 0;
    CheckHr ( pStore->GetRoot ( &pRoot ), S_OK, L"GetRoot" );

    wchar_t szPath[512]; unsigned int cch = 512;
    CheckHr ( pRoot->GetPath ( szPath, &cch ), S_OK, L"the root has a path" );
    Check ( ::wcscmp ( szPath, L"" ) == 0, L"and it is the empty string" );

    unsigned int uKind = 0;
    CheckHr ( pRoot->GetKind ( &uKind ), S_OK, L"GetKind on the root" );
    Check ( uKind == MSGF_KIND_ITEM, L"the root is an item" );

    // ONE pair of verbs, two collections.
    CheckHr ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"Hello", 0, 0 )
            , S_OK, L"a child in the CHILD scope" );
    CheckHr ( pRoot->DeclareText ( MSGF_SCOPE_ATTR,  L"Title", L"attr!", 0, 0 )
            , S_OK, L"the SAME name in the ATTR scope" );

    unsigned int uCount = 0;
    CheckHr ( pRoot->GetCount ( MSGF_SCOPE_CHILD, &uCount ), S_OK, L"child count" );
    Check ( uCount == 1, L"one child" );
    CheckHr ( pRoot->GetCount ( MSGF_SCOPE_ATTR, &uCount ), S_OK, L"attr count" );
    Check ( uCount == 1, L"one attribute" );

    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"Title" ), S_OK,    L"child exists" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"Nope" ),  S_FALSE, L"a miss is S_FALSE, not an error" );
    CheckHr ( pRoot->Exists ( 99, L"Title" ), MSGF_E_SCOPE, L"an unknown scope is refused" );

    // The two are genuinely different nodes.
    IMsgNode *pChild = 0, *pAttr = 0;
    CheckHr ( pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Title", &pChild ), S_OK, L"GetChild" );
    CheckHr ( pRoot->GetChild ( MSGF_SCOPE_ATTR,  L"Title", &pAttr  ), S_OK, L"GetChild in ATTR" );

    wchar_t szText[64];
    cch = 64; CheckHr ( pChild->GetText ( szText, &cch ), S_OK, L"read the child" );
    Check ( ::wcscmp ( szText, L"Hello" ) == 0, L"the child's value" );
    cch = 64; CheckHr ( pAttr->GetText ( szText, &cch ), S_OK, L"read the attribute" );
    Check ( ::wcscmp ( szText, L"attr!" ) == 0, L"the attribute's value" );

    cch = 512;
    CheckHr ( pChild->GetPath ( szPath, &cch ), S_OK, L"the child's path" );
    Check ( ::wcscmp ( szPath, L".Title" ) == 0, L"'.' introduces a descendant" );
    cch = 512;
    CheckHr ( pAttr->GetPath ( szPath, &cch ), S_OK, L"the attribute's path" );
    Check ( ::wcscmp ( szPath, L"@Title" ) == 0, L"'@' introduces an attribute" );

    pAttr->Release ( );
    pChild->Release ( );
    pRoot->Release ( );
    pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  3 -- the caller-sized buffer protocol
// ---------------------------------------------------------------------------
static void
Test03_BufferProtocol ( IMsgLibrary *pLib )
{
    Section ( L"3  the caller-sized buffer protocol" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Word", L"abcdef", 0, 0 );

    IMsgNode *pNode = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Word", &pNode );

    unsigned int cch = 0;
    CheckHr ( pNode->GetText ( 0, &cch ), S_OK, L"a NULL buffer is a size query" );
    Check ( cch == 7, L"and the size counts the terminator" );

    wchar_t szSmall[4]; cch = 4;
    CheckHr ( pNode->GetText ( szSmall, &cch )
            , HRESULT_FROM_WIN32 ( ERROR_MORE_DATA ), L"a short buffer is refused" );
    Check ( cch == 7, L"and still reports what was needed" );

    wchar_t szExact[7]; cch = 7;
    CheckHr ( pNode->GetText ( szExact, &cch ), S_OK, L"an exact buffer fits" );
    Check ( ::wcscmp ( szExact, L"abcdef" ) == 0, L"and holds the whole value" );

    CheckHr ( pNode->GetText ( szExact, 0 ), E_POINTER, L"a null size is refused" );

    pNode->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  4 -- every value type, and the width rules
// ---------------------------------------------------------------------------
static void
Test04_Values ( IMsgLibrary *pLib )
{
    Section ( L"4  values, widths and the type rules" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    struct { const wchar_t *name; unsigned char type; long long value; }
    aInts[] =
    {
      { L"i8",  MSGF_TYPE_INT08,  -100        },
      { L"u8",  MSGF_TYPE_UINT08,  200        },
      { L"i16", MSGF_TYPE_INT16,  -30000      },
      { L"u16", MSGF_TYPE_UINT16,  60000      },
      { L"i32", MSGF_TYPE_INT32,  -2000000000 },
      { L"u32", MSGF_TYPE_UINT32,  4000000000 },
      { L"i64", MSGF_TYPE_INT64,   9000000000000LL },
      { L"u64", MSGF_TYPE_UINT64,  9000000000000LL },
      { L"b",   MSGF_TYPE_BOOL,    1          },
    };

    for ( int i = 0; i < _countof(aInts); ++i )
    {
      HRESULT hr = pRoot->DeclareInt ( MSGF_SCOPE_CHILD, aInts[i].name
                                     , aInts[i].value, aInts[i].type, 0, 0 );
      CheckHr ( hr, S_OK, L"declare an integer at an explicit width" );

      IMsgNode *pNode = 0;
      pRoot->GetChild ( MSGF_SCOPE_CHILD, aInts[i].name, &pNode );

      unsigned char uType = 0;
      pNode->GetType ( &uType );
      Check ( uType == aInts[i].type, L"the declared width is what was stored" );

      long long iBack = 0; int bUnsigned = -1;
      CheckHr ( pNode->GetInt ( &iBack, &bUnsigned ), S_OK
              , L"a width-agnostic read of any integer" );
      Check ( iBack == aInts[i].value, L"the value round-trips at its own width" );

      bool bWantUnsigned = aInts[i].type == MSGF_TYPE_UINT08
                        || aInts[i].type == MSGF_TYPE_UINT16
                        || aInts[i].type == MSGF_TYPE_UINT32
                        || aInts[i].type == MSGF_TYPE_UINT64;
      Check ( ( bUnsigned != 0 ) == bWantUnsigned
            , L"and reports whether its subtype was unsigned" );

      pNode->Release ( );
    }

    // The width check the core does NOT do: it would narrow in the cast and
    // store the wrong number without a word.
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"over", 300, MSGF_TYPE_INT08, 0, 0 )
            , MSGF_E_LIMIT, L"300 does not fit an INT08" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"neg", -1, MSGF_TYPE_UINT16, 0, 0 )
            , MSGF_E_LIMIT, L"-1 does not fit a UINT16" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"over" ), S_FALSE
            , L"and the refused declare wrote nothing" );

    // Reals.
    CheckHr ( pRoot->DeclareReal ( MSGF_SCOPE_CHILD, L"d", 3.5, 0, 0, 0 ), S_OK, L"a DOUBLE" );
    CheckHr ( pRoot->DeclareReal ( MSGF_SCOPE_CHILD, L"f", 1.5, MSGF_TYPE_FLOAT, 0, 0 ), S_OK, L"a FLOAT" );

    IMsgNode *pNode = 0; double dBack = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"d", &pNode );
    CheckHr ( pNode->GetReal ( &dBack ), S_OK, L"read a DOUBLE" );
    Check ( dBack == 3.5, L"the DOUBLE round-trips" );
    pNode->Release ( );

    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"f", &pNode );
    CheckHr ( pNode->GetReal ( &dBack ), S_OK
            , L"read a FLOAT -- which the core's own GetDouble raises on" );
    Check ( dBack == 1.5, L"the FLOAT round-trips" );
    // Asking for the wrong family is an ANSWER, not an exception.
    long long iJunk = 0;
    CheckHr ( pNode->GetInt ( &iJunk, 0 ), MSGF_E_TYPE, L"a FLOAT is not an integer" );
    pNode->Release ( );

    // Blob.
    const unsigned char aBytes[] = { 0x00, 0x01, 0xFE, 0xFF, 0x42 };
    CheckHr ( pRoot->DeclareBlob ( MSGF_SCOPE_CHILD, L"blob", aBytes, sizeof(aBytes), 0, 0 )
            , S_OK, L"a BLOB" );
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"blob", &pNode );
    unsigned char aBack[16]; unsigned int cb = sizeof(aBack);
    CheckHr ( pNode->GetBlob ( aBack, &cb ), S_OK, L"read the BLOB" );
    Check ( cb == sizeof(aBytes), L"the byte count round-trips" );
    Check ( ::memcmp ( aBack, aBytes, sizeof(aBytes) ) == 0
          , L"including the embedded NUL and the high bytes" );
    pNode->Release ( );

    // GUID, as canonical text.
    const wchar_t *szGuid = L"3F2504E0-4F89-11D3-9A0C-0305E82C3301";
    CheckHr ( pRoot->DeclareGuid ( MSGF_SCOPE_CHILD, L"guid", szGuid, 0, 0 ), S_OK, L"a GUID" );
    CheckHr ( pRoot->DeclareGuid ( MSGF_SCOPE_CHILD, L"guid2", L"{3F2504E0-4F89-11D3-9A0C-0305E82C3301}", 0, 0 )
            , S_OK, L"braces are tolerated" );
    CheckHr ( pRoot->DeclareGuid ( MSGF_SCOPE_CHILD, L"guid3", L"not-a-guid", 0, 0 )
            , MSGF_E_NAME, L"a malformed GUID is refused" );
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"guid", &pNode );
    wchar_t szBack[64]; unsigned int cch = 64;
    CheckHr ( pNode->GetGuid ( szBack, &cch ), S_OK, L"read the GUID" );
    Check ( ::wcscmp ( szBack, szGuid ) == 0, L"canonical, upper hex, no braces" );
    pNode->Release ( );

    // Text limits.
    CheckHr ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"t", L"", 0, 0 ), S_OK
            , L"an empty string is a value" );

    pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  5 -- declare collisions, names and scopes
// ---------------------------------------------------------------------------
static void
Test05_DeclareRules ( IMsgLibrary *pLib )
{
    Section ( L"5  declare collisions, names and scopes" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"n", 1, 0, 0, 0 ), S_OK, L"first declare" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"n", 2, 0, 0, 0 ), MSGF_E_EXISTS
            , L"a second one collides -- the core would silently return the first" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"n", 2, 0, MSGF_DECLARE_UPDATE, 0 )
            , S_OK, L"asking to update is explicit" );

    IMsgNode *pNode = 0; long long iVal = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"n", &pNode );
    pNode->GetInt ( &iVal, 0 );
    Check ( iVal == 2, L"and the update landed" );
    pNode->Release ( );

    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"",      1, 0, 0, 0 ), MSGF_E_NAME, L"an empty name" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"a.b",   1, 0, 0, 0 ), MSGF_E_NAME, L"a name holding '.'" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"a@b",   1, 0, 0, 0 ), MSGF_E_NAME, L"a name holding '@'" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"a:b",   1, 0, 0, 0 ), MSGF_E_NAME, L"a name holding ':'" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"a\\b",  1, 0, 0, 0 ), MSGF_E_NAME, L"a name holding '\\'" );
    CheckHr ( pRoot->DeclareInt ( 7, L"x", 1, 0, 0, 0 ), MSGF_E_SCOPE, L"an unknown scope" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"x", 1, 200, 0, 0 ), MSGF_E_TYPE
            , L"an unknown storage type" );

    // The out-node is optional.
    IMsgNode *pOut = 0;
    CheckHr ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"withOut", L"v", 0, &pOut ), S_OK
            , L"a declare that asks for the node back" );
    Check ( pOut != 0, L"and gets one" );
    if ( pOut ) pOut->Release ( );

    pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  6 -- in-place writes keep the declared width
// ---------------------------------------------------------------------------
static void
Test06_InPlaceWrites ( IMsgLibrary *pLib )
{
    Section ( L"6  in-place writes keep the declared width" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    // UINT08 and UINT16 are the two widths the core has NO accessor for -- a
    // facade built on the c_* family could not write either of them.
    struct { const wchar_t *name; unsigned char type; long long from; long long to; }
    a[] =
    {
      { L"i8",  MSGF_TYPE_INT08,   1,  -128       },
      { L"u8",  MSGF_TYPE_UINT08,  1,   255       },
      { L"i16", MSGF_TYPE_INT16,   1,  -32768     },
      { L"u16", MSGF_TYPE_UINT16,  1,   65535     },
      { L"i32", MSGF_TYPE_INT32,   1,   123456    },
      { L"u32", MSGF_TYPE_UINT32,  1,   4000000000 },
      { L"i64", MSGF_TYPE_INT64,   1,   1234567890123LL },
      { L"u64", MSGF_TYPE_UINT64,  1,   1234567890123LL },
      { L"b",   MSGF_TYPE_BOOL,    0,   1         },
    };

    for ( int i = 0; i < _countof(a); ++i )
    {
      pRoot->DeclareInt ( MSGF_SCOPE_CHILD, a[i].name, a[i].from, a[i].type, 0, 0 );
      IMsgNode *pNode = 0;
      pRoot->GetChild ( MSGF_SCOPE_CHILD, a[i].name, &pNode );

      CheckHr ( pNode->SetInt ( a[i].to ), S_OK, L"SetInt at this width" );

      long long iBack = 0;
      pNode->GetInt ( &iBack, 0 );
      Check ( iBack == a[i].to, L"the new value is there" );

      unsigned char uType = 0;
      pNode->GetType ( &uType );
      Check ( uType == a[i].type, L"and the width did not change" );

      CheckHr ( pNode->SetInt ( 1LL << 62 ), a[i].type == MSGF_TYPE_INT64 ||
                                             a[i].type == MSGF_TYPE_UINT64 ||
                                             a[i].type == MSGF_TYPE_BOOL
                                               ? S_OK : MSGF_E_LIMIT
              , L"an over-wide write is refused rather than truncated" );

      pNode->Release ( );
    }

    // Text grows in place.
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"t", L"short", 0, 0 );
    IMsgNode *pNode = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"t", &pNode );
    CheckHr ( pNode->SetText ( L"a considerably longer value than before" ), S_OK
            , L"SetText grows the stored value" );
    wchar_t szBack[128]; unsigned int cch = 128;
    pNode->GetText ( szBack, &cch );
    Check ( ::wcscmp ( szBack, L"a considerably longer value than before" ) == 0
          , L"and reads back whole" );
    CheckHr ( pNode->SetInt ( 5 ), MSGF_E_TYPE, L"a text node is not an integer" );
    pNode->Release ( );

    // Reals.
    pRoot->DeclareReal ( MSGF_SCOPE_CHILD, L"f", 1.0, MSGF_TYPE_FLOAT, 0, 0 );
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"f", &pNode );
    CheckHr ( pNode->SetReal ( 2.25 ), S_OK, L"SetReal on a FLOAT" );
    double d = 0; pNode->GetReal ( &d );
    Check ( d == 2.25, L"the FLOAT round-trips" );
    unsigned char uType = 0; pNode->GetType ( &uType );
    Check ( uType == MSGF_TYPE_FLOAT, L"and stays a FLOAT" );
    pNode->Release ( );

    pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  7 -- THE HEADLINE: a node is a path, not a pointer
// ---------------------------------------------------------------------------
static void
Test07_NodesSurviveMutation ( IMsgLibrary *pLib )
{
    Section ( L"7  a node survives every mutation" );

    IMsgStore *pStore = 0; pLib->CreateStore ( MSGF_ADDR_32, 512, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Keep", L"original", 0, 0 );

    IMsgNode *pKeep = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Keep", &pKeep );

    // Force the heap to grow, many times over.  This is the exact operation
    // after which the kernel's own flat ABI says no handle is still valid: a
    // relocation moves every block, so a raw handle taken before this loop is
    // a dangling pointer after it.
    for ( int i = 0; i < 400; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, szName
                         , L"padding padding padding padding padding", 0, 0 );
    }

    unsigned int uCount = 0;
    pRoot->GetCount ( MSGF_SCOPE_CHILD, &uCount );
    Check ( uCount == 401, L"401 children now" );

    wchar_t szText[64]; unsigned int cch = 64;
    CheckHr ( pKeep->GetText ( szText, &cch ), S_OK
            , L"the node held across 400 allocations still resolves" );
    Check ( ::wcscmp ( szText, L"original" ) == 0, L"and reads its own value" );

    CheckHr ( pKeep->SetText ( L"still mine" ), S_OK, L"and can still be written" );
    cch = 64; pKeep->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"still mine" ) == 0, L"the write landed" );

    // The root node too.
    CheckHr ( pRoot->GetCount ( MSGF_SCOPE_CHILD, &uCount ), S_OK
            , L"the root node is equally unbothered" );

    // Delete the node it names, and it says so rather than resolving to
    // whatever now occupies that block.
    CheckHr ( pRoot->Delete ( MSGF_SCOPE_CHILD, L"Keep" ), S_OK, L"delete the node" );
    cch = 64;
    CheckHr ( pKeep->GetText ( szText, &cch ), MSGF_E_NO_ITEM
            , L"the handle to a deleted node reports it" );
    CheckHr ( pKeep->SetText ( L"x" ), MSGF_E_NO_ITEM, L"and refuses to write" );

    // Put the name back and the same handle works again -- which is what
    // holding a path rather than an address means.
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Keep", L"reborn", 0, 0 );
    cch = 64;
    CheckHr ( pKeep->GetText ( szText, &cch ), S_OK, L"re-declaring the name revives it" );
    Check ( ::wcscmp ( szText, L"reborn" ) == 0, L"with the new value" );

    pKeep->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  8 -- restructuring
// ---------------------------------------------------------------------------
static void
Test08_Restructure ( IMsgLibrary *pLib )
{
    Section ( L"8  rename, move, retype, truncate" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pBox = 0;
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"Box", 0, 0, 0, &pBox );
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Old", L"value", 0, 0 );

    // Rename.
    CheckHr ( pRoot->Rename ( MSGF_SCOPE_CHILD, L"Old", L"New" ), S_OK, L"rename a child" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"Old" ), S_FALSE, L"the old name is gone" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"New" ), S_OK,    L"the new name is there" );
    CheckHr ( pRoot->Rename ( MSGF_SCOPE_CHILD, L"New", L"New" ), S_FALSE
            , L"renaming to the same name is a no-op, not an error" );
    CheckHr ( pRoot->Rename ( MSGF_SCOPE_CHILD, L"Ghost", L"X" ), MSGF_E_NO_ITEM
            , L"renaming what is not there" );
    CheckHr ( pRoot->Rename ( MSGF_SCOPE_CHILD, L"New", L"a.b" ), MSGF_E_NAME
            , L"renaming to an unusable name" );

    // Move.
    CheckHr ( pRoot->Move ( MSGF_SCOPE_CHILD, L"New", pBox ), S_OK, L"move a child" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"New" ), S_FALSE, L"gone from the source" );
    CheckHr ( pBox->Exists  ( MSGF_SCOPE_CHILD, L"New" ), S_OK,    L"arrived at the destination" );

    IMsgNode *pMoved = 0;
    pBox->GetChild ( MSGF_SCOPE_CHILD, L"New", &pMoved );
    wchar_t szPath[256]; unsigned int cch = 256;
    pMoved->GetPath ( szPath, &cch );
    Check ( ::wcscmp ( szPath, L".Box.New" ) == 0, L"and its path says so" );
    wchar_t szText[64]; cch = 64;
    pMoved->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"value" ) == 0, L"with its value intact" );
    pMoved->Release ( );

    // Retype.
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"Num", 42, 0, 0, 0 );
    CheckHr ( pRoot->Retype ( MSGF_SCOPE_CHILD, L"Num", MSGF_TYPE_DOUBLE ), S_OK, L"retype" );
    IMsgNode *pNum = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Num", &pNum );
    unsigned char uType = 0; pNum->GetType ( &uType );
    Check ( uType == MSGF_TYPE_DOUBLE, L"the node is a DOUBLE now" );
    double d = -1; CheckHr ( pNum->GetReal ( &d ), S_OK, L"and reads as one" );
    Check ( d == 0.0, L"seeded with a zero value" );
    pNum->Release ( );

    CheckHr ( pRoot->Retype ( MSGF_SCOPE_CHILD, L"Ghost", MSGF_TYPE_INT32 ), MSGF_E_NO_ITEM
            , L"retyping what is not there" );

    // Truncate is SCOPED -- the core's own Truncate drops both collections.
    pRoot->DeclareText ( MSGF_SCOPE_ATTR, L"a1", L"x", 0, 0 );
    pRoot->DeclareText ( MSGF_SCOPE_ATTR, L"a2", L"y", 0, 0 );
    unsigned int uChildren = 0, uAttrs = 0;
    pRoot->GetCount ( MSGF_SCOPE_CHILD, &uChildren );
    Check ( uChildren > 0, L"there are children" );
    CheckHr ( pRoot->Truncate ( MSGF_SCOPE_ATTR ), S_OK, L"truncate the ATTR scope" );
    pRoot->GetCount ( MSGF_SCOPE_ATTR,  &uAttrs );
    pRoot->GetCount ( MSGF_SCOPE_CHILD, &uChildren );
    Check ( uAttrs == 0,    L"the attributes are gone" );
    Check ( uChildren > 0,  L"and the children are NOT" );

    pBox->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  9 -- lists
// ---------------------------------------------------------------------------
static void
Test09_Lists ( IMsgLibrary *pLib )
{
    Section ( L"9  lists" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgList *pList = 0;
    CheckHr ( pRoot->DeclareList ( MSGF_SCOPE_CHILD, L"Items", &pList ), S_OK, L"declare a list" );
    CheckHr ( pRoot->DeclareList ( MSGF_SCOPE_CHILD, L"Items", 0 ), E_POINTER
            , L"a null out pointer is refused" );

    unsigned int n = 99;
    CheckHr ( pList->GetCount ( &n ), S_OK, L"count a new list" );
    Check ( n == 0, L"which is empty" );

    CheckHr ( pList->AddInt  ( 10, 0, MSGF_ADD_TAIL ), S_OK, L"append an int" );
    CheckHr ( pList->AddInt  ( 20, 0, MSGF_ADD_TAIL ), S_OK, L"append another" );
    CheckHr ( pList->AddInt  (  5, 0, MSGF_ADD_HEAD ), S_OK, L"prepend one" );
    CheckHr ( pList->AddReal ( 1.5, 0, MSGF_ADD_TAIL ), S_OK, L"append a real" );
    CheckHr ( pList->AddText ( L"tail", MSGF_ADD_TAIL ), S_OK, L"append text" );

    pList->GetCount ( &n );
    Check ( n == 5, L"five cells" );

    long long iVal = 0;
    CheckHr ( pList->GetIntAt ( 0, &iVal, 0 ), S_OK, L"read cell 0" );
    Check ( iVal == 5, L"the prepend went to the head" );
    CheckHr ( pList->GetIntAt ( 2, &iVal, 0 ), S_OK, L"read cell 2" );
    Check ( iVal == 20, L"the appends kept their order" );

    double dVal = 0;
    CheckHr ( pList->GetRealAt ( 3, &dVal ), S_OK, L"read the real cell" );
    Check ( dVal == 1.5, L"its value" );

    wchar_t szText[64]; unsigned int cch = 64;
    CheckHr ( pList->GetTextAt ( 4, szText, &cch ), S_OK, L"read the text cell" );
    Check ( ::wcscmp ( szText, L"tail" ) == 0, L"its value" );

    unsigned char uType = 0;
    CheckHr ( pList->GetTypeAt ( 0, &uType ), S_OK, L"type of cell 0" );
    Check ( uType == MSGF_TYPE_INT32, L"which is INT32" );
    CheckHr ( pList->GetTypeAt ( 99, &uType ), S_OK
            , L"an index past the end is a SAFE PROBE, not an error" );
    Check ( uType == MSGF_TYPE_NULL, L"answering NULL" );

    CheckHr ( pList->GetIntAt ( 99, &iVal, 0 ), MSGF_E_RANGE
            , L"but READING past the end is out of range" );

    CheckHr ( pList->SetIntAt  ( 0, 7 ), S_OK, L"overwrite a cell" );
    pList->GetIntAt ( 0, &iVal, 0 );
    Check ( iVal == 7, L"the write landed" );
    CheckHr ( pList->SetRealAt ( 0, 1.0 ), MSGF_E_TYPE, L"a typed cell keeps its type" );
    // A list cell lives inside the list's own block and cannot be grown, so a
    // text write must fit what the cell was created with.
    CheckHr ( pList->SetTextAt ( 4, L"TAIL" ), S_OK, L"overwrite a text cell in place" );
    cch = 64; pList->GetTextAt ( 4, szText, &cch );
    Check ( ::wcscmp ( szText, L"TAIL" ) == 0, L"the text write landed" );
    CheckHr ( pList->SetTextAt ( 4, L"no" ), S_OK, L"a shorter value is fine" );
    cch = 64; pList->GetTextAt ( 4, szText, &cch );
    Check ( ::wcscmp ( szText, L"no" ) == 0, L"and shortens the cell" );
    CheckHr ( pList->SetTextAt ( 4, L"far too long for that cell" ), MSGF_E_LIMIT
            , L"a longer one is refused rather than growing the block" );

    CheckHr ( pList->DeleteAt ( 1 ), S_OK, L"delete a cell by index" );
    pList->GetCount ( &n );
    Check ( n == 4, L"four cells now" );
    CheckHr ( pList->DeleteAt ( 99 ), MSGF_E_RANGE, L"deleting past the end" );

    CheckHr ( pList->Drop ( MSGF_DROP_HEAD ), S_OK, L"drop the head" );
    CheckHr ( pList->Drop ( MSGF_DROP_TAIL ), S_OK, L"drop the tail" );
    pList->GetCount ( &n );
    Check ( n == 2, L"two cells now" );

    CheckHr ( pList->Truncate ( ), S_OK, L"truncate" );
    pList->GetCount ( &n );
    Check ( n == 0, L"empty" );
    CheckHr ( pList->Drop ( MSGF_DROP_HEAD ), MSGF_E_RANGE, L"dropping from an empty list" );

    // A list handle survives a mutation, exactly as a node does.
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, szName, L"padding padding padding", 0, 0 );
    }
    CheckHr ( pList->AddInt ( 1, 0, MSGF_ADD_TAIL ), S_OK
            , L"the list handle still works after 200 allocations" );
    pList->GetCount ( &n );
    Check ( n == 1, L"and the cell is there" );

    // GetList tells absent from wrong-kind.
    IMsgList *pOther = 0;
    CheckHr ( pRoot->GetList ( MSGF_SCOPE_CHILD, L"Items", &pOther ), S_OK, L"re-open the list" );
    pOther->Release ( );
    CheckHr ( pRoot->GetList ( MSGF_SCOPE_CHILD, L"Ghost", &pOther ), MSGF_E_NO_ITEM
            , L"no such name" );
    CheckHr ( pRoot->GetList ( MSGF_SCOPE_CHILD, L"pad000", &pOther ), MSGF_E_TYPE
            , L"that name is not a list" );

    pList->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  10 -- vects, including nesting
// ---------------------------------------------------------------------------
static void
Test10_Vects ( IMsgLibrary *pLib )
{
    Section ( L"10  vects" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgVect *pVect = 0;
    CheckHr ( pRoot->DeclareVect ( MSGF_SCOPE_CHILD, L"Row", 4, MSGF_TYPE_INT32, &pVect )
            , S_OK, L"declare a vect of 4 INT32" );

    unsigned int n = 0;
    CheckHr ( pVect->GetCount ( &n ), S_OK, L"count" );
    Check ( n == 4, L"four elements" );

    for ( unsigned int i = 0; i < 4; ++i )
      CheckHr ( pVect->SetIntAt ( i, (long long)( i * 11 ) ), S_OK, L"write an element" );

    for ( unsigned int i = 0; i < 4; ++i )
    {
      long long iVal = -1;
      CheckHr ( pVect->GetIntAt ( i, &iVal, 0 ), S_OK, L"read an element" );
      Check ( iVal == (long long)( i * 11 ), L"the value round-trips" );
    }

    CheckHr ( pVect->GetIntAt ( 99, 0, 0 ), MSGF_E_RANGE, L"reading past the end" );
    CheckHr ( pVect->SetIntAt ( 99, 1 ),    MSGF_E_RANGE, L"writing past the end" );

    unsigned char uType = 0;
    CheckHr ( pVect->GetTypeAt ( 0, &uType ), S_OK, L"element type" );
    Check ( uType == MSGF_TYPE_INT32, L"which is the declared prototype" );

    unsigned int uKind = 0;
    CheckHr ( pVect->GetKindAt ( 0, &uKind ), S_OK, L"element kind" );
    Check ( uKind == MSGF_KIND_ITEM || uKind == MSGF_KIND_DATA
          , L"a scalar element is an item or a bare value" );

    // A vect of a different width.
    IMsgVect *pReal = 0;
    CheckHr ( pRoot->DeclareVect ( MSGF_SCOPE_CHILD, L"Reals", 2, MSGF_TYPE_DOUBLE, &pReal )
            , S_OK, L"a vect of DOUBLE" );
    CheckHr ( pReal->SetRealAt ( 1, 2.5 ), S_OK, L"write a double element" );
    double d = 0;
    CheckHr ( pReal->GetRealAt ( 1, &d ), S_OK, L"read it back" );
    Check ( d == 2.5, L"the value round-trips" );
    pReal->Release ( );

    // A vect handle survives a mutation.
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, szName, L"padding padding padding", 0, 0 );
    }
    long long iVal = -1;
    CheckHr ( pVect->GetIntAt ( 3, &iVal, 0 ), S_OK
            , L"the vect handle still works after 200 allocations" );
    Check ( iVal == 33, L"and the element is unchanged" );

    CheckHr ( pVect->DeleteAt ( 0 ), S_OK, L"delete an element" );
    pVect->GetCount ( &n );
    Check ( n == 3, L"three elements now" );

    CheckHr ( pVect->Truncate ( ), S_OK, L"truncate" );

    IMsgVect *pOther = 0;
    CheckHr ( pRoot->GetVect ( MSGF_SCOPE_CHILD, L"Row", &pOther ), S_OK, L"re-open the vect" );
    pOther->Release ( );
    CheckHr ( pRoot->GetVect ( MSGF_SCOPE_CHILD, L"Ghost", &pOther ), MSGF_E_NO_ITEM, L"no such name" );
    CheckHr ( pRoot->GetVect ( MSGF_SCOPE_CHILD, L"pad000", &pOther ), MSGF_E_TYPE
            , L"that name is not a vect" );

    pVect->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  11 -- cursors
// ---------------------------------------------------------------------------
static void
Test11_Cursors ( IMsgLibrary *pLib )
{
    Section ( L"11  cursors" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    const wchar_t *aNames[] = { L"alpha", L"beta", L"gamma", L"delta" };
    for ( int i = 0; i < 4; ++i )
      pRoot->DeclareInt ( MSGF_SCOPE_CHILD, aNames[i], i, 0, 0, 0 );

    IMsgCursor *pCurs = 0;
    CheckHr ( pRoot->OpenCursor ( MSGF_SCOPE_CHILD, &pCurs ), S_OK, L"open a cursor" );

    unsigned int uCount = 0;
    CheckHr ( pCurs->GetCount ( &uCount ), S_OK, L"count" );
    Check ( uCount == 4, L"four children" );

    // THE canonical loop.  The core's own IsEoCursor answers TRUE while
    // standing ON the last element, so a loop driven by it visits three of the
    // four; this one must visit all four.
    int nSeen = 0;
    for ( int bEnd = 0; SUCCEEDED ( pCurs->IsEnd ( &bEnd ) ) && !bEnd; pCurs->Next ( ) )
    {
      wchar_t szName[64]; unsigned int cch = 64;
      if ( SUCCEEDED ( pCurs->GetName ( szName, &cch ) ) )
        ++nSeen;
    }
    Check ( nSeen == 4, L"a cursor loop visits EVERY element, including the last" );

    int bEnd = 0;
    pCurs->IsEnd ( &bEnd );
    Check ( bEnd != 0, L"and stops at the end" );
    CheckHr ( pCurs->Next ( ), S_FALSE
            , L"advancing from the end is S_FALSE -- the core's ++ raises there" );
    CheckHr ( pCurs->GetName ( 0, 0 ), MSGF_E_RANGE, L"and there is no current element" );

    CheckHr ( pCurs->Rewind ( ), S_OK, L"rewind" );
    pCurs->IsEnd ( &bEnd );
    Check ( bEnd == 0, L"back on an element" );

    // Goto by name is case-insensitive in the core, and the facade reports the
    // STORED spelling rather than the caller's.
    CheckHr ( pCurs->GotoName ( L"GAMMA" ), S_OK, L"GotoName matches without case" );
    wchar_t szName[64]; unsigned int cch = 64;
    pCurs->GetName ( szName, &cch );
    Check ( ::wcscmp ( szName, L"gamma" ) == 0, L"and answers the stored spelling" );

    unsigned int uIndex = 99;
    CheckHr ( pCurs->GetIndex ( &uIndex ), S_OK, L"GetIndex" );
    CheckHr ( pCurs->GotoName ( L"nosuch" ), MSGF_E_NO_ITEM, L"GotoName misses" );
    unsigned int uAfter = 99;
    CheckHr ( pCurs->GetIndex ( &uAfter ), S_OK, L"and leaves the cursor usable" );
    Check ( uAfter == uIndex, L"exactly where it was" );

    CheckHr ( pCurs->GotoIndex ( 0 ), S_OK, L"GotoIndex" );
    CheckHr ( pCurs->GotoIndex ( 99 ), MSGF_E_RANGE, L"GotoIndex past the end" );

    // The bridge back to a route.
    pCurs->GotoName ( L"beta" );
    IMsgNode *pNode = 0;
    CheckHr ( pCurs->GetNode ( &pNode ), S_OK, L"a node for the current element" );
    wchar_t szPath[128]; cch = 128;
    pNode->GetPath ( szPath, &cch );
    Check ( ::wcscmp ( szPath, L".beta" ) == 0, L"with the right path" );
    pNode->Release ( );

    // Delete through the cursor.
    CheckHr ( pCurs->Delete ( ), S_OK, L"delete the current element" );
    CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"beta" ), S_FALSE, L"it is gone" );
    pCurs->GetCount ( &uCount );
    Check ( uCount == 3, L"three left" );
    pCurs->IsEnd ( &bEnd );
    Check ( bEnd == 0, L"and the cursor is still usable" );

    pCurs->Release ( );

    // The attribute scope walks separately.
    pRoot->DeclareText ( MSGF_SCOPE_ATTR, L"x", L"1", 0, 0 );
    pRoot->DeclareText ( MSGF_SCOPE_ATTR, L"y", L"2", 0, 0 );
    CheckHr ( pRoot->OpenCursor ( MSGF_SCOPE_ATTR, &pCurs ), S_OK, L"an ATTR cursor" );
    pCurs->GetCount ( &uCount );
    Check ( uCount == 2, L"sees the attributes only" );
    pCurs->Release ( );

    // An empty scope is at the end immediately.
    IMsgNode *pLeaf = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"alpha", &pLeaf );
    CheckHr ( pLeaf->OpenCursor ( MSGF_SCOPE_CHILD, &pCurs ), S_OK, L"a cursor over nothing" );
    pCurs->IsEnd ( &bEnd );
    Check ( bEnd != 0, L"is at the end from the start" );
    pCurs->GetCount ( &uCount );
    Check ( uCount == 0, L"and counts nothing" );
    pCurs->Release ( );
    pLeaf->Release ( );

    pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  12 -- walkers
// ---------------------------------------------------------------------------
static void
Test12_Walkers ( IMsgLibrary *pLib )
{
    Section ( L"12  walkers" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    //  A            B            C
    //  |- A1        |- B1
    //  |- A2           |- B1a
    IMsgNode *pA = 0, *pB = 0, *pB1 = 0;
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"A", 0, 0, 0, &pA );
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"B", 0, 0, 0, &pB );
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"C", 0, 0, 0, 0 );
    pA->DeclareInt ( MSGF_SCOPE_CHILD, L"A1", 0, 0, 0, 0 );
    pA->DeclareInt ( MSGF_SCOPE_CHILD, L"A2", 0, 0, 0, 0 );
    pB->DeclareInt ( MSGF_SCOPE_CHILD, L"B1", 0, 0, 0, &pB1 );
    pB1->DeclareInt ( MSGF_SCOPE_CHILD, L"B1a", 0, 0, 0, 0 );

    // Full descent: 3 + 2 + 1 + 1 = 7 nodes.
    IMsgWalker *pWalk = 0;
    CheckHr ( pRoot->OpenWalker ( MSGF_SCOPE_CHILD, &pWalk ), S_OK, L"open a walker" );

    int nSeen = 0, nMaxDepth = 0;
    for ( int bEnd = 0; SUCCEEDED ( pWalk->IsEnd ( &bEnd ) ) && !bEnd; pWalk->Next ( ) )
    {
      ++nSeen;
      int nDepth = 0;
      pWalk->GetDepth ( &nDepth );
      if ( nDepth > nMaxDepth ) nMaxDepth = nDepth;
      pWalk->Push ( 0 );                        // descend wherever we can
    }
    Check ( nSeen == 7, L"a full walk visits every node in the subtree" );
    Check ( nMaxDepth == 2, L"and reports the depth it reached" );
    pWalk->Release ( );

    // Pruned: descend into A only.  3 top-level + A's 2 = 5.
    CheckHr ( pRoot->OpenWalker ( MSGF_SCOPE_CHILD, &pWalk ), S_OK, L"open another" );
    nSeen = 0;
    for ( int bEnd = 0; SUCCEEDED ( pWalk->IsEnd ( &bEnd ) ) && !bEnd; pWalk->Next ( ) )
    {
      ++nSeen;
      wchar_t szName[64]; unsigned int cch = 64;
      pWalk->GetName ( szName, &cch );
      if ( ::wcscmp ( szName, L"A" ) == 0 )
        CheckHr ( pWalk->Push ( 0 ), S_OK, L"descend into A" );
    }
    Check ( nSeen == 5, L"a pruned walk visits only what it descended into" );
    pWalk->Release ( );

    // Push's three answers.
    CheckHr ( pRoot->OpenWalker ( MSGF_SCOPE_CHILD, &pWalk ), S_OK, L"open a third" );
    pWalk->Next ( ); pWalk->Next ( );            // -> C, which has no children
    wchar_t szName[64]; unsigned int cch = 64;
    pWalk->GetName ( szName, &cch );
    Check ( ::wcscmp ( szName, L"C" ) == 0, L"standing on C" );
    CheckHr ( pWalk->Push ( 0 ), S_FALSE, L"pushing into a childless item is S_FALSE" );
    int nDepth = -1;
    pWalk->GetDepth ( &nDepth );
    Check ( nDepth == 0, L"and does not move the walker" );

    CheckHr ( pWalk->Pop ( 0 ), MSGF_E_RANGE, L"popping the outermost level" );
    pWalk->Release ( );

    // Push / Pop / Break.
    CheckHr ( pRoot->OpenWalker ( MSGF_SCOPE_CHILD, &pWalk ), S_OK, L"open a fourth" );
    nDepth = -1;
    CheckHr ( pWalk->Push ( &nDepth ), S_OK, L"descend into A" );
    Check ( nDepth == 1, L"depth 1" );
    CheckHr ( pWalk->Pop ( &nDepth ), S_OK, L"ascend" );
    Check ( nDepth == 0, L"depth 0" );
    pWalk->Push ( 0 );
    CheckHr ( pWalk->Break ( ), S_OK, L"break out of every pushed level" );
    pWalk->GetDepth ( &nDepth );
    Check ( nDepth == 0, L"back at the outermost" );
    // ...and the walk continues with A's SIBLINGS rather than ending.
    CheckHr ( pWalk->Next ( ), S_OK, L"the walk continues after a break" );
    cch = 64; pWalk->GetName ( szName, &cch );
    Check ( ::wcscmp ( szName, L"B" ) == 0, L"with the next sibling" );
    pWalk->Release ( );

    pB1->Release ( ); pB->Release ( ); pA->Release ( );
    pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  13 -- the path grammar
// ---------------------------------------------------------------------------
static void
Test13_Paths ( IMsgLibrary *pLib )
{
    Section ( L"13  the path grammar" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pCfg = 0, *pWin = 0;
    pRoot->DeclareInt  ( MSGF_SCOPE_CHILD, L"Config", 0, 0, 0, &pCfg );
    pCfg->DeclareInt   ( MSGF_SCOPE_CHILD, L"Window", 0, 0, 0, &pWin );
    pWin->DeclareText  ( MSGF_SCOPE_ATTR,  L"Colour", L"blue", 0, 0 );
    pWin->DeclareInt   ( MSGF_SCOPE_CHILD, L"Width",  800, 0, 0, 0 );

    struct { const wchar_t *path; } aGood[] =
    {
      { L"" }, { L".Config" }, { L".Config.Window" },
      { L".Config.Window.Width" }, { L".Config.Window@Colour" },
    };

    for ( int i = 0; i < _countof(aGood); ++i )
    {
      IMsgNode *pNode = 0;
      CheckHr ( pStore->NodeFromPath ( aGood[i].path, &pNode ), S_OK, L"resolve a path" );
      if ( !pNode ) continue;
      wchar_t szBack[256]; unsigned int cch = 256;
      pNode->GetPath ( szBack, &cch );
      Check ( ::wcscmp ( szBack, aGood[i].path ) == 0, L"and it round-trips exactly" );
      pNode->Release ( );
    }

    IMsgNode *pNode = 0;
    CheckHr ( pStore->NodeFromPath ( L"Config", &pNode ), MSGF_E_PATH
            , L"a step must be introduced by '.' or '@'" );
    CheckHr ( pStore->NodeFromPath ( L".", &pNode ), MSGF_E_PATH, L"an empty step" );
    CheckHr ( pStore->NodeFromPath ( L".Config..Window", &pNode ), MSGF_E_PATH, L"a doubled '.'" );
    CheckHr ( pStore->NodeFromPath ( L".Ghost", &pNode ), MSGF_E_NO_ITEM
            , L"a path that parses but names nothing" );
    CheckHr ( pStore->NodeFromPath ( L".Config@Ghost", &pNode ), MSGF_E_NO_ITEM
            , L"a missing attribute" );

    // Positions.
    unsigned long long uPos = 0;
    CheckHr ( pWin->GetPos ( &uPos ), S_OK, L"a node's position" );
    Check ( uPos != 0, L"which is not zero" );

    IMsgNode *pByPos = 0;
    CheckHr ( pStore->NodeFromPos ( uPos, &pByPos ), S_OK, L"resolve it back" );
    wchar_t szPath[256]; unsigned int cch = 256;
    pByPos->GetPath ( szPath, &cch );
    Check ( ::wcscmp ( szPath, L".Config.Window" ) == 0
          , L"and a position becomes the right PATH" );
    pByPos->Release ( );

    CheckHr ( pStore->NodeFromPos ( 0, &pByPos ), MSGF_E_NO_POS, L"position 0" );
    CheckHr ( pStore->NodeFromPos ( 0xDEADBEEF, &pByPos ), MSGF_E_NO_POS
            , L"a position that is not in this store" );

    // An attribute node has a position too.
    IMsgNode *pAttr = 0;
    pWin->GetChild ( MSGF_SCOPE_ATTR, L"Colour", &pAttr );
    CheckHr ( pAttr->GetPos ( &uPos ), S_OK, L"an attribute's position" );
    CheckHr ( pStore->NodeFromPos ( uPos, &pByPos ), S_OK, L"resolves too" );
    cch = 256; pByPos->GetPath ( szPath, &cch );
    Check ( ::wcscmp ( szPath, L".Config.Window@Colour" ) == 0
          , L"to the attribute path" );
    pByPos->Release ( );
    pAttr->Release ( );

    pWin->Release ( ); pCfg->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  14 -- persistence, and a node held across a Load
// ---------------------------------------------------------------------------
static void
Test14_Persistence ( IMsgLibrary *pLib )
{
    Section ( L"14  persistence" );

    ::DeleteFileW ( g_szFile );
    ::DeleteFileW ( g_szFile2 );

    {
      IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
      IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

      pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"persisted", 0, 0 );
      pRoot->DeclareInt  ( MSGF_SCOPE_CHILD, L"Count", 7, 0, 0, 0 );
      pRoot->DeclareText ( MSGF_SCOPE_ATTR,  L"Lang",  L"en", 0, 0 );

      IMsgList *pList = 0;
      pRoot->DeclareList ( MSGF_SCOPE_CHILD, L"Items", &pList );
      pList->AddText ( L"one", MSGF_ADD_TAIL );
      pList->AddText ( L"two", MSGF_ADD_TAIL );
      pList->Release ( );

      int bDirty = 0;
      pStore->IsDirty ( &bDirty );
      Check ( bDirty != 0, L"a store with unsaved writes is dirty" );

      CheckHr ( pStore->Save ( g_szFile, 0 ), S_OK, L"Save" );

      wchar_t szName[MAX_PATH]; unsigned int cch = MAX_PATH;
      CheckHr ( pStore->GetFilename ( szName, &cch ), S_OK, L"GetFilename" );
      Check ( ::wcsstr ( szName, L"FacadeSmokeTest.p2p" ) != 0, L"names the file it saved to" );

      CheckHr ( pStore->Save ( g_szFile2, MSGF_SAVE_DEFRAGMENT ), S_OK
              , L"Save with defragment" );

      pRoot->Release ( ); pStore->Release ( );
    }

    // Read it back into a fresh store.
    {
      IMsgStore *pStore = 0;
      CheckHr ( pLib->OpenStore ( g_szFile, &pStore ), S_OK, L"OpenStore" );

      IMsgNode *pNode = 0;
      CheckHr ( pStore->NodeFromPath ( L".Title", &pNode ), S_OK, L"the saved child is there" );
      wchar_t szText[64]; unsigned int cch = 64;
      pNode->GetText ( szText, &cch );
      Check ( ::wcscmp ( szText, L"persisted" ) == 0, L"with its value" );
      pNode->Release ( );

      CheckHr ( pStore->NodeFromPath ( L"@Lang", &pNode ), S_OK, L"the saved attribute is there" );
      cch = 64; pNode->GetText ( szText, &cch );
      Check ( ::wcscmp ( szText, L"en" ) == 0, L"with its value" );
      pNode->Release ( );

      IMsgNode *pRoot = 0; pStore->GetRoot ( &pRoot );
      IMsgList *pList = 0;
      CheckHr ( pRoot->GetList ( MSGF_SCOPE_CHILD, L"Items", &pList ), S_OK
              , L"the saved list is there" );
      unsigned int n = 0; pList->GetCount ( &n );
      Check ( n == 2, L"with both cells" );
      cch = 64; pList->GetTextAt ( 1, szText, &cch );
      Check ( ::wcscmp ( szText, L"two" ) == 0, L"in order" );
      pList->Release ( ); pRoot->Release ( );

      pStore->Release ( );
    }

    // A store that is not a store.
    {
      IMsgStore *pStore = 0;
      CheckHr ( pLib->OpenStore ( L"no-such-file-anywhere.p2p", &pStore ), MSGF_E_FILE
              , L"opening a file that is not there" );
      Check ( pStore == 0, L"and nothing comes back" );
    }

    // THE test a raw handle could not pass: a node held across a Load that
    // replaces the entire heap.
    {
      IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
      IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"before the load", 0, 0 );

      IMsgNode *pTitle = 0;
      pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Title", &pTitle );

      CheckHr ( pStore->Load ( g_szFile ), S_OK, L"Load over a live store" );

      wchar_t szText[64]; unsigned int cch = 64;
      CheckHr ( pTitle->GetText ( szText, &cch ), S_OK
              , L"a node held across a Load still resolves" );
      Check ( ::wcscmp ( szText, L"persisted" ) == 0
            , L"and now names the node in the NEW tree" );

      CheckHr ( pRoot->Exists ( MSGF_SCOPE_CHILD, L"Items" ), S_OK
              , L"the root node sees the loaded tree too" );

      pTitle->Release ( ); pRoot->Release ( ); pStore->Release ( );
    }

    ::DeleteFileW ( g_szFile );
    ::DeleteFileW ( g_szFile2 );
}

// ---------------------------------------------------------------------------
//  15 -- change notification
// ---------------------------------------------------------------------------
class CountingSink : public IMsgStoreEvents
{
    public:
      virtual void OnTrigger ( unsigned int type, unsigned long long pos )
      {
        ++m_nCalls;
        m_uLastType = type;
        m_uLastPos  = pos;
      }
      int                m_nCalls{0};
      unsigned int       m_uLastType{0};
      unsigned long long m_uLastPos{0};
};

static void
Test15_Triggers ( IMsgLibrary *pLib )
{
    Section ( L"15  change notification" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pWatched = 0;
    pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"Watched", 1, 0, 0, &pWatched );

    unsigned long long uPos = 0;
    pWatched->GetPos ( &uPos );

    CountingSink oSink;
    CheckHr ( pStore->SetEvents ( &oSink ), S_OK, L"install a sink" );
    CheckHr ( pStore->Arm ( MSGF_TRIG_UPDATE, uPos ), S_OK, L"arm a node" );

    unsigned int uFired = 0;
    CheckHr ( pStore->Fire ( MSGF_TRIG_UPDATE, uPos, &uFired ), S_OK, L"fire it" );
    Check ( oSink.m_nCalls == 1, L"the sink was called" );
    Check ( oSink.m_uLastType == MSGF_TRIG_UPDATE, L"with the trigger type" );
    Check ( oSink.m_uLastPos  == uPos, L"and the position" );

    CheckHr ( pStore->Disarm ( MSGF_TRIG_UPDATE, uPos ), S_OK, L"disarm" );
    int nWas = oSink.m_nCalls;
    pStore->Fire ( MSGF_TRIG_UPDATE, uPos, 0 );
    Check ( oSink.m_nCalls == nWas, L"a disarmed node fires nothing" );

    CheckHr ( pStore->Arm ( 0, uPos ), E_INVALIDARG, L"an empty mask is refused" );
    CheckHr ( pStore->Arm ( MSGF_TRIG_UPDATE, 0 ), E_INVALIDARG, L"position 0 is refused" );

    // Clearing the sink before it goes out of scope is the caller's job, and
    // the header says so -- nothing here can tell a freed sink from a live one.
    CheckHr ( pStore->SetEvents ( 0 ), S_OK, L"clear the sink" );
    pStore->Arm ( MSGF_TRIG_UPDATE, uPos );
    nWas = oSink.m_nCalls;
    pStore->Fire ( MSGF_TRIG_UPDATE, uPos, 0 );
    Check ( oSink.m_nCalls == nWas, L"and nothing is delivered after that" );

    pWatched->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  16 -- a closed store
// ---------------------------------------------------------------------------
static void
Test16_Closed ( IMsgLibrary *pLib )
{
    Section ( L"16  a closed store" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"x", 0, 0 );

    IMsgNode   *pChild = 0; pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Title", &pChild );
    IMsgCursor *pCurs  = 0; pRoot->OpenCursor ( MSGF_SCOPE_CHILD, &pCurs );
    IMsgList   *pList  = 0; pRoot->DeclareList ( MSGF_SCOPE_CHILD, L"L", &pList );

    // Releasing the store does not free it -- the outstanding objects hold it
    // -- but it does CLOSE it, and every one of them says so.
    pStore->Release ( );

    wchar_t szText[64]; unsigned int cch = 64;
    CheckHr ( pChild->GetText ( szText, &cch ), MSGF_E_CLOSED, L"a node after the close" );
    CheckHr ( pRoot->DeclareInt ( MSGF_SCOPE_CHILD, L"n", 1, 0, 0, 0 ), MSGF_E_CLOSED
            , L"a declare after the close" );
    int bEnd = 0;
    CheckHr ( pCurs->IsEnd ( &bEnd ), MSGF_E_CLOSED, L"a cursor after the close" );
    unsigned int n = 0;
    CheckHr ( pList->GetCount ( &n ), MSGF_E_CLOSED, L"a list after the close" );

    // ...and releasing them in any order is safe: the last one out frees the
    // manager.
    pList->Release ( );
    pCurs->Release ( );
    pChild->Release ( );
    pRoot->Release ( );
    ++g_nPass;   // getting here without a fault IS the check
    wprintf ( L"  (the last object out freed the store)\n" );
}

// ---------------------------------------------------------------------------
//  20 -- Clear
//
//  Its own section because the obvious implementation of it is a trap: the
//  core's P2PmsgMgr::Nullify closes the heap and leaves the manager pointing at
//  nothing, IsValid still answering TRUE, and the next call through it reading
//  a closed heap. Clear replaces the manager instead, so everything below has
//  to still work afterwards.
// ---------------------------------------------------------------------------
static void
Test20_Clear ( IMsgLibrary *pLib )
{
    Section ( L"20  Clear" );

    IMsgStore *pStore = 0; pLib->CreateStore ( MSGF_ADDR_32, 4096, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"before", 0, 0 );
    pRoot->DeclareText ( MSGF_SCOPE_ATTR,  L"Lang",  L"en", 0, 0 );
    IMsgNode *pHeld = 0;
    pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Title", &pHeld );

    unsigned int uCount = 0;
    pRoot->GetCount ( MSGF_SCOPE_CHILD, &uCount );
    Check ( uCount == 1, L"one child before the clear" );

    CheckHr ( pStore->Clear ( ), S_OK, L"Clear" );

    // The store is still USABLE -- which is the whole check.
    int nValid = 0;
    CheckHr ( pStore->IsValid ( &nValid ), S_OK, L"IsValid after Clear" );
    Check ( nValid != 0, L"and the store is sound" );

    CheckHr ( pRoot->GetCount ( MSGF_SCOPE_CHILD, &uCount ), S_OK, L"the root still answers" );
    Check ( uCount == 0, L"and it is empty" );
    CheckHr ( pRoot->GetCount ( MSGF_SCOPE_ATTR, &uCount ), S_OK, L"the attribute scope too" );
    Check ( uCount == 0, L"which is also empty" );

    // A node held across the clear says the node is gone, rather than reading
    // a heap that is not there.
    wchar_t szText[64]; unsigned int cch = 64;
    CheckHr ( pHeld->GetText ( szText, &cch ), MSGF_E_NO_ITEM
            , L"a node held across a Clear reports the node gone" );

    // ...and the store takes new content.
    CheckHr ( pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"after", 0, 0 ), S_OK
            , L"declaring into a cleared store" );
    cch = 64;
    CheckHr ( pHeld->GetText ( szText, &cch ), S_OK, L"and the held node revives" );
    Check ( ::wcscmp ( szText, L"after" ) == 0, L"with the new value" );

    unsigned int uSize = 0;
    pStore->GetSize ( &uSize );
    Check ( uSize > 0, L"the new heap is a heap" );

    pHeld->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  18 -- the value stack (ABI 2)
// ---------------------------------------------------------------------------
static void
Test18_ValueStack ( IMsgLibrary *pLib )
{
    Section ( L"18  the value stack" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pNode = 0;
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Larry", L"original", 0, &pNode );

    int bStacked = 1;
    CheckHr ( pNode->IsStacked ( &bStacked ), S_OK, L"IsStacked on a fresh node" );
    Check ( bStacked == 0, L"which is not stacked" );

    // Popping an unstacked node is S_FALSE -- a SUCCESS code. The core's own
    // Pop is a silent no-op there, so a drain loop driven by the return alone
    // would never terminate.
    CheckHr ( pNode->PopValue ( ), S_FALSE, L"popping an unstacked node" );
    CheckHr ( pNode->DropValue ( ), S_FALSE, L"dropping an unstacked node" );

    CheckHr ( pNode->PushValue ( ), S_OK, L"push the value" );
    pNode->IsStacked ( &bStacked );
    Check ( bStacked != 0, L"the node is stacked now" );

    CheckHr ( pNode->SetText ( L"overridden" ), S_OK, L"overwrite it" );
    wchar_t szText[64]; unsigned int cch = 64;
    pNode->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"overridden" ) == 0, L"the override is there" );

    CheckHr ( pNode->PopValue ( ), S_OK, L"pop it back" );
    cch = 64; pNode->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"original" ) == 0, L"and the original is back" );
    pNode->IsStacked ( &bStacked );
    Check ( bStacked == 0, L"nothing stacked afterwards" );

    // Drop forgets the saved pair instead of restoring it.
    pNode->PushValue ( );
    pNode->SetText ( L"kept" );
    CheckHr ( pNode->DropValue ( ), S_OK, L"drop the saved pair" );
    cch = 64; pNode->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"kept" ) == 0, L"the override survives a drop" );
    pNode->IsStacked ( &bStacked );
    Check ( bStacked == 0, L"and nothing is stacked" );

    // THE PART A CALLER COULD NOT HAND-ROLL: the saved pair lives in the store,
    // so it survives every mutation a node survives -- including 200 heap
    // growths, and including a node handle that is only a route.
    pNode->SetText ( L"before" );
    CheckHr ( pNode->PushValue ( ), S_OK, L"push again" );
    for ( int i = 0; i < 200; ++i )
    {
      wchar_t szName[32];
      ::swprintf_s ( szName, L"pad%03d", i );
      pRoot->DeclareText ( MSGF_SCOPE_CHILD, szName, L"padding padding padding", 0, 0 );
    }
    pNode->SetText ( L"after" );

    IMsgNode *pFresh = 0;
    pStore->NodeFromPath ( L".Larry", &pFresh );
    CheckHr ( pFresh->IsStacked ( &bStacked ), S_OK, L"a DIFFERENT handle sees the stack" );
    Check ( bStacked != 0, L"because the stack is in the store, not the handle" );
    CheckHr ( pFresh->PopValue ( ), S_OK, L"and can pop it" );
    cch = 64; pNode->GetText ( szText, &cch );
    Check ( ::wcscmp ( szText, L"before" ) == 0, L"restoring across 200 allocations" );
    pFresh->Release ( );

    // A node that is not there cannot be stacked.
    pRoot->Delete ( MSGF_SCOPE_CHILD, L"Larry" );
    CheckHr ( pNode->PushValue ( ), MSGF_E_NO_ITEM, L"pushing a deleted node" );
    CheckHr ( pNode->IsStacked ( &bStacked ), MSGF_E_NO_ITEM, L"asking a deleted node" );
    CheckHr ( pNode->IsStacked ( 0 ), E_POINTER, L"a null out pointer" );

    pNode->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  19 -- demand paging (ABI 2)
// ---------------------------------------------------------------------------
class CountingPager : public IMsgPagingEvents
{
    public:
      virtual int OnPageIn ( unsigned long long pos )
      {
        ++m_nIn; m_uLastIn = pos; return m_bHandle ? 1 : 0;
      }
      virtual int OnPageOut ( unsigned long long pos, int flush )
      {
        ++m_nOut; m_uLastOut = pos; m_nLastFlush = flush; return m_bHandle ? 1 : 0;
      }
      int                m_nIn{0};
      int                m_nOut{0};
      int                m_nLastFlush{-1};
      bool               m_bHandle{true};
      unsigned long long m_uLastIn{0};
      unsigned long long m_uLastOut{0};
};

static void
Test19_Paging ( IMsgLibrary *pLib )
{
    Section ( L"19  demand paging" );

    IMsgStore *pStore = 0; pLib->CreateStore ( 0, 0, 0, &pStore );
    IMsgNode  *pRoot  = 0; pStore->GetRoot ( &pRoot );

    IMsgNode *pSet = 0;
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, L"Dataset", L"", 0, &pSet );
    unsigned long long uPos = 0;
    pSet->GetPos ( &uPos );
    Check ( uPos != 0, L"the dataset has a position" );

    // With nothing installed the calls are inert -- and say so with S_FALSE
    // rather than pretending they did something.
    CheckHr ( pStore->PageIn ( uPos ), S_FALSE, L"page in with no sink" );
    CheckHr ( pStore->PageOut ( uPos, 0 ), S_FALSE, L"page out with no sink" );
    CheckHr ( pStore->PageIn ( 0 ), E_INVALIDARG, L"position 0 is refused" );

    CountingPager oPager;
    CheckHr ( pStore->SetPaging ( &oPager ), S_OK, L"install a paging sink" );

    CheckHr ( pStore->PageIn ( uPos ), S_OK, L"page in" );
    Check ( oPager.m_nIn == 1, L"the sink was asked" );
    Check ( oPager.m_uLastIn == uPos, L"for the right position" );

    CheckHr ( pStore->PageOut ( uPos, 1 ), S_OK, L"page out, flushing" );
    Check ( oPager.m_nOut == 1, L"the sink was asked" );
    Check ( oPager.m_uLastOut == uPos, L"for the right position" );
    Check ( oPager.m_nLastFlush != 0, L"and told to flush" );

    // The sink's ANSWER is the call's answer -- that is what makes it a sink
    // rather than a notification.
    oPager.m_bHandle = false;
    CheckHr ( pStore->PageIn ( uPos ), S_FALSE, L"a sink that declines" );
    Check ( oPager.m_nIn == 2, L"was still asked" );
    oPager.m_bHandle = true;

    // Push / pop the whole registration: the way to run a section with paging
    // suppressed. The core's own push does not nest, so a second one is
    // MSGF_E_STATE rather than a lost registration.
    CheckHr ( pStore->PopPaging ( ), MSGF_E_STATE, L"popping without a push" );
    CheckHr ( pStore->PushPaging ( ), S_OK, L"push the registration" );
    CheckHr ( pStore->PushPaging ( ), MSGF_E_STATE, L"a second push is refused" );

    const int nWas = oPager.m_nIn;
    pStore->SetPaging ( 0 );                       // nothing installed in here
    CheckHr ( pStore->PageIn ( uPos ), S_FALSE, L"nothing pages while suppressed" );
    Check ( oPager.m_nIn == nWas, L"and the sink was not asked" );

    CheckHr ( pStore->PopPaging ( ), S_OK, L"pop it back" );
    CheckHr ( pStore->PopPaging ( ), MSGF_E_STATE, L"popping an empty push" );

    // Clearing the sink before it dies is the caller's job, as with triggers.
    CheckHr ( pStore->SetPaging ( 0 ), S_OK, L"clear the sink" );
    CheckHr ( pStore->PageIn ( uPos ), S_FALSE, L"and nothing is delivered after that" );

    // A push with nothing installed is refused too -- the core's pop asserts
    // unless it has something to restore, so that push is an abort deferred to
    // the matching pop.
    CheckHr ( pStore->PushPaging ( ), MSGF_E_STATE, L"pushing with no sink installed" );

    pSet->Release ( ); pRoot->Release ( ); pStore->Release ( );
}

// ---------------------------------------------------------------------------
//  17 -- the header-only convenience layer
// ---------------------------------------------------------------------------
static void
Test17_FnLayer ( )
{
    Section ( L"17  the MsgFacadeFn.hpp layer" );

    try
    {
      Library lib;
      Check ( !lib.version ( ).empty ( ), L"the version string" );
      Check ( lib.validName ( L"Title" ),  L"validName" );
      Check ( !lib.validName ( L"a.b" ),   L"validName refuses a dotted name" );
      Check ( lib.match ( L"Con*", L"Config" ), L"wildcard match" );
      Check ( lib.typeName ( MSGF_TYPE_INT64 ) == L"INT64", L"typeName" );

      Store st = lib.createStore ( );
      Node root = st.root ( );

      root.declareText ( L"Title", L"Hello" );
      root.declareInt  ( L"Count", 42 );
      root.declareText ( L"Lang",  L"en", Attr );

      Check ( root.child ( L"Title" ).asText ( ) == L"Hello", L"declare and read text" );
      Check ( root.child ( L"Count" ).asInt ( )  == 42,       L"declare and read an int" );
      Check ( root.child ( L"Lang", Attr ).asText ( ) == L"en", L"the attribute scope" );
      Check ( root.count ( ) == 2,       L"two children" );
      Check ( root.count ( Attr ) == 1,  L"one attribute" );
      Check ( root.exists ( L"Title" ),  L"exists" );
      Check ( !root.exists ( L"Ghost" ), L"exists says no without throwing" );
      Check ( !root.tryChild ( L"Ghost" ).valid ( ), L"tryChild for a missing name" );

      // A node is a path here too.
      Node keep = root.child ( L"Title" );
      for ( int i = 0; i < 200; ++i )
      {
        wchar_t szName[32];
        ::swprintf_s ( szName, L"pad%03d", i );
        root.declareText ( szName, L"padding padding padding padding" );
      }
      Check ( keep.asText ( ) == L"Hello", L"a Node survives 200 allocations" );

      std::vector<std::wstring> a = root.names ( );
      Check ( a.size ( ) == 202, L"names() enumerates every child" );

      List l = root.declareList ( L"Items" );
      l.addText ( L"one" );
      l.addText ( L"two" );
      l.addInt  ( 3 );
      Check ( l.count ( ) == 3,          L"a list through the sugar" );
      Check ( l.textAt ( 0 ) == L"one",  L"and reads back" );
      Check ( l.intAt  ( 2 ) == 3,       L"including the int cell" );

      Vect v = root.declareVect ( L"Row", 3, MSGF_TYPE_INT32 );
      v.setIntAt ( 0, 5 );
      Check ( v.count ( ) == 3,  L"a vect through the sugar" );
      Check ( v.intAt ( 0 ) == 5, L"and reads back" );

      int nSeen = 0;
      for ( Cursor c = root.cursor ( ); !c.end ( ); c.next ( ) )
        ++nSeen;
      Check ( nSeen == 204, L"a range-style cursor loop" );

      Check ( root.child ( L"Title" ).path ( ) == L".Title", L"path()" );
      Check ( st.at ( L".Title" ).asText ( ) == L"Hello",    L"at(path)" );

      // Errors arrive as exceptions carrying the HRESULT.
      bool bThrew = false;
      try { root.child ( L"Ghost" ); }
      catch ( const Error& e ) { bThrew = e.code ( ) == MSGF_E_NO_ITEM; }
      Check ( bThrew, L"a failure throws msgf::Error carrying the HRESULT" );

      // std::function change sink.
      int nFired = 0;
      Events sink ( [&] ( unsigned int, unsigned long long ) { ++nFired; } );
      st.events ( &sink );
      unsigned long long uPos = root.child ( L"Count" ).pos ( );
      st.arm ( uPos, MSGF_TRIG_UPDATE );
      st.fire ( uPos, MSGF_TRIG_UPDATE );
      Check ( nFired == 1, L"a lambda change sink" );
      st.events ( 0 );
    }
    catch ( const Error& e )
    {
      ++g_nFail;
      wprintf ( L"  FAIL: unexpected msgf::Error 0x%08X\n", (unsigned)e.code ( ) );
    }
}

// ---------------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------------
int
wmain ( )
{
    // Unbuffered, because a failure in this DLL's kernel is an MFC ASSERT and
    // MFC answers an un-answerable assert with AfxAbort() -- which does not
    // flush.  Buffered output would lose the last thing printed, which is
    // exactly the line that says where it died.
    ::setvbuf ( stdout, NULL, _IONBF, 0 );

    wprintf ( L"MsgFacade smoke test\n" );

    IMsgLibrary *pLib = 0;
    Test01_Library ( &pLib );
    if ( !pLib )
    {
      wprintf ( L"\nFATAL: no library; nothing else can run.\n" );
      return 1;
    }

    Test02_StoreAndScopes      ( pLib );
    Test03_BufferProtocol      ( pLib );
    Test04_Values              ( pLib );
    Test05_DeclareRules        ( pLib );
    Test06_InPlaceWrites       ( pLib );
    Test07_NodesSurviveMutation( pLib );
    Test08_Restructure         ( pLib );
    Test09_Lists               ( pLib );
    Test10_Vects               ( pLib );
    Test11_Cursors             ( pLib );
    Test12_Walkers             ( pLib );
    Test13_Paths               ( pLib );
    Test14_Persistence         ( pLib );
    Test15_Triggers            ( pLib );
    Test18_ValueStack          ( pLib );
    Test19_Paging              ( pLib );
    Test20_Clear               ( pLib );
    Test16_Closed              ( pLib );   // last: it releases a store early

    pLib->Release ( );

    Test17_FnLayer ( );

    wprintf ( L"\n----------------------------------------\n" );
    wprintf ( L"%d passed, %d failed\n", g_nPass, g_nFail );
    return g_nFail == 0 ? 0 : 1;
}
