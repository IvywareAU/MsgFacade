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
// FacadeLibrary.cpp -- the singleton, and the one exported function.
#include "stdafx.h"
#include "FacadeLibrary.h"
#include "FacadeStore.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace msgf;

FacadeLibrary   *FacadeLibrary::s_pInstance = nullptr;
CCriticalSection FacadeLibrary::s_oCSectInstance;

// ---------------------------------------------------------------------------
//  The singleton
// ---------------------------------------------------------------------------

HRESULT
FacadeLibrary::Acquire ( IMsgLibrary **outLibrary )
{
    if ( !outLibrary )
      return E_POINTER;
    *outLibrary = nullptr;

    CSingleLock oLock ( &s_oCSectInstance, TRUE );
    if ( !s_pInstance )
      s_pInstance = new FacadeLibrary ( );
    s_pInstance -> AddRef ( );
    *outLibrary = s_pInstance;
    return S_OK;
}

void
FacadeLibrary::AddRef ( )
{
    ::InterlockedIncrement ( &m_cRef );
}

//
//  Drop one reference
//  NOTES: The instance pointer is cleared under the same lock Acquire takes,
//         so a client releasing the last reference on one thread while another
//         asks for a new library cannot see a half-deleted singleton
//
ULONG
FacadeLibrary::ReleaseRef ( )
{
    CSingleLock oLock ( &s_oCSectInstance, TRUE );
    LONG cRef = ::InterlockedDecrement ( &m_cRef );
    if ( cRef == 0 )
    {
      if ( s_pInstance == this )
        s_pInstance = nullptr;
      delete this;
    }
    return (ULONG)cRef;
}

ULONG
FacadeLibrary::Release ( )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    return ReleaseRef ( );
}

// ---------------------------------------------------------------------------
//  msgf::IMsgLibrary
// ---------------------------------------------------------------------------

HRESULT
FacadeLibrary::CreateStore ( unsigned char addr
                           , unsigned int initialBytes, unsigned int maxBytes
                           , IMsgStore **outStore )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    return FacadeStore::Create ( this, addr, initialBytes, maxBytes, outStore );
}

HRESULT
FacadeLibrary::OpenStore ( const wchar_t *filename, IMsgStore **outStore )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    return FacadeStore::Open ( this, filename, outStore );
}

//
//  The core's own wildcard match
//  NOTES: S_OK / S_FALSE rather than a BOOL out-parameter, matching Exists on
//         a node: "no" is an answer here, not a failure
//
HRESULT
FacadeLibrary::WildcardMatch ( const wchar_t *pattern, const wchar_t *name ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    if ( !pattern || !name )
      return E_POINTER;
    try { return MsgcoreWildcard ( pattern, name ) ? S_OK : S_FALSE; }
    catch ( ... ) { return MSGF_E_CORE; }
}

HRESULT
FacadeLibrary::TypeName ( unsigned char type, wchar_t *buf, unsigned int *cch ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    return CopyOut ( TypeToName ( type ), buf, cch );
}

HRESULT
FacadeLibrary::TypeFromName ( const wchar_t *name, unsigned char *outType ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    if ( !name || !outType )
      return E_POINTER;
    unsigned char uType = ::TypeFromName ( name );
    if ( uType == 0xFF )
      return MSGF_E_TYPE;
    *outType = uType;
    return S_OK;
}

HRESULT
FacadeLibrary::IsValidName ( const wchar_t *name ) const
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );
    return IsUsableName ( name ) ? S_OK : S_FALSE;
}

const wchar_t*
FacadeLibrary::VersionString ( ) const
{
    // Static storage: the header says the pointer is owned by the DLL and
    // valid for its lifetime, so it must not be a member of a refcounted
    // object a client may have released by the time it reads the string.
    static const wchar_t s_szVersion[] =
        L"MsgFacade 1.0 (ABI 2) over Msgcore " _CRT_WIDE(MSGCORE_VERSION_STRING);
    return s_szVersion;
}

// ---------------------------------------------------------------------------
//  The one exported function
// ---------------------------------------------------------------------------

//
//  Hand back the library, if this client's header agrees with this DLL
//  NOTES: The version check is the whole reason the argument exists.  A client
//         built against a header whose interfaces have different vtable shapes
//         would read every method at the wrong offset, and the failure would
//         surface as a crash somewhere else entirely.  Refusing here costs one
//         comparison and turns that into a return code at the call site
//
extern "C" MSGF_API HRESULT __stdcall
MSGF_CreateLibrary ( unsigned int abiVersion, IMsgLibrary **outLibrary )
{
    AFX_MANAGE_STATE ( AfxGetStaticModuleState() );

    if ( !outLibrary )
      return E_POINTER;
    *outLibrary = nullptr;

    if ( abiVersion < ABI_VERSION_MIN || abiVersion > ABI_VERSION )
      return MSGF_E_ABI_MISMATCH;

    return FacadeLibrary::Acquire ( outLibrary );
}
