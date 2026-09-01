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
// stdafx.h : standard precompiled header for MsgFacade (regular MFC DLL)
//
// Msgcore is MFC-based -- its headers use CString, CList, ASSERT and the
// AFX allocation hooks, and its classes are exported with the MFC_EXT_CLASS
// pattern -- so the facade DLL uses MFC dynamically.  The PUBLIC header
// (include\MsgFacade.h) stays MFC-free: everything here is internal to the DLL
// and nothing in it appears in a signature a client can see.
//
// The include ORDER below is not arbitrary.  Msgcore's own headers assume a
// stdafx has already pulled in the Win32 and MFC surface (Msgcore.h types
// itself in terms of LPCTSTR / DWORD_PTR on line one), so afx must come first
// or every Msgcore header fails on its first typedef.  This mirrors
// Msgcore\stdafx.h rather than inventing a second order.
#pragma once

#include "Targetver.h"

#pragma warning(disable:4251)   // exported class with a non-exported member
#pragma warning(disable:26496)  // const expressions (Msgcore headers)

#define WIN32_LEAN_AND_MEAN

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include <afx.h>
#include <afxwin.h>
#include <afxext.h>
#include <afxmt.h>
#include <afxtempl.h>
#include <atlstr.h>
#include <comutil.h>

#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include <string>
#include <vector>

// Msgcore -- the wrapped kernel (internal use only; never re-exported).
//
// These are the C++ classes, deliberately: the facade sits on the object model
// itself, NOT on Msgcore_c.h's flat handle ABI.  Going through that layer would
// buy nothing (it is a thinner wrapper of the same calls) and would cost the
// two things this facade is built on -- the ability to hold a live P3PmsgField
// on the stack for the duration of one operation, and access to the parts of
// the object model the flat surface never exposed.
#include "P2Pmsg.h"
#include "P2PmsgMgr.h"
#include "MsgAttr.h"
#include "MsgDesc.h"
#include "MsgCurs.h"
#include "MsgList.h"
#include "MsgVect.h"
#include "MsgStck.h"
#include "Msgexception.h"
