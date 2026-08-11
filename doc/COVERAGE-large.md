# Test coverage (large-object build)

How much of the C sources the test suite executes, and what it never
makes them do. Regenerate with

    meson setup bcovlarge -Db_coverage=true -Dlarge-object=true
    tools/coverage.sh bcovlarge full > doc/COVERAGE-large.md

(needs gcov; takes a few minutes, since it builds an instrumented tree
and runs the profile in it. The setup line is not optional: only a
default `bcov` is configured on the caller's behalf, and a directory
named for some other configuration is refused rather than quietly given
this one.)

## What was measured

The **full** profile: 205 of the tests defined in that build, all of
which passed (205 ok, 0 failed). A coverage report over a run with
failures in it is a measurement of what the sources did while getting an
answer wrong, and it looks exactly like a report over a run without, so
the generator reads the exit status and writes nothing at all when the
profile fails.

The profile is named rather than left to default. `meson test` with no
selection takes in the corpus tests, which render real programs fetched
into `tests/corpus` and skip where they have not been fetched: the
numbers then come from the interpreter suite alone while reading as
though everything had run. The `full` profile is every test but the
corpus, so what is excluded is excluded on purpose and says so here.

These numbers are one platform and one object width: this run measured
the **large-object** build. Code chosen at build time for anything else --
the Windows halves of the compatibility layer, the portable path
confinement used where the kernel has no openat2, and the small-object
half of every WANT_LARGE_OBJECT alternative -- cannot run here and reads
as uncovered whatever the other CI lanes do with it.

## The two numbers

**81.8% of 18521 lines** and **64.7% of 13320 branch outcomes**, across
49 files. 4708 outcomes are never taken.

Branch coverage is the harder of the two and the one worth reading. A
line is covered when control reaches it; an outcome is covered when the
condition actually comes out that way. Every guard in this tree is a
condition whose refusing outcome is the whole point of it, and a line
figure counts such a guard as tested the first time it lets something
through.

## Coverage is not detection

A mutation study over the VM core at this line coverage measured **35%
fault detection**: of 219 compiling mutants, 142 survived the suite --
on lines gcov records as executed. Executing a line and pinning down
what it does are different measurements, and only the second is a
quality figure. Read what follows as a floor: it says where there is
nothing to argue about, not that the rest is settled.

## Where the gaps matter most

A ranking by consequence rather than by line count. Which kinds of gap
matter is the judgement in this section; which code is in each kind is
read out of this run, so a heading here cannot come to name what the
tables below no longer say. The discount at the end is the one reading
that is not a measurement, and the rankings above it are built with it
applied rather than merely stated beside them.

**Guards nothing has ever made refuse.** Conditions the suite reaches
by the hundred million and never once makes come out the other way.
The refusing side of a guard is the whole point of it, so a guard that
has never refused anything is untested however often it is reached.
The dictionary-growth use-after-free was found on the far side of one
of these, which is reason enough to want the refusals driven on purpose
rather than waited for. The eight most evaluated, discounted rows
taken out:

- `src/lib/xpost_interpreter.c:707`, 833,305,252 evaluations, 2 of 4 outcomes never taken -- `if (btype == invalidtype || btype >= XPOST_OBJECT_NTYPES)`
- `src/lib/xpost_interpreter.c:697`, 833,305,252 evaluations, 1 of 2 outcomes never taken -- `if (abase)`
- `src/lib/xpost_interpreter.c:675`, 833,305,252 evaluations, 1 of 2 outcomes never taken -- `if (ctx->quit)`
- `src/lib/xpost_op_path.c:225`, 630,982,979 evaluations, 1 of 2 outcomes never taken -- `if (esz > used - o)   /* the element must fit the decla...`
- `src/lib/xpost_op_path.c:222`, 630,982,979 evaluations, 2 of 4 outcomes never taken -- `if (p[o] < PATH_CMD_MOVE || p[o] > PATH_CMD_CLOSE)`
- `src/lib/xpost_stack.c:134`, 464,297,436 evaluations, 1 of 2 outcomes never taken -- `if (xpost_object_get_type(obj) == invalidtype)`
- `src/lib/xpost_free.c:296`, 440,726,998 evaluations, 2 of 4 outcomes never taken -- `if (e > XPOST_OBJECT_COMP_MAX_ENT ||`
- `src/lib/xpost_interpreter.c:1113`, 312,148,770 evaluations, 2 of 4 outcomes never taken -- `slot.comp_.off != off + 1 ||`

**Global VM is barely exercised.** Two banks of memory, one of them
tested. The conditions that ask which bank an object is in, and
never once this run came out saying the global one:

- `src/lib/xpost_garbage.c:894` -- `if (isglobal)`
- `src/lib/xpost_garbage.c:890` -- `if (!isglobal && getenv("XPOST_GC_XBANK_CHECK") && ctx ...`
- `src/lib/xpost_garbage.c:888` -- `if (!isglobal && getenv("XPOST_GC_VERIFY") && ctx)`
- `src/lib/xpost_garbage.c:758` -- `if (isglobal)`

**Functions nothing in the suite enters.** 24 of them, listed in full
further down. A function nothing reaches is not partly tested, and
where they cluster is where the suite stops short:

- `src/lib/xpost_file.c`: 15
- `src/lib/xpost_op_context.c`: 2
- `src/lib/xpost_dev_jpeg.c`: 2
- `src/lib/xpost_operator.c`: 1
- `src/lib/xpost_interpreter.c`: 1
- `src/lib/xpost_free.c`: 1

**Discounted as unreachable in this configuration**, so that nobody
spends effort on them. Every ranking above is built with these taken
out:

- `xpost_compat_posix.c`
- `xpost_dev_xcb.c`
- `xpost_log.c`
- every `CHECK_VALID_ENT` refusal, which fires only where memory is already corrupt

Beside them, and not separable from the tables by file or by
condition: the Windows halves of the compatibility layer, the portable
path-confinement fallback used where the kernel has no openat2, the
arm of the memory file belonging to the backing this build did not
take, the file-backed VM paths, and the 4 GiB growth clamp.

**Largest raw uncovered but lower consequence**, for completeness. The
biggest blocks of unexecuted lines outside the discount:

- `src/lib/xpost_file.c`: 457 lines never executed (branch 70.47%)
- `src/lib/xpost_op_font.c`: 444 lines never executed (branch 60.34%)
- `src/lib/xpost_interpreter.c`: 210 lines never executed (branch 60.13%)
- `src/lib/xpost_dev_generic.c`: 209 lines never executed (branch 69.81%)

and the lowest branch coverage outside it:

- `src/lib/xpost_dsc_parse.c`: 45.18% of branch outcomes, over 587 lines
- `src/lib/xpost_free.c`: 48.81% of branch outcomes, over 138 lines
- `src/lib/xpost_oplib.c`: 50.00% of branch outcomes, over 73 lines

These are last here rather than first because size is the one thing
about a gap that says nothing about what it costs.

## Conditions whose refusing side nothing takes

3084 conditions are reached by the suite and have an outcome it never
produces, 2988 of them outside the discount above. The count is how many
times the condition was evaluated, so the top of this list is code the
suite leans on constantly without ever testing what it is there for.
This table is the measurement rather than the reading of it, so the
discount is not applied here. The 40 most-evaluated:

| Evaluations | Site | Never taken | Condition |
|---:|---|---:|---|
| 833,305,252 | `src/lib/xpost_interpreter.c:707` | 2 of 4 | `if (btype == invalidtype \|\| btype >= XPOST_OBJECT_NTYPES)` |
| 833,305,252 | `src/lib/xpost_interpreter.c:697` | 1 of 2 | `if (abase)` |
| 833,305,252 | `src/lib/xpost_interpreter.c:675` | 1 of 2 | `if (ctx->quit)` |
| 630,982,979 | `src/lib/xpost_op_path.c:225` | 1 of 2 | `if (esz > used - o)   /* the element must fit the declared ...` |
| 630,982,979 | `src/lib/xpost_op_path.c:222` | 2 of 4 | `if (p[o] < PATH_CMD_MOVE \|\| p[o] > PATH_CMD_CLOSE)` |
| 517,773,782 | `src/lib/xpost_memory.c:931` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 464,297,436 | `src/lib/xpost_stack.c:134` | 1 of 2 | `if (xpost_object_get_type(obj) == invalidtype)` |
| 440,726,998 | `src/lib/xpost_free.c:296` | 2 of 4 | `if (e > XPOST_OBJECT_COMP_MAX_ENT \|\|` |
| 312,148,770 | `src/lib/xpost_interpreter.c:1113` | 2 of 4 | `slot.comp_.off != off + 1 \|\|` |
| 309,529,385 | `src/lib/xpost_stack.c:181` | 1 of 2 | `if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* fin...` |
| 287,985,204 | `src/lib/xpost_interpreter.c:820` | 1 of 4 | `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot >= 1)` |
| 262,227,415 | `src/lib/xpost_operator.c:1050` | 1 of 4 | `if (!sp[i].fp && (xpost_object_get_type(op.proc) == arrayty...` |
| 261,879,815 | `src/lib/xpost_operator.c:709` | 1 of 2 | `assert(n < XPOST_STACK_SEGMENT_SIZE);` |
| 261,879,815 | `src/lib/xpost_operator.c:1077` | 1 of 10 | `switch(sp[i].in)` |
| 247,614,477 | `src/lib/xpost_dict.c:229` | 1 of 2 | `if (xpost_object_is_composite(k))` |
| 244,012,203 | `src/lib/xpost_memory.c:838` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 235,812,365 | `src/lib/xpost_object.c:215` | 1 of 4 | `if (type == dicttype && xpost_object_dict_get_access)` |
| 223,675,816 | `src/lib/xpost_memory.c:906` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 220,363,499 | `src/lib/xpost_memory.c:860` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 220,363,499 | `src/lib/xpost_free.c:316` | 1 of 2 | `if (!ret)` |
| 220,363,499 | `src/lib/xpost_free.c:298` | 1 of 2 | `mem->table.tab[e].tag != 0)` |
| 217,095,389 | `src/lib/xpost_file.c:5171` | 1 of 2 | `if (!xpost_memory_get(mem, f.mark_.padw, 0, sizeof fp, &fp))` |
| 217,095,389 | `src/lib/xpost_file.c:5169` | 2 of 4 | `if (!xpost_memory_table_get_tag(mem, f.mark_.padw, &tag) \|\|...` |
| 214,635,818 | `src/lib/xpost_free.c:338` | 1 of 2 | `if (!ret)` |
| 213,792,659 | `src/lib/xpost_file.c:737` | 1 of 2 | `if (df->poll_before_read)` |
| 201,199,157 | `src/lib/xpost_interpreter.c:1079` | 11 of 30 | `EVALARRAY_SYNC_SLOT();` |
| 197,921,857 | `src/lib/xpost_object.c:217` | 1 of 4 | `else if (type == filetype && xpost_object_file_get_access)` |
| 191,763,276 | `src/lib/xpost_interpreter.c:1385` | 2 of 4 | `if (type == invalidtype \|\| type >= XPOST_OBJECT_NTYPES)` |
| 191,763,276 | `src/lib/xpost_interpreter.c:1364` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 187,488,020 | `src/lib/xpost_interpreter.c:1089` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 179,401,082 | `src/lib/xpost_dict.c:586` | 1 of 4 | `if (xpost_object_get_type(a) == nametype &&` |
| 171,158,726 | `src/lib/xpost_memory.c:954` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 165,369,112 | `src/lib/xpost_dict.c:729` | 1 of 2 | `if (!dp)` |
| 156,074,385 | `src/lib/xpost_interpreter.c:1112` | 1 of 2 | `slot.comp_.sz != remaining - 1 \|\|` |
| 156,074,385 | `src/lib/xpost_interpreter.c:1111` | 1 of 2 | `if (slot.tag != a.comp_.tag \|\|` |
| 117,962,595 | `src/lib/xpost_op_stack.c:119` | 1 of 2 | `if (!xpost_stack_push(ctx->lo, ctx->os, src[i]))` |
| 111,592,076 | `src/lib/xpost_interpreter.c:668` | 2 of 4 | `EVALARRAY_RESOLVE_ABASE();` |
| 101,330,522 | `src/lib/xpost_save.c:166` | 1 of 2 | `if (!xpost_ent_valid(mem, ent))` |
| 89,497,662 | `src/lib/xpost_dict.c:590` | 1 of 2 | `a.mark_.padw == b.mark_.padw;` |
| 87,296,798 | `src/lib/xpost_array.c:151` | 1 of 2 | `if (ret)` |

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Lines | Line % | Branch % | Uncovered |
|---|---:|---:|---:|---:|
| `src/lib/xpost_file.c` | 2403 | 80.98% | 70.47% | 457 |
| `src/lib/xpost_op_font.c` | 2285 | 80.57% | 60.34% | 444 |
| `src/lib/xpost_interpreter.c` | 1136 | 81.51% | 60.13% | 210 |
| `src/lib/xpost_dev_generic.c` | 1598 | 86.92% | 69.81% | 209 |
| `src/lib/xpost_dsc_parse.c` | 587 | 70.70% | 45.18% | 172 |
| `src/lib/xpost_font.c` | 606 | 76.73% | 56.32% | 141 |
| `src/lib/xpost_op_file.c` | 950 | 86.84% | 66.39% | 125 |
| `src/lib/xpost_op_path.c` | 1026 | 89.86% | 70.59% | 104 |
| `src/lib/xpost_op_token.c` | 670 | 84.63% | 78.69% | 103 |
| `src/lib/xpost_operator.c` | 472 | 78.81% | 86.84% | 100 |
| `src/lib/xpost_garbage.c` | 321 | 71.96% | 62.31% | 90 |
| `src/lib/xpost_compat_posix.c` | 204 | 58.33% | 39.44% | 85 |
| `src/lib/xpost_memory.c` | 241 | 66.39% | 52.14% | 81 |
| `src/lib/xpost_dev_xcb.c` | 276 | 71.74% | 52.17% | 78 |
| `src/lib/xpost_dev_png.c` | 328 | 77.44% | 61.86% | 74 |
| `src/lib/xpost_op_control.c` | 414 | 84.54% | 70.21% | 64 |
| `src/lib/xpost_dev_raster.c` | 317 | 80.44% | 66.48% | 62 |
| `src/lib/xpost_log.c` | 113 | 46.02% | 28.30% | 61 |
| `src/lib/xpost_dev_jpeg.c` | 242 | 77.27% | 58.73% | 55 |
| `src/lib/xpost_free.c` | 138 | 61.59% | 48.81% | 53 |
| `src/lib/xpost_dict.c` | 368 | 85.60% | 77.83% | 53 |
| `src/lib/xpost_op_dict.c` | 304 | 83.88% | 67.74% | 49 |
| `src/lib/xpost_context.c` | 175 | 72.57% | 56.41% | 48 |
| `src/lib/xpost_object.c` | 124 | 62.10% | 53.33% | 47 |
| `src/lib/xpost_dev_bgr.c` | 175 | 78.86% | 58.18% | 37 |
| `src/bin/xpost_main.c` | 266 | 86.09% | 65.93% | 37 |
| `src/lib/xpost_op_array.c` | 243 | 85.60% | 71.26% | 35 |
| `src/lib/xpost_oplib.c` | 73 | 54.79% | 50.00% | 33 |
| `src/lib/xpost_op_string.c` | 213 | 84.98% | 66.43% | 32 |
| `src/lib/xpost_name.c` | 157 | 80.89% | 67.95% | 30 |
| `src/lib/xpost_save.c` | 123 | 78.86% | 63.64% | 26 |
| `src/lib/xpost_op_type.c` | 307 | 91.53% | 68.73% | 26 |
| `src/lib/xpost_garbage_diag.c` | 125 | 83.20% | 68.63% | 21 |
| `src/lib/xpost_op_misc.c` | 191 | 90.05% | 68.42% | 19 |
| `src/lib/xpost_op_context.c` | 77 | 81.82% | 58.82% | 14 |
| `src/lib/xpost_op_stack.c` | 109 | 88.99% | 67.65% | 12 |
| `src/lib/xpost_dsc_file.c` | 47 | 76.60% | 61.54% | 11 |
| `src/lib/xpost_op_param.c` | 61 | 83.61% | 58.33% | 10 |
| `src/lib/xpost_string.c` | 64 | 85.94% | 63.89% | 9 |
| `src/lib/xpost_op_matrix.c` | 255 | 96.47% | 64.52% | 9 |
| `src/lib/xpost_main.c` | 41 | 78.05% | 54.17% | 9 |
| `src/lib/xpost_stack.c` | 151 | 94.70% | 88.24% | 8 |
| `src/lib/xpost_array.c` | 56 | 89.29% | 80.56% | 6 |
| `src/lib/xpost_op_math.c` | 187 | 97.33% | 59.57% | 5 |
| `src/lib/xpost_op_save.c` | 90 | 95.56% | 76.92% | 4 |
| `src/lib/xpost_op_packedarray.c` | 33 | 90.91% | 65.00% | 3 |
| `src/lib/xpost_compat.c` | 33 | 90.91% | 60.00% | 3 |
| `src/lib/xpost_op_boolean.c` | 104 | 98.08% | 60.87% | 2 |
| `src/lib/xpost_matrix.c` | 42 | 100.00% | -- | 0 |

## Functions the suite never enters

The blind spots: 24 functions nothing in the suite reaches.

(A further 25 are defined in headers, so every object that includes
one carries its own copy and the copies that are not called read as
zero. They are listed at the end rather than here.)

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`

**`src/lib/xpost_dev_xcb.c`**

- `_fillpoly`

**`src/lib/xpost_file.c`**

- `a85_flush`
- `a85_writech`
- `dctenc_error_exit`
- `dctenc_output_message`
- `enc_readch`
- `enc_unreadch`
- `_filter_cons_abandon`
- `filter_flush`
- `filter_writech`
- `memory_flush`
- `memory_purge`
- `memory_seek`
- `memory_tell`
- `memory_writech`
- `rsd_flush`

**`src/lib/xpost_font.c`**

- `_strike_resample`

**`src/lib/xpost_free.c`**

- `_dump_chain`

**`src/lib/xpost_interpreter.c`**

- `evalquit`

**`src/lib/xpost_op_context.c`**

- `_i_am_free_`
- `_i_am_zombie_`

**`src/lib/xpost_operator.c`**

- `_stack_number_number`

## Header-defined functions with an uncalled copy

Not blind spots: each is compiled into every object that includes its
header, and only the copies nothing calls are counted here.

- `xpost_isatty` (in `src/lib/xpost_compat.c`)
- `xpost_fd_realpath` (in `src/lib/xpost_compat_posix.c`)
- `xpost_renameat_beneath` (in `src/lib/xpost_compat_posix.c`)
- `xpost_context_dump` (in `src/lib/xpost_context.c`)
- `xpost_dev_jpeg_options_set` (in `src/lib/xpost_dev_jpeg.c`)
- `xpost_dev_png_options_set` (in `src/lib/xpost_dev_png.c`)
- `xpost_strbuf_free` (in `src/lib/xpost_file.c`)
- `xpost_font_face_glyph_index_get` (in `src/lib/xpost_font.c`)
- `xpost_free_dump` (in `src/lib/xpost_free.c`)
- `xpost_ent_ptr` (in `src/lib/xpost_garbage_diag.c`)
- `xpost_interpreter_exit` (in `src/lib/xpost_interpreter.c`)
- `xpost_log_print_cb_set` (in `src/lib/xpost_log.c`)
- `xpost_log_print_cb_stdout` (in `src/lib/xpost_log.c`)
- `xpost_memory_table_dump_ent` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_get_mark` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_addr` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_mark` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_size` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_tag` (in `src/lib/xpost_memory.c`)
- `xpost_object_install_file_get_access` (in `src/lib/xpost_object.c`)
- `xpost_object_install_file_set_access` (in `src/lib/xpost_object.c`)
- `xpost_memory_name_stack_adr` (in `src/lib/xpost_op_dict.c`)
- `xpost_operator_dump` (in `src/lib/xpost_operator.c`)
- `xpost_object_is_exe` (in `src/lib/xpost_op_font.c`)
- `xpost_op_breakhere` (in `src/lib/xpost_oplib.c`)
