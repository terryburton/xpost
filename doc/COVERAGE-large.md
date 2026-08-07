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

The **full** profile: 199 of the tests defined in that build, all of
which passed (199 ok, 0 failed). A coverage report over a run with
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

**81.1% of 18523 lines** and **64.0% of 13324 branch outcomes**, across
49 files. 4797 outcomes are never taken.

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

A ranking by consequence rather than by line count. It is a reading of
the tables below and is revised with them.

**Guards nothing has ever made refuse.** The first table is the one to
act on: conditions the suite evaluates by the hundred million whose
refusing side it never once produces. Among them the write bound in
`xpost_memory_put`, the entity-number ceiling in
`xpost_memory_table_alloc`, the free-list validation in `xpost_free.c`
that discards a corrupt list rather than hand out an entity two owners
share, and the index bounds in `xpost_stack.c`. These are the last
thing between a mis-sized entity and a write outside the arena, and
nothing in the suite has yet made one say no. The dictionary-growth
use-after-free came out of this family, which is reason enough to want
the refusals driven on purpose rather than waited for. Read the table
past the `CHECK_VALID_ENT` rows, which are discounted below.

**Global VM is barely exercised.** Across every collection this run
performs, the global half is never the one collected: the `isglobal`
arms in `xpost_garbage.c` never come out true. The relocation recheck
against the global bank in the fused `def` and array fast paths in
`xpost_interpreter.c` is never true either, while the local-bank twin
beside it fires a handful of times in twelve million. Two banks of
memory, one of them tested.

**Documented options with no coverage at all.** `_xpost_main_list_add`
is never entered, so `-D`/`--define` and `-I`/`--include` -- both in the
usage text the binary prints -- are untested.

**Reachable from ordinary PostScript, never entered.** Among the
functions listed further down: `filter_flush`, `filter_writech` and
`a85_writech`, the whole memory-file method set
(`memory_writech`/`seek`/`tell`/`flush`/`purge`), `enc_readch` and
`enc_unreadch`, `rsd_flush` and `rsd_unreadch`, `_filter_cons_abandon`,
`dct_skip_input_data` and all four JPEG error handlers,
`_outline_conicto` -- no TrueType quadratic is ever traced to a path --
`program_take`, `_strike_resample`, and `_xpost_glyph_name_to_unicode`.

**Discount as unreachable in this configuration**, so that nobody
spends effort on them: the Windows halves of `xpost_compat_posix.c` and
the portable path-confinement fallback, `xpost_dev_xcb.c` (it needs a
display), the mmap and file-backed VM paths, `xpost_log.c`, `evalquit`,
the 4 GiB growth clamp, and the `CHECK_VALID_ENT` refusals.

**Largest raw uncovered but lower consequence**, for completeness:
`xpost_file.c` and `xpost_op_font.c` hold the two largest blocks of
unexecuted lines in the tree, and `xpost_dsc_parse.c` and
`xpost_font.c` the lowest branch coverage of any file not discounted
above. None of the four guards memory, which is why they are last here
rather than first.

## Conditions whose refusing side nothing takes

3090 conditions are reached by the suite and have an outcome it never
produces. The count is how many times the condition was evaluated, so
the top of this list is code the suite leans on constantly without ever
testing what it is there for. The 40 most-evaluated:

| Evaluations | Site | Never taken | Condition |
|---:|---|---:|---|
| 822,461,170 | `src/lib/xpost_interpreter.c:707` | 2 of 4 | `if (btype == invalidtype \|\| btype >= XPOST_OBJECT_NTYPES)` |
| 822,461,170 | `src/lib/xpost_interpreter.c:697` | 1 of 2 | `if (abase)` |
| 822,461,170 | `src/lib/xpost_interpreter.c:675` | 1 of 2 | `if (ctx->quit)` |
| 619,691,434 | `src/lib/xpost_op_path.c:225` | 1 of 2 | `if (esz > used - o)   /* the element must fit the declared ...` |
| 619,691,434 | `src/lib/xpost_op_path.c:222` | 2 of 4 | `if (p[o] < PATH_CMD_MOVE \|\| p[o] > PATH_CMD_CLOSE)` |
| 506,244,463 | `src/lib/xpost_memory.c:925` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 458,090,353 | `src/lib/xpost_stack.c:134` | 1 of 2 | `if (xpost_object_get_type(obj) == invalidtype)` |
| 440,721,682 | `src/lib/xpost_free.c:296` | 2 of 4 | `if (e > XPOST_OBJECT_COMP_MAX_ENT \|\|` |
| 309,216,704 | `src/lib/xpost_interpreter.c:1113` | 2 of 4 | `slot.comp_.off != off + 1 \|\|` |
| 304,672,073 | `src/lib/xpost_stack.c:181` | 1 of 2 | `if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* fin...` |
| 283,562,225 | `src/lib/xpost_interpreter.c:820` | 1 of 4 | `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot >= 1)` |
| 259,084,542 | `src/lib/xpost_operator.c:1050` | 1 of 4 | `if (!sp[i].fp && (xpost_object_get_type(op.proc) == arrayty...` |
| 258,748,524 | `src/lib/xpost_operator.c:709` | 1 of 2 | `assert(n < XPOST_STACK_SEGMENT_SIZE);` |
| 258,748,524 | `src/lib/xpost_operator.c:1077` | 1 of 10 | `switch(sp[i].in)` |
| 245,704,414 | `src/lib/xpost_dict.c:229` | 1 of 2 | `if (xpost_object_is_composite(k))` |
| 243,538,354 | `src/lib/xpost_memory.c:832` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 232,684,222 | `src/lib/xpost_object.c:215` | 1 of 4 | `if (type == dicttype && xpost_object_dict_get_access)` |
| 220,360,841 | `src/lib/xpost_memory.c:854` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 220,360,841 | `src/lib/xpost_free.c:316` | 1 of 2 | `if (!ret)` |
| 220,360,841 | `src/lib/xpost_free.c:298` | 1 of 2 | `mem->table.tab[e].tag != 0)` |
| 216,062,702 | `src/lib/xpost_memory.c:900` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 214,633,384 | `src/lib/xpost_free.c:338` | 1 of 2 | `if (!ret)` |
| 209,482,301 | `src/lib/xpost_file.c:5171` | 1 of 2 | `if (!xpost_memory_get(mem, f.mark_.padw, 0, sizeof fp, &fp))` |
| 209,482,301 | `src/lib/xpost_file.c:5169` | 2 of 4 | `if (!xpost_memory_table_get_tag(mem, f.mark_.padw, &tag) \|\|...` |
| 206,294,930 | `src/lib/xpost_file.c:737` | 1 of 2 | `if (df->poll_before_read)` |
| 198,959,470 | `src/lib/xpost_interpreter.c:1079` | 11 of 30 | `EVALARRAY_SYNC_SLOT();` |
| 195,386,789 | `src/lib/xpost_object.c:217` | 1 of 4 | `else if (type == filetype && xpost_object_file_get_access)` |
| 189,375,717 | `src/lib/xpost_interpreter.c:1385` | 2 of 4 | `if (type == invalidtype \|\| type >= XPOST_OBJECT_NTYPES)` |
| 189,375,717 | `src/lib/xpost_interpreter.c:1364` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 185,213,356 | `src/lib/xpost_interpreter.c:1089` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 176,852,371 | `src/lib/xpost_dict.c:586` | 1 of 4 | `if (xpost_object_get_type(a) == nametype &&` |
| 169,758,321 | `src/lib/xpost_memory.c:951` | 1 of 2 | `if ((unsigned long long)offset * sz + sz > mem->table.tab[e...` |
| 169,758,321 | `src/lib/xpost_memory.c:949` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 164,458,089 | `src/lib/xpost_dict.c:729` | 1 of 2 | `if (!dp)` |
| 154,608,352 | `src/lib/xpost_interpreter.c:1112` | 1 of 2 | `slot.comp_.sz != remaining - 1 \|\|` |
| 154,608,352 | `src/lib/xpost_interpreter.c:1111` | 1 of 2 | `if (slot.tag != a.comp_.tag \|\|` |
| 117,904,481 | `src/lib/xpost_op_stack.c:119` | 1 of 2 | `if (!xpost_stack_push(ctx->lo, ctx->os, src[i]))` |
| 110,321,479 | `src/lib/xpost_interpreter.c:668` | 2 of 4 | `EVALARRAY_RESOLVE_ABASE();` |
| 100,551,805 | `src/lib/xpost_save.c:166` | 1 of 2 | `if (!xpost_ent_valid(mem, ent))` |
| 88,246,165 | `src/lib/xpost_dict.c:590` | 1 of 2 | `a.mark_.padw == b.mark_.padw;` |

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Lines | Line % | Branch % | Uncovered |
|---|---:|---:|---:|---:|
| `src/lib/xpost_file.c` | 2403 | 80.23% | 69.91% | 475 |
| `src/lib/xpost_op_font.c` | 2285 | 80.39% | 60.16% | 448 |
| `src/lib/xpost_interpreter.c` | 1136 | 81.34% | 59.86% | 212 |
| `src/lib/xpost_dev_generic.c` | 1598 | 86.92% | 69.81% | 209 |
| `src/lib/xpost_font.c` | 606 | 67.49% | 46.43% | 197 |
| `src/lib/xpost_dsc_parse.c` | 587 | 70.70% | 45.18% | 172 |
| `src/lib/xpost_op_file.c` | 950 | 86.84% | 66.07% | 125 |
| `src/lib/xpost_op_path.c` | 1026 | 89.86% | 70.59% | 104 |
| `src/lib/xpost_op_token.c` | 670 | 84.63% | 78.69% | 103 |
| `src/lib/xpost_operator.c` | 472 | 78.81% | 86.84% | 100 |
| `src/lib/xpost_garbage.c` | 321 | 71.96% | 62.31% | 90 |
| `src/lib/xpost_compat_posix.c` | 204 | 58.33% | 39.44% | 85 |
| `src/lib/xpost_memory.c` | 242 | 65.70% | 50.70% | 83 |
| `src/lib/xpost_dev_xcb.c` | 276 | 71.74% | 52.17% | 78 |
| `src/bin/xpost_main.c` | 266 | 71.43% | 52.75% | 76 |
| `src/lib/xpost_dev_png.c` | 328 | 77.74% | 62.37% | 73 |
| `src/lib/xpost_op_control.c` | 415 | 83.13% | 69.39% | 70 |
| `src/lib/xpost_log.c` | 113 | 46.02% | 28.30% | 61 |
| `src/lib/xpost_dev_raster.c` | 317 | 80.76% | 67.04% | 61 |
| `src/lib/xpost_dev_jpeg.c` | 242 | 75.21% | 57.94% | 60 |
| `src/lib/xpost_free.c` | 138 | 61.59% | 48.81% | 53 |
| `src/lib/xpost_dict.c` | 368 | 85.60% | 77.83% | 53 |
| `src/lib/xpost_op_dict.c` | 304 | 83.88% | 67.74% | 49 |
| `src/lib/xpost_context.c` | 175 | 72.57% | 56.41% | 48 |
| `src/lib/xpost_object.c` | 124 | 62.10% | 53.33% | 47 |
| `src/lib/xpost_dev_bgr.c` | 175 | 78.86% | 58.18% | 37 |
| `src/lib/xpost_op_array.c` | 243 | 85.60% | 71.26% | 35 |
| `src/lib/xpost_oplib.c` | 73 | 54.79% | 50.00% | 33 |
| `src/lib/xpost_op_string.c` | 213 | 84.98% | 66.43% | 32 |
| `src/lib/xpost_name.c` | 157 | 80.89% | 67.95% | 30 |
| `src/lib/xpost_save.c` | 123 | 78.86% | 63.64% | 26 |
| `src/lib/xpost_op_type.c` | 307 | 91.53% | 68.73% | 26 |
| `src/lib/xpost_garbage_diag.c` | 125 | 83.20% | 68.63% | 21 |
| `src/lib/xpost_op_misc.c` | 191 | 90.05% | 68.42% | 19 |
| `src/lib/xpost_stack.c` | 151 | 88.08% | 77.94% | 18 |
| `src/lib/xpost_op_context.c` | 77 | 81.82% | 58.82% | 14 |
| `src/lib/xpost_op_stack.c` | 109 | 88.99% | 67.65% | 12 |
| `src/lib/xpost_dsc_file.c` | 47 | 76.60% | 61.54% | 11 |
| `src/lib/xpost_op_param.c` | 61 | 83.61% | 58.33% | 10 |
| `src/lib/xpost_string.c` | 64 | 85.94% | 63.89% | 9 |
| `src/lib/xpost_op_matrix.c` | 255 | 96.47% | 64.52% | 9 |
| `src/lib/xpost_main.c` | 41 | 78.05% | 54.17% | 9 |
| `src/lib/xpost_array.c` | 56 | 89.29% | 80.56% | 6 |
| `src/lib/xpost_op_math.c` | 187 | 97.33% | 59.57% | 5 |
| `src/lib/xpost_op_save.c` | 90 | 95.56% | 76.92% | 4 |
| `src/lib/xpost_op_packedarray.c` | 33 | 90.91% | 65.00% | 3 |
| `src/lib/xpost_compat.c` | 33 | 90.91% | 60.00% | 3 |
| `src/lib/xpost_op_boolean.c` | 104 | 98.08% | 60.87% | 2 |
| `src/lib/xpost_matrix.c` | 42 | 100.00% | -- | 0 |

## Functions the suite never enters

The blind spots: 32 functions nothing in the suite reaches.

(A further 26 are defined in headers, so every object that includes
one carries its own copy and the copies that are not called read as
zero. They are listed at the end rather than here.)

**`src/bin/xpost_main.c`**

- `_xpost_main_list_add`

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`
- `_JPEGFatalErrorHandler`

**`src/lib/xpost_dev_xcb.c`**

- `_fillpoly`

**`src/lib/xpost_file.c`**

- `a85_flush`
- `a85_writech`
- `dctenc_error_exit`
- `dctenc_output_message`
- `dct_output_message`
- `dct_skip_input_data`
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
- `rsd_unreadch`

**`src/lib/xpost_font.c`**

- `_outline_conicto`
- `program_take`
- `_strike_resample`
- `_xpost_glyph_name_to_unicode`

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
- `xpost_font_face_free` (in `src/lib/xpost_font.c`)
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
