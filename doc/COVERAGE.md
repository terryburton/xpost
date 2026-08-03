# Test coverage

How much of the C sources the test suite executes. Regenerate with
`tools/coverage.sh > doc/COVERAGE.md` (needs gcov; takes a few minutes,
since it builds an instrumented tree and runs the whole suite in it).

Coverage is a floor, not a score: a covered line is one that ran, not one
whose behaviour anything asserted. Read the second table as the list of
places where there is nothing to argue about.

**79.0% of 17215 lines**, across 49 files.

## By file, most uncovered lines first

Uncovered lines, not percentage, is what picks the next thing to test: a
small file at 50% hides less than a large one at 85%.

| File | Covered | Lines | Uncovered |
|---|---:|---:|---:|
| `src/lib/xpost_file.c` | 74.89% | 2198 | 552 |
| `src/lib/xpost_op_font.c` | 79.88% | 2008 | 404 |
| `src/lib/xpost_dev_generic.c` | 86.67% | 1403 | 187 |
| `src/lib/xpost_interpreter.c` | 82.21% | 1040 | 185 |
| `src/lib/xpost_dsc_parse.c` | 70.70% | 587 | 172 |
| `src/lib/xpost_font.c` | 71.23% | 570 | 164 |
| `src/lib/xpost_op_file.c` | 84.82% | 896 | 136 |
| `src/lib/xpost_op_token.c` | 82.64% | 674 | 117 |
| `src/lib/xpost_garbage.c` | 67.87% | 333 | 107 |
| `src/lib/xpost_op_path.c` | 89.80% | 980 | 100 |
| `src/lib/xpost_memory.c` | 59.67% | 243 | 98 |
| `src/lib/xpost_op_control.c` | 74.23% | 357 | 92 |
| `src/lib/xpost_operator.c` | 77.75% | 400 | 89 |
| `src/lib/xpost_compat_posix.c` | 58.33% | 204 | 85 |
| `src/lib/xpost_dev_raster.c` | 63.56% | 225 | 82 |
| `src/lib/xpost_dev_xcb.c` | 73.54% | 291 | 77 |
| `src/bin/xpost_main.c` | 71.43% | 259 | 74 |
| `src/lib/xpost_dev_png.c` | 77.60% | 308 | 69 |
| `src/lib/xpost_op_dict.c` | 79.60% | 299 | 61 |
| `src/lib/xpost_log.c` | 46.02% | 113 | 61 |
| `src/lib/xpost_context.c` | 65.91% | 176 | 60 |
| `src/lib/xpost_op_array.c` | 76.02% | 246 | 59 |
| `src/lib/xpost_free.c` | 63.40% | 153 | 56 |
| `src/lib/xpost_object.c` | 57.48% | 127 | 54 |
| `src/lib/xpost_op_string.c` | 75.23% | 214 | 53 |
| `src/lib/xpost_dict.c` | 84.59% | 344 | 53 |
| `src/lib/xpost_op_misc.c` | 71.98% | 182 | 51 |
| `src/lib/xpost_dev_jpeg.c` | 73.98% | 196 | 51 |
| `src/lib/xpost_save.c` | 75.76% | 132 | 32 |
| `src/lib/xpost_dev_bgr.c` | 79.26% | 135 | 28 |
| `src/lib/xpost_stack.c` | 81.88% | 149 | 27 |
| `src/lib/xpost_op_type.c` | 90.49% | 284 | 27 |
| `src/lib/xpost_name.c` | 83.44% | 157 | 26 |
| `src/lib/xpost_garbage_diag.c` | 84.43% | 122 | 19 |
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

The blind spots: 67 functions nothing in the suite reaches.

**`src/lib/xpost_compat.c`**

- `xpost_isatty`

**`src/lib/xpost_compat_posix.c`**

- `xpost_fd_realpath`
- `xpost_renameat_beneath`

**`src/lib/xpost_context.c`**

- `xpost_context_dump`
- `xpost_context_fork1`
- `xpost_context_fork2`

**`src/lib/xpost_dev_jpeg.c`**

- `_JPEGErrorHandler`
- `_JPEGErrorHandler2`
- `_JPEGFatalErrorHandler`
- `xpost_dev_jpeg_options_set`

**`src/lib/xpost_dev_png.c`**

- `xpost_dev_png_options_set`

**`src/lib/xpost_dev_xcb.c`**

- `_fillpoly`
- `_getpix`

**`src/lib/xpost_dict.c`**

- `xpost_dict_dump_memory`

**`src/lib/xpost_file.c`**

- `a85_flush`
- `a85_purge`
- `a85_writech`
- `dctenc_empty_output_buffer`
- `dctenc_error_exit`
- `dctenc_output_message`
- `dct_error_exit`
- `dct_output_message`
- `dct_skip_input_data`
- `enc_readch`
- `enc_unreadch`
- `fax_close`
- `filter_flush`
- `filter_writech`
- `memory_flush`
- `memory_purge`
- `memory_seek`
- `memory_tell`
- `memory_writech`
- `rsd_flush`
- `xpost_strbuf_free`

**`src/lib/xpost_font.c`**

- `_strike_resample`
- `xpost_font_face_free`
- `xpost_font_face_glyph_index_get`
- `_xpost_glyph_name_to_unicode`

**`src/lib/xpost_free.c`**

- `_dump_chain`
- `xpost_free_dump`

**`src/lib/xpost_interpreter.c`**

- `evalquit`
- `xpost_interpreter_exit`

**`src/lib/xpost_log.c`**

- `xpost_log_print_cb_set`
- `xpost_log_print_cb_stdout`

**`src/lib/xpost_memory.c`**

- `xpost_memory_file_dump`
- `xpost_memory_table_dump`
- `xpost_memory_table_dump_ent`
- `xpost_memory_table_get_mark`
- `xpost_memory_table_set_addr`
- `xpost_memory_table_set_mark`
- `xpost_memory_table_set_size`
- `xpost_memory_table_set_tag`

**`src/lib/xpost_object.c`**

- `xpost_object_install_file_get_access`
- `xpost_object_install_file_set_access`

**`src/lib/xpost_op_context.c`**

- `_i_am_free_`
- `_i_am_zombie_`

**`src/lib/xpost_op_dict.c`**

- `xpost_op_cleardictstack`

**`src/lib/xpost_operator.c`**

- `_stack_number_number`
- `xpost_operator_dump`

**`src/lib/xpost_op_font.c`**

- `xpost_object_is_exe`

**`src/lib/xpost_oplib.c`**

- `xpost_op_breakhere`

**`src/lib/xpost_op_misc.c`**

- `debugloadoff`
- `debugloadon`
- `dumpvm`
- `Odumpnames`

**`src/lib/xpost_stack.c`**

- `xpost_stack_dump`
