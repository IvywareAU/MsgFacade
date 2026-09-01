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
// FacadeCursor.h -- FacadeCursor and FacadeWalker, the objects behind
// msgf::IMsgCursor and msgf::IMsgWalker.
//
// These are the two objects in this DLL that hold a real Msgcore POSITION
// between calls rather than a route, and they are therefore the only two the
// heap's relocation rule still governs.  That is not an oversight: an iterator
// that re-walked a route per step would be O(n*depth) and could only visit
// what the caller had already named, which is not iteration.
//
// The rule is in the public header, at IMsgCursor.  Here is what it costs:
// FacadeCursor keeps the resolved owner field alive for its whole life,
// because a P3PmsgCurs over the ATTRIBUTE scope holds a raw pointer into that
// field's cached attribute collection.
#pragma once

#include "FacadeInternal.h"

class FacadeStore;

class FacadeCursor : public msgf::IMsgCursor
{
    public:
        // `rOwner` is the route of the node whose `uScope` is being walked.
        static HRESULT
          Make ( FacadeStore *pStore, const FacadeRoute& rOwner
               , unsigned int uScope, msgf::IMsgCursor **outCursor );

    // msgf::IMsgCursor
    public:
      virtual HRESULT Rewind    ( );
      virtual HRESULT Next      ( );
      virtual HRESULT GotoName  ( const wchar_t *name );
      virtual HRESULT GotoIndex ( unsigned int index );
      virtual HRESULT IsEnd     ( int *outEnd ) const;
      virtual HRESULT GetCount  ( unsigned int *outCount ) const;
      virtual HRESULT GetIndex  ( unsigned int *outIndex ) const;
      virtual HRESULT GetKind   ( unsigned int *outKind ) const;
      virtual HRESULT GetName   ( wchar_t *buf, unsigned int *cch ) const;
      virtual HRESULT GetNode   ( msgf::IMsgNode **outNode );
      virtual HRESULT Delete    ( );
      virtual ULONG   Release   ( );

    private:
        FacadeCursor ( FacadeStore *pStore, const FacadeRoute& rOwner
                     , unsigned int uScope );
       ~FacadeCursor ( );

    private:
        FacadeStore *m_pStore{nullptr};
        FacadeRoute  m_oOwner;
        unsigned int m_uScope{msgf::MSGF_SCOPE_CHILD};
        // The owner, resolved once and held -- see the note at the top.
        P3PmsgField *m_pField{nullptr};
        P3PmsgCurs  *m_pCurs{nullptr};
};

// ---------------------------------------------------------------------------
//  FacadeWalker -- a subtree walk as an explicit stack of cursors
//
//  NOTES: NOT P2PmsgRecurs, which is the core's own walker.  Two of its
//         properties cannot be expressed through this ABI: its level counter
//         is protected AND its advance pops levels silently, so no honest
//         GetDepth is possible on top of it; and its Push RAISES on an element
//         it cannot descend into rather than reporting it
//       : Each level's cursor is built from the level above's current item,
//         and P3PmsgCurs(P3PmsgItem&) takes its own copy of the parent it is
//         handed -- so no level's storage depends on another's, and popping is
//         just a delete
//       : Only the CHILD scope is descended into.  A walk asked for over the
//         attribute scope walks the attributes of the node it started at and
//         descends into their children, which is what "the subtree under this
//         scope" means; there is no second attribute level to invent
// ---------------------------------------------------------------------------
class FacadeWalker : public msgf::IMsgWalker
{
    public:
        static HRESULT
          Make ( FacadeStore *pStore, const FacadeRoute& rOwner
               , unsigned int uScope, msgf::IMsgWalker **outWalker );

    // msgf::IMsgWalker
    public:
      virtual HRESULT Next      ( );
      virtual HRESULT Push      ( int *outDepth );
      virtual HRESULT Pop       ( int *outDepth );
      virtual HRESULT Break     ( );
      virtual HRESULT IsEnd     ( int *outEnd ) const;
      virtual HRESULT GetDepth  ( int *outDepth ) const;
      virtual HRESULT GetKind   ( unsigned int *outKind ) const;
      virtual HRESULT GetName   ( wchar_t *buf, unsigned int *cch ) const;
      virtual ULONG   Release   ( );

    private:
        FacadeWalker ( FacadeStore *pStore, const FacadeRoute& rOwner
                     , unsigned int uScope );
       ~FacadeWalker ( );

        //
        //  One open level of the walk
        //  NOTES: `bFresh` is what makes Push and Next compose.  A cursor is
        //         born positioned ON its first element, so a walker that
        //         descended and then advanced would step straight OVER the
        //         first child of every branch it entered -- the classic
        //         off-by-one of a hand-rolled tree walk, and the reason the
        //         core's own P2PmsgRecurs::Push backs its new cursor up by one
        //         before returning.  A flag says the same thing without
        //         needing a "before the beginning" cursor state, which
        //         P3PmsgCurs represents as "no current element" and could not
        //         be told apart from the end
        //
        struct Level
        {
            P3PmsgCurs *pCurs{nullptr};
            bool        bFresh{false};   // not yet stepped onto by Next
        };

        // The innermost open level, or NULL when the walk is over.
        P3PmsgCurs* Top ( ) const;

    private:
        FacadeStore *m_pStore{nullptr};
        FacadeRoute  m_oOwner;
        unsigned int m_uScope{msgf::MSGF_SCOPE_CHILD};
        P3PmsgField *m_pField{nullptr};      // the root of the walk, held alive
        // One entry per open level, outermost first.  Cursors are owned.
        std::vector<Level> m_aLevels;
};
