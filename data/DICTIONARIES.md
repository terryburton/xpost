# The interpreter's dictionaries

The graphics language and the machinery that implements it are kept out of the
program's namespace and out of reach of tampering. Where a name lives is a
deliberate choice along two axes.

**Data lifecycle** decides the **VM** a value lives in:

- *static* — defined once during start-up, only read afterwards.
- *per-render mutable* — rewritten during every page/render (scratch, the live
  graphics state, a device's page geometry).
- *persistent* — accumulates deliberately and is meant to outlive a job
  (the resource tables, `globaldict`).

Static and persistent state may live in **global VM**. Per-render mutable state
**must** live in **local VM**: `save`/`restore` bracket each job and revert local
VM, so a job's changes are isolated; global VM is *not* reverted, so a mutable
global dictionary leaks one job's changes into the next. This is the rule behind
several placements below (device classes, `statusdict`, `.gscratchdict`).

**Access needs** decide **which dictionary** and whether it is **sealed** (made
read-only) and/or **hidden** (anchor dropped from userdict):

- reachable by the program by bare name → on the dict stack: `systemdict`
  (public, base) or `userdict` (program-owned).
- interpreter-private → a private namespace reached only through a frozen `//`
  reference, so a program can neither name nor enumerate it.
- reached by C by name at run time → must stay anchored on the dict stack
  (cannot be hidden), even if private.

## The dict stack

Exactly three deep, searched top-down: `systemdict` (0, bottom), `globaldict`
(1, empty), `userdict` (2, top). `statusdict`, `serverdict`, `$error`,
`errordict`, `FontDirectory` are **not** on the stack; `systemdict` *names* them
(they resolve by bare name through that entry) but they are local dictionaries.

## The dictionaries

### Public

| Dictionary | VM | Lifecycle | Sealed | Role |
|---|---|---|---|---|
| `systemdict` | global | static | read-only after load | the standard operators and procedures of the language; the base of the dict stack |
| `globaldict` | global | persistent | no | the shared global VM programs write to; survives restore by design (PLRM) |
| `userdict` | local | program-owned | no | the program's own dictionary; kept clear of interpreter names |
| `$error`, `errordict` | local | mutable | no | error record and handlers; local so a job's error state reverts; **named in** `systemdict` |
| `statusdict`, `serverdict` | local | mutable | no | device/product status and job-server state a program may change; local so changes revert; **named in** `systemdict` |
| `FontDirectory` | local | mutable | no | fonts defined this job; local, so they revert (global fonts persist as resources); **named in** `systemdict` |
| `StandardEncoding`, `ISOLatin1Encoding` | global | static | read-only | shared encoding vectors; read-only per PLRM (findfont copies before installing) |

Local dictionaries named in `systemdict` (`$error`, `statusdict`, …) use the
sanctioned global-names-local exception, applied in C by `copyudtosd` under a
narrow `ignoreinvalidaccess` window; the load-time relocation then drops their
userdict copies so each resolves only through `systemdict`.

### Private — machinery, sealed and hidden after graphics load

| Dictionary | VM | Lifecycle | Sealed | Reached by |
|---|---|---|---|---|
| `.xpostsys` | global | static | read-only + anchor dropped | frozen `//.xpostsys /h get exec`; the home for helpers that embed a local reference |
| `.xpostsys` | global | static (+ `.resources` persistent) | read-only + anchor dropped | frozen `//.xpostsys /h ...`; the general private global-helper home |
| `.internaldict` | global | static | read-only + anchor dropped | `1183615869 internaldict` (GS-compatible) or frozen `//`; holds the C operators relocated out of `systemdict` |
| `.xpostsys /.resources` | global | persistent | (member, writable) | the resource instance table; `defineresource` writes it; global-persistent per PLRM. A read-only seal of `.xpostsys` is *shallow* and correctly leaves this member writable |

`.xpostsys` and `.xpostsys` are both global because a global `systemdict`
procedure may freeze a `//` reference to them, and a global object may not
reference a local one. The seal is read-only (shallow) plus dropping the
userdict anchor, so a program can neither reach, enumerate, nor overwrite the
namespaces; the frozen references keep them working. Reads are not fully
prevented (a determined program can extract a reference by decompiling a readable
`systemdict` procedure), which is accepted: it is self-harm under per-job
isolation, and closing it would need `executeonly` everywhere, which is
non-standard and breaks the `1183615869 internaldict` path.

### Private — local state (userdict, dotted, not hidden)

Per-render mutable state must be local, so it is anchored in `userdict` under a
dotted name (private by convention, invisible to a program's non-dotted lookups
but still enumerable). It cannot be hidden when reached by name at run time.

| Dictionary | Lifecycle | Role |
|---|---|---|
| `.gscratchdict` | per-render mutable | the scratch the machinery rewrites every render (stroke, caches, local resources) |
| `.graphicsdict` | per-render mutable | the live graphics state: `currgstate` and the gstack of local gstate copies |
| `.gstatetemplate` | mutable (device change) | the template a fresh gstate is copied from; `setpagedevice` writes its device |
| `.xpost_BBOX` … `.xpost_SVGWRITE` | mutable (setpagedevice) | device class templates; local so a job's page geometry reverts. Reached by name from the C drivers and the `newXdevice` makers, so they stay anchored |
| `.error` | mutable | the error hook `signalerror` runs; records into the local `$error` |
| `.fontsubstitutions` | static | findfont's substitution table |
| `.gscratchproc`, `.graphicsdictproc`, `.DEVICEproc`, `.=stringproc` | static | procedures wrapped into operators; anchored so the collector keeps them (the operator table is outside its view) |
| `.resourcepath` | static after host setup | resource search path; the host appends directories at start-up |

### REPL

| Dictionary | VM | Lifecycle | Role |
|---|---|---|---|
| `execdict` | global | mutable (balanced) | the executive's dictionary; global because it is frozen into the now-global `executive`. Holds `execdepth`/`quitflag`, interpreter REPL state, incremented and decremented in balance, not program data |

## Adding a procedure, operator, or dictionary

1. **Determine the lifecycle.** Anything written during a render (or that a
   program may change and expect `restore` to revert) is *per-render mutable* →
   it, and any dictionary that holds it, must be **local**. Only *static* or
   *deliberately persistent* values may be **global**.

2. **Determine the access.**
   - A standard, program-facing operator or procedure → `systemdict` (public,
     sealed).
   - A standard program-facing dictionary that is mutable → a **local** dict
     that `systemdict` names (`copyudtosd` + relocation), like `statusdict`.
   - Interpreter-private machinery, static, reached only by the machinery →
     `.xpostsys` (or `.xpostsys` if it must freeze a local reference), where it
     is sealed and hidden. Reference it with a frozen `//` so it survives the
     seal.
   - Private but reached **by name at run time** (by C, or by a program using a
     standard name) → it must stay anchored on the dict stack (`userdict`,
     dotted if private); it cannot be hidden.

3. **A global dictionary may hold a mutable *member* only if that member is
   itself intentionally persistent** (like `.xpostsys /.resources`). An
   accidentally-mutable member of a global dictionary leaks across jobs — this
   is the bug fixed for the device classes and `statusdict`/`serverdict`. Note
   `readonly` is shallow (per PLRM, per-object for arrays/strings, shared for
   dicts, never recursive), so sealing a container does not lock its members.
