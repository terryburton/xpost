# The interpreter's dictionaries

The graphics language and the machinery that implements it are kept out of the
program's namespace and out of reach of tampering. Where a name lives is a
deliberate choice along three axes: **which VM**, **visible or private**, and
(for private things) **which private home**.

## Axis 1 — VM (local vs global), decided by lifecycle

- *static* — defined once during start-up, only read afterwards.
- *per-render mutable* — rewritten during every page/render (scratch, the live
  graphics state, a device's page geometry).
- *persistent* — accumulates deliberately and is meant to outlive a job
  (the resource tables, `globaldict`).

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
**`userdict` is empty at start-up** — every interpreter name has been relocated
into `systemdict` or a private home, so a program starts with a clean namespace.

## The dictionaries

### Public — on the dict stack, or named in systemdict

| Dictionary | VM | Lifecycle | Sealed | Role |
|---|---|---|---|---|
| `systemdict` | global | static | read-only after load | the standard operators/procedures of the language + a documented extension set + six kept dotted operators; the base of the dict stack |
| `globaldict` | global | persistent | no | the shared global VM programs write to; survives restore by design (PLRM) |
| `userdict` | local | program-owned | no | the program's own dictionary; **empty at start-up** |
| `$error`, `errordict` | local | mutable | no | error record and handlers; local so a job's error state reverts; **named in** `systemdict` |
| `statusdict`, `serverdict` | local | mutable | no | device/product status and job-server state a program may change; local so changes revert; **named in** `systemdict` |
| `FontDirectory` | local | mutable | no | fonts defined this job; local, so they revert; **named in** `systemdict` |
| `GlobalFontDirectory`, `SharedFontDirectory` | global | persistent | no | gs-compatible font-directory aliases, deliberately program-facing (see below) |
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
references, relocates the language into `systemdict` and the private C operators
into `.internaldict`, seals and hides the private namespaces, and returns the
device classes to local VM in `privatedict`. The lockdown is not graphics work;
it only runs after `loadgraphics` because it must seal *after* everything that
will ever be loaded. `.finalize` guards its graphics-only steps behind
`/graphicsdict where`, so it hardens the interpreter with or without graphics —
loading graphics stays genuinely optional. The `GRAPHICS_LOADED` latch (now in
`.internaldict`), read through a frozen `//.internaldict`, keeps both steps
idempotent.

| Dictionary | VM | Lifecycle | Sealed | Reached by / holds |
|---|---|---|---|---|
| `.xpostsys` | global | static (+ `.resources` persistent) | read-only + anchor dropped | the single private helper namespace. Reached by run-time `//.xpostsys /h get exec` (the mutually-recursive path/clip/image/graphics family + interpreter control `.finalize`/`loadgraphics`/`.loadmodule`/`.devicemakers`) and by baking `.xpostsys begin … //h exec` (the colour/shading/pattern/halftone/font-CID families) |
| `.internaldict` | global | static | read-only + anchor dropped | `1183615869 internaldict` (GS-compatible) or frozen `//`; the C operators relocated out of `systemdict`; the internal flags `QUIET`/`USEDRAWLINE`/`GRAPHICS_LOADED`; the `.=stringproc` anchor; the machinery rasterisers (`.fillpoly` …) |
| `.xpostsys /.resources` | global | persistent | (member, writable) | the resource instance table; `defineresource` writes it; global-persistent per PLRM. A shallow read-only seal of `.xpostsys` correctly leaves this member writable |

`.xpostsys` is global — a global `systemdict` procedure may freeze a `//`
reference to it, and a global object may not reference a local one; local
machinery lives in `privatedict` instead. (It was once two dictionaries, a local
`.xpostint` and a global `.xpostsys`; when `.xpostint` became global the
distinction was only convention — call form and subsystem grouping — so they
were merged into one.) The seal is read-only (shallow) plus dropping the userdict
anchor, so
a program can neither reach, enumerate, nor overwrite the namespaces; the frozen
references keep them working. Reads are not fully prevented (a determined program
can extract a reference by decompiling a readable `systemdict` procedure), which
is accepted: it is self-harm under per-job isolation, and closing it would need
`executeonly` everywhere, non-standard and breaking the `1183615869 internaldict`
path.

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
| `.gscratchproc`, `.graphicsdictproc`, `.DEVICEproc` | static | procedures wrapped into operators; anchored so the collector keeps them (the operator table is outside its view) |
| `.error` | mutable | the error hook `signalerror` runs; records into the local `$error` |
| `.resourcepath` | static after host setup | resource search path; the host appends directories at start-up |
| `DATA_DIR` | static | the directory modules load from |
| `start`, `startstdin`, `startfilename`, `startfile`, `…nographics` | static | the start procedures; the C fetches them through `ctx->privatedict` to prime a run |

The wrapped-operator anchors that hold *global* objects stay in the global
homes: `.=stringproc` is in `.internaldict` (the `=string` scratch is global).

### REPL

| Dictionary | VM | Lifecycle | Role |
|---|---|---|---|
| `execdict` | global | mutable (balanced) | the executive's dictionary; global because it is frozen into the now-global `executive`. Holds `execdepth`/`quitflag`, incremented and decremented in balance, not program data. The interactive `/start` also installs a transient `userdict /quit` shadow so `quit` exits the REPL cleanly rather than the process; it is not present in non-interactive runs |

## Adding a procedure, operator, or dictionary — the decision

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
   accidentally-mutable member of a global dictionary leaks across jobs. Note
   `readonly` is shallow (per PLRM, per-object for arrays/strings, shared for
   dicts, never recursive), so sealing a container does not lock its members.
