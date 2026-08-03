# Test coverage

How much of the C sources the test suite executes. Regenerate with
`tools/coverage.sh > doc/COVERAGE.md` (needs gcov; takes a few minutes,
since it builds an instrumented tree and runs the whole suite in it).

Coverage is a floor, not a score: a covered line is one that ran, not one
whose behaviour anything asserted. Read the second table as the list of
places where there is nothing to argue about.

These numbers are one platform. Code chosen at build time for another --
the Windows halves of the compatibility layer, and the portable path
confinement used where the kernel has no openat2 -- cannot run here and
reads as uncovered whatever the other CI lanes do with it.

**80.7% of 17228 lines**, across 49 files.

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Covered | Lines | Uncovered |
|---|---:|---:|---:|
| `src/lib/xpost_file.c` | 78.40% | 2204 | 476 |
| `src/lib/xpost_op_font.c` | 80.63% | 2008 | 389 |
| `src/lib/xpost_dev_generic.c` | 86.67% | 1403 | 187 |
| `src/lib/xpost_interpreter.c` | 82.50% | 1040 | 182 |
| `src/lib/xpost_dsc_parse.c` | 70.70% | 587 | 172 |
| `src/lib/xpost_font.c` | 71.05% | 570 | 165 |
| `src/lib/xpost_op_file.c` | 84.93% | 896 | 135 |
| `src/lib/xpost_garbage.c` | 67.76% | 335 | 108 |
| `src/lib/xpost_op_token.c` | 84.42% | 674 | 105 |
| `src/lib/xpost_op_path.c` | 89.69% | 980 | 101 |
| `src/lib/xpost_operator.c` | 78.50% | 400 | 86 |
| `src/lib/xpost_memory.c` | 65.02% | 243 | 85 |
| `src/lib/xpost_compat_posix.c` | 58.33% | 204 | 85 |
| `src/lib/xpost_dev_xcb.c` | 73.54% | 291 | 77 |
| `src/bin/xpost_main.c` | 71.81% | 259 | 73 |
| `src/lib/xpost_dev_png.c` | 77.60% | 308 | 69 |
| `src/lib/xpost_op_control.c` | 82.91% | 357 | 61 |
| `src/lib/xpost_log.c` | 46.02% | 113 | 61 |
| `src/lib/xpost_context.c` | 65.91% | 176 | 60 |
| `src/lib/xpost_free.c` | 63.40% | 153 | 56 |
| `src/lib/xpost_dev_raster.c` | 75.44% | 228 | 56 |
| `src/lib/xpost_dev_jpeg.c` | 73.98% | 196 | 51 |
| `src/lib/xpost_op_dict.c` | 83.28% | 299 | 50 |
| `src/lib/xpost_object.c` | 62.20% | 127 | 48 |
| `src/lib/xpost_dict.c` | 87.50% | 344 | 43 |
| `src/lib/xpost_op_array.c` | 84.15% | 246 | 39 |
| `src/lib/xpost_op_string.c` | 84.58% | 214 | 33 |
| `src/lib/xpost_save.c` | 75.76% | 132 | 32 |
| `src/lib/xpost_dev_bgr.c` | 79.26% | 135 | 28 |
| `src/lib/xpost_op_type.c` | 90.49% | 284 | 27 |
| `src/lib/xpost_name.c` | 83.44% | 157 | 26 |
| `src/lib/xpost_garbage_diag.c` | 84.43% | 122 | 19 |
| `src/lib/xpost_stack.c` | 89.26% | 149 | 16 |
| `src/lib/xpost_op_misc.c` | 92.93% | 184 | 13 |
| `src/lib/xpost_op_context.c` | 82.89% | 76 | 13 |
| `src/lib/xpost_op_stack.c` | 89.29% | 112 | 12 |
| `src/lib/xpost_op_param.c` | 78.18% | 55 | 12 |
| `src/lib/xpost_dsc_file.c` | 76.60% | 47 | 11 |
| `src/lib/xpost_string.c` | 85.00% | 60 | 9 |
| `src/lib/xpost_main.c` | 78.05% | 41 | 9 |
| `src/lib/xpost_op_matrix.c` | 96.86% | 255 | 8 |
| `src/lib/xpost_oplib.c` | 87.76% | 49 | 6 |
| `src/lib/xpost_array.c` | 89.29% | 56 | 6 |
| `src/lib/xpost_op_save.c` | 93.24% | 74 | 5 |
| `src/lib/xpost_op_math.c` | 97.85% | 186 | 4 |
| `src/lib/xpost_op_packedarray.c` | 90.91% | 33 | 3 |
| `src/lib/xpost_compat.c` | 86.36% | 22 | 3 |
| `src/lib/xpost_op_boolean.c` | 98.04% | 102 | 2 |
| `src/lib/xpost_matrix.c` | 100.00% | 42 | 0 |

## Functions the suite never enters

The blind spots: 29 functions nothing in the suite reaches.

(A further 26 are defined in headers, so every object that includes
one carries its own copy and the copies that are not called read as
zero. They are listed at the end rather than here.)

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`
- `_JPEGFatalErrorHandler`

**`src/lib/xpost_dev_xcb.c`**

- `_fillpoly`
- `_getpix`

**`src/lib/xpost_file.c`**

- `a85_flush`
- `a85_writech`
- `dctenc_empty_output_buffer`
- `dctenc_error_exit`
- `dctenc_output_message`
- `dct_output_message`
- `dct_skip_input_data`
- `enc_readch`
- `enc_unreadch`
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
- `xpost_context_fork1` (in `src/lib/xpost_context.c`)
- `xpost_context_fork2` (in `src/lib/xpost_context.c`)
- `xpost_dev_jpeg_options_set` (in `src/lib/xpost_dev_jpeg.c`)
- `xpost_dev_png_options_set` (in `src/lib/xpost_dev_png.c`)
- `xpost_strbuf_free` (in `src/lib/xpost_file.c`)
- `xpost_font_face_free` (in `src/lib/xpost_font.c`)
- `xpost_font_face_glyph_index_get` (in `src/lib/xpost_font.c`)
- `xpost_free_dump` (in `src/lib/xpost_free.c`)
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
- `xpost_operator_dump` (in `src/lib/xpost_operator.c`)
- `xpost_object_is_exe` (in `src/lib/xpost_op_font.c`)
- `xpost_op_breakhere` (in `src/lib/xpost_oplib.c`)
