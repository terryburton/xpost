project('xpost', 'c',
  version         : '0.0.1',
  license         : 'BSD 3 clause',
  default_options : [
                      'buildtype=debug',
                      'warning_level=2',
                      'c_std=gnu99'
                    ],
  meson_version   : '>= 1.0.0',
)

v_array = meson.project_version().split('.')
v_maj = v_array[0]
v_min = v_array[1]
v_mic = v_array[2]

# install paths
dir_prefix = get_option('prefix')
dir_include = join_paths(dir_prefix, get_option('includedir'))
dir_pkginclude = join_paths(dir_include, meson.project_name())
dir_bin = join_paths(dir_prefix, get_option('bindir'))
dir_lib = join_paths(dir_prefix, get_option('libdir'))
dir_data = join_paths(dir_prefix, get_option('datadir'))
dir_pkgdata = join_paths(dir_data, meson.project_name())
#dir_locale = join_paths(dir_prefix, get_option('localedir'))
#dir_doc = dir_data / 'doc' / meson.project_name()

# host

host_os = host_machine.system()
win32 = ['windows']

sys_windows = win32.contains(host_os)

# configuration

config_dir = [include_directories('.')]

config_h = configuration_data()

# programs

cc = meson.get_compiler('c')

# Strict warning set matching the autotools build. warning_level=2 already
# supplies -Wall -Wextra; these add the rest of the coverage. Unrecognised
# flags are filtered out per compiler, so the list stays portable across gcc,
# clang and mingw.
strict_warnings = [
  '-Wshadow',
  '-Wdeclaration-after-statement',
  '-Wmissing-prototypes',
  '-Wstrict-prototypes',
  '-Wpointer-arith',
  '-Wwrite-strings',
  '-Winline',
  '-Wno-missing-field-initializers',
]
add_project_arguments(cc.get_supported_arguments(strict_warnings), language: 'c')

# Release builds enable link-time optimization by default: it inlines the hot
# cross-translation-unit interpreter accessors and is the single largest
# speedup. debug and debugoptimized builds stay LTO-free for fast compiles, so
# a release build is the way to opt in and any other buildtype opts out.
if get_option('buildtype') == 'release'
  lto_flags = cc.get_supported_arguments('-flto=auto')
  if lto_flags.length() == 0
    lto_flags = cc.get_supported_arguments('-flto')
  endif
  # Without -fno-semantic-interposition a shared library assumes its own global
  # functions may be interposed at runtime, so the optimizer cannot inline calls
  # to them across translation units even under LTO -- which is exactly the
  # inlining of the hot object accessors we want.
  lto_flags += cc.get_supported_arguments('-fno-semantic-interposition')
  add_project_arguments(lto_flags, language: 'c')
  add_project_link_arguments(lto_flags, language: 'c')
endif

splint = find_program('splint', required: false)

if splint.found()

  splint_cmd = [
    splint,
    '+posixlib',
    '-boolops',
    '-predboolint',
    '+ignoresigns',
    '-type',
    '+charindex',
    '-nestcomment',
    '-noeffect',
    '-redef',
    '-shiftnegative',
    '-castfcnptr',
    '-shiftimplementation',
    '-predboolothers',
    '-exportlocal',
    '-mustfreefresh',
    '-preproc',
    '-I' + meson.current_source_dir() / 'src' / 'lib',
     meson.current_source_dir() / 'src' / 'lib' / '*.c',
     meson.current_source_dir() / 'src' / 'lib' / '*.h',
     meson.current_source_dir() / 'src' / 'bin' / '*.c',
     meson.current_source_dir() / 'src' / 'bin' / '*.h',
  ]

  custom_target('splint',
    output: 'splint',
    command: splint_cmd,
    install: false
  )
endif

wc = find_program('wc', required: false)

if wc.found()

  wc_cmd = [
    wc,
    '-l',
    meson.current_source_dir() / 'src' / 'bin' / '*.c',
    meson.current_source_dir() / 'src' / 'bin' / '*.h',
    meson.current_source_dir() / 'src' / 'lib' / '*.c',
    meson.current_source_dir() / 'src' / 'lib' / '*.h',
    meson.current_source_dir() / 'data' / '*.ps'
  ]

  custom_target('wc',
    output: 'wc',
    command: wc_cmd,
    install: false
  )
endif

# dependencies

fontconfig_dep = dependency('fontconfig', required: false)
if fontconfig_dep.found()
  config_h.set('HAVE_FONTCONFIG', 1)
endif

freetype_dep = dependency('freetype2', required: false)
if freetype_dep.found()
  config_h.set('HAVE_FREETYPE2', 1)
endif

jpeg_dep = dependency('libjpeg', method: 'pkg-config', required: false)
if jpeg_dep.found()
  config_h.set('HAVE_LIBJPEG', 1)
endif

libpng_dep = dependency('libpng', required: false)
if libpng_dep.found()
  config_h.set('HAVE_LIBPNG', 1)
endif

zlib_dep = dependency('zlib', required: false)
if zlib_dep.found()
  config_h.set('HAVE_ZLIB', 1)
endif

xcb_dep = dependency('xcb-image xcb-icccm xcb xcb-aux', required: false)
if xcb_dep.found()
  config_h.set('HAVE_XCB', 1)
endif

if sys_windows
  bcrypt_dep = cc.find_library('bcrypt')
  gdi_dep = cc.find_library('gdi32')
  gl_dep = cc.find_library('opengl32')
  winsock2_dep = cc.find_library('ws2_32')
else

  if cc.has_header('sys/select.h')
    config_h.set10('HAVE_SYS_SELECT_H', true)
  endif

  m_dep = cc.find_library('m', required : false)

# dladdr
  dl_dep = dependency('dl')
  if not cc.has_function('dladdr', dependencies: dl_dep)
    error('dladdr() is mandatory on UNIX')
  endif

# fpurge
  if cc.has_function('__fpurge',
    prefix: [
      '#include <stdio.h>',
      '#include <stdio_ext.h>'
    ])
    config_h.set10('HAVE_STDIO_EXT_H', true)
  elif cc.has_function('fpurge', prefix: '#include <stdio.h>')
    if not cc.compiles(
      '''
      #include <stdio.h>
      int main() { int i = fpurge(NULL); return 0; }
      ''')
      config_h.set10('HAVE_NO_FPURGE_SGN', true)
    endif
  else
    error('__fpurge() or fpurge() are mandatory on UNIX')
  endif

# clock_gettime
  if cc.has_function('clock_gettime', prefix: '#include <time.h>')
    config_h.set10('HAVE_CLOCK_GETTIME', true)
    if cc.compiles(
      '''
      #include <time.h>
      int main()
      {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return 0;
      }
      ''')
      config_h.set10('HAVE_CLOCK_MONOTONIC', true)
      message('CLOCK_MONOTONIC is available with clock_gettime()')
    elif cc.compiles(
      '''
      #include <time.h>
      int main()
      {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        return 0;
      }
      ''')
      message('CLOCK_REALTIME is available with clock_gettime()')
    else
      error('CLOCK_MONOTONIC or CLOCK_REALTIME with clock_gettime() are mandatory on UNIX')
    endif
  endif

# page size
  if cc.compiles(
      '''
      #include <unistd.h>
      int main() { long sz = sysconf(_SC_PAGESIZE); return 0; }
      ''')
      config_h.set10('HAVE_SYSCONF_PAGESIZE', true)
      message('_SC_PAGESIZE is available with sysconf()')
  elif cc.compiles(
      '''
      #include <unistd.h>
      int main() { long sz = sysconf(_SC_PAGE_SIZE); return 0; }
      ''')
      config_h.set10('HAVE_SYSCONF_PAGE_SIZE', true)
      message('_SC_PAGE_SIZE is available with sysconf()')
  elif cc.compiles(
      '''
      #include <unistd.h>
      int main() { long sz = getpagesize(); return 0; }
      ''')
      config_h.set10('HAVE_GETPAGESIZE', true)
      message('getpagesize() is available()')
  endif

endif

config_h.set('XPOST_VERSION_MAJ', v_maj)
config_h.set('XPOST_VERSION_MIN', v_min)
config_h.set('XPOST_VERSION_MIC', v_mic)

config_h.set_quoted('PACKAGE_VERSION', meson.project_version())

subdir('src/lib')
subdir('src/bin')
subdir('data')
if get_option('doc')
  subdir('doc')
endif

# Core-interpreter conformance: run the PLRM operator-example suite through
# the built interpreter and require its failcount to reach zero. The build-tree
# binary is not installed, so point it at the source data via XPOST_DATA_DIR.
test('interpreter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('data/test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 120)

# Embedding contract: run-status outcomes and error identity across a
# persistent context.
run_status_exe = executable('run_status_test',
  'tests/run_status_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('run-status',
  run_status_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedding contract: showpage and copypage both end a page, so both must
# take the page semantics the embedder selected rather than announcing the
# page and reading the line editor regardless.
page_semantics_exe = executable('page_semantics_test',
  'tests/page_semantics_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('page-semantics',
  page_semantics_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedding contract: a process may create, use and destroy contexts for as
# long as it lives, so no per-context table or allocation may carry over.
context_reuse_exe = executable('context_reuse_test',
  'tests/context_reuse_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('context-reuse',
  context_reuse_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedding contract: a run closes the file it wrapped around the program
# however the run ended, so a context serving many failing jobs does not
# exhaust the descriptors the process is allowed.
run_input_close_exe = executable('run_input_close_test',
  'tests/run_input_close_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('run-input-close',
  run_input_close_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Object-layer bounds discipline: a string object with a corrupt or
# sentinel ent yields NULL from the pointer adapter, never a wild pointer.
string_bounds_exe = executable('string_bounds_test',
  'tests/string_bounds_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('string-bounds',
  string_bounds_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The one growable byte buffer every byte-stream builder in the tree grows
# through: its return convention (0 for no error, the error code to raise
# otherwise), a zero-filled buffer being a valid empty one, content survival
# across arbitrarily many doublings, and a capacity request no allocator can
# satisfy answering the error instead of wrapping around.
strbuf_exe = executable('strbuf_test',
  'tests/strbuf_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
# the unsatisfiable reserve asks the allocator for a block no allocator can
# serve; AddressSanitizer aborts on such a request unless told to answer null
# the way the C library does
test('strbuf', strbuf_exe,
  env: { 'ASAN_OPTIONS': 'allocator_may_return_null=1' })

# Memory-table bookkeeping under dictionary growth: dicgrow's entity swap
# must move (adr, sz, used) together; the invariant used <= sz holds for
# every ent after repeated regrowth.
dict_grow_exe = executable('dict_grow_test',
  'tests/dict_grow_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dict-grow',
  dict_grow_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Object semantics: file-object comparison and user-space pathbbox.
test('semantics',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/semantics_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Graphics regression checks: operator wrapping and the wrapped-operator
# error posture, asserted through the interpreter's private operators.
test('graphics-regression',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/graphics_regression_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Render classes: Type 3 and composite fonts, the shading types, tiling and
# shading patterns, and screen/halftone setup. Standard PostScript that prints
# SUCCESS on the reference interpreters too, so it stands as a conformance test.
test('render-classes',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/render_classes_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Raster device classes as a program-made instance: the grayscale and rgb
# classes mark the same pixels for the same geometry, bounds violations are
# swallowed, and the marking methods stay silent (a stray diagnostic would
# interleave with a page stream written to standard output). The strict
# wrapper requires the output to be exactly SUCCESS.
test('raster-class',
  find_program('tests/run-strict-ps-test.sh'),
  args: [ xpost_exe, files('tests/raster_class_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Form cache on a raster device: caching an execform builds an image device and
# clears it with the rect-fill C operators, run through the ppm device. Guards
# the regression where those operators, relocated into .internaldict, could no
# longer be fetched by name.
test('execform-cache',
  find_program('tests/run-ppm-test.sh'),
  args: [ xpost_exe, files('tests/execform_cache_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Bounding-box device: render a known fill through -d bbox and check the box.
test('bbox-device',
  find_program('tests/run-bbox-test.sh'),
  args: [ xpost_exe, files('tests/bbox_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# PDF device: write a PDF and check its structure; round-trip via a reference consumer when
# present. The input is a single fill large enough to overflow the content
# buffer, exercising the flush path.
test('pdf-device',
  find_program('tests/run-pdf-test.sh'),
  args: [ xpost_exe, files('tests/pdf_bigfill_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# PDF content appends under memory pressure: a FillPoly whose content
# append fails must raise rather than leave a truncated page that
# reported success. Skips where the shell cannot impose the limit.
test('pdf-append-fail',
  find_program('tests/run-append-fail-test.sh'),
  args: [ xpost_exe, files('tests/pdf_append_fail_test.ps'), 'pdfwrite' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 120)

# The same append-completeness contract on the svgwrite device, whose
# FillPoly emits the fill colour through the same unchecked hot loop.
test('svg-append-fail',
  find_program('tests/run-append-fail-test.sh'),
  args: [ xpost_exe, files('tests/svg_append_fail_test.ps'), 'svgwrite' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 120)

# Access attributes in the device blitters: painting into a raster row a
# program restocked with a read-only string raises invalidaccess and
# leaves the read-only bytes untouched (PLRM 3.3.2).
test('blit-access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/blit_access_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# SVG device: render fills, strokes and glyph outlines through
# setpagedevice and require the document's structural landmarks,
# including resolution-independent point sizing.
test('svg-device',
  find_program('tests/run-svg-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# pngalpha device: RGBA output where the erased page is transparent and
# marks (including explicit white) are opaque; png stays RGB.
test('pngalpha-device',
  find_program('tests/run-pngalpha-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Save/restore VM: drive the entity count past 65536 and require dicts and
# arrays to revert exactly -- guards the saverec entity-width truncation.
test('save-restore',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Local/global VM integrity: the put operators reject a local value entering a
# global container and accept the reverse; a characterisation guard records
# that literal construction ([ ] and { }) skips the check.
test('vm',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/vm_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# roll operator: correctness (checked against Ghostscript) plus a large-roll
# performance guard -- an O(n^2) roll on a deep stack times the test out.
test('roll',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/roll_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# counttomark and the [ ] / << >> constructors built on it: correctness plus
# a performance guard -- an O(n^2) counttomark on a deep stack times the test out.
test('counttomark',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/counttomark_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# In-RIP trapping procedure set (PLRM 6.3): settrapparams/currenttrapparams
# maintain the trapping-parameter state, settrapzone accepts a zone.
test('trapping',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/trapping_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Arithmetic/math operators (PLRM 8.2): zero-divisor mod/idiv are undefinedresult
# not a SIGFPE, sqrt of a negative is rangecheck, and atan/sin/cos hit the PLRM
# examples (a truncated RAD_PER_DEG had skewed them). From the 8.2 compliance campaign.
test('op-math',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_math_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# writehexstring over the whole byte range: every byte must encode as two
# hexadecimal digits and read back identically, including above 0x7F where a
# signed character index would run off the front of the digit table.
test('op-hexstring',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_hexstring_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Execution-context operators, an extension to the language: fork, join, detach, yield and
# currentcontext. A forked context is never scheduled, so join reports in
# bounded time instead of waiting forever, and a fork with no free table
# slot is an error rather than an index into an unrelated slot.
test('op-context',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_context_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 60)

# Control operators (PLRM 8.2): repeat of a negative count is rangecheck (PLRM: the
# count is a nonnegative integer), plus if/ifelse/for/loop/exit/stopped behaviour.
test('op-control',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_control_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# exit across every PLRM looping context, including filenameforall, whose loop
# frame must carry the sentinel exit searches for so an exit inside its
# procedure ends that loop (and only that loop) and releases the glob results.
test('loop-exit',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/loop_exit_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Matrix/coordinate and graphics-state operators (PLRM 8.2): a bad-length matrix
# is rangecheck (was an out-of-bounds read), setlinecap/join outside 0..2 and
# setmiterlimit below 1 are rangecheck, and rotate uses full-precision degrees.
test('op-matrix-gstate',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_matrix_gstate_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A wrong-typed operand is a typecheck, not a stackunderflow, when a higher-arity
# signature's operands are absent (PLRM/Distiller; gs differs for the matrix
# builders, so this one is xpost-only).
test('op-dispatch-typecheck',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_dispatch_typecheck_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Path-construction operators (PLRM 8.2): currentpoint tracking, nocurrentpoint,
# and arc endpoints on the axes (full-precision degrees).
test('op-path',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_path_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Rectangle operators (PLRM 8.2): rectfill/rectstroke/rectclip accept the
# numarray and numstring forms and rectstroke an optional trailing matrix.
test('op-rect',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_rect_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Resource and user-object operators (PLRM 8.2): resourcestatus on a category
# that does not exist raises undefined (was false); the UserObjects rules.
test('op-resource',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_resource_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# status operator (PLRM 8.2): the (filename) form returns pages/bytes/referred/
# created/true or false, alongside the existing file-object boolean form.
test('op-status',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_status_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# User/system parameter operators (PLRM 8.2): current* return a parameter
# dictionary and set* apply the parameters they recognise.
test('op-userparams',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_userparams_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# bytesavailable (PLRM 8.2): remaining bytes for a seekable file, -1 for a filter.
test('op-bytesavailable',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_bytesavailable_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Operand restoration on error (PLRM 3.11): an operator error restores the
# operands, including the integer types the program pushed.
test('op-error-restore',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_error_restore_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Array packing mode (PLRM 8.2): setpacking/currentpacking and read-only procs.
test('op-packing',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_packing_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# bind (PLRM 8.2): bind rewrites packed arrays and descends into nested ones,
# but leaves ordinary read-only arrays untouched -- and freezes the operators.
test('bind-packed',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/bind_packed_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# vmreclaim (PLRM 8.2): 2 (local and global) reclaims local VM like 1 (local).
test('op-vmreclaim',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_vmreclaim_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Integer-literal range (PLRM 3.3.2): an over-range constant becomes a real.
test('op-intoverflow',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_intoverflow_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# SIGINT raises the PostScript interrupt error between evaluation steps;
# errordict's handler stops the job and the interpreter exits. POSIX-only.
test('interrupt',
  find_program('tests/run-interrupt-test.sh'),
  args: [ xpost_exe, files('tests/interrupt_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Composite-size limits: the small-object build holds Appendix B.1's 65,535
# ceiling with limitcheck; the large-object build carries composites to
# memory. The test probes the build and holds it to its own contract.
test('large-object',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/large_object_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Unterminated string literals: end of input inside (...), <...> or <~...~>
# is a syntaxerror on both scan paths (PLRM is silent; Distiller and gs agree).
test('scanner-eof',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/scanner_eof_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Radix number range (PLRM 3.2): unsigned twos-complement in the integer width,
# limitcheck beyond it. Integer-width-specific, so xpost-only (see the file).
test('op-radix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_radix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvi/cvr number conversion (PLRM 8.2): reject non-PostScript syntax; cvi of an
# over-range real is rangecheck, not a wrap.
test('op-cvi',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvi_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Graphics-state number parameters are reals (PLRM), matching Distiller and gs.
test('op-gstate-types',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_gstate_types_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvs yields --nostringval-- for every type outside the string-representable set.
test('op-cvs',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvs_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# array and string get/put require an integer index (a real is a typecheck).
test('op-index-type',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_index_type_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# reading from a closed file reports end-of-data rather than raising an error.
test('op-closed-read',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_closed_read_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# save/restore (and gsave/grestore) must revert the CTM. scale/concat/rotate
# write the current-matrix array in place; that write must be backed up for
# save/restore. Guards the regression where it was not -- a transform set inside
# a save leaked past the restore, so dvips documents (casselman) rendered text
# at a runaway scale and overflowed the operand stack.
test('save-restore-ctm',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_ctm_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Interpreter isolation: internal helper procedures hide in .xpostsys, so their
# names neither pollute nor obscure a program's own, and a program may redefine
# them without disturbing the graphics machinery (a clash that broke real
# programs on xpost). Also covers surviving a program shadowing get/exec.
test('isolation',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/isolation_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Dispatch-dictionary tamper resistance: no internal dispatch dictionary may
# store an executable operator NAME (re-looked-up at run time, so a program's
# redefinition hijacks internal machinery -- the casselman ch8 loop); operator
# values must be stored instead. Scans every reachable internal namespace,
# self-testing that the scanner catches a planted unsafe entry.
# SubFileDecode EOD semantics (PLRM 3.13.3): a positive count passes data up to
# and including the count-th delimiter; count zero consumes it without passing.
test('op-subfiledecode',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_subfiledecode_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# String data sources for filters (PLRM 3.8.4): a filter may read from a string.
test('op-string-filter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_string_filter_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# readstring (PLRM 8.2): verbatim transfer, the boolean result at end of
# file, and the rangecheck a zero-length string operand raises.
test('op-readstring',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_readstring_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Boundaries of the file operators (PLRM 3.8, 8.2): empty operands, end of
# data, closed files, files that do not exist, and the operators whose
# answer on a filter differs from a positionable disk file.
test('op-file-edges',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_file_edges_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The virtual-memory parameter operators: vmstatus and globalvmstatus report
# the save level and memory consumption, and vmreclaim takes the five values
# PLRM 8.2 defines for it and refuses the rest.
test('op-vm-param',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_vm_param_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvn interns a name from characters that live in the memory file the
# interning allocates in, so the interning must not read them from where they
# used to be. Many names never seen before, interned amid allocation.
test('op-cvn',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvn_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Exceeding the number of files a context may hold open is a limitcheck
# (PLRM 8.2, file). Run with the process's allowance lowered so the limit is
# reached at once.
test('op-file-limit',
  find_program('tests/run-file-limit-test.sh'),
  args: [ xpost_exe, files('tests/op_file_limit_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The collector's own diagnostics, which no ordinary run reaches: an
# independent reachability verifier and the cross-bank scan that holds global
# containers to PLRM 3.7.2. A collection-heavy job must leave both silent.
test('gc-diagnostics',
  find_program('tests/run-gc-diagnostics-test.sh'),
  args: [ xpost_exe, files('tests/gc_diagnostics_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A collection between two pieces of vector output must leave the device's
# open output file alone: the file lives in local memory whatever dictionary
# holds it, and the record holding it is global.
test('vecout-gc',
  find_program('tests/run-vecout-gc-test.sh'),
  args: [ xpost_exe, files('tests/vecout_gc_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The file operator's access strings (PLRM 3.8.1, Table 3.5): all six of
# r/w/a with and without +, the access attribute each confers, and the
# refusal of an access string outside the table.
test('op-file-access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_file_access_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# readline (PLRM 8.2): terminator handling for CR, LF and CRLF, the boolean
# result at end of file, and the rangecheck boundary -- a line that exactly
# fills the string has been read, not overrun.
test('op-readline',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_readline_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# filenameforall enumerates the files matching a template (PLRM 8.2), and a
# template that matches none of them enumerates nothing rather than failing.
test('op-filenameforall',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_filenameforall_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A filter and the stream beneath it may be closed in either order: the
# stream a filter reads from (or writes to) outlives the file object that
# opened it, and a filter over a closed stream reports end of data.
test('filter-source-life',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_source_life_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Procedure data sources for filters (PLRM 3.8.4).
test('op-proc-filter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_proc_filter_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# setcachedevice/setcharwidth are valid only within a glyph description (PLRM 8.2).
test('op-glyphcontext',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_glyphcontext_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

test('tamper-dispatch',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/tamper_dispatch_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Startup namespace cleanliness: after startup userdict and globaldict are empty
# and systemdict exposes precisely the golden key set, so no internal operator or
# procedure can leak into a userland-visible dictionary unnoticed. A deliberate
# change to systemdict's public surface fails this until the golden set is
# regenerated (tools/gen_namespace_test.py), making every such change reviewed.
test('namespace',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/namespace_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# No-graphics lockdown: --no-graphics selects the no-graphics start procedures,
# which run .finalize (the interpreter lockdown) without loadgraphics. The
# language still relocates into systemdict and the private namespaces are still
# sealed and hidden, while the graphics modules are never loaded and the
# interpreter stays functional -- so loading graphics is genuinely optional, not
# a prerequisite for a hardened interpreter.
test('nographics',
  find_program('tests/run-nographics-test.sh'),
  args: [ xpost_exe, files('tests/nographics_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Filter EOD consumption: a decode filter must swallow its own end-of-data marker
# so that the next fresh filter reading the same currentfile stream stays in sync
# -- the dvips/colorimage per-scanline idiom. Guards the regressions where
# ASCII85Decode ('z' zero group), FlateDecode (output a multiple of its buffer),
# LZWDecode (the 257 code) and the others stranded their terminator, desynchronising
# every following scanline. Generated by tools/gen_filter_eod_test.py.
test('filter-eod',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_eod_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
test('stream-exec',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/stream_exec_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })


# Access-attribute enforcement: readonly, executeonly and noaccess reduce the
# value and execution access of composite objects, and the standard operators
# raise invalidaccess (or the access operators typecheck) accordingly, across
# arrays, strings and dictionaries. Standard PostScript that prints SUCCESS on
# a conforming interpreter too, so it is a conformance test rather than an xpost tautology.
test('access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/access_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every output device renders: the rest of the suite runs under -d null,
# which never loads the graphics/device stack, so a device that fails to
# initialise or emit a page would otherwise be invisible. Render a trivial
# page through each built device and require non-empty output.
test('devices',
  find_program('tests/run-devices-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

# Relocation stress: every memory grow forced to relocate, so a pointer
# derived before an allocating call and used after it reads freed memory.
# Covers string-to-name conversion, encoded number strings, path filling and
# image blitting; dictionary rehash is covered by the dict-grow C test.
test('reloc-stress',
  find_program('tests/run-reloc-stress-test.sh'),
  args: [ xpost_exe, files('tests/reloc_stress_test.ps'), 'pgm' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 120)

# The library's exported symbol set is a register, not a side effect. A
# declaration interposed between `static` and a function silently gives it
# external linkage; the compiler reports only a useless-storage-class note.
test('check-exported-symbols',
  find_program('tests/check-exported-symbols.sh'),
  args: [ libxpost_lib.full_path(), files('tests/exported_symbols.golden') ])

# Document Structuring Convention parsing: a header list continued over %%+
# lines is accumulated into the header record, and the parallel font array is
# indexed by that recorded count.
dsc_parse_exe = executable('dsc_parse_test',
  'tests/dsc_parse_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: [ libxpost_dep, libxpost_dsc_dep ],
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dsc-parse', dsc_parse_exe)

# A test must be able to fail. Each rule here encodes a defect this suite
# actually carried: a failure counter incremented with def inside a scratch
# dictionary (the increment is discarded, so the suite prints FAIL lines and
# reports SUCCESS); a wrapper that ignores the interpreter's exit status (a
# crash after SUCCESS passes); a guard that accepts an empty golden file
# (it then compares nothing).
test('check-test-quality',
  find_program('tests/check-test-quality.sh'),
  args: [ meson.current_source_dir() / 'tests' ])

# The distribution lists must cover every built file: a header present in the
# meson build but missing from the autotools lists yields a `make dist` tarball
# that does not compile, which no in-tree build would ever reveal.
test('check-dist-lists',
  find_program('tests/check-dist-lists.sh'),
  args: [ meson.current_source_dir() ])

# A quiet run must be silent: the interpreter's own self-checks report only
# when something is actually wrong, so stderr stays meaningful.
test('clean-startup',
  find_program('tests/run-clean-startup-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Fused-execution equivalence: the interpreter inlines the hottest operators
# while walking a procedure, and those copies must behave exactly as the
# operators they stand in for -- same results, same errors, same access
# enforcement. Each case runs both ways and the outcomes must match.
test('fused-equivalence',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/fused_equivalence_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# ... and every fused operator must actually have those cases: adding one
# to the fast path without equivalence coverage fails here.
test('check-fused-ops',
  find_program('tests/check-fused-ops.sh'),
  args: [ meson.current_source_dir() / 'src' / 'lib',
          files('tests/fused_equivalence_test.ps') ])

# The byte-identity render gate: the golden page (fills, strokes, clipping,
# images, shading, patterns, an inline Type 3 font) rendered through every
# deterministic output device must reproduce the recorded sha256 exactly.
# A refactor claiming "no behavioral change" is held to these bytes; an
# intended rendering change regenerates the manifest in the same commit
# (tests/run-golden-render.sh ... --regen).
test('golden-render',
  find_program('tests/run-golden-render.sh'),
  args: [ xpost_exe, files('tests/golden_page.ps'),
          meson.current_source_dir() / 'tests' / 'golden' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

# Device method contract, safety tier: every built device must accept the
# boundary inputs its methods can legally receive (degenerate, inverted,
# fractional, out-of-range rectangles, pixels and lines) without error and
# still emit a page. The exact extent semantics stay unasserted until the
# device layer consolidates on one definition.
test('device-contract',
  find_program('tests/run-device-contract-test.sh'),
  args: [ xpost_exe, files('tests/device_contract_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

# Device teardown discipline: Destroy is called at setpagedevice, at job end,
# and possibly by the program itself, so every device's Destroy must be
# idempotent -- clear the stored resource handle, never free it twice -- and
# a device-owned framebuffer must be released unless handed to the client.
test('device-destroy',
  find_program('tests/run-device-destroy-test.sh'),
  args: [ xpost_exe, files('tests/device_destroy_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

# Multi-page output: a save...showpage...restore job (the separation-plate
# idiom, which rewinds local virtual memory between pages) must land each page
# in its own file when the output name carries a %d, and keep only the last
# page when it does not. Guards the page counter living in a global dictionary
# rather than the local device dict, where restore would rewind it.
test('multipage',
  find_program('tests/run-multipage-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

# Embedded Type 1 with a no-access Private: definefont must reach the loader and
# read Subrs out of Private in C, not raise invalidaccess at a PostScript-level
# read. Guards the regression where access enforcement broke every embedded
# Type 1 font (the whole casselman corpus).
test('font-type1-noaccess',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_type1_noaccess_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedded Type 1 with an over-allocated /Subrs array -- more slots declared than
# filled, leaving null entries. definefont must still load the font. Guards the
# regression where .loadfont1 rejected the whole font on the first null Subr,
# which is why xpost rendered 0/13 of the (dvips Computer Modern) casselman corpus
# while Ghostscript rendered all of it -- CMR10 declares 38 Subrs and fills 15.
test('font-type1-sparse-subrs',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_type1_sparse_subrs_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Type 1 emission across many doublings of the font-program buffer: an
# embedded font whose eexec section outgrows its buffer several times
# over, every glyph declaring its own advance width through hsbw and
# every one of those widths read back with stringwidth. A byte lost,
# duplicated or reordered at any grow lands in a charstring the check
# reads.
test('font-bigbuf',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_bigbuf_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Differential corpus: render whatever real-program corpora the developer has
# fetched (tests/corpus/fetch.sh) through xpost and Ghostscript and report the
# per-page difference. Skips when no corpus is present or the reference tools
# are absent, so it is never a build-time dependency; fails only when xpost
# crashes or hangs on a program.
test('corpus',
  find_program('tests/corpus/run-corpus-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: 'corpus',
  timeout: 1800)

# Binary tokens (PLRM 3.14): the conformance corpus against a golden
# record generated from the reference interpreter, which revalidates
# the golden when a reference interpreter is present.
test('binary-tokens',
  find_program('tests/run-bintok-test.sh'),
  args: [ xpost_exe,
          files('tests/binary_token_test.ps'),
          files('tests/binary_token_test.expected') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Error reporting: a top-level job error prints the standard Adobe error line
# and flush notice; a clean quit or a caught error prints neither.
test('error-format',
  find_program('tests/run-error-format-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# Single-opener invariant: fopen() appears only inside xpost_diskfile_fopen(),
# so every disk file passes one enforcement point.
test('fopen-funnel',
  find_program('tests/check-fopen-funnel.sh'),
  args: [ meson.current_source_dir() / 'src' / 'lib' ])

# Device-driver skeleton: the compiled-buffer device fleet reaches its
# private struct, operand folding and FillRect extent arithmetic through
# xpost_dev_driver.h -- the contract stays written in one place.
test('check-device-skeleton',
  find_program('tests/check-device-skeleton.sh'),
  args: [ meson.current_source_dir() / 'src' / 'lib' ])

# Machinery stays born in its final home: every registered private-namespace
# member keeps its dictionary and every graphics-state template slot survives,
# per the register in tests/dict_homes.golden.
test('dict-homes',
  find_program('tests/check-dict-homes.sh'),
  args: [ meson.current_source_dir() ])

# The raster classes stay a single generated suite: pgm/ppm are parameter
# files over .makerasterclass, pbm/tiff dict-copies overriding emission,
# and every shared method is defined exactly once, in the prototype.
test('raster-classes-derivation',
  find_program('tests/check-raster-classes.sh'),
  args: [ meson.current_source_dir() ])

# The vector writers share one base: exactly one two-decimal formatter
# (.num2str) and one page-to-points unscale (.devres-to-points) exist,
# each writer reaches them by reference, and the DSC device stays a
# dict-copy derivation of the PDF device.
test('vecbase',
  find_program('tests/check-vecbase.sh'),
  args: [ meson.current_source_dir() ])

# One growable byte buffer for the whole tree: the builders that assemble
# a byte stream of unknown final size grow it through xpost_strbuf.h, none
# of them carries a grow loop or a pointer/length/capacity triple of its
# own, and none reads the answer as a success flag.
test('one-bytebuf',
  find_program('tests/check-one-bytebuf.sh'),
  args: [ meson.current_source_dir() ])

# Path-component validation and beneath-root confinement: traversal and
# symlink-escape attempts are refused. POSIX-only: the escape probe builds
# its test tree with symlink(), which the Windows CRT does not provide.
if host_machine.system() != 'windows'
  path_confine_exe = executable('path_confine_test',
    'tests/path_confine_test.c',
    c_args: '-DHAVE_CONFIG_H',
    dependencies: libxpost_dep,
    include_directories: [ config_dir, include_directories('src/lib') ],
    install: false)
  test('path-confine', path_confine_exe)
endif

# File-access sandbox: once engaged, only permitted directories may be
# opened by the running program.
sandbox_exe = executable('sandbox_test',
  'tests/sandbox_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('sandbox',
  sandbox_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# From-PostScript lockdown: .permitfileread/.permitfilewrite/.lockdown raise
# the sandbox from within a program and freeze the permit set.
lockdown_exe = executable('lockdown_test',
  'tests/lockdown_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('lockdown',
  lockdown_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Implicit resource loading: findresource loads a named instance from the
# search path on a miss, confined and leaf-validated.
resource_load_exe = executable('resource_load_test',
  'tests/resource_load_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('resource-load',
  resource_load_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The driver contract's rectangle definition: xpost_dev_rect_normalize()
# is the one statement of which pixels FillRect paints (inclusive far
# corner, negative extents reflected, clipped to the device), and the
# folding helpers truncate. Every device routed through the driver
# header inherits exactly these semantics.
dev_rect_exe = executable('dev_rect_test',
  'tests/dev_rect_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dev-rect', dev_rect_exe)
#if get_option('nls') == true
#  subdir('po')
#endif

# Use config_h after all subdirs have set values

configure_file(output : 'config.h', configuration : config_h)

## pc files

pkgmod = import('pkgconfig')

pkgmod.generate(
    libxpost_lib,
    description: 'Postscript level 2 interpreter.',
    filebase: 'xpost',
    name: 'Xpost',
    variables: 'pkgincludedir=' + '-' + v_maj,
)

summary({'OS': host_os,
         'fontconfig': fontconfig_dep.found().to_string('yes', 'no'),
         'freetype': freetype_dep.found().to_string('yes', 'no'),
         'jpeg device': jpeg_dep.found().to_string('yes', 'no'),
         'png device': libpng_dep.found().to_string('yes', 'no'),
         'documentation': (get_option('doc') and doxygen.found()).to_string('yes', 'no'),
         'splint': splint.found().to_string('yes', 'no'),
         'wc': wc.found().to_string('yes', 'no'),
        }, section: 'Configuration Options Summary:')

summary({'prefix': dir_prefix,
         'bindir': dir_bin,
         'libdir': dir_lib,
         'incdir': dir_include,
         'pkgincdir': dir_pkginclude,
         'datadir': dir_data,
         'pkgdatadir': dir_pkgdata,
        }, section: 'Directories:')

summary({'compilation': 'ninja',
         'installation': 'ninja install',
        }, section: 'Compilation')
