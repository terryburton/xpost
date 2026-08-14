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

The **full** profile: 310 of the tests defined in that build, all of
which passed (310 ok, 0 failed). A coverage report over a run with
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

**82.5% of 23930 lines** and **65.7% of 17965 branch outcomes**, across
55 files. 6158 outcomes are never taken.

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

- `src/lib/xpost_interpreter.c:551`, 10,249,798,092 evaluations, 1 of 4 outcomes never taken -- `if (ctx->gl && ctx->gl->garbage_collect_pending)`
- `src/lib/xpost_interpreter.c:546`, 10,249,798,092 evaluations, 1 of 4 outcomes never taken -- `if (ctx->lo && ctx->lo->garbage_collect_pending)`
- `src/lib/xpost_interpreter.c:742`, 8,653,070,733 evaluations, 2 of 4 outcomes never taken -- `if (btype == invalidtype || btype >= XPOST_OBJECT_NTYPES)`
- `src/lib/xpost_interpreter.c:732`, 8,653,070,733 evaluations, 1 of 2 outcomes never taken -- `if (abase)`
- `src/lib/xpost_interpreter.c:725`, 8,653,070,733 evaluations, 1 of 2 outcomes never taken -- `if (ctx->lo->push_refused)`
- `src/lib/xpost_interpreter.c:701`, 8,653,070,733 evaluations, 1 of 2 outcomes never taken -- `if (ctx->quit)`
- `src/lib/xpost_stack.c:134`, 5,319,841,031 evaluations, 1 of 2 outcomes never taken -- `if (xpost_object_get_type(obj) == invalidtype)`
- `src/lib/xpost_interpreter.c:855`, 3,921,251,295 evaluations, 1 of 4 outcomes never taken -- `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot...`

**Global VM is barely exercised.** Two banks of memory, one of them
tested. The conditions that ask which bank an object is in, and
never once this run came out saying the global one:

- `src/lib/xpost_garbage.c:1076` -- `if (isglobal)`
- `src/lib/xpost_garbage.c:1223` -- `Xpost_Memory_File *globalmem = isglobal ? mem : other;`
- `src/lib/xpost_garbage.c:1222` -- `Xpost_Memory_File *localmem  = isglobal ? other : mem;`
- `src/lib/xpost_garbage.c:1209` -- `if (!isglobal && getenv("XPOST_GC_XBANK_CHECK") && ctx ...`

**Functions nothing in the suite enters.** 32 of them, listed in full
further down. A function nothing reaches is not partly tested, and
where they cluster is where the suite stops short:

- `src/lib/xpost_file.c`: 19
- `src/lib/xpost_op_context.c`: 2
- `src/lib/xpost_dev_record.c`: 2
- `src/lib/xpost_dev_jpeg.c`: 2
- `src/lib/xpost_record.c`: 1
- `src/lib/xpost_op_file.c`: 1

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

- `src/lib/xpost_file.c`: 472 lines never executed (branch 71.93%)
- `src/lib/xpost_op_font.c`: 423 lines never executed (branch 62.43%)
- `src/lib/xpost_dev_record.c`: 322 lines never executed (branch 62.34%)
- `src/lib/xpost_record.c`: 275 lines never executed (branch 65.24%)

and the lowest branch coverage outside it:

- `src/lib/xpost_dsc_parse.c`: 45.18% of branch outcomes, over 587 lines
- `src/lib/xpost_oplib.c`: 50.00% of branch outcomes, over 75 lines
- `src/lib/xpost_spill.c`: 53.26% of branch outcomes, over 105 lines

These are last here rather than first because size is the one thing
about a gap that says nothing about what it costs.

## Conditions whose refusing side nothing takes

3932 conditions are reached by the suite and have an outcome it never
produces, 3830 of them outside the discount above. The count is how many
times the condition was evaluated, so the top of this list is code the
suite leans on constantly without ever testing what it is there for.
This table is the measurement rather than the reading of it, so the
discount is not applied here. The 40 most-evaluated:

| Evaluations | Site | Never taken | Condition |
|---:|---|---:|---|
| 10,249,798,092 | `src/lib/xpost_interpreter.c:551` | 1 of 4 | `if (ctx->gl && ctx->gl->garbage_collect_pending)` |
| 10,249,798,092 | `src/lib/xpost_interpreter.c:546` | 1 of 4 | `if (ctx->lo && ctx->lo->garbage_collect_pending)` |
| 8,653,070,733 | `src/lib/xpost_interpreter.c:742` | 2 of 4 | `if (btype == invalidtype \|\| btype >= XPOST_OBJECT_NTYPES)` |
| 8,653,070,733 | `src/lib/xpost_interpreter.c:732` | 1 of 2 | `if (abase)` |
| 8,653,070,733 | `src/lib/xpost_interpreter.c:725` | 1 of 2 | `if (ctx->lo->push_refused)` |
| 8,653,070,733 | `src/lib/xpost_interpreter.c:701` | 1 of 2 | `if (ctx->quit)` |
| 5,319,841,031 | `src/lib/xpost_stack.c:134` | 1 of 2 | `if (xpost_object_get_type(obj) == invalidtype)` |
| 3,921,251,295 | `src/lib/xpost_interpreter.c:855` | 1 of 4 | `if (w == (unsigned int)XPOST_OP_CODE(ctx, optype) && ot >= 1)` |
| 3,688,175,245 | `src/lib/xpost_object.c:215` | 1 of 4 | `if (type == dicttype && xpost_object_dict_get_access)` |
| 3,599,447,731 | `src/lib/xpost_memory.c:1111` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 3,399,964,274 | `src/lib/xpost_interpreter.c:1150` | 2 of 4 | `slot.comp_.off != off + 1 \|\|` |
| 2,891,635,748 | `src/lib/xpost_object.c:217` | 1 of 4 | `else if (type == filetype && xpost_object_file_get_access)` |
| 2,848,640,844 | `src/lib/xpost_stack.c:192` | 1 of 2 | `if (s->prevseg) s = xpost_stack_at(mem, s->prevseg); /* fin...` |
| 2,586,336,267 | `src/lib/xpost_operator.c:1103` | 1 of 4 | `if (!sp[i].fp && (xpost_object_get_type(op.proc) == arrayty...` |
| 2,564,562,401 | `src/lib/xpost_operator.c:737` | 1 of 2 | `assert(n < XPOST_STACK_SEGMENT_SIZE);` |
| 2,564,562,401 | `src/lib/xpost_operator.c:1130` | 1 of 10 | `switch(sp[i].in)` |
| 2,209,910,516 | `src/lib/xpost_interpreter.c:1116` | 11 of 30 | `EVALARRAY_SYNC_SLOT();` |
| 2,175,969,094 | `src/lib/xpost_dict.c:603` | 1 of 4 | `if (xpost_object_get_type(a) == nametype &&` |
| 2,057,078,394 | `src/lib/xpost_interpreter.c:1126` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 1,805,211,178 | `src/lib/xpost_dict.c:246` | 1 of 2 | `if (xpost_object_is_composite(k))` |
| 1,699,982,137 | `src/lib/xpost_interpreter.c:1149` | 1 of 2 | `slot.comp_.sz != remaining - 1 \|\|` |
| 1,699,982,137 | `src/lib/xpost_interpreter.c:1148` | 1 of 2 | `if (slot.tag != a.comp_.tag \|\|` |
| 1,596,727,352 | `src/lib/xpost_interpreter.c:1432` | 2 of 4 | `if (type == invalidtype \|\| type >= XPOST_OBJECT_NTYPES)` |
| 1,596,727,352 | `src/lib/xpost_interpreter.c:1411` | 1 of 2 | `if (_xpost_interpreter_is_tracing)` |
| 1,596,725,780 | `src/lib/xpost_interpreter.c:1945` | 1 of 2 | `if (ctx->lo->push_refused)` |
| 1,559,645,122 | `src/lib/xpost_memory.c:1134` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 1,501,920,678 | `src/lib/xpost_handle.c:108` | 1 of 2 | `if (!xpost_memory_get(mem, ent, 0, sizeof(index), &index))` |
| 1,501,847,907 | `src/lib/xpost_handle.c:112` | 1 of 4 | `if ((_slots[index].mem != mem) \|\| (_slots[index].ent != ent))` |
| 1,468,436,886 | `src/lib/xpost_memory.c:1086` | 1 of 2 | `CHECK_VALID_ENT(ent,mem,0)` |
| 1,468,428,612 | `src/lib/xpost_handle.c:171` | 2 of 6 | `if (!slot \|\| (slot->kind != kind) \|\| (slot->size != size))` |
| 1,468,428,612 | `src/lib/xpost_file.c:5737` | 2 of 4 | `if (!xpost_memory_table_get_tag(mem, f.mark_.padw, &tag) \|\|...` |
| 1,089,777,108 | `src/lib/xpost_string.c:225` | 1 of 4 | `if (i < 0 \|\| i >= s.comp_.sz)` |
| 1,089,777,106 | `src/lib/xpost_string.c:228` | 1 of 2 | `if (!ret)` |
| 1,087,357,405 | `src/lib/xpost_dict.c:607` | 1 of 2 | `a.mark_.padw == b.mark_.padw;` |
| 1,079,533,121 | `src/lib/xpost_string.c:207` | 1 of 2 | `if (!xpost_object_is_writeable(ctx, s))` |
| 1,079,533,121 | `src/lib/xpost_string.c:206` | 1 of 2 | `if (!ctx->gl->interpreter_get_initializing())` |
| 1,079,533,121 | `src/lib/xpost_string.c:190` | 1 of 2 | `if (!ret)` |
| 1,079,533,121 | `src/lib/xpost_string.c:187` | 2 of 4 | `if (i < 0 \|\| i >= s.comp_.sz)` |
| 1,066,779,795 | `src/lib/xpost_dict.c:641` | 1 of 2 | `if (!dp)` |
| 1,066,779,795 | `src/lib/xpost_dict.c:637` | 1 of 2 | `if (xpost_object_get_type(k) == invalidtype)` |

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Lines | Line % | Branch % | Uncovered |
|---|---:|---:|---:|---:|
| `src/lib/xpost_file.c` | 2628 | 82.04% | 71.93% | 472 |
| `src/lib/xpost_op_font.c` | 2405 | 82.41% | 62.43% | 423 |
| `src/lib/xpost_dev_record.c` | 1522 | 78.84% | 62.34% | 322 |
| `src/lib/xpost_record.c` | 1237 | 77.77% | 65.24% | 275 |
| `src/lib/xpost_interpreter.c` | 1485 | 83.50% | 64.64% | 245 |
| `src/lib/xpost_dev_generic.c` | 1598 | 87.73% | 70.70% | 196 |
| `src/lib/xpost_dsc_parse.c` | 587 | 70.70% | 45.18% | 172 |
| `src/lib/xpost_font.c` | 608 | 77.14% | 57.18% | 139 |
| `src/lib/xpost_op_file.c` | 1010 | 86.53% | 64.29% | 136 |
| `src/lib/xpost_vm_image.c` | 577 | 76.60% | 61.23% | 135 |
| `src/lib/xpost_op_path.c` | 1111 | 89.92% | 70.18% | 112 |
| `src/lib/xpost_compat_posix.c` | 213 | 49.77% | 34.72% | 107 |
| `src/lib/xpost_op_token.c` | 711 | 85.79% | 80.58% | 101 |
| `src/lib/xpost_operator.c` | 496 | 80.44% | 87.34% | 97 |
| `src/lib/xpost_dev_png.c` | 469 | 80.17% | 69.18% | 93 |
| `src/lib/xpost_garbage.c` | 409 | 79.95% | 66.67% | 82 |
| `src/lib/xpost_dev_jpeg.c` | 405 | 80.25% | 67.98% | 80 |
| `src/lib/xpost_log.c` | 122 | 38.52% | 22.03% | 75 |
| `src/lib/xpost_memory.c` | 283 | 74.20% | 56.88% | 73 |
| `src/lib/xpost_dev_xcb.c` | 310 | 77.74% | 55.75% | 69 |
| `src/lib/xpost_op_control.c` | 408 | 84.31% | 68.10% | 64 |
| `src/lib/xpost_context.c` | 229 | 76.86% | 59.00% | 53 |
| `src/lib/xpost_object.c` | 130 | 63.85% | 53.33% | 47 |
| `src/lib/xpost_op_dict.c` | 306 | 85.29% | 66.67% | 45 |
| `src/lib/xpost_dict.c` | 363 | 88.71% | 81.25% | 41 |
| `src/lib/xpost_dev_bgr.c` | 210 | 80.48% | 62.50% | 41 |
| `src/lib/xpost_free.c` | 137 | 71.53% | 59.52% | 39 |
| `src/lib/xpost_dev_raster.c` | 312 | 88.46% | 73.06% | 36 |
| `src/lib/xpost_oplib.c` | 75 | 54.67% | 50.00% | 34 |
| `src/lib/xpost_op_array.c` | 244 | 86.07% | 69.19% | 34 |
| `src/bin/xpost_main.c` | 310 | 89.68% | 70.59% | 32 |
| `src/lib/xpost_op_string.c` | 213 | 85.45% | 64.81% | 31 |
| `src/lib/xpost_name.c` | 159 | 81.13% | 67.95% | 30 |
| `src/lib/xpost_op_misc.c` | 213 | 87.79% | 65.10% | 26 |
| `src/lib/xpost_op_param.c` | 172 | 85.47% | 66.43% | 25 |
| `src/lib/xpost_op_type.c` | 315 | 92.70% | 68.04% | 23 |
| `src/lib/xpost_handle.c` | 145 | 84.83% | 68.27% | 22 |
| `src/lib/xpost_garbage_diag.c` | 127 | 82.68% | 67.92% | 22 |
| `src/lib/xpost_spill.c` | 105 | 80.95% | 53.26% | 20 |
| `src/lib/xpost_save.c` | 127 | 84.25% | 70.45% | 20 |
| `src/lib/xpost_op_context.c` | 82 | 82.93% | 58.00% | 14 |
| `src/lib/xpost_op_stack.c` | 108 | 89.81% | 64.77% | 11 |
| `src/lib/xpost_dsc_file.c` | 47 | 76.60% | 61.54% | 11 |
| `src/lib/xpost_string.c` | 71 | 87.32% | 67.50% | 9 |
| `src/lib/xpost_op_matrix.c` | 257 | 96.50% | 61.05% | 9 |
| `src/lib/xpost_main.c` | 41 | 78.05% | 54.17% | 9 |
| `src/lib/xpost_op_save.c` | 109 | 93.58% | 75.64% | 7 |
| `src/lib/xpost_stack.c` | 152 | 96.71% | 91.18% | 5 |
| `src/lib/xpost_span.c` | 117 | 95.73% | 88.18% | 5 |
| `src/lib/xpost_op_math.c` | 188 | 97.34% | 55.77% | 5 |
| `src/lib/xpost_array.c` | 58 | 93.10% | 86.11% | 4 |
| `src/lib/xpost_op_packedarray.c` | 33 | 93.94% | 65.38% | 2 |
| `src/lib/xpost_op_boolean.c` | 106 | 98.11% | 58.75% | 2 |
| `src/lib/xpost_compat.c` | 33 | 96.97% | 60.00% | 1 |
| `src/lib/xpost_matrix.c` | 42 | 100.00% | -- | 0 |

## Functions the suite never enters

The blind spots: 32 functions nothing in the suite reaches.

(A further 28 are defined in headers, so every object that includes
one carries its own copy and the copies that are not called read as
zero. They are listed at the end rather than here.)

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`

**`src/lib/xpost_dev_record.c`**

- `_lost`
- `_place_ops`

**`src/lib/xpost_file.c`**

- `a85_writech`
- `dctenc_error_exit`
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

**`src/lib/xpost_op_context.c`**

- `_i_am_free_`
- `_i_am_zombie_`

**`src/lib/xpost_operator.c`**

- `_stack_number_number`

**`src/lib/xpost_op_file.c`**

- `xpost_op_proc_filter_int`

**`src/lib/xpost_record.c`**

- `_spill_rows`

## Header-defined functions with an uncalled copy

Not blind spots: each is compiled into every object that includes its
header, and only the copies nothing calls are counted here.

- `xpost_fd_realpath` (in `src/lib/xpost_compat_posix.c`)
- `xpost_mkstemp` (in `src/lib/xpost_compat_posix.c`)
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
- `xpost_object_install_file_get_access` (in `src/lib/xpost_object.c`)
- `xpost_object_install_file_set_access` (in `src/lib/xpost_object.c`)
- `xpost_memory_name_stack_adr` (in `src/lib/xpost_op_dict.c`)
- `xpost_operator_dump` (in `src/lib/xpost_operator.c`)
- `xpost_object_is_exe` (in `src/lib/xpost_op_font.c`)
- `xpost_op_breakhere` (in `src/lib/xpost_oplib.c`)
- `xpost_record_release` (in `src/lib/xpost_record.c`)
- `xpost_record_spent` (in `src/lib/xpost_record.c`)
- `xpost_vm_image_bank_field_name` (in `src/lib/xpost_vm_image.c`)
- `xpost_vm_image_row_field_name` (in `src/lib/xpost_vm_image.c`)
