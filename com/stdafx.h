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
// stdafx.h : precompiled header for MsgcoreCom (ATL in-proc COM server)
//
// This DLL is pure ATL -- NO MFC, no TargetCore, no VBHeap, no P2Pmsg.h.  It
// sees the store only through MsgFacade's public header, which is the point:
// Msgcore is an MFC extension DLL, and if this server compiles and links
// without a line of MFC then that header really is a complete boundary rather
// than a partial one.
//
// (The process still loads MFC at run time, because Msgcore.dll needs it. That
// is a deployment fact about the dependency, not a compile-time coupling of
// this server -- nothing here can name an MFC type, so nothing here can drift
// into depending on one.)
//
// It used to include Msgcore_c.h, the kernel's flat C ABI, and get the same
// property that way. MsgFacade replaced it because the two things this server
// spent the most code on -- re-resolving a node reference after a heap
// relocation, and telling a live handle from a detached copy -- are what that
// facade exists to have already done. See the note at the top of ComUtil.h.
#pragma once

#include "Targetver.h"

#define WIN32_LEAN_AND_MEAN
#define STRICT
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
// Objects here are explicitly CComMultiThreadModel and register
// ThreadingModel=Both, so no _ATL_*_THREADED define is needed.

#include <windows.h>
#include <objbase.h>
#include <process.h>

#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

// MsgFacade's public header -- the ONLY view MsgcoreCom has of the store.
// Vtables, HRESULTs and constants; no MFC, no CString, no Msgexception.
#include "MsgFacade.h"
