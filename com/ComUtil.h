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
// ComUtil.h -- the pieces every object in this server shares.
//
// Three jobs. Two of them are the same jobs this file has always had; the third
// used to be the largest thing in the server and is now four lines.
//
//  * VARIANT <-> a node's value.  The store has sixteen declared types and a
//    VARIANT carries its own, so the switch lives here once and IMsgFieldCom,
//    IMsgAttrCom, IMsgDescCom, IMsgListCom and IMsgVectCom all read and write
//    by exactly the same rules.
//
//  * IErrorInfo.  Every failure this layer can produce gets a sentence, for the
//    same reason TargetCom does it: the interesting failures are runtime
//    failures over a STRING (a name, a path, a type name), so there is nothing
//    typed at the call site for a script author to inspect, and a bare
//    0x8004030B says nothing at all.
//
//  * The exception boundary.  MSGF_GUARD wraps every entry point -- but read
//    what it is for now, below: it is a backstop rather than a load-bearing
//    part, because the facade already catches.
//
// ---------------------------------------------------------------------------
// WHAT CHANGED WHEN THIS SERVER MOVED ONTO MsgFacade
// ---------------------------------------------------------------------------
// It used to sit on Msgcore_c.h, the kernel's flat C ABI, and three of its five
// documented departures existed to survive that surface:
//
//  * THE NAME-CHAIN RESOLVER IS GONE.  A flat handle is invalidated by the next
//    heap relocation, so this server kept every node as a std::vector<wstring>
//    and re-resolved it on entry to every method -- ~200 lines, and the reason
//    CMsgStore::ResolveLive existed.  msgf::IMsgNode IS that, in the DLL below,
//    so a node reference here is now just an IMsgNode*.
//
//  * LIVE vs DETACHED IS GONE.  msgcore_field_child aliased the tree while
//    msgcore_mgr_p2pos2field deep-copied, both returning the same handle type,
//    and a write through the copy silently reached nothing.  Every IMsgNode is
//    a route into the live store, so there is no second kind and msgcDetached
//    cannot arise.
//
//  * THE STRICT-ACCESSOR SWITCH IS SMALLER.  msgcore_field_get_int threw on
//    anything that was not INT32; GetInt reads the whole integer family at any
//    width and answers MSGF_E_TYPE for anything else.
//
// What is left in this file is the part that was always about COM: turning a
// VARIANT into a value and a failure into a sentence.
#pragma once

#include "MsgcoreCom_h.h"

// ---------------------------------------------------------------------------
// The HRESULTs, mirroring the MsgcoreError enum in the IDL.
//
// Spelled out as HRESULT constants rather than used through the generated enum
// because MIDL emits values above 0x7FFFFFFF into an enum whose underlying type
// is then unsigned, and every comparison against an HRESULT would need a cast
// at the point of use. One cast here instead of a hundred there.
// ---------------------------------------------------------------------------
const HRESULT MSGC_E_CLOSED   = (HRESULT)0x80040300L;
const HRESULT MSGC_E_LOAD     = (HRESULT)0x80040301L;
const HRESULT MSGC_E_SAVE     = (HRESULT)0x80040302L;
const HRESULT MSGC_E_NO_FIELD = (HRESULT)0x80040303L;
const HRESULT MSGC_E_NO_POS   = (HRESULT)0x80040304L;
const HRESULT MSGC_E_TYPE     = (HRESULT)0x80040305L;
const HRESULT MSGC_E_DECLARE  = (HRESULT)0x80040306L;
const HRESULT MSGC_E_NOT_LIST = (HRESULT)0x80040307L;
const HRESULT MSGC_E_NOT_VECT = (HRESULT)0x80040308L;
const HRESULT MSGC_E_NO_COLL  = (HRESULT)0x80040309L;
const HRESULT MSGC_E_RANGE    = (HRESULT)0x8004030AL;
// 0x8004030B was msgcDetached. It cannot arise over MsgFacade -- there is no
// detached node -- and the value is left reserved rather than reused, so a
// client that still tests for it compiles and simply never sees it.
const HRESULT MSGC_E_KERNEL   = (HRESULT)0x8004030CL;
const HRESULT MSGC_E_STALE    = (HRESULT)0x8004030DL;
const HRESULT MSGC_E_NAME     = (HRESULT)0x8004030EL;
const HRESULT MSGC_E_NO_SINK  = (HRESULT)0x8004030FL;
const HRESULT MSGC_E_FOREIGN  = (HRESULT)0x80040310L;
const HRESULT MSGC_E_PAGESTATE = (HRESULT)0x80040311L;
const HRESULT MSGC_E_PATH     = (HRESULT)0x80040312L;
const HRESULT MSGC_E_LIMIT    = (HRESULT)0x80040313L;

// A NULL BSTR is a legal way for a client to say "empty string".
inline LPCWSTR Str ( BSTR bs ) { return ( bs != NULL ) ? bs : L""; }

// msgf::MAX_NAME. Named here so the check reads as a rule rather than as a
// number, and so it moves if the facade's does.
const UINT MSGC_MAX_NAME = msgf::MAX_NAME;

// ---------------------------------------------------------------------------
// IErrorInfo, shared by every object in this server.
//
// ComFail names the call, echoes the arguments it was given, and spells out
// what the code means. Called on FAILED results only.
// ---------------------------------------------------------------------------
HRESULT ComFail ( const IID& iid, HRESULT hr, LPCWSTR wszCall
                , BSTR bsArg1 = NULL, BSTR bsArg2 = NULL );

// The same, for a call whose argument is a literal rather than a client BSTR.
HRESULT ComFailW ( const IID& iid, HRESULT hr, LPCWSTR wszCall, LPCWSTR wszArg );

// Every facade HRESULT this server can meet, mapped onto the MsgcoreError code
// a script author reads. Called on FAILED results; S_OK and S_FALSE pass
// through untouched, because S_FALSE is never an error in either ABI.
HRESULT FromFacade ( HRESULT hrFacade );

// ---------------------------------------------------------------------------
// The exception boundary.
//
// Wraps the body of an entry point. Every object defines a file-local
//     static inline HRESULT Fail ( HRESULT, LPCWSTR, BSTR = NULL, BSTR = NULL );
// before using these, exactly as ComNetwork.cpp does in TargetCom.
//
// THIS IS NOW A BACKSTOP, NOT A MECHANISM. Against Msgcore_c.h it was
// load-bearing: the kernel throws, roughly a third of the flat wrappers did not
// catch, and an exception crossing a COM vtable is undefined behaviour. Every
// MsgFacade entry point catches and answers MSGF_E_CORE, so nothing should
// reach here -- and it stays because "should" is not "does", and because the
// STL and ATL calls in this file can throw on their own account.
// ---------------------------------------------------------------------------
#define MSGF_GUARD_BEGIN            try {
#define MSGF_GUARD_END(wszCall)     } catch ( ... ) { return Fail ( MSGC_E_KERNEL, wszCall ); }

// ---------------------------------------------------------------------------
// VARIANT plumbing
// ---------------------------------------------------------------------------

// Unwraps VT_BYREF and VARIANT-in-VARIANT, which is what VB6/VBScript hand over
// for a ByRef argument.
const VARIANT* UnwrapVariant ( const VARIANT& v );

// A node's value, read according to its own declared type.
HRESULT ReadNodeValue ( msgf::IMsgNode *pNode, VARIANT *pOut );

// The reverse, onto a node that already exists (Value = x). Keeps the node's
// declared type: this is a write, not a re-declare.
HRESULT WriteNodeValue ( msgf::IMsgNode *pNode, const VARIANT& v );

// Create-or-update under `pOwner`, in `uScope`, with the node's type taken from
// the VARIANT. One function for both collections -- the scope is an argument
// here, where the flat ABI had three near-identical families.
HRESULT DeclareVariant ( msgf::IMsgNode *pOwner, unsigned int uScope, LPCWSTR wszName
                       , const VARIANT& v, int bUpdate, msgf::IMsgNode **ppOut );

// The same, at an EXPLICIT MsgDataType -- the one thing a VARIANT cannot say.
HRESULT DeclareTypedVariant ( msgf::IMsgNode *pOwner, unsigned int uScope, LPCWSTR wszName
                            , const VARIANT& v, unsigned char uDataType
                            , int bUpdate, msgf::IMsgNode **ppOut );

// SAFEARRAY(VT_UI1) <-> raw bytes, for BLOB16 nodes.
HRESULT BytesToVariant ( const void *pData, unsigned int cb, VARIANT *pOut );
HRESULT VariantToBytes ( const VARIANT& v, std::vector<BYTE>& out );

// A node's timestamp is epoch seconds; a script wants a Date.
DATE      EpochToDate ( long long tsSeconds );
long long DateToEpoch ( DATE dt );

// A name the store will accept. Empty and over-long are two of the three it
// will not; the third (a name holding the path grammar's own characters) is the
// facade's own check, which every Declare makes.
HRESULT CheckName ( BSTR bsName );

// The caller-sized buffer protocol, once, for every string a facade object
// answers. Reads into a CComBSTR, which is what every property here hands back.
HRESULT ReadString ( msgf::IMsgNode *pNode
                   , HRESULT (msgf::IMsgNode::*pfn)(wchar_t*,unsigned int*) const
                   , ATL::CComBSTR& out );

// ---------------------------------------------------------------------------
// RAII for a facade interface pointer.
//
// ATL::CComPtr would do this, and does it for COM interfaces elsewhere in the
// server -- but the facade's interfaces are NOT IUnknown-derived (they have
// Release and no AddRef, deliberately: see MsgFacade.h), so CComPtr cannot
// compile against them. This is the two members of it that apply.
// ---------------------------------------------------------------------------
template <class I>
class CFacadePtr
{
  public:
    CFacadePtr ( )               : m_p ( NULL ) { }
    explicit CFacadePtr ( I *p ) : m_p ( p )    { }
   ~CFacadePtr ( )                              { Free (); }

    void Attach ( I *p )     { Free (); m_p = p; }
    I*   Detach ( )          { I *p = m_p; m_p = NULL; return p; }
    void Free   ( )          { if ( m_p ) { m_p->Release (); m_p = NULL; } }

    I**  operator& ( )       { Free (); return &m_p; }
    I*   operator-> ( ) const { return m_p; }
    operator I* ( ) const    { return m_p; }
    bool operator! ( ) const { return m_p == NULL; }

  private:
    CFacadePtr ( const CFacadePtr& );
    CFacadePtr& operator= ( const CFacadePtr& );
    I *m_p;
};

typedef CFacadePtr<msgf::IMsgNode>   CNodePtr;
typedef CFacadePtr<msgf::IMsgList>   CListPtr;
typedef CFacadePtr<msgf::IMsgVect>   CVectPtr;
typedef CFacadePtr<msgf::IMsgCursor> CCursorPtr;
typedef CFacadePtr<msgf::IMsgWalker> CWalkerPtr;
