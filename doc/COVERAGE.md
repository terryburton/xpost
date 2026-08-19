# Test coverage (small-object build)

How much of the C sources the test suite executes, and what it never
makes them do. Regenerate with

    meson setup bcov -Db_coverage=true
    tools/coverage.sh bcov full > doc/COVERAGE.md

(needs gcov; takes a few minutes, since it builds an instrumented tree
and runs the profile in it. The setup line is not optional: only a
default `bcov` is configured on the caller's behalf, and a directory
named for some other configuration is refused rather than quietly given
this one.)

## What was measured

The **full** profile: 387 of the tests defined in that build, all of
which passed (386 ok, 0 failed). A coverage report over a run with
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
the **small-object** build. Code chosen at build time for anything else --
the Windows halves of the compatibility layer, the portable path
confinement used where the kernel has no openat2, and the large-object
half of every WANT_LARGE_OBJECT alternative -- cannot run here and reads
as uncovered whatever the other CI lanes do with it.

## The two numbers

**82.6% of 25260 lines** and **66.6% of 18751 branch outcomes**, across
60 files. 6261 outcomes are never taken.

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

- `src/lib/xpost_interpreter.c:554`, 16,848,171,944 evaluations, 1 of 4 outcomes never taken -- `if (ctx->gl && ctx->gl->garbage_collect_pending)`
- `src/lib/xpost_interpreter.c:549`, 16,848,171,944 evaluations, 1 of 4 outcomes never taken -- `if (ctx->lo && ctx->lo->garbage_collect_pending)`
- `src/lib/xpost_interpreter.c:790`, 14,118,180,992 evaluations, 2 of 4 outcomes never taken -- `if (btype == invalidtype || btype >= XPOST_OBJECT_NTYPES)`
- `src/lib/xpost_interpreter.c:780`, 14,118,180,992 evaluations, 1 of 2 outcomes never taken -- `if (abase)`
- `src/lib/xpost_interpreter.c:773`, 14,118,180,992 evaluations, 1 of 2 outcomes never taken -- `if (ctx->lo->push_refused)`
- `src/lib/xpost_interpreter.c:749`, 14,118,180,992 evaluations, 1 of 2 outcomes never taken -- `if (ctx->quit)`
- `src/lib/xpost_interpreter.c:903`, 5,974,797,574 evaluations, 1 of 4 outcomes never taken -- `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot...`
- `src/lib/xpost_interpreter.c:1207`, 5,303,847,800 evaluations, 2 of 4 outcomes never taken -- `slot.comp_.off != off + 1 ||`

**Global VM is barely exercised.** Two banks of memory, one of them
tested. The conditions that ask which bank an object is in, and
never once this run came out saying the global one:

- `src/lib/xpost_garbage.c:1184` -- `if (isglobal)`
- `src/lib/xpost_garbage.c:1361` -- `Xpost_Memory_File *globalmem = isglobal ? mem : other;`
- `src/lib/xpost_garbage.c:1360` -- `Xpost_Memory_File *localmem  = isglobal ? other : mem;`
- `src/lib/xpost_garbage.c:1347` -- `if (!isglobal && getenv("XPOST_GC_XBANK_CHECK") && ctx ...`

**The command line, where nothing enters it.** These are in the
binary rather than the library, so what they carry is an option the
usage text offers and the suite never takes up:

- `_xpost_view_license` (in `src/bin/xpost_view.c`)
- `xpost_view_page_change` (in `src/bin/xpost_view.c`)
- `_xpost_view_page_set` (in `src/bin/xpost_view.c`)
- `_xpost_view_version` (in `src/bin/xpost_view.c`)
- `xpost_view_main_loop` (in `src/bin/xpost_view_xcb.c`)
- `xpost_view_page_display` (in `src/bin/xpost_view_xcb.c`)
- `xpost_view_win_del` (in `src/bin/xpost_view_xcb.c`)
- `xpost_view_win_new` (in `src/bin/xpost_view_xcb.c`)

**Functions nothing in the suite enters.** 42 of them, listed in full
further down. A function nothing reaches is not partly tested, and
where they cluster is where the suite stops short:

- `src/lib/xpost_file.c`: 18
- `src/bin/xpost_view_xcb.c`: 4
- `src/bin/xpost_view.c`: 4
- `src/lib/xpost_operator.c`: 2
- `src/lib/xpost_op_context.c`: 2
- `src/lib/xpost_dev_record.c`: 2

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

- `src/lib/xpost_file.c`: 449 lines never executed (branch 73.73%)
- `src/lib/xpost_op_font.c`: 422 lines never executed (branch 63.45%)
- `src/lib/xpost_dev_record.c`: 313 lines never executed (branch 62.81%)
- `src/lib/xpost_record.c`: 278 lines never executed (branch 65.66%)

and the lowest branch coverage outside it:

- `src/bin/xpost_view_xcb.c`: 0.00% of branch outcomes, over 103 lines
- `src/bin/xpost_view.c`: 11.54% of branch outcomes, over 118 lines
- `src/lib/xpost_dsc_parse.c`: 48.00% of branch outcomes, over 587 lines

These are last here rather than first because size is the one thing
about a gap that says nothing about what it costs.

## Conditions whose refusing side nothing takes

3993 conditions are reached by the suite and have an outcome it never
produces, 3879 of them outside the discount above. The count is how many
times the condition was evaluated, so the top of this list is code the
suite leans on constantly without ever testing what it is there for.
This table is the measurement rather than the reading of it, so the
discount is not applied here. The 40 most-evaluated:

| Evaluations | Site | Never taken | Condition |
|---:|---|---:|---|
| 16,848,171,944 | `src/lib/xpost_interpreter.c:554` | 1 of 4 | `if (ctx->gl && ctx->gl->garbage_collect_pending)` |
| 16,848,171,944 | `src/lib/xpost_interpreter.c:549` | 1 of 4 | `if (ctx->lo && ctx->lo->garbage_collect_pending)` |
| 14,118,180,992 | `src/lib/xpost_interpreter.c:790` | 2 of 4 | `if (btype == invalidtype \|\| btype >= XPOST_OBJECT_NTYPES)` |
| 14,118,180,992 | `src/lib/xpost_interpreter.c:780` | 1 of 2 | `if (abase)` |
| 14,118,180,992 | `src/lib/xpost_interpreter.c:773` | 1 of 2 | `if (ctx->lo->push_refused)` |
| 14,118,180,992 | `src/lib/xpost_interpreter.c:749` | 1 of 2 | `if (ctx->quit)` |
| 6,239,164,602 | `src/lib/xpost_memory.c:1481` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 5,974,797,574 | `src/lib/xpost_interpreter.c:903` | 1 of 4 | `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot >= 1)` |
| 5,303,847,800 | `src/lib/xpost_interpreter.c:1207` | 2 of 4 | `slot.comp_.off != off + 1 \|\|` |
| 4,397,649,361 | `src/lib/xpost_stack.c:294` | 1 of 2 | `if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* fin...` |
| 3,654,183,091 | `src/lib/xpost_interpreter.c:1173` | 11 of 30 | `EVALARRAY_SYNC_SLOT();` |
| 3,532,579,950 | `src/lib/xpost_object.c:222` | 1 of 4 | `if (type == dicttype && xpost_object_dict_get_access)` |
| 3,511,365,899 | `src/lib/xpost_dict.c:595` | 1 of 4 | `if (xpost_object_get_type(a) == nametype &&` |
| 3,394,157,483 | `src/lib/xpost_interpreter.c:1183` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 3,258,020,863 | `src/lib/xpost_operator.c:1270` | 1 of 4 | `if (!sp[i].fp && (xpost_object_get_type(op.proc) == arrayty...` |
| 3,231,252,817 | `src/lib/xpost_operator.c:874` | 1 of 2 | `assert(n < XPOST_STACK_SEGMENT_SIZE);` |
| 3,231,252,817 | `src/lib/xpost_operator.c:1297` | 2 of 10 | `switch(sp[i].in)` |
| 3,225,930,762 | `src/lib/xpost_dict.c:246` | 1 of 2 | `if (xpost_object_is_composite(k))` |
| 3,057,446,624 | `src/lib/xpost_handle.c:108` | 1 of 2 | `if (!xpost_memory_get(mem, ent, 0, sizeof(index), &index))` |
| 3,056,537,461 | `src/lib/xpost_handle.c:112` | 1 of 4 | `if ((_slots[index].mem != mem) \|\| (_slots[index].ent != ent))` |
| 3,045,046,397 | `src/lib/xpost_memory.c:1456` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 3,045,023,911 | `src/lib/xpost_file.c:5922` | 1 of 4 | `if (!xpost_memory_table_get_tag(mem, f.mark_.padw, &tag) \|\|...` |
| 3,045,023,907 | `src/lib/xpost_handle.c:171` | 2 of 6 | `if (!slot \|\| (slot->kind != kind) \|\| (slot->size != size))` |
| 2,729,990,935 | `src/lib/xpost_interpreter.c:1507` | 2 of 4 | `if (type == invalidtype \|\| type >= XPOST_OBJECT_NTYPES)` |
| 2,729,990,935 | `src/lib/xpost_interpreter.c:1493` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 2,729,986,082 | `src/lib/xpost_interpreter.c:592` | 1 of 4 | `if (ctx->gl && ctx->gl->compact_pending)` |
| 2,729,986,082 | `src/lib/xpost_interpreter.c:587` | 1 of 4 | `if (ctx->lo && ctx->lo->compact_pending)` |
| 2,729,986,082 | `src/lib/xpost_interpreter.c:2021` | 1 of 2 | `if (ctx->lo->push_refused)` |
| 2,651,923,900 | `src/lib/xpost_interpreter.c:1206` | 1 of 2 | `slot.comp_.sz != remaining - 1 \|\|` |
| 2,651,923,900 | `src/lib/xpost_interpreter.c:1205` | 1 of 2 | `if (slot.tag != a.comp_.tag \|\|` |
| 2,261,867,100 | `src/lib/xpost_object.c:224` | 1 of 4 | `else if (type == filetype && xpost_object_file_get_access)` |
| 2,241,503,324 | `src/lib/xpost_memory.c:1504` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 1,917,811,791 | `src/lib/xpost_array.c:213` | 1 of 4 | `if (i < 0 \|\| (unsigned int)i >= a.comp_.sz)` |
| 1,917,811,700 | `src/lib/xpost_array.c:219` | 1 of 2 | `if (!ret)` |
| 1,753,417,679 | `src/lib/xpost_interpreter.c:742` | 2 of 4 | `EVALARRAY_RESOLVE_ABASE();` |
| 1,745,837,134 | `src/lib/xpost_dict.c:599` | 1 of 2 | `a.mark_.padw == b.mark_.padw;` |
| 1,650,099,726 | `src/lib/xpost_dict.c:633` | 1 of 2 | `if (!dp)` |
| 1,650,099,726 | `src/lib/xpost_dict.c:629` | 1 of 2 | `if (xpost_object_get_type(k) == invalidtype)` |
| 1,572,734,118 | `src/lib/xpost_garbage.c:342` | 1 of 2 | `if (!mem) return 0;` |
| 1,500,444,071 | `src/lib/xpost_op_token.c:798` | 2 of 6 | `} while(c != '\n' && c != '\f' && c != EOF);` |

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Lines | Line % | Branch % | Uncovered |
|---|---:|---:|---:|---:|
| `src/lib/xpost_file.c` | 2704 | 83.39% | 73.73% | 449 |
| `src/lib/xpost_op_font.c` | 2504 | 83.15% | 63.45% | 422 |
| `src/lib/xpost_dev_record.c` | 1504 | 79.19% | 62.81% | 313 |
| `src/lib/xpost_record.c` | 1255 | 77.85% | 65.66% | 278 |
| `src/lib/xpost_interpreter.c` | 1517 | 83.59% | 63.72% | 249 |
| `src/lib/xpost_dev_generic.c` | 1788 | 87.08% | 70.81% | 231 |
| `src/lib/xpost_dsc_parse.c` | 587 | 73.42% | 48.00% | 156 |
| `src/lib/xpost_font.c` | 647 | 77.43% | 59.11% | 146 |
| `src/lib/xpost_op_file.c` | 1029 | 86.49% | 64.27% | 139 |
| `src/lib/xpost_vm_image.c` | 591 | 77.50% | 62.11% | 133 |
| `src/lib/xpost_op_path.c` | 1120 | 89.82% | 69.97% | 114 |
| `src/bin/xpost_view_xcb.c` | 103 | 0.00% | 0.00% | 103 |
| `src/lib/xpost_op_token.c` | 713 | 85.83% | 80.76% | 101 |
| `src/lib/xpost_operator.c` | 541 | 81.33% | 87.34% | 101 |
| `src/lib/xpost_memory.c` | 362 | 73.48% | 57.14% | 96 |
| `src/lib/xpost_compat_posix.c` | 231 | 59.31% | 50.00% | 94 |
| `src/bin/xpost_view.c` | 118 | 23.73% | 11.54% | 90 |
| `src/lib/xpost_garbage.c` | 459 | 80.61% | 69.90% | 89 |
| `src/lib/xpost_log.c` | 122 | 38.52% | 22.03% | 75 |
| `src/lib/xpost_dev_xcb.c` | 343 | 79.59% | 59.22% | 70 |
| `src/lib/xpost_dev_png.c` | 414 | 84.30% | 69.67% | 65 |
| `src/lib/xpost_dev_jpeg.c` | 329 | 83.89% | 69.74% | 53 |
| `src/lib/xpost_context.c` | 228 | 76.75% | 59.18% | 53 |
| `src/lib/xpost_op_control.c` | 393 | 88.55% | 70.49% | 45 |
| `src/lib/xpost_free.c` | 202 | 77.72% | 68.57% | 45 |
| `src/bin/xpost_main.c` | 448 | 89.96% | 73.68% | 45 |
| `src/bin/xpost_dsc.c` | 84 | 50.00% | 54.55% | 42 |
| `src/lib/xpost_dict.c` | 363 | 88.71% | 82.14% | 41 |
| `src/lib/xpost_op_dict.c` | 302 | 86.75% | 67.43% | 40 |
| `src/lib/xpost_name.c` | 214 | 82.71% | 69.57% | 37 |
| `src/lib/xpost_object.c` | 130 | 72.31% | 60.00% | 36 |
| `src/lib/xpost_oplib.c` | 75 | 54.67% | 50.00% | 34 |
| `src/lib/xpost_dev_bgr.c` | 187 | 81.82% | 62.07% | 34 |
| `src/lib/xpost_op_param.c` | 214 | 85.51% | 64.12% | 31 |
| `src/lib/xpost_op_array.c` | 242 | 87.19% | 69.79% | 31 |
| `src/lib/xpost_dev_raster.c` | 286 | 89.51% | 72.39% | 30 |
| `src/lib/xpost_op_string.c` | 211 | 86.73% | 65.38% | 28 |
| `src/lib/xpost_op_misc.c` | 220 | 87.73% | 64.52% | 27 |
| `src/lib/xpost_op_type.c` | 358 | 93.58% | 72.53% | 23 |
| `src/lib/xpost_garbage_diag.c` | 127 | 82.68% | 68.87% | 22 |
| `src/lib/xpost_spill.c` | 99 | 79.80% | 56.58% | 20 |
| `src/lib/xpost_save.c` | 126 | 84.13% | 75.00% | 20 |
| `src/lib/xpost_handle.c` | 140 | 85.71% | 68.63% | 20 |
| `src/lib/xpost_op_context.c` | 82 | 82.93% | 58.00% | 14 |
| `src/lib/xpost_main.c` | 62 | 77.42% | 61.76% | 14 |
| `src/lib/xpost_op_matrix.c` | 271 | 95.20% | 62.64% | 13 |
| `src/lib/xpost_dev_driver.c` | 62 | 79.03% | 76.36% | 13 |
| `src/lib/xpost_stack.c` | 184 | 94.02% | 86.67% | 11 |
| `src/lib/xpost_dsc_file.c` | 47 | 78.72% | 65.38% | 10 |
| `src/lib/xpost_op_stack.c` | 107 | 91.59% | 67.05% | 9 |
| `src/lib/xpost_string.c` | 71 | 88.73% | 70.00% | 8 |
| `src/lib/xpost_span.c` | 159 | 94.97% | 94.23% | 8 |
| `src/lib/xpost_op_save.c` | 110 | 93.64% | 76.25% | 7 |
| `src/lib/xpost_op_math.c` | 188 | 97.34% | 56.41% | 5 |
| `src/lib/xpost_array.c` | 59 | 91.53% | 80.95% | 5 |
| `src/lib/xpost_strbuf.c` | 14 | 85.71% | 50.00% | 2 |
| `src/lib/xpost_op_packedarray.c` | 33 | 93.94% | 65.38% | 2 |
| `src/lib/xpost_op_boolean.c` | 106 | 98.11% | 58.75% | 2 |
| `src/lib/xpost_compat.c` | 33 | 96.97% | 60.00% | 1 |
| `src/lib/xpost_matrix.c` | 42 | 100.00% | -- | 0 |

## Functions the suite never enters

The blind spots: 42 functions nothing in the suite reaches.

(A further 25 are defined in headers, so every object that includes
one carries its own copy and the copies that are not called read as
zero. They are listed at the end rather than here.)

**`src/bin/xpost_view.c`**

- `_xpost_view_license`
- `xpost_view_page_change`
- `_xpost_view_page_set`
- `_xpost_view_version`

**`src/bin/xpost_view_xcb.c`**

- `xpost_view_main_loop`
- `xpost_view_page_display`
- `xpost_view_win_del`
- `xpost_view_win_new`

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`

**`src/lib/xpost_dev_record.c`**

- `_lost`
- `_place_ops`

**`src/lib/xpost_file.c`**

- `a85_writech`
- `dctenc_output_message`
- `enc_readch`
- `enc_unreadch`
- `_filter_cons_abandon`
- `filter_writech`
- `memory_flush`
- `memory_purge`
- `memory_seek`
- `memory_tell`
- `memory_writech`
- `proc_purge`
- `proc_seek`
- `procsrc_writech`
- `proc_tell`
- `proctgt_flush`
- `proctgt_readch`
- `proc_unreadch`

**`src/lib/xpost_font.c`**

- `_strike_resample`

**`src/lib/xpost_free.c`**

- `_dump_chain`

**`src/lib/xpost_interpreter.c`**

- `evalquit`

**`src/lib/xpost_log.c`**

- `_xpost_log_dump`

**`src/lib/xpost_main.c`**

- `_init_gave_up`

**`src/lib/xpost_op_context.c`**

- `_i_am_free_`
- `_i_am_zombie_`

**`src/lib/xpost_operator.c`**

- `_stack_none`
- `_stack_number_number`

**`src/lib/xpost_op_file.c`**

- `xpost_op_proc_filter_int`

**`src/lib/xpost_record.c`**

- `_spill_rows`

**`src/lib/xpost_span.c`**

- `_bandspancomp`

## Header-defined functions with an uncalled copy

Not blind spots: each is compiled into every object that includes its
header, and only the copies nothing calls are counted here.

- `xpost_mkstemp` (in `src/lib/xpost_compat_posix.c`)
- `xpost_renameat_beneath` (in `src/lib/xpost_compat_posix.c`)
- `xpost_context_dump` (in `src/lib/xpost_context.c`)
- `xpost_dev_jpeg_options_set` (in `src/lib/xpost_dev_jpeg.c`)
- `xpost_dev_png_options_set` (in `src/lib/xpost_dev_png.c`)
- `xpost_strbuf_free` (in `src/lib/xpost_file.c`)
- `xpost_font_face_glyph_index_get` (in `src/lib/xpost_font.c`)
- `xpost_mask_cache_clear` (in `src/lib/xpost_font.c`)
- `xpost_free_dump` (in `src/lib/xpost_free.c`)
- `xpost_interpreter_exit` (in `src/lib/xpost_interpreter.c`)
- `xpost_log_print_cb_set` (in `src/lib/xpost_log.c`)
- `xpost_log_print_cb_stdout` (in `src/lib/xpost_log.c`)
- `xpost_memory_table_dump_ent` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_get_mark` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_addr` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_mark` (in `src/lib/xpost_memory.c`)
- `xpost_memory_table_set_size` (in `src/lib/xpost_memory.c`)
- `xpost_memory_name_stack_ent` (in `src/lib/xpost_op_dict.c`)
- `xpost_operator_dump` (in `src/lib/xpost_operator.c`)
- `xpost_object_is_exe` (in `src/lib/xpost_op_font.c`)
- `xpost_op_breakhere` (in `src/lib/xpost_oplib.c`)
- `xpost_record_release` (in `src/lib/xpost_record.c`)
- `xpost_record_spent` (in `src/lib/xpost_record.c`)
- `xpost_vm_image_bank_field_name` (in `src/lib/xpost_vm_image.c`)
- `xpost_vm_image_row_field_name` (in `src/lib/xpost_vm_image.c`)
