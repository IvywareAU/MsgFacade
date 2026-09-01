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
// FacadeLibrary.h -- FacadeLibrary, the object behind msgf::IMsgLibrary.
//
// A refcounted process-wide singleton, and the only thing MSGF_CreateLibrary
// ever hands out.  It owns no kernel state -- Msgcore has no process-level
// startup or shutdown of its own -- so what it is really for is being the ONE
// place a store comes from, and holding the ABI check that decides whether
// this client's header and this DLL agree about the shape of every vtable
// behind it.
#pragma once

#include "FacadeInternal.h"

class FacadeLibrary : public msgf::IMsgLibrary
{
    public:
        // Create on first call, add a reference on every later one.
        static HRESULT
          Acquire ( msgf::IMsgLibrary **outLibrary );

        // Held by every store, so that the DLL's singleton outlives a client
        // that released the library before its stores.
        void   AddRef     ( );
        ULONG  ReleaseRef ( );

    // msgf::IMsgLibrary
    public:
      virtual HRESULT CreateStore   ( unsigned char addr
                                    , unsigned int initialBytes
                                    , unsigned int maxBytes
                                    , msgf::IMsgStore **outStore );
      virtual HRESULT OpenStore     ( const wchar_t *filename
                                    , msgf::IMsgStore **outStore );
      virtual HRESULT WildcardMatch ( const wchar_t *pattern
                                    , const wchar_t *name ) const;
      virtual HRESULT TypeName      ( unsigned char type
                                    , wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT TypeFromName  ( const wchar_t *name
                                    , unsigned char *outType ) const;
      virtual HRESULT IsValidName   ( const wchar_t *name ) const;
      virtual const wchar_t* VersionString ( ) const;
      virtual ULONG   Release       ( );

    private:
        FacadeLibrary ( ) { }
       ~FacadeLibrary ( ) { }

    // Attributes
    private:
        LONG m_cRef{0};

        static FacadeLibrary   *s_pInstance;
        static CCriticalSection s_oCSectInstance;
};
