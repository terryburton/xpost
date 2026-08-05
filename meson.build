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

# The object width the interpreter is built for. The sources gate on
# this define; autotools sets it from the same option, and without it
# here a build configured for large objects is silently a small-object
# one.
if get_option('large-object')
  config_h.set('WANT_LARGE_OBJECT', 1)
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

# The interpreter's surface as an embedding program finds it: the same
# homes, spaces and access attributes the CLI-side test states, reached
# through xpost_create and xpost_run instead of a start procedure.
startup_surface_api_exe = executable('startup_surface_api_test',
  'tests/startup_surface_api_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('startup-surface-api',
  startup_surface_api_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

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

# The same embedding runs with the collector set to collect constantly. What
# the interpreter holds outside the collector's root set survives by luck at
# the ordinary threshold and not at all at this one.
test('gc-pressure',
  find_program('tests/run-gc-pressure-test.sh'),
  args: [ run_status_exe, context_reuse_exe ],
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

# Embedding contract: the key=value definitions an embedder supplies land in
# userdict as objects of the type each value spells, and the library reports
# where it and its data live.
api_definitions_exe = executable('api_definitions_test',
  'tests/api_definitions_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('api-definitions',
  api_definitions_exe,
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
# Growing a dictionary past its allotment: the wide build's dictionaries
# reach far further before the size field fills, so the same test does
# very much more work there.
test('dict-grow',
  dict_grow_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

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
# The raster device's pixel formats, named after the colon in the device
# string. Only the fallback was exercised, so neither the selection of the
# others nor the buffer each sizes had run; a name matching none of them has to
# be harmless rather than leaving the format unset.
test('raster-formats',
  find_program('tests/run-raster-formats-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every painting operator through every device that runs without a display.
# Which methods a device must implement itself depends on how it keeps its
# raster, so a device can fill and stroke for years while being unable to
# paint a glyph -- which is what three of them were doing.
test('device-features',
  find_program('tests/run-device-features-test.sh'),
  args: [ xpost_exe, files('tests/device_features_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 180)

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
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)

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

# Every file operator against every filter. Which methods a filter carries
# itself depends on how it holds its data, so it can read perfectly while
# being unable to be reset or closed -- one operator at a time is what a
# per-operator test does not catch.
test('filter-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The predictor stage over a compressed stream: a stage that did nothing
# would return the differences, which read as an image rather than as an
# error.
test('filter-predictor',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_predictor_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every mutating operator against restore. Whether an operator records
# what it overwrites is a property of the operator, not of the type, so
# one reverting says nothing about the next.
test('save-restore-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every graphics state parameter through gsave and grestore. A parameter
# left out of the copy leaks across the grestore and changes how
# everything painted afterwards looks.
test('gstate-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gstate_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every colour space family through every colour operator. Each family
# decides for itself how many components it takes and how a colour in it
# is recorded, so one working says little about the next.
test('colorspace-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/colorspace_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Program code is called out to through one bracket. This holds the tree
# to it: every graphicsdict slot is registered as one a bracket saves or
# one the render owns outright, and the dictionary-scope escape has one
# spelling. Both failures are silent until a program re-enters or raises.
test('callout-bracket',
  find_program('tests/check-callout-bracket.sh'),
  args: [ meson.current_source_dir() ])

# Every procedure the graphics machinery calls out to, asked whether the
# caller's state survives the call. A callback is the one moment the
# machinery is half way through its own work with the program holding
# the interpreter, and each site decides for itself what it saves.
test('callback-reentry',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/callback_reentry_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every text-showing operator against both kinds of font. Each operator
# walks its string its own way, so one positioning a glyph correctly says
# nothing about the next.
test('showop-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/showop_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every function type against every shading that can take one. A branch
# per type means one evaluating correctly says nothing about the next.
test('function-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/function_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 120)

# Every resource operator against every category the interpreter admits
# to, asked for rather than listed, so a category added later is held to
# the same contract without editing the test.
test('resource-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/resource_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every way of presenting an image against every way of painting one:
# sample depth, data source, operator and transform are each decoded by
# a branch of their own.
test('image-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/image_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every halftone dictionary fault against the error it is defined to
# raise: the type, the missing entry and the wrongly-typed entry each
# have their own answer, and raising something is not raising the right
# thing.
test('halftone-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/halftone_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The ordered comparisons take two numbers or two strings, and refusing
# anything else is what separates them from eq and ne.
test('op-compare',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_compare_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Operators handed an operand of a type they cannot want. Accepting one
# is not a lenient failure: it answers a question with no answer, or
# stores what can never be read back.
test('op-operand-types',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_operand_types_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# An operator written in PostScript stating the operands it takes, so
# the dispatcher enforces them as it does for one written in C.
test('op-wrapsig',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_wrapsig_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The two font directories hold different populations: the local one goes
# away with the save level that defined its fonts, the global one does not.
test('font-directory',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_directory_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Where a pattern's tiles land: the tiling is anchored in pattern space,
# so the path being filled moves neither its phase nor its period.
test('pattern-phase',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/pattern_phase_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Where every group of names ends up: which dictionary, which VM, and
# what a program is allowed to do to it.
test('startup-surface',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/startup_surface_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Standard operators written in PostScript are operator objects, so bind
# reaches them and a prolog redefining one in terms of itself terminates.
test('op-bind',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_bind_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The operators taking more than one shape of operand list: every shape
# the language defines is reached, and one matching none is refused.
test('op-operand-shapes',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_operand_shapes_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Names implemented twice, in C and in PostScript: which of the two wins
# is settled while the language loads and nothing reports it, so the set
# is declared and checked.
test('check-shadowed-operators',
  find_program('tests/check-shadowed-operators.sh'),
  args: [ meson.current_source_dir() / 'src/lib',
          meson.current_source_dir() / 'data',
          files('tests/shadowed_operators.golden') ])

# A standard operator written in PostScript holds the operators it calls,
# not their names: a name is resolved when it runs, so one left in a body
# is a program's redefinition waiting to be picked up. The declared list
# is what an already-bound body put out of bind's reach, and only shrinks.
test('check-wrapped-bind',
  find_program('tests/check-wrapped-bind.sh'),
  args: [ xpost_exe, meson.current_source_dir(), files('tests/wrapped_bind.golden') ])

# The same invariant as a program sees it: redefine what a standard
# operator calls, and watch whether the operator picks the redefinition up.
test('wrapped-bind',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/wrapped_bind_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# initgraphics on its own: the fault it guards against is invisible in a
# file that has already been drawing, since the earlier drawing can
# empty the path by itself.
test('op-initgraphics',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_initgraphics_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The showing operators and the state each requires.
test('op-showstate',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_showstate_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The printer's descent into a structure that contains itself.
test('op-printdepth',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_printdepth_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 60)

# What the error dictionary carries and what the standard handler does
# to it: a program handling its own errors reads both.
test('error-dict',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/error_dict_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# An error is named in C, by the ERRORS enumeration, and in PostScript, by
# the list errordict's standard handlers are built from. The two sets have
# to be the same one: a C name with no handler is reported through the
# interpreter's fallback instead of through errordict, and a handler for a
# name C never returns reads as coverage it is not.
test('check-error-names',
  find_program('tests/check-error-names.sh'),
  args: [ xpost_exe, meson.current_source_dir() ])

# The binary form of the error report, which has to be checked on the
# bytes rather than from inside the interpreter.
test('error-binary',
  find_program('tests/run-error-binary-test.sh'),
  args: [ xpost_exe ],
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

# File lifetime against a small open-file allowance: eight hundred files ended
# by restore, by the program in either order, and as reusable streams, with a
# collection asked for in every round. A descriptor not released shows up as a
# refusal to open rather than as a leak nobody notices.
test('file-lifetime',
  find_program('tests/run-file-lifetime-test.sh'),
  args: [ xpost_exe, files('tests/file_lifetime_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The font cache parameter operators: cachestatus reports what setcachelimit
# and setcacheparams set, within whatever range the implementation achieves.
test('op-fontcache',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_fontcache_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The clock operators: realtime counts real time, usertime the time the
# interpreter has spent executing, both in milliseconds from an arbitrary
# origin. Each loop runs until its clock ticks rather than a fixed count.
test('op-time',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_time_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The environment operators: a variable set by putenv has to survive the call,
# and getenv answers its value or reports there is none.
test('op-env',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_env_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# String and number literals as the scanner reads them (PLRM 3.2): the escapes,
# the end-of-line markers inside a string, the hexadecimal form and its missing
# final digit, and the number that will not fit an integer.
test('op-literal',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_literal_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The command line: the geometry option sets the page size and a geometry that
# does not parse is refused, and the three reporting options report.
test('cli-options',
  find_program('tests/run-cli-options-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The dictionary stack: begin and end above the three permanent entries,
# cleardictstack back down to them, and dictstack's copy of the whole stack.
test('op-dictstack',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_dictstack_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The loop operators resume through continuations whose state sits on the
# execution stack, which is held in segments. Each has a fast path for a frame
# inside one segment and a fallback for a frame astride two; ordinary programs
# run shallow, so only the fast one is otherwise taken.
test('op-loop-depth',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_loop_depth_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A JPEG stream that stops early, or was never a JPEG, must end the decode
# rather than leave it waiting for input that will never come.
test('dct-truncated',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/dct_truncated_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 60)

# The interpreter's own debugging printers, which live in the private
# dictionary and which nothing in ordinary use reaches: they walk the memory
# table and the name stacks, so they stop working when either changes shape.
test('internal-debug-ops',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/internal_debug_ops_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# %statementedit gathers standard input until the statement is complete, over
# as many lines as that takes. Nothing else in the suite supplies input, so
# none of the nesting it tracks had ever run.
test('statementedit',
  find_program('tests/run-statementedit-test.sh'),
  args: [ xpost_exe ],
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
# The write-side counterpart: closefile on an encoding filter writes the
# coding's end-of-data marker, and a target that cannot accept it leaves a
# stream that stops short of its own terminator. Needs a target that
# refuses data, so it skips where the platform has none.
test('filter-close-eod',
  find_program('tests/run-refusing-target-test.sh'),
  args: [ xpost_exe, files('tests/filter_close_eod_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# A filter releases the stream it was given to wrap: it drops the reference
# it took, and closes and frees a stream the machinery made for it alone --
# the in-memory file behind the string form, and the decompressor a
# predictor stage stands in front of. Nothing in PostScript can observe
# that, so the run happens under a leak checker, and skips without one.
test('filter-leak',
  find_program('tests/run-filter-leak-test.sh'),
  args: [ xpost_exe, files('tests/filter_leak_test.ps'),
          files('glibc.supp') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)
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

# An operator implemented in C reaches another operator by holding it, not by
# pushing its name: a name is resolved against the dictionary stack when it
# runs, so a program's definition of that name would take over the inside of
# a standard operator. Each case redefines the names an operator was observed
# to push and requires that the operator neither reached them nor lost its
# own behaviour.
test('op-references',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_reference_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Fused-execution equivalence: the interpreter inlines the hottest operators
# while walking a procedure, and those copies must behave exactly as the
# operators they stand in for -- same results, same errors, same access
# enforcement. Each case runs both ways and the outcomes must match.
test('fused-equivalence',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/fused_equivalence_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# ... and the complement of the op-references test: that one shows the rule
# holding where it was broken, this one shows it cannot be broken again.
# No name pushed for execution, the reference table reached only through its
# accessors, and every surviving by-string lookup on a register that only
# shrinks.
test('check-op-references',
  find_program('tests/check-op-references.sh'),
  args: [ meson.current_source_dir() ])

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
# One test per corpus, so that meson overlaps them instead of running them end
# to end; each skips on its own when its corpus has not been fetched.
foreach corpus : [ 'ghostscript', 'casselman', 'bwipp', 'adobe' ]
  test('corpus-' + corpus,
    find_program('tests/corpus/run-corpus-test.sh'),
    args: [ xpost_exe, corpus ],
    env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
    suite: 'corpus',
    timeout: 1800)
endforeach

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

# A function that can refuse returns that fact and no caller may ignore
# it. The compiler enforces that wherever XPOST_MUST_CHECK is applied;
# this holds the set it is applied to, so the enforcement cannot be
# undone by dropping a mark, and counts the sites that claim a refusal
# cannot arise.
test('must-check',
  find_program('tests/check-must-check.sh'),
  args: [ meson.current_source_dir() ])

# Machinery stays born in its final home: every registered private-namespace
# member keeps its dictionary and every graphics-state template slot survives,
# per the register in tests/dict_homes.golden.
test('dict-homes',
  find_program('tests/check-dict-homes.sh'),
  args: [ meson.current_source_dir() ])

# The guards read data/ and tests/ under a source root they are given, and
# the interpreter finds the working tree by other routes when that root is
# wrong -- so a misdirected guard reports a true result about a tree nobody
# asked about. Each one must refuse a path that is not a source root.
test('guard-paths',
  find_program('tests/check-guard-paths.sh'),
  args: [ meson.current_source_dir() ])

# An operator's operand statement is recorded where the operator is
# defined, through the one mechanism that files it. Four operators once
# could not reach it and had their statements written by hand instead.
test('opsigs-writer',
  find_program('tests/check-opsigs-writer.sh'),
  args: [ meson.current_source_dir() ])

# The complement of dict-homes: that guards a registered member against
# moving, this guards a reference against landing nowhere. Every bare
# fetch from a private namespace must name something the namespace holds,
# checked against a live startup rather than a second set of patterns.
test('private-refs',
  find_program('tests/check-private-refs.sh'),
  args: [ xpost_exe, meson.current_source_dir() ])

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

# The filter base: what every filter shares is declared once, every filter
# struct begins with one of the two bases, and what a filter is a filter
# over is stated in the one call it is constructed through rather than
# looked up in a hand-maintained list of the method tables.
test('filter-base',
  find_program('tests/check-filter-base.sh'),
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
