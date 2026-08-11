# The interpreter's dictionaries

The graphics language and the machinery that implements it are kept out of the
program's namespace and out of reach of tampering. Where a name lives is a
deliberate choice along three axes: **which VM**, **visible or private**, and
(for private things) **which private home**.

## Axis 0 — the language, or what this run settled

Before anything else: is the value the same for every run of this build, or did
*this* launch decide it?

Almost everything is the language — the same names with the same values however
the interpreter was started. A handful of values are not. Where this run found
its boot files, which directories a resource search covers, whether there is a
user at the other end of standard input, what a page does when it ends and where
it goes: each comes from the command line, the environment, the embedding
caller, or the state of the process, and each is decided afresh on every launch.

`WIN32` is *not* one of these. It is settled by the build (`#ifdef _WIN32`), so
it is the same for every run of that build and belongs with the language.

Those live in **`.hostdict`** and nowhere else (below). The interpreter writes
every one of them itself, from one table in `xpost_interpreter.c`, *after* the
language is in place — so a value that came from the invocation can never be
mistaken for part of the language, and a language built once and reused cannot
carry one launch's answer into the next. `tests/host_settings.golden` registers
the set and `tests/check-host-settings.sh` holds the register, the C table, the
readers among the boot files and a live startup to one another.

A value the *load itself* consumes and nothing reads afterwards is not one of
these, however it arrived. `QUIET` (the `-q` flag) guards the load-time progress
messages and has no reader once loading is over; `GRAPHICS_LOAD_STOPPED` and
`GRAPHICS_LOAD_DEPTH` are the graphics load's own bookkeeping, written and read
inside it; the `newdefaultdevice` bootstrap string is run once while the device
modules load and dropped from `systemdict` at lockdown. These are spent by the
load that read them, and stay with the machinery that reads them.

## Axis 1 — VM (local vs global), decided by lifecycle

- *static* — defined once during start-up, only read afterwards.
- *per-render mutable* — rewritten during every page/render (scratch, the live
  graphics state, a device's page geometry).
- *persistent* — accumulates deliberately and is meant to outlive a job
  (the resource tables, `globaldict`).
- *per-run settled* — written by the host before a program runs and read for the
  whole life of the context (`.hostdict`). Global, because a job's `restore` must
  leave it standing.

Static and persistent state may live in **global VM**. Per-render mutable state
**must** live in **local VM**: `save`/`restore` bracket each job and revert local
VM, so a job's changes are isolated; global VM is *not* reverted, so a mutable
global dictionary leaks one job's changes into the next.

One exception matters. `restore` resets composite objects in local VM **except
strings** (PLRM 3.7.3) — a string's contents are never rolled back, in xpost as
in Ghostscript. So local VM does *not* isolate a mutable **string**: shared
static string data (an encoding vector) must instead be made *read-only*, and a
transient scratch string such as the one behind `=string` (overwritten on every
`cvs`) carries no state between jobs and needs neither treatment. Dictionaries
and arrays *are* reverted, so local VM isolates them.

The global-holds-local rule enforces this: a global composite may not reference a
local object (`invalidaccess`, raised at load per commit `00d8a9b3`). It is why
local machinery cannot simply be dropped into a global private dictionary, and
why `privatedict` (below) exists.

## Axis 2 — visible or private, decided by whether a program needs it

- **reachable by the program by bare name** → on the dict stack: `systemdict`
  (public, base) or `userdict` (program-owned).
- **interpreter-private** → a private home a program can neither name nor
  enumerate, reached only through a frozen `//` reference or the context.

## Axis 3 — which private home, decided by VM and by what reaches it

- **global, static, reached only through a frozen `//`** → a private *global*
  namespace: `.xpostsys` (the single private helper namespace, reached by
  run-time `//.xpostsys /h get exec` or baked via `.xpostsys begin … //h exec`)
  or `.internaldict` (relocated C operators + internal flags). Sealed read-only
  and un-anchored at lockdown.
- **local** (holds local objects, or is per-render mutable) **and hidden** →
  `privatedict`, a local dictionary rooted in the *context*, never on the dict
  stack. This is the only private home that may hold local objects, because a
  global private dictionary may not (axis 1).
- **reached by C by name at run time, or by a program using a standard name** →
  must stay anchored on the dict stack (`systemdict`), even if non-standard. It
  cannot be hidden. This is the whole of what non-standard names remain in
  `systemdict` (see the six kept operators below).

## The dict stack

Exactly three deep, searched top-down: `systemdict` (0, bottom), `globaldict`
(1, empty), `userdict` (2, top). `statusdict`, `serverdict`, `$error`,
`errordict`, `FontDirectory` are **not** on the stack; `systemdict` *names* them
(they resolve by bare name through that entry) but they are local dictionaries.
**`userdict` is empty at start-up** — every interpreter name is *defined* in its
final home (`systemdict` for the language, a private dictionary for the
machinery), so a program starts with a clean namespace. Nothing is staged in
`userdict` and moved out afterwards.

## The dictionaries

### Public

| Dictionary | VM | Lifecycle | Sealed | Role |
|---|---|---|---|---|
| `systemdict` | global | static | read-only after load | the standard operators/procedures of the language + a documented extension set + six kept dotted operators; the base of the dict stack |
| `globaldict` | global | persistent | no | the shared global VM programs write to; survives restore by design (PLRM) |
| `userdict` | local | program-owned | no | the program's own dictionary; **empty at start-up** |
| `$error`, `errordict` | local | mutable | no | error record and handlers; local so a job's error state reverts; **named in** `systemdict` |
| `statusdict`, `serverdict` | local | mutable | no | device/product status and job-server state a program may change; local so changes revert; **named in** `systemdict` |
| `FontDirectory` | local | mutable | no | fonts defined while the allocation mode was local; they revert with the save level that defined them; **named in** `systemdict`. `setglobal` rebinds the name to `GlobalFontDirectory` while the mode is global, so a font defined in terms of another finds the directory its own is going into (PLRM) |
| `GlobalFontDirectory` | global | persistent | no | fonts defined while the allocation mode was global, and only those; survives the restores that empty `FontDirectory` (PLRM 3.7, Table 3.4) |
| `SharedFontDirectory` | global | persistent | no | the same dictionary as `GlobalFontDirectory` under its older name |
| `StandardEncoding`, `ISOLatin1Encoding` | global | static | read-only | shared encoding vectors; read-only per PLRM (findfont copies before installing) |

Local dictionaries named in `systemdict` (`$error`, `statusdict`, …) use the
sanctioned global-names-local exception, applied in C by `copyudtosd` under a
narrow `ignoreinvalidaccess` window; the load-time relocation then drops their
userdict copies so each resolves only through `systemdict`.

#### What is allowed to sit in `systemdict`

`systemdict` is the program-visible base, so its membership is a conformance
surface. Three kinds of name belong:

1. **PLRM-standard operators and procedures** — the language (PostScript Level
   3 / PLRM 3rd ed). The bulk.
2. **A documented extension set** — non-PLRM names kept deliberately: the
   LaserWriter page-size prologs (`letter`, `a4`, …), the `gs`-compatible font
   directories (`GlobalFontDirectory`, `SharedFontDirectory`), print/debug
   utilities (`=only`, `==only`, `=print`), device selection (`newdefaultdevice`
   is *not* here — it is undef'd after load), and the multi-context and other
   `gs`/DPS operators. A non-standard name in `systemdict` must be on this list.
3. **Six kept dotted operators** — internal operators that *cannot* be relocated
   into `.internaldict` because they are reached by name at a time the frozen
   references cannot cover. All are dotted, so no program name collides:

   | Kept | Why it cannot move |
   |---|---|
   | `.lockdown`, `.permitfileread`, `.permitfilewrite`, `.resourcefileopen` | the file-access sandbox API; a trusted prolog calls the permits after `.finalize` but before `.lockdown`, so they must be reachable then. `.lockdown` undefs the whole set from `systemdict` when it engages. |
   | `.gscratch`, `.privatedict` | the accessors that reach the per-render scratch and the private local dictionary; named at run time by driver prologs and machinery the bind pass does not cover — relocating them breaks rendering (verified). |

Everything else non-standard has been hidden. `QUIET`, `GRAPHICS_LOADED`,
`USEDRAWLINE` (internal flags), `newdefaultdevice`, `.sysdictunlock`,
`.sysdictrelock`, the `newXdevice` makers, and every other dotted C operator now
live in `.internaldict` or `privatedict`, not `systemdict`.

### Private — global machinery, sealed and hidden by the lockdown step

Start-up runs in two separated stages, each a proc the start procedures call in
turn. `loadgraphics` **loads** — it pulls in the graphics modules and nothing
else. `.finalize` **locks down** — it freezes the machinery's operator
references, promotes the procedure-implemented standard operators to real
operators, moves the private C operators into `.internaldict`, seals and hides
the private namespaces, and returns the device classes to local VM in
`privatedict`. The language itself needs no moving: `loadgraphics` opens
`systemdict` and reads it straight in. The lockdown is not graphics work;
it only runs after `loadgraphics` because it must seal *after* everything that
will ever be loaded. `.finalize` guards its graphics-only steps behind
`/graphicsdict where`, so it hardens the interpreter with or without graphics —
loading graphics stays genuinely optional. The `GRAPHICS_LOADED` latch (now in
`.internaldict`), read through a frozen `//.internaldict`, keeps both steps
idempotent. `loadgraphics` is idempotent over a load that stopped as well:
`GRAPHICS_LOAD_STOPPED` holds the name the load stopped under and every later
call raises it again, because the window on `systemdict` is a one-shot held in
the context and a second reading would have nowhere to define.

| Dictionary | VM | Lifecycle | Sealed | Reached by / holds |
|---|---|---|---|---|
| `.xpostsys` | global | static (+ `.resources` persistent) | read-only + anchor dropped | the single private helper namespace. Reached by run-time `//.xpostsys /h get exec` (the mutually-recursive path/clip/image/graphics family + interpreter control `.finalize`/`loadgraphics`/`.loadmodule`/`.devicemakers`) and by baking `.xpostsys begin … //h exec` (the colour/shading/pattern/halftone/font-CID families) |
| `.internaldict` | global | static | read-only + anchor dropped | `1183615869 internaldict` (GS-compatible) or frozen `//`; the C operators relocated out of `systemdict`; the internal flags `QUIET`/`USEDRAWLINE`/`GRAPHICS_LOADED`/`GRAPHICS_LOAD_STOPPED`/`GRAPHICS_LOAD_DEPTH`; the `.=stringproc` anchor; the machinery rasterisers (`.fillpoly` …) |
| `.xpostsys /.resources` | global | persistent | (member, writable) | the resource instance table; `defineresource` writes it; global-persistent per PLRM. A shallow read-only seal of `.xpostsys` correctly leaves this member writable |
| `.xpostsys /.hostdict` | global | per-run settled | (member, writable) | **what this run settled, and nothing else** — see below. Writable for the same reason `.resources` is: the seal is shallow |

#### `.hostdict` — what this run settled

Everything above is the language. `.hostdict` is the one dictionary that is not:
every member is a value *this* launch decided, and every member is written by the
interpreter itself, from `host_settings[]` in `xpost_interpreter.c`, once the
language is in place. A setting the host has nothing to say about is written as a
**null** rather than left out, so a member can never be inherited from an earlier
launch — or, when there is a pre-initialised virtual-memory image to load, from
the machine that took it.

| Member | Settled from |
|---|---|
| `DATA_DIR` | where this run found its boot files: `XPOST_DATA_DIR`, the shared library's own directory, the configured data directory, or a relative path from wherever the process started |
| `.resourcepath` | `-I` and `xpost_add_resource_dir`, in the order given |
| `.interactive` | whether the caller asked for a batch run, and whether standard input is a terminal; settled again for every run the context serves |
| `ShowpageSemantics` | the semantics `xpost_create` was given: pause at a page, carry on, or hand control back |
| `SUBDEVICE` | the mode selector of a `-d device:mode` selection, which the raster device reads for its pixel format |
| `OutputFileName` | `-o`. The **host's** binding only: a program's own `/OutputFileName`, on the dictionary stack, still wins, and `setpagedevice` writes the program's into `userdict` from `/OutputFile`. The device machinery looks on the dictionary stack first and here second, which is the precedence the host's copy had when it sat at the bottom of that stack |
| `OutputBufferIn`, `OutputBufferOut` | the framebuffer an embedding caller lends the raster device and where it wants the finished one written back; the pointers travel in strings a program cannot read |

The boot files read a setting through the accessor `.xpostsys /.hostvalue`, so a
caller names the setting and not its home. A name that is not a setting is
refused where it is asked for (`undefined`) rather than answered with a null the
caller would carry off somewhere else; `check-host-settings.sh` makes such a
caller unshippable in any case, by holding every name written beside `.hostdict`
or `.hostvalue` to the register.

`DATA_DIR` is seeded once by `init.ps`, off the handover the bootstrap makes
through `userdict`, because the module loader needs it before the load is
finished. The seed cannot outlive the load: the interpreter clears and rewrites
the whole table afterwards, unconditionally.

`.xpostsys` is global — a global `systemdict` procedure may freeze a `//`
reference to it, and a global object may not reference a local one; local
machinery lives in `privatedict` instead. The seal is read-only (shallow) plus
dropping the userdict anchor, so a program can neither reach, enumerate, nor
overwrite the namespace; the frozen references keep it working. Reads are not
fully prevented (a determined program can extract a reference by decompiling a
readable `systemdict` procedure), which is accepted: it is self-harm under
per-job isolation, and closing it would need `executeonly` everywhere,
non-standard and breaking the `1183615869 internaldict` path.

#### What the lockdown still moves, and why each has to stay

Three relocations remain. Each looks like a definition made in the wrong
place, and each has been tried; the measurements are here so the attempt is
not repeated.

| Moved | Size | Why it cannot be defined in place |
|---|---|---|
| private C operators → `.internaldict` | 76 names | the machinery calls them by bare name all through loading — **161 uses across 47 of them** — so they can only move once the bind pass has frozen those references. Installing them into `.internaldict` at registration compiles and then dies in `init.ps` at the first such use. |
| device classes → `privatedict` | 9 names | a derived class is written by naming its parent (`/.xpost_PBMIMAGE .xpost_PGMIMAGE dup length 2 add dict copy def`) — **51 sites across 11 files**. The step is also not a move: it takes a **local copy with headroom**, which is what makes a job's page setup revert with the save level instead of persisting into the next. |
| `.error` → `privatedict` | 1 name | `errordict`'s handlers bake `//.error` about a dozen times, so it has to be findable on the dictionary stack while `errordict` is built. |

Everything else that used to move now does not: the language is read into
`systemdict`, and the machinery anchors, the wrapped-procedure register and
the device makers are defined into `privatedict` directly.

### Private — local machinery, in `privatedict` (off the dict stack)

`privatedict` is a **local** dictionary rooted in the interpreter context
(`ctx->privatedict`, GC-marked like `window_device`), **never pushed on the dict
stack**. The C reaches it directly through the context field; PostScript reaches
it through the `.privatedict` operator (an accessor kept in `systemdict`, like
`.gscratch`). Because it is off the stack, a program can neither name nor
enumerate its members; because it is local, it may hold local objects — the
thing a global private namespace cannot do. It is the home for every piece of
machinery that is *both* local *and* private.

| Member | Lifecycle | Role |
|---|---|---|
| `.xpost_BBOX` … `.xpost_SVGWRITE` | mutable (setpagedevice) | the device class templates; local so a job's page geometry reverts |
| `newPGMIMAGEdevice` … `newDSCWRITEdevice` | static | the device makers; a program builds a device through `setpagedevice`, never by naming a maker |
| `.graphicsdict` | per-render mutable | the live graphics state: `currgstate` and the gstack of local gstate copies |
| `.gstatetemplate` | mutable (device change) | the template a fresh gstate is copied from; `setpagedevice` writes its device |
| `.gscratchdict` | per-render mutable | the scratch the machinery rewrites every render (stroke, caches, local resources) |
| `.gscratchproc`, `.graphicsdictproc`, `.DEVICEproc` | static | the procedures the `.gscratch`, `graphicsdict` and `DEVICE` operators run; kept here because the operator table is outside the collector's view. Each takes its dictionary as a value rather than baking the name, which is what lets it be defined here — an immediately-evaluated reference would need the name on the dict stack, and `privatedict` is deliberately off it |
| `.wrappedprocs` | static | the procedures behind every wrapped standard operator, for the same reason: the operator table cannot keep them alive. Each is in **global** VM — a local one would be freed by a `restore` past its creation with the table still pointing at it |
| `.error` | mutable | the error hook `signalerror` runs; records into the local `$error` |
| `start`, `startstdin`, `startfilename`, `startfile`, `…nographics` | static | the start procedures; the C fetches them through `ctx->privatedict` to prime a run |

What this run settled is *not* here: `DATA_DIR`, `.resourcepath` and
`.interactive` live in `.hostdict` (axis 0). `privatedict` holds local machinery,
which is the same for every run.

The wrapped-operator anchors that hold *global* objects stay in the global
homes: `.=stringproc` is in `.internaldict` (the `=string` scratch is global).

### REPL

| Dictionary | VM | Lifecycle | Role |
|---|---|---|---|
| `execdict` | global | mutable (balanced) | the executive's dictionary; global because it is frozen into the now-global `executive`. Holds `execdepth`/`quitflag`, incremented and decremented in balance, not program data. The interactive `/start` also installs a transient `userdict /quit` shadow so `quit` exits the REPL cleanly rather than the process; it is not present in non-interactive runs |

## Adding a procedure, operator, or dictionary

0. **Is it the language, or is it this run's?** If the value comes from the
   command line, the environment, the embedding caller, or the state of the
   process (a terminal, a file, a clock) — and anything reads it once the
   language is loaded — it is a **setting**: register it in
   `tests/host_settings.golden`, add it to `host_settings[]` in
   `xpost_interpreter.c`, write it there, and read it through `.hostvalue`. It
   belongs in no other dictionary. A value the load consumes and nothing reads
   afterwards is not a setting and stays with the machinery that reads it.

1. **Lifecycle → VM.** Anything written during a render, or that a program may
   change and expect `restore` to revert, is *per-render mutable* → it, and any
   dictionary holding it, must be **local**. Only *static* or *deliberately
   persistent* values may be **global**. (A global composite may not reference a
   local one — the load-time check enforces it.)

2. **Program-facing?**
   - A standard operator/procedure → `systemdict` (public, sealed).
   - A standard program-facing dictionary that is mutable → a **local** dict
     that `systemdict` names (`copyudtosd` + relocation), like `statusdict`.
   - A non-standard name a program is *meant* to see → it must be on the
     documented extension list, or it does not belong in `systemdict`.

3. **Private → which home?**
   - Static, global, reached only by a frozen `//` → `.xpostsys` (bake it via
     `.xpostsys begin … //h exec` for hot paths, or use the run-time
     `//.xpostsys /h get exec` form for mutually-recursive helpers), or
     `.internaldict` for a relocated C operator or an internal flag. Reference it
     with a frozen `//` so it survives the seal.
   - Local (per-render mutable, or holds local objects) → **`privatedict`**.
     Reach it with the `.privatedict` operator (PostScript) or `ctx->privatedict`
     (C). This keeps local machinery both isolated *and* hidden.
   - Reached **by name at run time** by C or by a standard program name → it must
     stay anchored on the dict stack; it cannot be hidden. Keep it dotted if
     private, and record why in the six-kept table above.

4. **A global dictionary may hold a mutable *member* only if that member is
   itself intentionally persistent** (like `.xpostsys /.resources`). An
   accidentally-mutable member of a global dictionary leaks across jobs — this
   is the bug fixed for the device classes and `statusdict`/`serverdict`. Note
   `readonly` is shallow (per PLRM, per-object for arrays/strings, shared for
   dicts, never recursive), so sealing a container does not lock its members.
