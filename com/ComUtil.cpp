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
// ComUtil.cpp -- shared value conversion, error text and the exception boundary.
#include "stdafx.h"
#include "ComUtil.h"

using namespace msgf;

// ---------------------------------------------------------------------------
// IErrorInfo -- one sentence per HRESULT this layer can hand back.
//
// The rule for the wording, taken from TargetCom: say what was rejected AND
// what would be accepted. A client that only learns "invalid" has to go and
// find the grammar; one that is told the grammar can fix the call and move on.
// ---------------------------------------------------------------------------
static LPCWSTR MeaningOf ( HRESULT hr )
{
    if ( hr == MSGC_E_CLOSED )
        return L"this store is closed, or was never opened. Close() releases the "
               L"store and every node, collection and cursor taken from it then "
               L"fails this way rather than reading freed memory; create another "
               L"MsgcoreCom.MsgStore.";

    if ( hr == MSGC_E_LOAD )
        return L"the file could not be loaded. It must be a Msgcore .p2p image "
               L"this build can read: a file saved by a store of a different "
               L"addressing mode is readable, one written by a foreign-endian "
               L"peer is not, and a truncated or non-Msgcore file is rejected at "
               L"its synchronisation word.";

    if ( hr == MSGC_E_SAVE )
        return L"the file could not be saved. Save with no argument writes back "
               L"over Filename, so a store that has never been opened from or "
               L"saved to a file must be given a path the first time.";

    if ( hr == MSGC_E_NO_FIELD )
        return L"there is no node of that name or at that path. Child takes ONE "
               L"literal name and never parses, so a dotted string is a name "
               L"containing dots to it; MsgStore.FieldAt is the one that walks a "
               L"path. Use Exists to ask, or Declare to create.";

    if ( hr == MSGC_E_NO_POS )
        return L"that P2Pos does not resolve in this store. A P2Pos is only "
               L"meaningful in the store it came from, and a node that has been "
               L"deleted does not come back -- which is the ordinary case inside "
               L"a msgcTriggerDelete handler, where the event tells you what was "
               L"removed rather than handing you the thing.";

    if ( hr == MSGC_E_TYPE )
        return L"that value's type cannot be stored here, or the node does not "
               L"hold what was asked for. The store holds integers (any width), "
               L"Single, Double, Boolean, String, Date, a byte array as BLOB16 "
               L"and a GUID as text; Empty, Null, an object and an array of "
               L"anything but bytes have no representation. A node's exact "
               L"declared width is set with DeclareTyped, not by the VARIANT.";

    if ( hr == MSGC_E_DECLARE )
        return L"the node could not be created or updated. With update False an "
               L"existing node of that name is refused rather than replaced -- "
               L"which is the difference between creating and assigning, and is "
               L"why it is an argument.";

    if ( hr == MSGC_E_NOT_LIST )
        return L"this node does not hold a linked list. IsList says so before "
               L"asking, and a node that holds children is not the same thing: a "
               L"list is a sequence of unnamed values, children are named nodes.";

    if ( hr == MSGC_E_NOT_VECT )
        return L"this node does not hold an indexed vector. IsVect says so before "
               L"asking.";

    if ( hr == MSGC_E_NO_COLL )
        return L"this node has no such collection. A list and a vector are "
               L"containers rather than items, so they carry no attributes and no "
               L"children of their own -- asking either for one is this error.";

    if ( hr == MSGC_E_RANGE )
        return L"the index is outside the collection. Indices start at 0 and run "
               L"to Count - 1; a negative index is this error and not an unsigned "
               L"wrap.";

    if ( hr == MSGC_E_KERNEL )
        return L"the store raised an exception. It has been contained -- an "
               L"exception crossing a COM call is undefined behaviour, so this "
               L"server catches at every entry point -- but the store may be "
               L"inconsistent, and anything already saved is the safe copy. This "
               L"is a defect in the store or in the data, not a usage error.";

    if ( hr == MSGC_E_STALE )
        return L"the node this object names is gone -- deleted, renamed, or moved "
               L"under another parent since the reference was taken. A node "
               L"reference here is a PATH re-resolved on use, not a pointer, so "
               L"this is a clean error where a raw handle would hand back a "
               L"dangling heap address. Re-read the path, or use Exists first.";

    if ( hr == MSGC_E_NAME )
        return L"the name is empty, too long, or holds a character a name may not "
               L"hold. A node name is 1 to 63 UTF-16 units and cannot contain "
               L". @ : ^ / \\ * ? | < > or \" -- the first two because they are "
               L"the path grammar's own separators, which is what makes a Path "
               L"reversible.";

    if ( hr == MSGC_E_FOREIGN )
        return L"that object belongs to a DIFFERENT store. Two stores are two "
               L"heaps with nothing in common, so a node of one cannot be moved "
               L"into, or compared with a position in, the other -- and doing it "
               L"by path would silently act on a same-named node in the wrong "
               L"tree. Copy the value across instead: read it from one store and "
               L"Declare it in the other.";

    if ( hr == MSGC_E_PAGESTATE )
        return L"paging is already suspended, was not suspended, or has no sink "
               L"to suspend. PushPaging does not nest -- the store saves ONE "
               L"registration -- and a push with nothing installed has nothing to "
               L"restore at the matching pop. Pair each PushPaging with exactly "
               L"one PopPaging, and install a sink first.";

    if ( hr == MSGC_E_NO_SINK )
        return L"nothing is registered to receive it. A trigger is delivered as "
               L"an OnChange event, so a client with no sink attached to the "
               L"store's connection point would never see it; attach one before "
               L"arming nodes.";

    if ( hr == MSGC_E_PATH )
        return L"that path does not parse. A path is a '.' before each child step "
               L"and an '@' before each attribute step, from the root: \"\" is the "
               L"root, \".Config\" a child of it, \".Config.Window@Colour\" an "
               L"attribute of a grandchild. It is exactly what a node's Path "
               L"property answers, so a path handed out here can be handed back.";

    if ( hr == MSGC_E_LIMIT )
        return L"the value is past what that node can hold. A declared width is "
               L"a promise: 300 does not fit an INT08 and is refused rather than "
               L"truncated to 44. A list cell cannot grow either -- overwrite it "
               L"with something that fits, or delete it and add a new one.";

    switch ( hr )
    {
      case DISP_E_TYPEMISMATCH:
        return L"the value could not be converted to anything this store can "
               L"hold. See msgcType for the list.";

      case E_OUTOFMEMORY:
        return L"out of memory.";

      case E_POINTER:
        return L"a required out-parameter was NULL.";

      case E_INVALIDARG:
        return L"an argument was rejected.";

      default:
        break;
    }

    return NULL;                        // not ours: leave the code to speak for itself
}

// Named, echoed, explained. Echoing the ARGUMENTS is half the value: it turns
// "0x80040303" into "Child ( 'widht' ) failed: there is no node of that name",
// and the typo is then visible without a debugger.
static HRESULT ReportOne ( const IID& iid, HRESULT hr, LPCWSTR wszCall
                         , LPCWSTR wszArg1, LPCWSTR wszArg2 )
{
    LPCWSTR wszWhy = MeaningOf ( hr );
    if ( wszWhy == NULL )
        return hr;                      // nothing to add; do not overwrite a better one

    ATL::CComBSTR bs ( wszCall );
    bs += L" ( ";
    if ( wszArg1 != NULL ) { bs += L"'"; bs += wszArg1; bs += L"'"; }
    if ( wszArg2 != NULL ) { bs += L", '"; bs += wszArg2; bs += L"'"; }
    bs += L" ) failed: ";
    bs += wszWhy;

    // CLSID_MsgStore for every object on purpose -- it is the one coclass with
    // a ProgID, so Err.Source resolves to "MsgcoreCom.MsgStore" rather than
    // being left empty. WHICH object failed is already in the description,
    // which names the interface method.
    return ATL::AtlReportError ( CLSID_MsgStore, (LPCOLESTR)bs, iid, hr );
}

HRESULT ComFail ( const IID& iid, HRESULT hr, LPCWSTR wszCall, BSTR bsArg1, BSTR bsArg2 )
{
    return ReportOne ( iid, hr, wszCall, bsArg1, bsArg2 );
}

HRESULT ComFailW ( const IID& iid, HRESULT hr, LPCWSTR wszCall, LPCWSTR wszArg )
{
    return ReportOne ( iid, hr, wszCall, wszArg, NULL );
}

// ---------------------------------------------------------------------------
// Facade code -> MsgcoreError code.
//
// The two vocabularies are deliberately not the same one. MsgFacade's codes
// describe a data model; these describe a scripting surface, and they existed
// first. Mapping in one place means a new facade code shows up as MSGC_E_KERNEL
// rather than as a number no client has a name for.
// ---------------------------------------------------------------------------
// CALL THIS EXACTLY ONCE PER ANSWER. The two code spaces overlap numerically --
// MSGF_E_NAME and MSGC_E_NO_FIELD are both 0x80040303, MSGF_E_TYPE and
// MSGC_E_RANGE are both 0x80040305 -- so a second pass over an already-converted
// code does not no-op, it silently rewrites the error into a different and
// entirely plausible one. That is not hypothetical: a node whose name had been
// renamed away answered msgcName, with a sentence about a name the caller had
// never typed, because a msgcNoField went through here twice.
//
// The rule that keeps it from happening: THE READ HELPERS ANSWER FACADE CODES
// (ReadString, ReadNodeValue, WriteNodeValue, the Kind helpers), so the entry
// point converts. The DECLARE helpers convert internally and their call sites
// pass the result to Fail() unconverted. Either discipline is fine; mixing them
// at one call site is what bites.
HRESULT FromFacade ( HRESULT hrFacade )
{
    if ( !FAILED ( hrFacade ) )
        return hrFacade;                       // S_OK and S_FALSE both pass

    if ( hrFacade == MSGF_E_NO_ITEM )   return MSGC_E_NO_FIELD;
    if ( hrFacade == MSGF_E_NAME )      return MSGC_E_NAME;
    if ( hrFacade == MSGF_E_TYPE )      return MSGC_E_TYPE;
    if ( hrFacade == MSGF_E_RANGE )     return MSGC_E_RANGE;
    if ( hrFacade == MSGF_E_FILE )      return MSGC_E_LOAD;
    if ( hrFacade == MSGF_E_CLOSED )    return MSGC_E_CLOSED;
    if ( hrFacade == MSGF_E_SCOPE )     return MSGC_E_NO_COLL;
    if ( hrFacade == MSGF_E_PATH )      return MSGC_E_PATH;
    if ( hrFacade == MSGF_E_EXISTS )    return MSGC_E_DECLARE;
    if ( hrFacade == MSGF_E_NO_POS )    return MSGC_E_NO_POS;
    if ( hrFacade == MSGF_E_LIMIT )     return MSGC_E_LIMIT;
    if ( hrFacade == MSGF_E_DEPTH )     return MSGC_E_PATH;
    if ( hrFacade == MSGF_E_STATE )     return MSGC_E_PAGESTATE;
    if ( hrFacade == MSGF_E_CORE )      return MSGC_E_KERNEL;
    if ( hrFacade == MSGF_E_ABI_MISMATCH ) return MSGC_E_KERNEL;

    // E_POINTER, E_INVALIDARG and ERROR_MORE_DATA are COM's own and mean here
    // what they mean everywhere.
    return hrFacade;
}

// ---------------------------------------------------------------------------
// VARIANT plumbing
// ---------------------------------------------------------------------------
const VARIANT* UnwrapVariant ( const VARIANT& v )
{
    const VARIANT *pv = &v;
    while ( pv != NULL && pv->vt == ( VT_VARIANT | VT_BYREF ) && pv->pvarVal != NULL )
        pv = pv->pvarVal;
    return pv;
}

HRESULT BytesToVariant ( const void *pData, unsigned int cb, VARIANT *pOut )
{
    if ( pOut == NULL ) return E_POINTER;
    ::VariantInit ( pOut );

    SAFEARRAY *psa = ::SafeArrayCreateVector ( VT_UI1, 0, cb );
    if ( psa == NULL ) return E_OUTOFMEMORY;

    if ( cb != 0 )
    {
        void *pDst = NULL;
        HRESULT hr = ::SafeArrayAccessData ( psa, &pDst );
        if ( FAILED(hr) ) { ::SafeArrayDestroy ( psa ); return hr; }
        ::memcpy ( pDst, pData, cb );
        ::SafeArrayUnaccessData ( psa );
    }

    pOut->vt     = VT_ARRAY | VT_UI1;
    pOut->parray = psa;
    return S_OK;
}

HRESULT VariantToBytes ( const VARIANT& vIn, std::vector<BYTE>& out )
{
    out.clear();

    const VARIANT *pv = UnwrapVariant ( vIn );
    if ( pv == NULL )
        return E_POINTER;

    if ( ( pv->vt & VT_ARRAY ) == 0 || ( pv->vt & VT_TYPEMASK ) != VT_UI1 )
        return DISP_E_TYPEMISMATCH;

    SAFEARRAY *psa = ( pv->vt & VT_BYREF ) ? ( pv->pparray ? *pv->pparray : NULL )
                                           : pv->parray;
    if ( psa == NULL )
        return S_OK;
    if ( ::SafeArrayGetDim ( psa ) != 1 )
        return DISP_E_TYPEMISMATCH;

    LONG lo = 0, hi = -1;
    HRESULT hr = ::SafeArrayGetLBound ( psa, 1, &lo );
    if ( SUCCEEDED(hr) ) hr = ::SafeArrayGetUBound ( psa, 1, &hi );
    if ( FAILED(hr) ) return hr;
    if ( hi < lo ) return S_OK;

    void *pData = NULL;
    hr = ::SafeArrayAccessData ( psa, &pData );
    if ( FAILED(hr) ) return hr;
    const BYTE *p = (const BYTE*)pData;
    out.assign ( p, p + ( (size_t)hi - lo + 1 ) );
    ::SafeArrayUnaccessData ( psa );
    return S_OK;
}

// ---------------------------------------------------------------------------
// The caller-sized buffer protocol, once.
//
// Ask with a NULL buffer, allocate, ask again. Every string the facade answers
// comes back this way, and the count includes the terminator.
// ---------------------------------------------------------------------------
HRESULT ReadString ( IMsgNode *pNode
                   , HRESULT (IMsgNode::*pfn)(wchar_t*,unsigned int*) const
                   , ATL::CComBSTR& out )
{
    out.Empty ();
    if ( pNode == NULL ) return E_POINTER;

    unsigned int cch = 0;
    HRESULT hr = ( pNode->*pfn ) ( NULL, &cch );
    if ( FAILED(hr) ) return hr;

    if ( cch <= 1 )                      // just the terminator: an empty string
    {
        out = L"";
        return S_OK;
    }

    std::vector<wchar_t> buf ( cch );
    hr = ( pNode->*pfn ) ( &buf[0], &cch );
    if ( FAILED(hr) ) return hr;

    out = &buf[0];
    return S_OK;
}

// ---------------------------------------------------------------------------
// Reading a node's value.
//
// The switch is on the node's OWN declared type, asked first. It is shorter
// than it was: the facade's GetInt reads the whole integer family at any width
// and reports the signedness, where the flat ABI had one throwing accessor per
// type and a separate tolerant one to avoid them.
//
// IT ANSWERS A FACADE CODE, NOT A MSGC ONE -- as ReadString and the Kind
// helpers do, and as every other helper here should. It used to convert, and
// that was a latent defect rather than a style question: THE TWO CODE SPACES
// OVERLAP NUMERICALLY. MSGF_E_NAME and MSGC_E_NO_FIELD are both 0x80040303, so
// putting an already-converted code through FromFacade a second time rewrites
// "there is no such node" into "that name is malformed". A node whose name had
// been renamed away reported msgcName, and the sentence a client saw was about
// a name it had never used. Converting exactly once, at the entry point, is
// what makes that impossible rather than merely unlikely.
// ---------------------------------------------------------------------------
HRESULT ReadNodeValue ( IMsgNode *pNode, VARIANT *pOut )
{
    if ( pOut == NULL ) return E_POINTER;
    ::VariantInit ( pOut );
    if ( pNode == NULL ) return E_POINTER;

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = pNode->GetType ( &uType );
    if ( FAILED(hr) ) return hr;

    switch ( uType )
    {
      case MSGF_TYPE_NULL:
        return S_OK;                                    // VT_EMPTY

      case MSGF_TYPE_BOOL:
      {
        long long ll = 0;
        hr = pNode->GetInt ( &ll, NULL );
        if ( FAILED(hr) ) return hr;
        pOut->vt      = VT_BOOL;
        pOut->boolVal = ( ll != 0 ) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
      }

      // Every integer width through the one width-agnostic accessor. Anything
      // that fits a Long stays a Long, because that is what a script can do
      // arithmetic on without ceremony; the rest become VT_I8.
      case MSGF_TYPE_INT08:
      case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16:
      case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32:
      case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64:
      case MSGF_TYPE_UINT64:
      {
        long long llValue = 0;
        int       bUnsigned = 0;
        hr = pNode->GetInt ( &llValue, &bUnsigned );
        if ( FAILED(hr) ) return hr;

        const bool bFitsLong = ( bUnsigned != 0 )
                             ? ( (unsigned long long)llValue <= 0x7FFFFFFFULL )
                             : ( llValue >= -2147483647LL - 1 && llValue <= 2147483647LL );
        if ( bFitsLong )
        {
            pOut->vt   = VT_I4;
            pOut->lVal = (LONG)llValue;
        }
        else
        {
            pOut->vt    = VT_I8;
            pOut->llVal = llValue;
        }
        return S_OK;
      }

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
      {
        double d = 0.0;
        hr = pNode->GetReal ( &d );
        if ( FAILED(hr) ) return hr;
        // A FLOAT answers VT_R4 so that a script comparing it against the
        // number it wrote is comparing at the width the store kept.
        if ( uType == MSGF_TYPE_FLOAT ) { pOut->vt = VT_R4; pOut->fltVal = (float)d; }
        else                            { pOut->vt = VT_R8; pOut->dblVal = d;        }
        return S_OK;
      }

      case MSGF_TYPE_BSTR16:
      case MSGF_TYPE_WSTR16:
      {
        ATL::CComBSTR bs;
        hr = ReadString ( pNode, &IMsgNode::GetText, bs );
        if ( FAILED(hr) ) return hr;
        pOut->vt      = VT_BSTR;
        pOut->bstrVal = bs.Detach ();
        return S_OK;
      }

      case MSGF_TYPE_BLOB16:
      {
        unsigned int cb = 0;
        hr = pNode->GetBlob ( NULL, &cb );
        if ( FAILED(hr) ) return hr;

        // A BLOB16 that is present but empty answers an EMPTY ARRAY, not
        // VT_EMPTY: the node exists and holds no bytes, which is a different
        // fact from holding no value.
        if ( cb == 0 )
            return BytesToVariant ( NULL, 0, pOut );

        std::vector<BYTE> bytes ( cb );
        hr = pNode->GetBlob ( &bytes[0], &cb );
        if ( FAILED(hr) ) return hr;
        return BytesToVariant ( &bytes[0], cb, pOut );
      }

      case MSGF_TYPE_GUID:
      {
        // Canonical text, because there is no VARIANT type for a GUID and the
        // text form is what a script compares, logs and puts in a registry key.
        ATL::CComBSTR bs;
        hr = ReadString ( pNode, &IMsgNode::GetGuid, bs );
        if ( FAILED(hr) ) return hr;
        pOut->vt      = VT_BSTR;
        pOut->bstrVal = bs.Detach ();
        return S_OK;
      }

      default:
        break;
    }

    // An unrecognised type is not an error to READ: the store may be newer than
    // this server, and the facade reports every type the kernel has rather than
    // only the ones it can declare. Answer its text if it has any, Empty if not.
    {
        ATL::CComBSTR bs;
        if ( SUCCEEDED ( ReadString ( pNode, &IMsgNode::GetText, bs ) ) )
        {
            pOut->vt      = VT_BSTR;
            pOut->bstrVal = bs.Detach ();
        }
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// Writing a value into a node that already exists.
//
// Narrower than DeclareVariant on purpose: this writes a node's value and does
// NOT change its declared type. A value that will not fit is msgcLimit rather
// than a silent truncation -- which is the facade's rule, not this server's.
// ---------------------------------------------------------------------------
HRESULT WriteNodeValue ( IMsgNode *pNode, const VARIANT& vIn )
{
    if ( pNode == NULL ) return E_POINTER;

    const VARIANT *pv = UnwrapVariant ( vIn );
    if ( pv == NULL ) return E_POINTER;

    unsigned char uType = MSGF_TYPE_NULL;
    HRESULT hr = pNode->GetType ( &uType );
    if ( FAILED(hr) ) return hr;

    // A byte array only ever means a BLOB16, whatever the node currently is --
    // and there is no in-place blob write at any layer below this one, so the
    // node has to be re-declared through its parent. Said as a type error
    // rather than pretended.
    if ( ( pv->vt & VT_ARRAY ) != 0 )
        return MSGF_E_TYPE;

    ATL::CComVariant vTmp;

    switch ( uType )
    {
      case MSGF_TYPE_BOOL:
        if ( FAILED ( vTmp.ChangeType ( VT_BOOL, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return pNode->SetInt ( ( vTmp.boolVal != VARIANT_FALSE ) ? 1 : 0 );

      case MSGF_TYPE_INT08:
      case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16:
      case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32:
      case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64:
      case MSGF_TYPE_UINT64:
        // One call for every width: SetInt keeps the node's declared width and
        // refuses a value it could not hold.
        if ( FAILED ( vTmp.ChangeType ( VT_I8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return pNode->SetInt ( vTmp.llVal );

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
        if ( FAILED ( vTmp.ChangeType ( VT_R8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return pNode->SetReal ( vTmp.dblVal );

      case MSGF_TYPE_BSTR16:
      case MSGF_TYPE_WSTR16:
        if ( FAILED ( vTmp.ChangeType ( VT_BSTR, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return pNode->SetText ( Str ( vTmp.bstrVal ) );

      default:
        break;
    }

    // NULL, GUID, BLOB16 and anything unrecognised: no in-place write exists,
    // so the honest answer is that the node has to be re-declared.
    return MSGF_E_TYPE;
}

// ---------------------------------------------------------------------------
// Create-or-update, with the node's type taken from the VARIANT.
//
// ONE function, where the flat ABI needed three near-copies -- the collection
// is an argument now. Two consequences a script author can see, both of them
// old warts of this server that are simply gone: a Boolean declared into the
// attribute collection used to land as an INT32 0/1, and a 64-bit value or a
// byte array could not be declared there at all.
// ---------------------------------------------------------------------------
HRESULT DeclareVariant ( IMsgNode *pOwner, unsigned int uScope, LPCWSTR wszName
                       , const VARIANT& vIn, int bUpdate, IMsgNode **ppOut )
{
    if ( ppOut != NULL ) *ppOut = NULL;
    if ( pOwner == NULL || wszName == NULL ) return E_POINTER;

    const VARIANT *pv = UnwrapVariant ( vIn );
    if ( pv == NULL ) return E_POINTER;

    const unsigned int uFlags = bUpdate ? MSGF_DECLARE_UPDATE : 0;

    // A byte array becomes a BLOB16.
    if ( ( pv->vt & VT_ARRAY ) != 0 )
    {
        std::vector<BYTE> bytes;
        HRESULT hr = VariantToBytes ( *pv, bytes );
        if ( FAILED(hr) ) return hr;
        return FromFacade ( pOwner->DeclareBlob ( uScope, wszName
                                                , bytes.empty() ? NULL : &bytes[0]
                                                , (unsigned int)bytes.size()
                                                , uFlags, ppOut ) );
    }

    ATL::CComVariant v;

    switch ( pv->vt )
    {
      // No representation, and coercing one would be a guess. VT_EMPTY is "no
      // argument was passed" in most scripting hosts, so silently storing an
      // empty string for it would turn a forgotten argument into stored data.
      case VT_EMPTY:
      case VT_NULL:
        return MSGC_E_TYPE;

      case VT_BOOL:
        return FromFacade ( pOwner->DeclareInt ( uScope, wszName
                                               , ( pv->boolVal != VARIANT_FALSE ) ? 1 : 0
                                               , MSGF_TYPE_BOOL, uFlags, ppOut ) );

      case VT_I1: case VT_UI1: case VT_I2: case VT_UI2:
      case VT_I4: case VT_UI4: case VT_INT: case VT_UINT:
        if ( FAILED ( v.ChangeType ( VT_I4, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareInt ( uScope, wszName, v.lVal, 0, uFlags, ppOut ) );

      case VT_I8: case VT_UI8:
        if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareInt ( uScope, wszName, v.llVal
                                               , MSGF_TYPE_INT64, uFlags, ppOut ) );

      // A Date IS an automation double, and the store has no date type, so it
      // is stored as one -- losslessly, and readable back as a Date by any host
      // that knows to expect one. The node's own Timestamp property is the
      // place where a date is stored AS a date.
      case VT_DATE:
      case VT_R4: case VT_R8: case VT_CY: case VT_DECIMAL:
        if ( FAILED ( v.ChangeType ( VT_R8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareReal ( uScope, wszName, v.dblVal, 0, uFlags, ppOut ) );

      case VT_BSTR:
        return FromFacade ( pOwner->DeclareText ( uScope, wszName, Str ( pv->bstrVal )
                                                , uFlags, ppOut ) );

      default:
        break;
    }

    // Anything else that will coerce to a string does; an object, a pointer or
    // an unhandled array does not.
    if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return MSGC_E_TYPE;
    return FromFacade ( pOwner->DeclareText ( uScope, wszName, Str ( v.bstrVal )
                                            , uFlags, ppOut ) );
}

// ---------------------------------------------------------------------------
// Create-or-update at an EXPLICIT type.
//
// The one thing a VARIANT cannot express: a node's declared WIDTH. A value file
// that reads UINT08 and writes back INT32 has silently retyped the store, which
// is why this is a separate method rather than a hint on Declare.
//
// Two types that could not be declared through the flat ABI can be declared
// here: FLOAT (DeclareReal takes the width) and every unsigned width (the flat
// ABI had no accessor for UINT08 or UINT16 at all).
// ---------------------------------------------------------------------------
HRESULT DeclareTypedVariant ( IMsgNode *pOwner, unsigned int uScope, LPCWSTR wszName
                            , const VARIANT& vIn, unsigned char uDataType
                            , int bUpdate, IMsgNode **ppOut )
{
    if ( ppOut != NULL ) *ppOut = NULL;
    if ( pOwner == NULL || wszName == NULL ) return E_POINTER;

    const VARIANT *pv = UnwrapVariant ( vIn );
    if ( pv == NULL ) return E_POINTER;

    const unsigned int uFlags = bUpdate ? MSGF_DECLARE_UPDATE : 0;
    ATL::CComVariant   v;

    switch ( uDataType )
    {
      case MSGF_TYPE_INT08:  case MSGF_TYPE_UINT08:
      case MSGF_TYPE_INT16:  case MSGF_TYPE_UINT16:
      case MSGF_TYPE_INT32:  case MSGF_TYPE_UINT32:
      case MSGF_TYPE_INT64:  case MSGF_TYPE_UINT64:
      case MSGF_TYPE_BOOL:
        if ( FAILED ( v.ChangeType ( VT_I8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareInt ( uScope, wszName, v.llVal
                                               , uDataType, uFlags, ppOut ) );

      case MSGF_TYPE_FLOAT:
      case MSGF_TYPE_DOUBLE:
        if ( FAILED ( v.ChangeType ( VT_R8, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareReal ( uScope, wszName, v.dblVal
                                                , uDataType, uFlags, ppOut ) );

      case MSGF_TYPE_BSTR16:
      case MSGF_TYPE_WSTR16:
        if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareText ( uScope, wszName, Str ( v.bstrVal )
                                                , uFlags, ppOut ) );

      case MSGF_TYPE_BLOB16:
      {
        std::vector<BYTE> bytes;
        HRESULT hr = VariantToBytes ( *pv, bytes );
        if ( FAILED(hr) ) return hr;
        return FromFacade ( pOwner->DeclareBlob ( uScope, wszName
                                                , bytes.empty() ? NULL : &bytes[0]
                                                , (unsigned int)bytes.size()
                                                , uFlags, ppOut ) );
      }

      case MSGF_TYPE_GUID:
        if ( FAILED ( v.ChangeType ( VT_BSTR, pv ) ) ) return DISP_E_TYPEMISMATCH;
        return FromFacade ( pOwner->DeclareGuid ( uScope, wszName, Str ( v.bstrVal )
                                                , uFlags, ppOut ) );

      // NULL is the one type with no declare: a node with no value at all is
      // what a node IS before something is put in it, and asking for one is
      // asking for nothing to happen.
      case MSGF_TYPE_NULL:
      default:
        return MSGC_E_TYPE;
    }
}

// ---------------------------------------------------------------------------
// Epoch seconds <-> automation DATE.
//
// 25569 is 1 January 1970 counted from the automation epoch (30 December 1899),
// which is the whole of the conversion; the rest is seconds per day.
// ---------------------------------------------------------------------------
static const double kUnixEpochAsDate = 25569.0;
static const double kSecondsPerDay   = 86400.0;

DATE EpochToDate ( long long tsSeconds )
{
    if ( tsSeconds == 0 )
        return (DATE)0.0;               // "unset", and 0 is how the store says it
    return (DATE)( kUnixEpochAsDate + (double)tsSeconds / kSecondsPerDay );
}

long long DateToEpoch ( DATE dt )
{
    if ( dt == (DATE)0.0 )
        return 0;
    const double dSeconds = ( (double)dt - kUnixEpochAsDate ) * kSecondsPerDay;
    // Rounded, not truncated: a Date built from a whole number of seconds is
    // not exactly representable, and truncating loses a second roughly half the
    // time -- which shows up as a timestamp that walks backwards on every
    // read-modify-write cycle.
    return (long long)( ( dSeconds >= 0.0 ) ? ( dSeconds + 0.5 ) : ( dSeconds - 0.5 ) );
}

HRESULT CheckName ( BSTR bsName )
{
    const UINT cch = ( bsName != NULL ) ? ::SysStringLen ( bsName ) : 0;
    if ( cch == 0 || cch > MSGC_MAX_NAME )
        return MSGC_E_NAME;
    return S_OK;
}
