# `MsgcoreCom` — the COM face of the Msgcore data model

An **ATL in-proc COM server over MsgFacade**. It is to the data model what
[`TargetFacade\com`](../../TargetFacade/com) is to the messaging kernel: the tier
at which a VBScript file, an Excel macro, a PowerShell script or a .NET host can
use the library.

```
MsgcoreCom.MsgStore          the one creatable object -- a store IS a document
  .Root / .FieldAt           -> IMsgFieldCom     a node: name, value, children
      .Attributes            -> IMsgAttrCom      the '@' scope
      .Descendants           -> IMsgDescCom      the child scope
      .Cursor                -> IMsgCursorCom    a position within a collection
      .List / .Vector        -> IMsgListCom / IMsgVectCom
      .Walker                -> IMsgRecursCom    a subtree from one flat loop
  _IMsgStoreEvents           OnChange / OnError  (connection point)
  .SetPagingSink             -> IMsgPagingSink   (registered, synchronous)
```

```vbscript
Set store = CreateObject("MsgcoreCom.MsgStore")
store.Open "C:\data\settings.p2p"
WScript.Echo store.FieldAt(".window.width")     ' DISPID_VALUE: prints 1024
For Each child In store.Root.Child("window")
    WScript.Echo child.Name & " = " & child
Next
```

**It contains no MFC.** It sees the store only through `MsgFacade.h`, and the
fact that it compiles and links without naming a single MFC type is what makes
that header a demonstrated boundary rather than an asserted one. (The process
still loads MFC at run time, because `Msgcore.dll` needs it — a deployment fact
about the dependency, not a compile-time coupling of this server.)

## Build and register

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "..\MsgFacade(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64   # first
& $msbuild ".\MsgcoreCom(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
regsvr32 /n /i:user ".\out\x64\Debug\MsgcoreCom.dll"    # per-user, HKCU only, no elevation
```

The post-build step stages **both** `MsgFacade.dll` and `Msgcore.dll` beside the
server and **fails the build if either is missing** — otherwise a missing DLL
only shows up as `0xC06D007E` when `regsvr32` tries to load the server.

## Files

| | |
| --- | --- |
| `MsgcoreCom.idl` | the contract, and the long-form reasoning for every departure from a 1:1 transliteration |
| `ComUtil.h/.cpp` | VARIANT ↔ value, the `IErrorInfo` sentences, the facade-code → `MsgcoreError` map, the exception boundary |
| `ComStore.cpp` | `MsgStore`: the store, the lock, the mutation counter, the event queue and its dispatch thread, paging |
| `ComField.cpp` | `MsgField`: one `msgf::IMsgNode`, and the value stack |
| `ComColl.cpp` | `MsgAttr`, `MsgDesc` (one body, one scope constant apart), `MsgList`, `MsgVect` |
| `ComCursor.cpp` | `MsgCursor` and the snapshot enumerators |
| `ComWalk.cpp` | `MsgRecurs` — the subtree walker |

## What the move onto MsgFacade changed

This server used to sit on `Msgcore_c.h`, the kernel's flat C ABI, and
documented **five departures** from a blind transliteration. **Two of them are
gone** — not solved differently, gone, because the facade solves them one layer
down for every client rather than only for this one:

1. **A node reference is a PATH.** A raw kernel handle is invalidated by the
   next heap relocation, so this server kept every node as a `std::vector<wstring>`
   and re-resolved it on entry to every method — about 90 lines, including a
   check for the *hollow* handle `msgcore_field_child` answers for a name it
   cannot resolve. `msgf::IMsgNode` is that, so `IMsgFieldCom` now holds one.
2. **Live vs detached is gone.** Two flat families returned the same handle
   type: one aliased the live tree, the other deep-copied, and a write through
   the copy silently reached nothing. `Item`, `IsLive` and `msgcDetached` have
   been **removed** rather than kept as ceremony; `0x8004030B` is left reserved
   so a client that still tests for it compiles and never sees it.

The other three are still this server's work and still documented in the `.idl`:
one `Value` property rather than sixteen accessors, **queued** change events
with a dispatch thread and GIT cookies, and one lock per store (the facade makes
each *call* atomic; a *sequence* is still this tier's problem).

### What a client gains

| | |
| --- | --- |
| `Child` reaches a list or a vect | `SelectItem` threw on a container, so `Child("Samples")` used to fail for a list that was plainly there |
| `FieldAt(p2pos)` is writable | it answered a detached copy; a position that has been deleted is now `msgcNoPos` rather than a debug break |
| `PathOf` round-trips | it answered the kernel's own path spelling, which `FieldAt` could not take back |
| `Path` can name an attribute | `.Config.Window@Colour`; the old dotted chain had no spelling for one |
| `RenameRoot` | the only rename here was `RenameFile`, a `MoveFileEx` |
| `Clear` leaves a usable store | `Nullify` had to be implemented by rebuilding, around a core call that closes the heap and leaves the manager pointing at nothing |
| `DeclareTyped` reaches FLOAT and the unsigned widths | the flat ABI had no accessor for `UINT08`/`UINT16` at all |
| attribute declares keep their type | a Boolean declared into the `@` scope used to land as `INT32`, and 64-bit and blob values could not be declared there |
| `IsValidName`, `ChildAt`, walker `Depth` / `Path` | new |

### What it costs

| | |
| --- | --- |
| `IMsgStackCom` and the `MsgStack` coclass | gone; a node's saved value is `PushValue` / `PopValue` / `DropValue` / `IsStacked` on `IMsgFieldCom` |
| `IMsgVectCom.Field` and `.InsertAt` | gone; a vect's elements are values, nested lists or nested vects, and a vect is sized at declare |
| `IMsgStoreCom.PageSumm`, `IMsgPagingSink.OnPopulate` | gone; neither is exposed below this tier |
| `RenameFile` | gone; a host has its own file API |

## Three defects found by writing it

All three were **fixed** rather than routed around, and each was found by making
a call nothing had made before:

* **`MsgStck::Push` cached a physical heap pointer across its own allocation**
  (`MsgStck.cpp`). It read `pVBLock` before `MsgStck__AllocItem`, and an
  allocation that grows the heap reallocates the base image and moves every
  block in it — so the write that followed landed in freed memory. Reliable
  once a store was full enough for the push to trigger a growth, which is why
  it had survived every previous test: they all pushed into a nearly-empty
  store. **Fixed in Msgcore** by re-deriving the pointer after the allocation.
* **`P2PmsgMgr::PageRegistrationPop` restored the registration and then
  returned FALSE.** **Fixed in Msgcore**; see the facade's README.
* **`IMsgStore::Clear` closed the heap and left the store unusable.** **Fixed in
  MsgFacade** by replacing the manager rather than nullifying it.

And one this server did to itself: **the change sink used to resolve the
changed node's PATH inside the callback**, on the mutating thread, with the
store mid-allocation. Against the flat ABI that was a cheap direct lookup and it
got away with it; `NodeFromPos` is a depth-first search, and running one while
the heap is being relocated access-violates. The position is queued bare now and
the path is resolved on the dispatch thread — which costs accuracy only for a
node that disappears between the two moments, and a `DELETE` never had a path
anyway.

## Verified by

| | |
| --- | --- |
| a late-bound PowerShell client | **35/35**, Debug and Release: declare/read, paths both ways, a writable position, attributes, a list, a vect, the value stack, `RenameRoot`, a cursor, `For Each`, the walker, save/reload, `Clear` |
| a vtable C++ client | the same sequence through `IMsgStoreCom` directly |

The two example trees that consume this server —
[`ComExamples`](../../_Msgcore_UseExamples/ComExamples) (C++) and
[`dotNetExamples`](../../_Msgcore_UseExamples/dotNetExamples) (C#), both now
inside the combined `_Msgcore_UseExamples` repository — were written against the
OLD contract and have not yet been updated for it.
