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

# Arithmetic/math operators (PLRM 8.2): zero-divisor mod/idiv are undefinedresult
# not a SIGFPE, sqrt of a negative is rangecheck, and atan/sin/cos hit the PLRM
# examples (a truncated RAD_PER_DEG had skewed them). From the 8.2 compliance campaign.
test('op-math',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_math_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Control operators (PLRM 8.2): repeat of a negative count is rangecheck (PLRM: the
# count is a nonnegative integer), plus if/ifelse/for/loop/exit/stopped behaviour.
test('op-control',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_control_test.ps') ],
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

# roll operator: correctness (checked against Ghostscript) plus a large-roll
# performance guard -- an O(n^2) roll on a deep stack times the test out.
test('roll',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/roll_test.ps') ],
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

# Machinery stays born in its final home: every registered private-namespace
# member keeps its dictionary and every graphics-state template slot survives,
# per the register in tests/dict_homes.golden.
test('dict-homes',
  find_program('tests/check-dict-homes.sh'),
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
