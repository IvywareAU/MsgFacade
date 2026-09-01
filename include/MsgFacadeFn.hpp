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
// MsgFacadeFn.hpp
//
// OPTIONAL header-only convenience layer over MsgFacade.h: RAII handles,
// std::wstring / std::vector returns, and a std::function change sink.
//
// Nothing in this file crosses the DLL boundary; it is pure client-side sugar
// around the flat vtable ABI, so richer C++ types (std::function, std::wstring,
// lambdas with captures) stay safely inside the client module.
//
// Two conventions, both deliberate:
//
//   * A FAILURE THROWS msgf::Error, which carries the HRESULT.  The flat ABI
//     returns codes because a vtable must; a C++ caller that has to test every
//     one of them writes more error handling than code.  Where absence is an
//     ANSWER rather than a failure -- exists, tryGet -- there is a non-throwing
//     spelling instead.
//
//   * S_FALSE IS NOT AN ERROR anywhere in this ABI, and this layer keeps that:
//     `exists` returns bool, `push` returns bool, `next` returns bool.
//
// Usage sketch (the whole common case, no macros anywhere):
//
//     msgf::Library lib;                                  // one init object
//     msgf::Store   st   = lib.createStore();
//     msgf::Node    root = st.root();
//
//     root.declareText ( L"Title", L"Hello" );            // a child
//     root.declareInt  ( L"Count", 42 );
//     msgf::Node cfg = root.declareText ( L"Lang", L"en", msgf::Attr );
//
//     for ( msgf::Cursor c = root.cursor(); !c.end(); c.next() )
//         wprintf ( L"%s\n", c.name().c_str() );
//
//     st.save ( L"demo.p2p" );
//     // everything torn down by destructors, in the right order

#pragma once

#include "MsgFacade.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace msgf {

// ---------------------------------------------------------------------------
//  Error
// ---------------------------------------------------------------------------
class Error : public std::runtime_error
{
    public:
        explicit Error ( HRESULT hr, const char *what = "MsgFacade call failed" )
          : std::runtime_error ( what ), m_hr ( hr ) { }
        HRESULT code ( ) const { return m_hr; }
    private:
        HRESULT m_hr;
};

inline void check ( HRESULT hr, const char *what = "MsgFacade call failed" )
{
    if ( FAILED ( hr ) )
      throw Error ( hr, what );
}

// Shorthand for the scope argument, so a call site reads as English.
const unsigned int Child = MSGF_SCOPE_CHILD;
const unsigned int Attr  = MSGF_SCOPE_ATTR;

// ---------------------------------------------------------------------------
//  Two helpers for the caller-sized buffer protocol
// ---------------------------------------------------------------------------
template <class T, class Fn>
inline std::wstring GetString ( const T *p, Fn fn )
{
    unsigned int cch = 0;
    check ( fn ( p, (wchar_t*)0, &cch ) );
    if ( cch <= 1 )
      return std::wstring ( );
    std::wstring s ( cch - 1, L'\0' );
    check ( fn ( p, &s[0], &cch ) );
    return s;
}

// ---------------------------------------------------------------------------
//  Handle -- the ownership half of every wrapper below
//
//  NOTES: Move-only.  Every facade object is released exactly once, and a
//         copyable wrapper over an interface with Release-and-no-AddRef could
//         not be
// ---------------------------------------------------------------------------
template <class I>
class Handle
{
    public:
        Handle ( ) : m_p ( 0 ) { }
        explicit Handle ( I *p ) : m_p ( p ) { }
        Handle ( Handle&& rhs ) noexcept : m_p ( rhs.m_p ) { rhs.m_p = 0; }
        Handle& operator = ( Handle&& rhs ) noexcept
        {
          if ( this != &rhs ) { reset ( ); m_p = rhs.m_p; rhs.m_p = 0; }
          return *this;
        }
       ~Handle ( ) { reset ( ); }

      void reset ( ) { if ( m_p ) { m_p->Release ( ); m_p = 0; } }
      I*   get   ( ) const { return m_p; }
      I**  put   ( ) { reset ( ); return &m_p; }
      bool valid ( ) const { return m_p != 0; }

      I* need ( ) const
      {
        if ( !m_p ) throw Error ( MSGF_E_CLOSED, "handle is empty" );
        return m_p;
      }

    private:
        Handle ( const Handle& );
        Handle& operator = ( const Handle& );
        I *m_p;
};

class Node;
class List;
class Vect;
class Cursor;
class Walker;

// ---------------------------------------------------------------------------
//  List
// ---------------------------------------------------------------------------
class List
{
    public:
        List ( ) { }
        explicit List ( IMsgList *p ) : m_h ( p ) { }
        IMsgList** put ( ) { return m_h.put ( ); }
        IMsgList*  get ( ) const { return m_h.get ( ); }
        bool       valid ( ) const { return m_h.valid ( ); }

      unsigned int count ( ) const
      {
        unsigned int n = 0; check ( m_h.need ( )->GetCount ( &n ) ); return n;
      }
      unsigned char typeAt ( unsigned int i ) const
      {
        unsigned char t = MSGF_TYPE_NULL;
        check ( m_h.need ( )->GetTypeAt ( i, &t ) );
        return t;
      }
      long long intAt ( unsigned int i ) const
      {
        long long v = 0; check ( m_h.need ( )->GetIntAt ( i, &v, 0 ) ); return v;
      }
      double realAt ( unsigned int i ) const
      {
        double v = 0; check ( m_h.need ( )->GetRealAt ( i, &v ) ); return v;
      }
      std::wstring textAt ( unsigned int i ) const
      {
        IMsgList *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetTextAt ( i, 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetTextAt ( i, &s[0], &cch ) );
        return s;
      }

      void setIntAt  ( unsigned int i, long long v ) { check ( m_h.need ( )->SetIntAt  ( i, v ) ); }
      void setRealAt ( unsigned int i, double v )    { check ( m_h.need ( )->SetRealAt ( i, v ) ); }
      void setTextAt ( unsigned int i, const std::wstring& v )
                                                     { check ( m_h.need ( )->SetTextAt ( i, v.c_str ( ) ) ); }

      void addInt  ( long long v, unsigned char type = 0, unsigned int flags = MSGF_ADD_TAIL )
                                              { check ( m_h.need ( )->AddInt  ( v, type, flags ) ); }
      void addReal ( double v, unsigned char type = 0, unsigned int flags = MSGF_ADD_TAIL )
                                              { check ( m_h.need ( )->AddReal ( v, type, flags ) ); }
      void addText ( const std::wstring& v, unsigned int flags = MSGF_ADD_TAIL )
                                              { check ( m_h.need ( )->AddText ( v.c_str ( ), flags ) ); }

      void drop     ( unsigned int flags = MSGF_DROP_TAIL ) { check ( m_h.need ( )->Drop ( flags ) ); }
      void deleteAt ( unsigned int i )                      { check ( m_h.need ( )->DeleteAt ( i ) ); }
      void truncate ( )                                     { check ( m_h.need ( )->Truncate ( ) ); }

    private:
        Handle<IMsgList> m_h;
};

// ---------------------------------------------------------------------------
//  Vect
// ---------------------------------------------------------------------------
class Vect
{
    public:
        Vect ( ) { }
        explicit Vect ( IMsgVect *p ) : m_h ( p ) { }
        IMsgVect** put ( ) { return m_h.put ( ); }
        IMsgVect*  get ( ) const { return m_h.get ( ); }
        bool       valid ( ) const { return m_h.valid ( ); }

      unsigned int count ( ) const
      {
        unsigned int n = 0; check ( m_h.need ( )->GetCount ( &n ) ); return n;
      }
      unsigned int kindAt ( unsigned int i ) const
      {
        unsigned int k = 0; check ( m_h.need ( )->GetKindAt ( i, &k ) ); return k;
      }
      unsigned char typeAt ( unsigned int i ) const
      {
        unsigned char t = MSGF_TYPE_NULL;
        check ( m_h.need ( )->GetTypeAt ( i, &t ) );
        return t;
      }
      std::wstring nameAt ( unsigned int i ) const
      {
        IMsgVect *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetNameAt ( i, 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetNameAt ( i, &s[0], &cch ) );
        return s;
      }
      long long intAt ( unsigned int i ) const
      {
        long long v = 0; check ( m_h.need ( )->GetIntAt ( i, &v, 0 ) ); return v;
      }
      double realAt ( unsigned int i ) const
      {
        double v = 0; check ( m_h.need ( )->GetRealAt ( i, &v ) ); return v;
      }
      std::wstring textAt ( unsigned int i ) const
      {
        IMsgVect *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetTextAt ( i, 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetTextAt ( i, &s[0], &cch ) );
        return s;
      }

      void setIntAt  ( unsigned int i, long long v ) { check ( m_h.need ( )->SetIntAt  ( i, v ) ); }
      void setRealAt ( unsigned int i, double v )    { check ( m_h.need ( )->SetRealAt ( i, v ) ); }
      void setTextAt ( unsigned int i, const std::wstring& v )
                                                     { check ( m_h.need ( )->SetTextAt ( i, v.c_str ( ) ) ); }

      inline List listAt ( unsigned int i );
      inline Vect vectAt ( unsigned int i );

      void deleteAt ( unsigned int i ) { check ( m_h.need ( )->DeleteAt ( i ) ); }
      void truncate ( )                { check ( m_h.need ( )->Truncate ( ) ); }

    private:
        Handle<IMsgVect> m_h;
};

inline List Vect::listAt ( unsigned int i )
{
    List o; check ( m_h.need ( )->GetListAt ( i, o.put ( ) ) ); return o;
}
inline Vect Vect::vectAt ( unsigned int i )
{
    Vect o; check ( m_h.need ( )->GetVectAt ( i, o.put ( ) ) ); return o;
}

// ---------------------------------------------------------------------------
//  Cursor
// ---------------------------------------------------------------------------
class Cursor
{
    public:
        Cursor ( ) { }
        explicit Cursor ( IMsgCursor *p ) : m_h ( p ) { }
        Cursor ( Cursor&& rhs ) noexcept : m_h ( std::move ( rhs.m_h ) ) { }
        Cursor& operator = ( Cursor&& rhs ) noexcept
        { m_h = std::move ( rhs.m_h ); return *this; }

        IMsgCursor** put ( ) { return m_h.put ( ); }
        IMsgCursor*  get ( ) const { return m_h.get ( ); }

      void rewind ( ) { check ( m_h.need ( )->Rewind ( ) ); }
      // true when there is a current element after the advance.
      bool next   ( ) { return m_h.need ( )->Next ( ) == S_OK; }
      bool gotoName  ( const std::wstring& n ) { return m_h.need ( )->GotoName  ( n.c_str ( ) ) == S_OK; }
      bool gotoIndex ( unsigned int i )        { return m_h.need ( )->GotoIndex ( i )          == S_OK; }

      bool end ( ) const
      {
        int e = 1; check ( m_h.need ( )->IsEnd ( &e ) ); return e != 0;
      }
      unsigned int count ( ) const
      {
        unsigned int n = 0; check ( m_h.need ( )->GetCount ( &n ) ); return n;
      }
      unsigned int index ( ) const
      {
        unsigned int i = 0; check ( m_h.need ( )->GetIndex ( &i ) ); return i;
      }
      unsigned int kind ( ) const
      {
        unsigned int k = 0; check ( m_h.need ( )->GetKind ( &k ) ); return k;
      }
      std::wstring name ( ) const
      {
        IMsgCursor *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetName ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetName ( &s[0], &cch ) );
        return s;
      }
      inline Node node ( );
      void remove ( ) { check ( m_h.need ( )->Delete ( ) ); }

    private:
        Handle<IMsgCursor> m_h;
};

// ---------------------------------------------------------------------------
//  Walker
// ---------------------------------------------------------------------------
class Walker
{
    public:
        Walker ( ) { }
        explicit Walker ( IMsgWalker *p ) : m_h ( p ) { }
        Walker ( Walker&& rhs ) noexcept : m_h ( std::move ( rhs.m_h ) ) { }
        Walker& operator = ( Walker&& rhs ) noexcept
        { m_h = std::move ( rhs.m_h ); return *this; }

        IMsgWalker** put ( ) { return m_h.put ( ); }

      bool next  ( ) { return m_h.need ( )->Next ( ) == S_OK; }
      // true when the walker descended; false when there was nothing under it.
      bool push  ( ) { return m_h.need ( )->Push ( 0 ) == S_OK; }
      bool pop   ( ) { return SUCCEEDED ( m_h.need ( )->Pop ( 0 ) ); }
      void breakOut ( ) { check ( m_h.need ( )->Break ( ) ); }

      bool end ( ) const
      {
        int e = 1; check ( m_h.need ( )->IsEnd ( &e ) ); return e != 0;
      }
      int depth ( ) const
      {
        int d = 0; check ( m_h.need ( )->GetDepth ( &d ) ); return d;
      }
      unsigned int kind ( ) const
      {
        unsigned int k = 0; check ( m_h.need ( )->GetKind ( &k ) ); return k;
      }
      std::wstring name ( ) const
      {
        IMsgWalker *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetName ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetName ( &s[0], &cch ) );
        return s;
      }

    private:
        Handle<IMsgWalker> m_h;
};

// ---------------------------------------------------------------------------
//  Node
// ---------------------------------------------------------------------------
class Node
{
    public:
        Node ( ) { }
        explicit Node ( IMsgNode *p ) : m_h ( p ) { }
        Node ( Node&& rhs ) noexcept : m_h ( std::move ( rhs.m_h ) ) { }
        Node& operator = ( Node&& rhs ) noexcept
        { m_h = std::move ( rhs.m_h ); return *this; }

        IMsgNode** put ( ) { return m_h.put ( ); }
        IMsgNode*  get ( ) const { return m_h.get ( ); }
        bool       valid ( ) const { return m_h.valid ( ); }

    // What this node is
    public:
      std::wstring name ( ) const
      {
        IMsgNode *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetName ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetName ( &s[0], &cch ) );
        return s;
      }
      std::wstring path ( ) const
      {
        IMsgNode *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetPath ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetPath ( &s[0], &cch ) );
        return s;
      }
      unsigned long long pos ( ) const
      {
        unsigned long long v = 0; check ( m_h.need ( )->GetPos ( &v ) ); return v;
      }
      unsigned int kind ( ) const
      {
        unsigned int k = 0; check ( m_h.need ( )->GetKind ( &k ) ); return k;
      }
      unsigned char type ( ) const
      {
        unsigned char t = 0; check ( m_h.need ( )->GetType ( &t ) ); return t;
      }
      bool isNull ( ) const
      {
        int n = 0; check ( m_h.need ( )->IsNull ( &n ) ); return n != 0;
      }

    // Its value
    public:
      long long asInt ( ) const
      {
        long long v = 0; check ( m_h.need ( )->GetInt ( &v, 0 ) ); return v;
      }
      long long asInt ( bool& rUnsigned ) const
      {
        long long v = 0; int u = 0;
        check ( m_h.need ( )->GetInt ( &v, &u ) );
        rUnsigned = u != 0;
        return v;
      }
      double asReal ( ) const
      {
        double v = 0; check ( m_h.need ( )->GetReal ( &v ) ); return v;
      }
      std::wstring asText ( ) const
      {
        IMsgNode *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetText ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetText ( &s[0], &cch ) );
        return s;
      }
      std::vector<unsigned char> asBlob ( ) const
      {
        IMsgNode *p = m_h.need ( );
        unsigned int cb = 0;
        check ( p->GetBlob ( 0, &cb ) );
        std::vector<unsigned char> v ( cb );
        if ( cb )
          check ( p->GetBlob ( &v[0], &cb ) );
        return v;
      }
      std::wstring asGuid ( ) const
      {
        IMsgNode *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetGuid ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetGuid ( &s[0], &cch ) );
        return s;
      }
      long long time ( ) const
      {
        long long v = 0; check ( m_h.need ( )->GetTime ( &v ) ); return v;
      }

      void set ( long long v )           { check ( m_h.need ( )->SetInt  ( v ) ); }
      void set ( double v )              { check ( m_h.need ( )->SetReal ( v ) ); }
      void set ( const std::wstring& v ) { check ( m_h.need ( )->SetText ( v.c_str ( ) ) ); }
      long long setTime ( long long v = -1 )
      {
        long long out = 0; check ( m_h.need ( )->SetTime ( v, &out ) ); return out;
      }

    // Its children
    public:
      unsigned int count ( unsigned int scope = Child ) const
      {
        unsigned int n = 0; check ( m_h.need ( )->GetCount ( scope, &n ) ); return n;
      }
      // Absence is an ANSWER here, so this does not throw for it.
      bool exists ( const std::wstring& n, unsigned int scope = Child ) const
      {
        return m_h.need ( )->Exists ( scope, n.c_str ( ) ) == S_OK;
      }
      Node child ( const std::wstring& n, unsigned int scope = Child )
      {
        Node o; check ( m_h.need ( )->GetChild ( scope, n.c_str ( ), o.put ( ) ) ); return o;
      }
      Node childAt ( unsigned int i, unsigned int scope = Child )
      {
        Node o; check ( m_h.need ( )->GetChildAt ( scope, i, o.put ( ) ) ); return o;
      }
      // The non-throwing lookup: an empty Node when there is no such child.
      Node tryChild ( const std::wstring& n, unsigned int scope = Child )
      {
        Node o;
        if ( FAILED ( m_h.need ( )->GetChild ( scope, n.c_str ( ), o.put ( ) ) ) )
          o.m_h.reset ( );
        return o;
      }

      Node declareInt ( const std::wstring& n, long long v
                      , unsigned int scope = Child, unsigned char type = 0
                      , unsigned int flags = MSGF_DECLARE_UPDATE )
      {
        Node o;
        check ( m_h.need ( )->DeclareInt ( scope, n.c_str ( ), v, type, flags, o.put ( ) ) );
        return o;
      }
      Node declareReal ( const std::wstring& n, double v
                       , unsigned int scope = Child, unsigned char type = 0
                       , unsigned int flags = MSGF_DECLARE_UPDATE )
      {
        Node o;
        check ( m_h.need ( )->DeclareReal ( scope, n.c_str ( ), v, type, flags, o.put ( ) ) );
        return o;
      }
      Node declareText ( const std::wstring& n, const std::wstring& v
                       , unsigned int scope = Child
                       , unsigned int flags = MSGF_DECLARE_UPDATE )
      {
        Node o;
        check ( m_h.need ( )->DeclareText ( scope, n.c_str ( ), v.c_str ( ), flags, o.put ( ) ) );
        return o;
      }
      Node declareBlob ( const std::wstring& n
                       , const void *pv, unsigned int cb
                       , unsigned int scope = Child
                       , unsigned int flags = MSGF_DECLARE_UPDATE )
      {
        Node o;
        check ( m_h.need ( )->DeclareBlob ( scope, n.c_str ( ), pv, cb, flags, o.put ( ) ) );
        return o;
      }
      Node declareGuid ( const std::wstring& n, const std::wstring& guid
                       , unsigned int scope = Child
                       , unsigned int flags = MSGF_DECLARE_UPDATE )
      {
        Node o;
        check ( m_h.need ( )->DeclareGuid ( scope, n.c_str ( ), guid.c_str ( ), flags, o.put ( ) ) );
        return o;
      }

      List declareList ( const std::wstring& n, unsigned int scope = Child )
      {
        List o; check ( m_h.need ( )->DeclareList ( scope, n.c_str ( ), o.put ( ) ) ); return o;
      }
      Vect declareVect ( const std::wstring& n, unsigned int elems
                       , unsigned char type = MSGF_TYPE_INT32
                       , unsigned int scope = Child )
      {
        Vect o;
        check ( m_h.need ( )->DeclareVect ( scope, n.c_str ( ), elems, type, o.put ( ) ) );
        return o;
      }
      List list ( const std::wstring& n, unsigned int scope = Child )
      {
        List o; check ( m_h.need ( )->GetList ( scope, n.c_str ( ), o.put ( ) ) ); return o;
      }
      Vect vect ( const std::wstring& n, unsigned int scope = Child )
      {
        Vect o; check ( m_h.need ( )->GetVect ( scope, n.c_str ( ), o.put ( ) ) ); return o;
      }

    // Restructuring
    public:
      void remove   ( const std::wstring& n, unsigned int scope = Child )
      { check ( m_h.need ( )->Delete ( scope, n.c_str ( ) ) ); }
      void truncate ( unsigned int scope = Child )
      { check ( m_h.need ( )->Truncate ( scope ) ); }
      void rename   ( const std::wstring& n, const std::wstring& to
                    , unsigned int scope = Child )
      { check ( m_h.need ( )->Rename ( scope, n.c_str ( ), to.c_str ( ) ) ); }
      void move     ( const std::wstring& n, Node& destin, unsigned int scope = Child )
      { check ( m_h.need ( )->Move ( scope, n.c_str ( ), destin.get ( ) ) ); }
      void retype   ( const std::wstring& n, unsigned char type
                    , unsigned int scope = Child )
      { check ( m_h.need ( )->Retype ( scope, n.c_str ( ), type ) ); }

    // Enumeration
    public:
      // The value stack. push/pop/drop answer bool rather than throwing on
      // "there was nothing to restore", because S_FALSE is not an error here.
      void pushValue ( ) { check ( m_h.need ( )->PushValue ( ) ); }
      bool popValue  ( ) { return m_h.need ( )->PopValue  ( ) == S_OK; }
      bool dropValue ( ) { return m_h.need ( )->DropValue ( ) == S_OK; }
      bool stacked   ( ) const
      {
        int s = 0; check ( m_h.need ( )->IsStacked ( &s ) ); return s != 0;
      }

      Cursor cursor ( unsigned int scope = Child )
      {
        Cursor o; check ( m_h.need ( )->OpenCursor ( scope, o.put ( ) ) ); return o;
      }
      Walker walker ( unsigned int scope = Child )
      {
        Walker o; check ( m_h.need ( )->OpenWalker ( scope, o.put ( ) ) ); return o;
      }

      // Every child name in one call -- the common reason to open a cursor.
      std::vector<std::wstring> names ( unsigned int scope = Child )
      {
        std::vector<std::wstring> a;
        Cursor c = cursor ( scope );
        for ( ; !c.end ( ); c.next ( ) )
          a.push_back ( c.name ( ) );
        return a;
      }

    private:
        friend class Cursor;
        Handle<IMsgNode> m_h;
};

inline Node Cursor::node ( )
{
    Node o; check ( m_h.need ( )->GetNode ( o.put ( ) ) ); return o;
}

// ---------------------------------------------------------------------------
//  Events -- a std::function change sink
//
//  NOTES: Read IMsgStoreEvents in the public header before using it.  The
//         callback arrives on the mutating thread with the store's lock held,
//         so the handler must not touch the store.  This layer cannot fix that
//         and does not pretend to; what it fixes is having to write a class
// ---------------------------------------------------------------------------
class Events : public IMsgStoreEvents
{
    public:
        typedef std::function<void(unsigned int, unsigned long long)> Fn;
        explicit Events ( Fn fn ) : m_fn ( fn ) { }
      virtual void OnTrigger ( unsigned int type, unsigned long long pos )
      {
        if ( m_fn ) m_fn ( type, pos );
      }
    private:
        Fn m_fn;
};

// ---------------------------------------------------------------------------
//  Paging -- a std::function demand-paging sink
//
//  NOTES: Read IMsgPagingEvents in the public header first.  This layer cannot
//         soften its contract and does not pretend to: the handler runs on the
//         accessing thread with the store's lock held, and the core is WAITING
//         for what it returns.  What this fixes is having to write a class
//       : An absent handler answers "not handled" rather than "handled", so a
//         half-installed sink fails visibly instead of silently swallowing a
//         page-in
// ---------------------------------------------------------------------------
class Paging : public IMsgPagingEvents
{
    public:
        typedef std::function<bool(unsigned long long)>       InFn;
        typedef std::function<bool(unsigned long long, bool)> OutFn;

        Paging ( InFn in, OutFn out ) : m_in ( in ), m_out ( out ) { }

      virtual int OnPageIn ( unsigned long long pos )
      {
        return ( m_in && m_in ( pos ) ) ? 1 : 0;
      }
      virtual int OnPageOut ( unsigned long long pos, int flush )
      {
        return ( m_out && m_out ( pos, flush != 0 ) ) ? 1 : 0;
      }

    private:
        InFn  m_in;
        OutFn m_out;
};

// ---------------------------------------------------------------------------
//  Store
// ---------------------------------------------------------------------------
class Store
{
    public:
        Store ( ) { }
        explicit Store ( IMsgStore *p ) : m_h ( p ) { }
        Store ( Store&& rhs ) noexcept : m_h ( std::move ( rhs.m_h ) ) { }
        Store& operator = ( Store&& rhs ) noexcept
        { m_h = std::move ( rhs.m_h ); return *this; }

        IMsgStore** put ( ) { return m_h.put ( ); }
        IMsgStore*  get ( ) const { return m_h.get ( ); }
        bool        valid ( ) const { return m_h.valid ( ); }

      void save ( const std::wstring& file = std::wstring ( )
                , unsigned int flags = 0 )
      { check ( m_h.need ( )->Save ( file.empty ( ) ? 0 : file.c_str ( ), flags ) ); }
      void load  ( const std::wstring& file ) { check ( m_h.need ( )->Load ( file.c_str ( ) ) ); }
      void clear ( )                          { check ( m_h.need ( )->Clear ( ) ); }
      void rename( const std::wstring& name ) { check ( m_h.need ( )->Rename ( name.c_str ( ) ) ); }

      std::wstring filename ( ) const
      {
        IMsgStore *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetFilename ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetFilename ( &s[0], &cch ) );
        return s;
      }
      std::wstring rootname ( ) const
      {
        IMsgStore *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->GetRootname ( 0, &cch ) );
        if ( cch <= 1 ) return std::wstring ( );
        std::wstring s ( cch - 1, L'\0' );
        check ( p->GetRootname ( &s[0], &cch ) );
        return s;
      }
      bool dirty ( ) const
      {
        int d = 0; check ( m_h.need ( )->IsDirty ( &d ) ); return d != 0;
      }
      void setDirty ( bool d ) { check ( m_h.need ( )->SetDirty ( d ? 1 : 0 ) ); }
      unsigned int size ( ) const
      {
        unsigned int n = 0; check ( m_h.need ( )->GetSize ( &n ) ); return n;
      }
      bool ok ( ) const
      {
        int v = 0; check ( m_h.need ( )->IsValid ( &v ) ); return v != 0;
      }

      Node root ( )
      {
        Node o; check ( m_h.need ( )->GetRoot ( o.put ( ) ) ); return o;
      }
      Node at ( const std::wstring& path )
      {
        Node o; check ( m_h.need ( )->NodeFromPath ( path.c_str ( ), o.put ( ) ) ); return o;
      }
      Node at ( unsigned long long pos )
      {
        Node o; check ( m_h.need ( )->NodeFromPos ( pos, o.put ( ) ) ); return o;
      }

      void events ( IMsgStoreEvents *sink ) { check ( m_h.need ( )->SetEvents ( sink ) ); }
      void arm    ( unsigned long long pos, unsigned int mask = MSGF_TRIG_ALL )
      { check ( m_h.need ( )->Arm ( mask, pos ) ); }
      void disarm ( unsigned long long pos, unsigned int mask = MSGF_TRIG_ALL )
      { check ( m_h.need ( )->Disarm ( mask, pos ) ); }
      unsigned int fire ( unsigned long long pos, unsigned int mask )
      {
        unsigned int n = 0; check ( m_h.need ( )->Fire ( mask, pos, &n ) ); return n;
      }

      // Demand paging. pageIn/pageOut answer bool: false is "nothing was
      // paged", which is what a store with no sink installed reports.
      void paging  ( IMsgPagingEvents *sink ) { check ( m_h.need ( )->SetPaging ( sink ) ); }
      bool pageIn  ( unsigned long long pos ) { return m_h.need ( )->PageIn ( pos ) == S_OK; }
      bool pageOut ( unsigned long long pos, bool flush = false )
      { return m_h.need ( )->PageOut ( pos, flush ? 1 : 0 ) == S_OK; }
      void pushPaging ( ) { check ( m_h.need ( )->PushPaging ( ) ); }
      void popPaging  ( ) { check ( m_h.need ( )->PopPaging  ( ) ); }

    private:
        Handle<IMsgStore> m_h;
};

// ---------------------------------------------------------------------------
//  Library -- one per client, constructed once
// ---------------------------------------------------------------------------
class Library
{
    public:
        Library ( )
        {
          IMsgLibrary *p = 0;
          check ( MSGF_CreateLibrary ( ABI_VERSION, &p ), "MSGF_CreateLibrary" );
          m_h = Handle<IMsgLibrary> ( p );
        }

      Store createStore ( unsigned char addr = 0
                        , unsigned int initialBytes = 0
                        , unsigned int maxBytes = 0 )
      {
        Store o;
        check ( m_h.need ( )->CreateStore ( addr, initialBytes, maxBytes, o.put ( ) ) );
        return o;
      }
      Store openStore ( const std::wstring& file )
      {
        Store o; check ( m_h.need ( )->OpenStore ( file.c_str ( ), o.put ( ) ) ); return o;
      }

      bool match ( const std::wstring& pattern, const std::wstring& name ) const
      {
        return m_h.need ( )->WildcardMatch ( pattern.c_str ( ), name.c_str ( ) ) == S_OK;
      }
      std::wstring typeName ( unsigned char type ) const
      {
        IMsgLibrary *p = m_h.need ( );
        unsigned int cch = 0;
        check ( p->TypeName ( type, 0, &cch ) );
        std::wstring s ( cch ? cch - 1 : 0, L'\0' );
        if ( cch > 1 )
          check ( p->TypeName ( type, &s[0], &cch ) );
        return s;
      }
      unsigned char typeFromName ( const std::wstring& name ) const
      {
        unsigned char t = 0;
        check ( m_h.need ( )->TypeFromName ( name.c_str ( ), &t ) );
        return t;
      }
      bool validName ( const std::wstring& name ) const
      {
        return m_h.need ( )->IsValidName ( name.c_str ( ) ) == S_OK;
      }
      std::wstring version ( ) const { return m_h.need ( )->VersionString ( ); }

    private:
        Handle<IMsgLibrary> m_h;
};

} // namespace msgf
