# MsgFacade

A minimal, macro-free facade DLL over `Msgcore.dll`. Clients include **one
header** (`include/MsgFacade.h`), link **one import lib**, and never see
`P2PmsgMgr`, `P3PmsgField`, `P3PmsgData`, `P3PmsgCurs`, `VBLock`, MFC,
`CString` or `Msgexception`.

```
  C++ client ─────────> MsgFacade.dll ──────> Msgcore.dll
                          (flat vtable ABI,     (MFC classes, a packed
                           HRESULT, no macros)   offset-addressed heap)
```

It is the sibling of `..\TargetFacade`, which does the same job for
`TargetCore` (the messaging kernel). This one covers the **data model** —
the structured message store underneath it.

It sits on Msgcore's **C++ object model**, not on `Msgcore_c.h`. That flat
handle ABI is a thinner wrapper of the same calls and would cost the two
things this facade is built on: holding a live `P3PmsgField` on the stack for
the duration of one operation, and reaching the parts of the object model the
flat surface never exposed.

## Status: complete and tested

Everything in the public header is implemented.

| build | result |
|---|---|
| `MsgFacade.dll` Debug\|x64, Release\|x64, Debug\|Win32, Release\|Win32 | all four build clean |
| `test\FacadeSmokeTest` Debug\|x64 | **465 checks, 0 failed** |
| `test\FacadeSmokeTest` Release\|x64 | **465 checks, 0 failed** |

```
cd out\x64\Debug && MsgFacadeSmokeTest.exe     # exit 0 = pass
```

`test/FacadeSmokeTest` is a console client that includes **only** the facade's
public headers — no MFC, no `afx*`, no Msgcore, no `UseOfMfc` in its project
file — so it fails to compile if the facade ever starts leaking its internals.

The facade→Msgcore mapping is documented in `src/FacadeInternal.h`.

## The API in 20 lines

```cpp
#include "MsgFacadeFn.hpp"      // or the raw MsgFacade.h if you prefer

msgf::Library lib;                               // one init object
msgf::Store   st   = lib.createStore();          // P2PmsgMgr inside
msgf::Node    root = st.root();

root.declareText ( L"Title", L"Hello" );         // a descendant
root.declareInt  ( L"Count", 42 );
root.declareText ( L"Lang",  L"en", msgf::Attr );// an attribute -- same verb

msgf::List items = root.declareList ( L"Items" );
items.addText ( L"one" );
items.addText ( L"two" );

for ( msgf::Cursor c = root.cursor(); !c.end(); c.next() )
    wprintf ( L"%s\n", c.name().c_str() );

st.save ( L"demo.p2p" );
// everything torn down by destructors, in the right order
```

## The three things this facade is for

Msgcore is a fast, packed, offset-addressed object store. Its speed comes from
properties that are hostile to a caller who is not the code that wrote it, and
each of the three is **fixed here** rather than documented at the caller.

### 1. A node is a path, not a pointer

Msgcore addresses blocks by offset and grows by reallocating its base image, so
*any* call that can allocate — a declare, a rename, a retype, a data write, a
load — may move every block in the store. A raw handle obtained before such a
call is a dangling pointer after it, with no way to tell. That is why the
kernel's own flat ABI tells callers that **no handle survives a mutation** and
to treat "obtain, use, discard" as the unit of work.

An `IMsgNode` holds the *route* to a node — the ordered `(scope, name)` steps
from the store root — and re-walks that route on entry to every method. So:

```cpp
IMsgNode *pKeep = 0;
pRoot->GetChild ( MSGF_SCOPE_CHILD, L"Keep", &pKeep );

for ( int i = 0; i < 400; ++i )                  // 400 heap growths
    pRoot->DeclareText ( MSGF_SCOPE_CHILD, name(i), L"padding...", 0, 0 );

pKeep->GetText ( buf, &cch );                    // still works. S_OK.
pStore->Load ( L"other.p2p" );                   // the whole heap is replaced
pKeep->GetText ( buf, &cch );                    // still works, against the NEW tree
```

Both of those are checks in the smoke test (sections 7 and 14). Delete the node
and the handle answers `MSGF_E_NO_ITEM` — a real answer, rather than the
kernel's "resolves to whatever now occupies that block".

Cost: O(depth) name lookups per call. Depth is the nesting of the store, not
its size.

### 2. One pair of verbs for both collections

Every Msgcore node carries **two** child collections — its descendants (the
tree proper) and its attributes (keyed by `@`) — and the kernel exposes them
through two parallel families of near-identical calls, plus a third on the
field itself. They differ in one thing: which collection is reached. So here
that is an **argument**, not a family:

```cpp
node->DeclareText ( MSGF_SCOPE_CHILD, L"Title", L"Hello", 0, 0 );
node->DeclareText ( MSGF_SCOPE_ATTR,  L"Lang",  L"en",    0, 0 );
```

A third collection would then be a case label rather than twenty more vtable
slots, twenty more IDL dispids and twenty more wrappers per language binding.

### 3. Reading a value never throws and never lies

Msgcore's scalar accessors are **strict**: `c_int()` on anything that is not
`INT32`, or `c_double()` on a `FLOAT`, raises rather than converts. A caller
that does not already know a node's exact subtype cannot safely read it.

`GetInt` reads the whole integer family (`INT08`..`UINT64` and `BOOL`) at any
width and reports whether the subtype was unsigned; `GetReal` reads `FLOAT` and
`DOUBLE`. Asking for a value the node does not hold is `MSGF_E_TYPE` — an
answer, not an exception.

Writes are the same story from the other side. The accessors are not just
strict, they are **incomplete**: `P3PmsgData` declares
`c_char`/`c_short`/`c_int`/`c_uint`/`c_int64`/`c_uint64` and no unsigned narrow
pair, so a `UINT08` or `UINT16` node has no accessor at all. `SetInt` goes
through `Recreate`, which writes the union member for every width — and refuses
a value the declared width could not hold (`MSGF_E_LIMIT`) rather than letting
the cast truncate it silently.

## The object model

| interface | is | lifetime rule |
|---|---|---|
| `IMsgLibrary` | the process-wide entry point, from `MSGF_CreateLibrary` | refcounted singleton |
| `IMsgStore` | one store: a tree, a heap, optionally a file | `Release` closes it; it is freed when the last object rooted in it goes |
| `IMsgNode` | a **position** in a store's tree | survives every mutation |
| `IMsgList` | a linked sequence of values under one name | survives every mutation |
| `IMsgVect` | an indexed vector of elements under one name | survives every mutation |
| `IMsgCursor` | one collection, one position | **invalidated by a mutation** |
| `IMsgWalker` | a whole subtree from one flat loop | **invalidated by a mutation** |
| `IMsgStoreEvents` | a client-implemented change sink | cleared with `SetEvents(NULL)` before it dies |
| `IMsgPagingEvents` | a client-implemented demand-paging sink (ABI 2) | cleared with `SetPaging(NULL)` before it dies |

Cursors and walkers are the two objects that hold a real kernel position rather
than a route, because holding a position is the whole point of them. They
therefore inherit the kernel's rule, and it is stated once: **do not mutate the
tree while one is open.** `IMsgCursor::GetNode` is the bridge back — collect
path-based nodes on the walk, release the cursor, then act on them.

## Threading

Msgcore's store has **no internal synchronisation of any kind**: one writer, or
external locking around every call, and concurrent *readers* are unsafe too
because a read can trigger the relocation above.

This facade puts one critical section around each store and takes it in every
method of every object belonging to that store. So a call is safe from any
thread, and two threads calling on one store are serialised rather than racing.
Two stores never contend: they share no state.

That makes each **call** atomic; it does not make a **sequence** atomic. A walk
interleaved with another thread's mutation is still a logic error.

## Paths

`IMsgNode::GetPath` answers the facade's own spelling: a `.` before each
descendant step and an `@` before each attribute step, from the root.

```
""                          the root
".Config"                   a child of the root
".Config.Window"            a grandchild
".Config.Window@Colour"     an attribute of that grandchild
```

It round-trips exactly through `IMsgStore::NodeFromPath`, and it is unambiguous
because the core's own name validator refuses both characters inside a name.
This is deliberately *not* Msgcore's internal path spelling, which uses `:` for
a root, `.` for a descendant and `@` for an attribute in a form built by
walking parent links — not reversible into scopes, and so not usable as a
locator.

`IMsgStore::NodeFromPos` is the one entry point that takes a **position** (the
store's natural inode number, and what triggers report). It searches the tree
for it once and hands back a path node, so everything downstream of it holds a
path.

## Errors

Every method returns an `HRESULT`. Facade codes live in `FACILITY_ITF` at
`0x0300+` and are only ever appended:

| code | means |
|---|---|
| `MSGF_E_ABI_MISMATCH` | header/DLL `ABI_VERSION` differ |
| `MSGF_E_CORE` | Msgcore raised or refused |
| `MSGF_E_NO_ITEM` | no item of that name in that scope |
| `MSGF_E_NAME` | name empty, over-long, or holding `. @ : ^ / \ * ? | < > "` |
| `MSGF_E_TYPE` | the node is not of the kind/type asked for |
| `MSGF_E_RANGE` | index outside the collection |
| `MSGF_E_FILE` | Load/Save failed |
| `MSGF_E_CLOSED` | the store this object belongs to is closed |
| `MSGF_E_SCOPE` | scope is neither `MSGF_SCOPE_CHILD` nor `_ATTR` |
| `MSGF_E_PATH` | path string does not parse |
| `MSGF_E_EXISTS` | declare onto an existing name without `MSGF_DECLARE_UPDATE` |
| `MSGF_E_NO_POS` | position does not resolve in this store |
| `MSGF_E_LIMIT` | value past what the storage type can hold |
| `MSGF_E_DEPTH` | node path deeper than `MAX_DEPTH` |
| `MSGF_E_STATE` | the object is not in a state that call is legal in |

`S_FALSE` is never an error: `Exists`, `WildcardMatch`, `IsValidName`,
`Cursor::Next`, `Walker::Push` and `Rename`-to-the-same-name all use it.

## `MsgFacadeFn.hpp` — the optional sugar

Header-only, client-side only, nothing crosses the DLL boundary: RAII handles,
`std::wstring` / `std::vector` returns, a `std::function` change sink, and
`msgf::Error` carrying the `HRESULT` for anything that fails. Where absence is
an *answer* rather than a failure (`exists`, `tryChild`) there is a
non-throwing spelling instead.

## Things measured about Msgcore while building this

Written down because each one cost time, and each is load-bearing somewhere in
`src/`:

* **`P3PmsgField::SelectItem` raises on a child that is a list or a vect** —
  "a list is not an item". Any resolver built on it can neither reach nor walk
  through a container. `P3PmsgCurs::Goto` handles all three kinds and reports
  absence with `false`. Every lookup in this facade is cursor-based.
* **`P3PmsgCurs::Goto` matches case-insensitively.** The facade records the
  *stored* spelling in a node's route, not the caller's, so `GetPath` renders
  something `NodeFromPath` could have produced.
* **`P3PmsgCurs::IsEoCursor` answers TRUE while positioned ON the last
  element** (`m_nItem >= nItems-1`), and `operator++` *raises* rather than
  saturating when advanced from the end. A loop driven by the core's own
  predicate visits every element but the last, then throws. The facade tests
  `Item()`/`GetCount()` itself.
* **A list cell cannot grow.** `IMsgNode::SetText` lengthens a node's value
  freely; `IMsgList::SetTextAt` refuses a longer value with `MSGF_E_LIMIT`,
  because a list cell lives inside the list's own block and the core's grow
  path asserts and aborts there. The capacity accessor that would let this be
  exact — `P3PmsgData::c_size_max` — is **declared and never defined**, and
  `VBLockData_BlobSize`/`_BlobMax` are not exported, so used-size is the bound.
* **`P2PmsgMgr::IsValid()` answers TRUE for a manager built from a file that
  does not exist** — it is asking about the heap, which is a perfectly good
  empty one. `P2PmsgMgr_IsValid(filename)` is the overload that asks the other
  question, and `OpenStore` calls it first.
* **`P2PmsgMgr::FireTrigger` takes the position first and the mask second**,
  the opposite way round from `CreateTrigger`/`DropTriggers` beside it.
* **`P3PmsgList::Connect(handle, addr, size)` is not the low-level version of
  assigning a `P3PmsgObject`.** It forwards to `P3PmsgObject::Connectx`, which
  re-derives the block layout from the size it is given; handing it a size
  taken from an already-connected object seats the wrapper on a header it then
  disagrees with, and the core asserts its way out. Assignment is the aliasing
  path.
* **`P3PmsgData::Recreate` does not clear `VBLockAttr_NULL`** (only the `c_*`
  accessors do), so a value written through it reads back as still-null unless
  `SetAttr(0, VBLockAttr_NULL)` follows.
* **`DeclareItem` with `bUpdate` assigns the whole `P3PmsgData`**, and
  `P3PmsgData::operator=` copies the type word with the bytes. So a
  declare-with-update *is* a retype that keeps the node's name, position and
  children — which is how `Retype` is implemented, in both scopes, without
  `P3PmsgRefactor_DataType` (which has no attribute-scope overload).
* **`P3PmsgField::Truncate` drops the descendants AND the attributes AND the
  position stack.** It is not a scoped operation; `IMsgNode::Truncate(scope)`
  goes through the collection's own `Truncate`.
* **MFC answers an un-answerable `ASSERT` with `AfxAbort()`**, which exits 3
  without flushing stdout. The smoke test runs unbuffered so the last line
  printed is the one that says where it died.
* **`P2PmsgMgr::PageRegistrationPop` restored the registration and then
  `return FALSE`** — reporting failure after doing the work. Its sibling `Push`
  returns TRUE, and `msgcore_mgr_page_registration_pop` documents "returns 1 on
  success", so both contracts over it disagreed with it. Nobody had noticed
  because the only caller in the tree was `SafeRegistrationPush`'s destructor,
  which cannot read a return value. **Fixed in Msgcore** (2026-08-15) rather
  than routed around, because the restore itself was always correct.
* **That same `Pop` asserts unless all three saved slots are non-null**, so a
  push made with no registration installed is an abort deferred to the matching
  pop. `IMsgStore::PushPaging` refuses that state with `MSGF_E_STATE` instead.

## ABI 2 — demand paging and the value stack

ABI 1 covered the store, nodes, containers, cursors, walkers and triggers. ABI 2
appends the two facilities the kernel has and ABI 1 did not reach. Both are
**appends** — new methods at the end of an interface, and one new
client-implemented interface — so ABI 1's vtables are still prefixes of these
and a client built against ABI 1 needs no rebuild.

**Demand paging** (`IMsgPagingEvents`, and five methods at the end of
`IMsgStore`) hands the decision "this subtree is not in memory yet" back to the
application:

```cpp
msgf::Paging sink ( [&](unsigned long long pos)       { return load ( pos ); }
                  , [&](unsigned long long pos, bool) { return evict ( pos ); } );
st.paging ( &sink );
st.pageIn ( node.pos ( ) );          // false == nothing was paged
st.pushPaging ( );  /* ...a Save, with paging suppressed... */  st.popPaging ( );
```

Its sink's contract is the strictest in this ABI and is stated at
`IMsgPagingEvents`: called synchronously, on the accessing thread, with the
store's lock held, and **the core waits for the answer** — a page-in that has
not returned yet is data that is not there.

**The value stack** (four methods at the end of `IMsgNode`) is a scoped
override of a node's own name and value, held inside the node by the kernel:

```cpp
node.pushValue ( );        // save name + value
node.set ( L"speculative" );
node.popValue ( );         // put it back -- false if nothing was stacked
```

A caller can hand-roll that for one value, and for one value it should. What
this buys is the two things the hand-rolled version cannot do: the **name**
travels with the value, and the saved pair lives **in the store**, so any handle
onto that node can pop what another one pushed, and the pair survives everything
a node survives. It is not a general stack — the kernel keeps one saved pair per
node, and pushing twice replaces the first.

Two states answer `MSGF_E_STATE` rather than reaching the core, and both are
protections: a second `PushPaging` (the core's save has one slot), and a
`PushPaging` with no sink installed (the core's pop asserts unless it has
something to restore, so that push is an abort deferred to the matching pop).

## Three more, measured by the first real client

`..\_Msgcore_UseExamplesLight` rewrites all eight `_Msgcore_UseExamples`
harnesses on this facade. It found three things the 419-check smoke test had
not, because it made calls nothing here had ever made. All three are fixed
above; each is written up at its fix site.

* **`MAX_NAME` was 127; the real bound is 63 UTF-16 units.** The length check
  trusted `P3Pmsg_IsValidItemname`, which counts to 127, while `P3PmsgName`
  stores 63 inline. A name between the two passed every check here, reached the
  core, and died in an MFC `ASSERT` — which `AfxAbort` turns into a process
  exit, not a return. The bound is now the storage's, enforced before the core
  is called.
* **`IMsgStore::Rename` was renaming the FILE.** `P2PmsgMgr::Rename` is named
  for `m_strFilename` and `MoveFileEx`s the store to the string it is given; it
  answers `FALSE` for a store that was never saved. So this method returned
  `MSGF_E_CORE` for an in-memory store and would have silently moved the file
  of a saved one. `GetRootname` is `r_name().c_name()`, and its inverse is what
  it calls now.
* **A `CreateStore` initial size of 1..256 bytes never returns.** At any width.
  512 is the smallest that comes back, so a non-zero request below it is raised
  to 512 rather than passed through.

That client also pinned two facts about this facade that are behaviour rather
than bugs, and are worth knowing before writing against it: a container (list
or vect) has no attribute collection, so declaring or counting attributes on
one is `MSGF_E_TYPE`; and `IsDirty` is TRUE for a brand-new store, because
building the empty heap is itself bytes no file has.

## Why there is no `com\` here

`TargetFacade` carries an ATL layer in `com\` because nothing else exposed
TargetCore to a script host. Msgcore already has one: `..\Msgcore\com\`
(`MsgcoreCom`), with its own IDL, its own `.rgs`, and a test tree in
`.._Msgcore_UseExamplesCom`. Building a second one over this facade would
duplicate it rather than add anything.

## Layout

```
MsgFacade/
  include/
    MsgFacade.h          the ONE public header -- vtables, HRESULTs, constants
    MsgFacadeFn.hpp      optional header-only RAII / std::function sugar
  src/
    FacadeInternal.h     the facade->Msgcore implementation map, and helpers
    FacadeInternal.cpp   the value layer: types, reads, writes, declares
    FacadeLibrary.*      the singleton + MSGF_CreateLibrary
    FacadeStore.*        the store, its lock, and the route resolver
    FacadeNode.*         a position in the tree, as a route
    FacadeContainer.*    IMsgList / IMsgVect
    FacadeCursor.*       IMsgCursor / IMsgWalker
    FacadeModule.cpp     the one CWinApp, for AFX_MANAGE_STATE
  test/FacadeSmokeTest/  419 checks, public headers only
```

---

## Licence

Distributed under the **Apache License, Version 2.0**. See [LICENSE](LICENSE).

```
Copyright © 2026 Khrustal & Mann
             MELBOURNE, VICTORIA, AUSTRALIA, 3000
```

Some files in this repository are **not** covered by that licence — Microsoft project-template
and wizard-generated files keep their own notices, and the MFC / ATL / Visual C++ runtime /
Windows SDK components this library links against are licensed separately and are not bundled
here. Msgcore itself is a separate repository under the same licence and is likewise not
bundled. [NOTICE](NOTICE) lists every one of them.
