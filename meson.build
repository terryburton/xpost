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

# The two comparison diagnostics are errors rather than warnings, in both
# object widths.
#
# word is the type every composite carries its element count in, and
# large-object doubles it: an unsigned short that promotes to int in the
# default build, an unsigned int that does not promote in the wide one. A
# comparison between a counter and a count therefore changes meaning
# between the two builds without changing shape, and the compiler says so
# in only one of them -- so the wide build accumulated forty-six of these
# while the default build reported none, and nobody building the default
# way could see them. -Wtype-limits catches the other half of the same
# thing: a bound whose width the personality decides can put a comparison
# permanently one side of its answer.
#
# Both are long-standing diagnostics in every compiler this builds with,
# and both builds are clean of them, so promoting them costs nothing and
# leaves no way for a comparison that means two things to be committed
# from behind whichever build reports nothing about it. They go through
# get_supported_arguments like the rest, so a compiler that does not know
# them drops them rather than failing.
comparison_errors = [
  '-Werror=sign-compare',
  '-Werror=type-limits',
]
add_project_arguments(cc.get_supported_arguments(comparison_errors), language: 'c')

# Diagnostics the tree already satisfies, promoted to errors so that it
# goes on satisfying them. Each says something a reviewer cannot see by
# reading one file: that a cast changes an object's alignment, that a
# function has no declaration anyone could have checked it against, that
# a name tested by the preprocessor was never defined, that a shift runs
# off the end of its type, that two arms of a condition were written to
# differ and do not. A warning nobody has to answer is a warning the
# build gets used to.
#
# They go through get_supported_arguments, so a compiler that does not
# know one drops it rather than failing, and none of them is promoted
# anywhere it would fire on a construct this code uses deliberately:
# -Wformat-security is taken rather than the whole of -Wformat=2,
# because -Wformat-nonliteral would fire on the device table's own
# templates, which are the interpreter's strings and not a program's.
#
# -Wcast-align is deliberately absent, and the reason is that the two
# compilers do not mean the same thing by it. gcc raises it only where a
# misaligned read faults, so on the usual targets it says nothing at all
# and promoting it buys nothing; clang raises it wherever the pointed-to
# type wants more alignment than the source type, which is every read of
# a record out of the memory file's own byte storage. That storage is
# aligned by the allocator that hands it out, and the cast is where that
# contract is spent rather than where it is stated -- so the diagnostic
# is answered by an invariant it cannot see, in three files, and turning
# it on would mean writing the invariant as a cast through void * at
# every site rather than stating it once.
#
# Which compiler answers to which is recorded beside each below, because
# accepting a flag and acting on it are separate questions and only the
# first is asked by get_supported_arguments. They come apart in two
# directions here: clang answers a warning group it does not know with a
# warning rather than an error, and the text of that warning quotes the
# flag -- so a probe reading exit status calls the unknown group
# supported, and a probe grepping the output for the flag's name reads
# the refusal as the diagnostic firing. And gcc reaches two of these
# only through passes the optimiser turns on, while meson's default
# buildtype is debug: a flag can be accepted by every compiler, promoted
# to an error, and still be answered with silence in every build that
# asks for it.
#
# The notes were taken by compiling a construct written to trip each
# flag, on gcc 13 and clang 17, and reading back whether the diagnostic
# appeared -- not by asking whether the flag was taken.
kept_clean = [
  # gcc only: clang means something else by the name and reports nothing
  # under it. The half of this job clang does is -Wmissing-prototypes,
  # in strict_warnings above.
  '-Werror=missing-declarations',
  # gcc only, and gcc only with the optimiser on: nothing is reported at
  # -O0, which is what buildtype=debug compiles at and what every build
  # that does not say otherwise gets. clang takes the flag and reports
  # nothing under it at any -O. The analysers workflow compiles the tree
  # once with the optimiser on so that this is an error somewhere.
  '-Werror=null-dereference',
  # gcc and clang
  '-Werror=vla',
  # gcc only: clang has -Wshift-overflow with no levels at all, so the
  # levelled spelling is an unknown group there and is dropped. The
  # level is the point -- 1 << 31 and 3 << 30 are reported at 2 and not
  # at 1 -- and clang's default says nothing about either.
  '-Werror=shift-overflow=2',
  # gcc only: an unknown group on clang, and dropped there
  '-Werror=duplicated-cond',
  # gcc only, an unknown group on clang; and like null-dereference it
  # wants the optimiser. The one instance this tree had was reported
  # from -O1 upwards and never at -O0.
  '-Werror=duplicated-branches',
  # gcc only: an unknown group on clang, and dropped there
  '-Werror=logical-op',
  '-Werror=jump-misses-init',
  # gcc only: clang takes the flag and reports a definition without a
  # prototype under -Wdeprecated-non-prototype instead, which is on by
  # default, so nothing is lost by the promotion being dropped.
  '-Werror=old-style-definition',
  # gcc and clang
  '-Werror=missing-include-dirs',
  # gcc only: clang takes the flag and reports nothing under it
  '-Werror=redundant-decls',
  # gcc and clang
  '-Werror=undef',
  # gcc only, clang taking it and reporting nothing; and the one entry
  # here that is not a promotion. Level 2 is not empty -- gcc reports a
  # comparison of an absolute value against zero under it, at every -O,
  # and level 1 reports nothing there -- but what it reports depends on
  # what the optimiser made of the code rather than on what the code
  # says, and the higher levels name simplifications a correct program
  # invites all the time. So it tells a reader something and does not
  # stop a build.
  '-Wstrict-overflow=2',
]
add_project_arguments(cc.get_supported_arguments(kept_clean), language: 'c')

# -Werror=format-security is asked for beside -Wformat rather than alone.
# The warning it promotes is one -Wformat raises, and a compiler that is
# handed the promotion without the warning may refuse the pair outright:
# one of the compilers this builds with makes that an error, so a probe
# offering the flag by itself is answered no and the promotion is
# silently dropped on that platform. The real build has -Wformat from
# warning_level, so only the probe needs the two together.
format_security = [ '-Wformat', '-Werror=format-security' ]
if cc.has_multi_arguments(format_security)
  add_project_arguments(format_security, language: 'c')
endif

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

# Link arguments for a test that reaches past the public API into the
# library's internals. Only the API carries an import declaration, so on
# Windows the linker resolves the rest with a 32-bit runtime relocation
# that the C runtime applies before main(). That relocation reaches 2G,
# and the 64-bit address layout scatters images across the whole address
# space, so whether a test lands within reach of the library it calls is
# chance: the same binary runs or aborts before main() from one start to
# the next, and optimisation decides which references take the form at
# all. Refusing the fallback makes the linker resolve the references
# through the import table, and turns one it cannot resolve that way into
# a link error naming the symbol rather than a start-up abort.
internals_link_args = []
if sys_windows
  internals_link_args = cc.get_supported_link_arguments('-Wl,--disable-auto-import')
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

# The switches an investigation turns on to take a guarantee away from a
# run -- the collector's reuse, for one -- so that what depends on it
# shows itself. They are read from the environment, which is the host's
# and not the library's, so they are compiled in only when a build asks
# for them: a shipped library that reads them lets whoever can set a
# variable in the process decide, for instance, whether the collector
# collects. Configure with -Ddebug-hooks=true to investigate.
if get_option('debug-hooks')
  config_h.set('WANT_DEBUG_HOOKS', 1)
endif

zlib_dep = dependency('zlib', required: false)
if zlib_dep.found()
  config_h.set('HAVE_ZLIB', 1)
endif

xcb_dep = dependency('xcb-image xcb-icccm xcb xcb-aux', required: false)
if xcb_dep.found()
  config_h.set('HAVE_XCB', 1)
endif

# the maths library is a separate object on unix and part of the C runtime
# on windows; asking for it either way keeps the name defined on both, so a
# target that needs maths does not have to know which platform it is on
m_dep = cc.find_library('m', required : false)

if sys_windows
  bcrypt_dep = cc.find_library('bcrypt')
  gdi_dep = cc.find_library('gdi32')
  gl_dep = cc.find_library('opengl32')
  winsock2_dep = cc.find_library('ws2_32')
else

  if cc.has_header('sys/select.h')
    config_h.set10('HAVE_SYS_SELECT_H', true)
  endif

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

# ---- test profiles ----
#
# A test's suites are two independent axes, so a selection can name
# either without naming the other.
#
# What a test is about
#   xpost     the interpreter's own suite: everything the tree can run
#             out of itself
#   corpus    the differential run over real programs fetched into
#             tests/corpus, which skips until they are
#   vendor    a downstream consumer's own test suite, run out of a
#             checkout of its own, which skips until there is one
#   memacct   the five that read the memory a run holds rather than
#             the answers it gives -- two weighing the process at the
#             end of it, three running it under a leak checker -- and so
#             cannot answer under an allocator that quarantines or
#             shadows
#
# The corpus and the vendor suite are the two that need something this
# tree does not carry, and the cost profiles below leave both out
# for the same reason: a profile that reports one skip on the machines
# without them and none on the machines with them says something
# different on each, and the one it says nothing about is the run that
# did less work.
#
# What a test costs, read off its own run time with the suite run one
# test at a time on an otherwise idle machine.
#   fast      under five seconds
#   slow      five seconds to fifty
#   veryslow  longer than fifty
#
# The upper boundary is where the measurement has its one large gap: the
# whole suite runs in a smooth tail up to twenty-two seconds and then
# jumps by a factor of four to two runs measured in minutes. Both of
# those are corpora, so nothing the tree runs out of itself is veryslow,
# and there is no test to promote into the band -- the tail below it has
# no gap at all. The tag is still what keeps a four-minute run out of
# the cheaper selections the moment one appears, and it already selects
# on the other axis, where the two expensive corpora carry it and the
# two cheap ones do not. What it cannot do is divide the profiles: a
# range drawn at the top of the slow band and a range drawn above it
# hold the same tests, so there is one profile over them and not two.
#
# The lower boundary has no gap to sit in -- the tail is dense -- so it
# is set by what the tag is worth: sixteen of the interpreter's tests
# take five seconds or more, which is eight in a hundred of them and
# half of what the suite costs. Below five seconds a tag would name
# many tests and save little.
#
# Every registration names exactly one cost, so a test cannot join the
# cheap profile by leaving the question open; tests/check-test-cost.sh
# holds the registrations to that, and holds the names to being spelt
# only here and in the profile wrapper -- a second list of what is slow
# is a second answer.
#
# The two axes cross, which is the point of keeping them apart: the
# corpus programs that run quickly are reachable without the ones that
# do not.
#
#   ninja -C build quick        cost fast                -- while editing
#   ninja -C build full         every cost               -- the tree's own suite
#   ninja -C build corpus       the corpus, all costs    -- with programs fetched
#   ninja -C build vendor       the vendor suite         -- with its checkout
#   ninja -C build everything   all of the above         -- and nothing skipped
#   meson test -C build         every test, as before
#
# `full` is not a verdict on the tree, and reading it as one is the
# mistake this rung exists to stop. It is every test the tree runs out of
# itself, which is every test but the ones that need something given to
# them: the corpus and the consumer suite are outside it by the argument
# above, and a green `full` says nothing whatever about either. Both find
# faults `full` cannot -- a corpus program is a page of real PostScript
# and the suite's own tests are not -- so a round of work that ends on a
# green `full` has been measured against the part of the instrument that
# was pointing at it.
#
# `everything` is the rung that is a verdict. It selects the whole of the
# build's tests, and it refuses to pass while any of them merely skipped:
# a skip is how a test says it was given nothing to work on, and the
# summary reports no failures either way, so a rung claiming everything
# ran has to read the run's record and hold itself to it. Where a machine
# genuinely cannot supply one -- the adobe corpus has no download, and a
# consumer checkout is a checkout -- XPOST_ALLOW_SKIP names it for that
# run and the verdict line says what the run does not speak for. The
# excuse lives in the invocation rather than in the tree, so there is no
# register of permitted absences to go on excusing something after it
# stops being absent, and a name excused that turns out to have run fails
# the run in its own right.
#
# What the cheap rung does not answer is the memacct question. Two of
# the five tests that ask it are in the quick profile, and both weigh
# the process once it has finished; the three that run the interpreter
# under a leak checker are all slow, and none of them can be made cheap.
# The checker's cost here is start-up and not workload -- the
# interpreter reads its whole start-up program before a test's first
# line, which takes about seven seconds under the checker against a
# quarter of a second without it -- so a leak-checking test cannot come
# in under the fast boundary, and one that dropped the graphics language
# to squeeze under it would be a narrower instrument bought with the
# coverage it gave up.
#
# So quick is blind to the faults that leave every assertion true:
# memory held past its last use, and a read outside the object it
# belongs to that hands back a plausible value. Its tests read what the
# interpreter computes; only the checker reads what it touches. That is
# what the full profile adds beyond the tests it takes longer over, and
# it is the reason to run it before a commit.
#
# Each target goes through tests/run-profile.sh, which is where the
# profiles are spelt and which checks the selection before running it.
# A meson suite filter naming a suite nothing carries is inert and exits
# zero either way -- it runs the whole suite where it meant to subtract,
# or none of it where it meant to select, and reports success. The
# wrapper computes what the profile names from the test listing and
# refuses unless the filter picks exactly that.
#
# Any other crossing by hand, for instance the corpus programs that are
# not the expensive ones, goes through meson directly and carries that
# risk:
#
#   meson test -C build --suite corpus --no-suite veryslow
#
# The targets run meson test with no -j, so it takes its own default of
# one process per core.
cost_fast = ['xpost', 'fast']
cost_slow = ['xpost', 'slow']
cost_veryslow = ['xpost', 'veryslow']

run_profile = find_program('tests/run-profile.sh')
foreach profile : [ 'quick', 'full', 'corpus', 'vendor', 'everything' ]
  run_target(profile, command: [ run_profile, profile ])
endforeach

# Core-interpreter conformance: run the PLRM operator-example suite through
# the built interpreter and require its failcount to reach zero. The build-tree
# binary is not installed, so point it at the source data via XPOST_DATA_DIR.
test('interpreter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('data/test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
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
  suite: cost_fast,
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
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedding contract: the interpreter is one instance holding a table of
# execution contexts, so a second xpost_create while an instance is live is
# refused and the live one carries on.
single_instance_exe = executable('single_instance_test',
  'tests/single_instance_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('single-instance',
  single_instance_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedding contract: a process may create, use and destroy contexts for as
# long as it lives, so no per-context table or allocation may carry over.
context_reuse_exe = executable('context_reuse_test',
  'tests/context_reuse_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
# In the memacct suite: it weighs the process, so it cannot answer under
# an allocator that keeps freed memory in quarantine and carries shadow
# maps of its own.
test('context-reuse',
  context_reuse_exe,
  suite: cost_fast + ['memacct'],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The same embedding runs with the collector set to collect constantly. What
# the interpreter holds outside the collector's root set survives by luck at
# the ordinary threshold and not at all at this one.
test('gc-pressure',
  find_program('tests/run-gc-pressure-test.sh'),
  args: [ run_status_exe, context_reuse_exe ],
  suite: cost_fast + ['memacct'],
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
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Object-layer bounds discipline: a string object with a corrupt or
# sentinel ent yields NULL from the pointer adapter, never a wild pointer.
string_bounds_exe = executable('string_bounds_test',
  'tests/string_bounds_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('string-bounds',
  string_bounds_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Memory-layer bounds discipline: the pair every composite reads and
# writes its storage through refuses a slot outside the allocation, and
# refuses it without copying -- read back from the neighbour and from
# the destination of the refused read, since a return value nobody looks
# at is served equally by a function that refused and one that did not.
# The bound is taken in a width wider than the index, so a slot number
# whose product wraps back inside the allocation is refused too.
memory_bounds_exe = executable('memory_bounds_test',
  'tests/memory_bounds_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('memory-bounds',
  memory_bounds_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What the free list takes in and hands back out. Its links live inside
# the freed entities' data, so an entity of no size has nowhere to carry
# one and would be linked through the next allocation's first word --
# and a link a stale write has turned into an arbitrary number names a
# table slot the walk must not read. Neither case can be presented from
# PostScript, and neither refusal is legible in a return value: the
# first is read off the neighbour's bytes and off the list, the second
# off the entity the walk declines to hand out and off the buckets it
# empties on the way.
free_list_admission_exe = executable('free_list_admission_test',
  'tests/free_list_admission_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('free-list-admission',
  free_list_admission_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What that refusal is worth. Emptying the buckets throws away every
# entity the list held, so the answer the allocator gives has to reach a
# collection: an allocator that emptied them and reported a plain
# failure would leave the run with no free list at all, and nothing
# about it visible from PostScript. Held over a real interpreter, since
# a collection takes the stacks for its roots.
free_list_recovery_exe = executable('free_list_recovery_test',
  'tests/free_list_recovery_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('free-list-recovery',
  free_list_recovery_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What the free list costs to allocate out of. Releasing a great many
# composites of one size and then allocating smaller ones is the shape
# an ordinary job makes between pages, and it is the shape in which a
# search that reads a bucket to the end charges the request for
# everything the job has already released. That cost grows for as long
# as a job runs, so nothing short measures it; the interpreter counts
# the entries an allocation examines, and the assertion is that count,
# held both to a ceiling and to being the same after four times as much
# was released.
test('free-scan',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/free_scan_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# The capacity a dictionary reports, against the header field that
# carries it. A capacity asked for above that field's width is a request
# only the library makes -- `dict` refuses one before any dictionary is
# built -- and a capacity stored wider than its field reads back as a
# far smaller number. It has to be read from an empty dictionary: the
# first entry raises a capacity the dictionary has outgrown to the size
# of its table, which would cover a truncated one.
dict_capacity_field_exe = executable('dict_capacity_field_test',
  'tests/dict_capacity_field_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dict-capacity-field',
  dict_capacity_field_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What a deletion does to the entries that follow it. The table probes
# linearly, so emptying a slot in the middle of a run of collided
# entries would cut the rest of the run off from their homes unless
# those whose home lies inside the closed-up stretch are moved back into
# the hole. Which slot a key takes is the hash's business, so the keys
# are chosen by where they land and each configuration is checked to be
# the one intended before the deletion that tests it.
dict_undef_reslot_exe = executable('dict_undef_reslot_test',
  'tests/dict_undef_reslot_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dict-undef-reslot',
  dict_undef_reslot_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Stack-layer bounds discipline: a bottom-up index is answered by
# choosing a segment and then a slot within it, and the two boundaries
# that meet there -- the index one segment long, which is the second
# segment's first element, and the index one past the top, which is a
# slot the stack has given up -- are each a segment length from anything
# the interpreter could be driven to to reach them.
stack_bounds_exe = executable('stack_bounds_test',
  'tests/stack_bounds_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('stack-bounds',
  stack_bounds_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The snapshot of the top of a stack converges. copy, roll and the
# interpreter's own start-up all take one, so a walk that does not
# converge stops every test in the suite before any of them reports
# anything, and nothing is left to say so but a harness noticing hours
# later. The walk is therefore run in a child under a bound on the work
# it may do and the parent, which walks nothing, reports both the answer
# and its absence. POSIX-only: the bounded run is a fork with a CPU
# budget on it, neither of which the Windows CRT provides.
if host_machine.system() != 'windows'
  stack_peek_bound_exe = executable('stack_peek_bound_test',
    'tests/stack_peek_bound_test.c',
    c_args: '-DHAVE_CONFIG_H',
    dependencies: libxpost_dep,
    link_args: internals_link_args,
    include_directories: [ config_dir, include_directories('src/lib') ],
    install: false)
  test('stack-peek-bound',
    stack_peek_bound_exe,
    suite: cost_fast,
    env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
endif

# What a collection decides about one allocation: kept while a stack
# refers to it, handed back once none does. Every other test of the
# collector reads a total, which any allocation's fate moves, so a
# collector reading a stack one slot past its elements -- and so keeping
# whatever the program last finished with -- is invisible to all of
# them.
gc_marking_exe = executable('gc_marking_test',
  'tests/gc_marking_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('gc-marking',
  gc_marking_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# How much of an allocation a save's backup says is in use. The extent
# is what the collector reads an allocation's contents by, so a backup
# that records the block's capacity instead has the collector reading
# the block's previous owner's leavings as objects -- which ends
# marking, and with it every later collection, without saying so.
save_backup_extent_exe = executable('save_backup_extent_test',
  'tests/save_backup_extent_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('save-backup-extent',
  save_backup_extent_exe,
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The editing streams open a temporary file before the text they are asked
# for is known to be acceptable, so a refusal is the only thing that can
# close it: a request refused for nesting depth must leave no descriptor
# behind.
edit_stream_close_exe = executable('edit_stream_close_test',
  'tests/edit_stream_close_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('edit-stream-close',
  edit_stream_close_exe,
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'ASAN_OPTIONS': 'allocator_may_return_null=1' })

# Memory-table bookkeeping under dictionary growth: dicgrow's entity swap
# must move (adr, sz, used) together; the invariant used <= sz holds for
# every ent after repeated regrowth.
dict_grow_exe = executable('dict_grow_test',
  'tests/dict_grow_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
# Growing a dictionary past its allotment: the wide build's dictionaries
# reach far further before the size field fills, so the same test does
# very much more work there.
test('dict-grow',
  dict_grow_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# How far the dictionary hash spreads its keys, read off the hash values
# a filled dictionary stored and the distance its entries sit from the
# slots those name. A dictionary holds the right entries whatever the
# hash does, so a hash blind to a whole class of keys is invisible from
# the contents and shows only as a lookup that walks the table. The
# fields an object occupies differ between the two builds, so the bound
# is asked of each.
dict_hash_spread_exe = executable('dict_hash_spread_test',
  'tests/dict_hash_spread_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: libxpost_dep,
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dict-hash-spread',
  dict_hash_spread_exe,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# Collecting over a dictionary whose hash table is longer than a
# composite's size field can count. Growing such a dictionary is covered
# above, but growth alone never walks the table the way the collector
# does, so the size at which the walk stops converging went unseen. The
# threshold is a property of the default build's narrower fields; the
# same sizes are unremarkable in the wide one, so both run the test.
test('gc-large-dict',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gc_large_dict_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# The same walk, in a collection nothing asked for. The ordinary
# threshold is a gigabyte of allocation, so the run that provokes one
# from a test has to bring the threshold down, and that is an
# environment setting rather than something the program can reach --
# hence a run of its own rather than a section of the above.
test('gc-large-dict-pressure',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gc_large_dict_pressure_test.ps') ],
  env: {
    'XPOST_DATA_DIR': meson.current_source_dir() / 'data',
    'XPOST_GC_THRESHOLD': '4000',
  },
  suite: cost_fast,
  timeout: 300)

# How deep and how wide the marking walk reaches. The depth of a chain
# of composites is bounded by the entity numbers and the memory it
# spends and by nothing else, so a walk that took a C frame per link
# would end the process on data a program is entitled to build; the
# width of a container is bounded by the size field, which is more than
# a fixed worklist holds at once. Depth and breadth are separate
# mechanisms and each fixture reads its marker back through the fixture,
# so a walk that stops short is a lost marker and not only a crash.
test('gc-deep-chain',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gc_deep_chain_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# A collection nothing asked for, provoked by the entity table rather
# than by the bytes behind it. A finely divided clipping region costs
# tens of thousands of entity slots and the next clip leaves every one
# of them to the collector, so a job that builds region after region
# says whether the collector is asked often enough to keep handing them
# back. The count is asserted, not merely the absence of an error: the
# ordinary build reports too few collections as an exhausted table, the
# wide one as a table that grows without limit, and only the count sees
# both.
test('gc-clip-region',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gc_clip_region_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# Enumerating over that same table, and over the largest array and
# string a build has. A dictionary's table is longer than a composite's
# own fields count, so an enumeration whose cursor lives in one of them
# wraps and repeats without end; an array or a string is exactly as long
# as such a cursor reaches, which is what makes the dictionary's case a
# bug rather than a limit. Both sides of the boundary run in both
# personalities, which agree on every count.
test('forall-large',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/forall_large_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# Object semantics: file-object comparison and user-space pathbbox.
test('semantics',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/semantics_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The clip is one value wearing several faces, and one procedure writes
# all of them. A site that sets one and not the rest leaves the others
# describing a clip that has gone, which no rasterised render disagrees
# with -- so the rule is held structurally rather than by a render.
test('check-clip-writer',
  find_program('tests/check-clip-writer.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# The clip region: what a program may do to it, and whether the faces of
# the one clip value -- the resolved region and the path shape a device
# that clips for itself is handed -- still describe the same clip.
test('clip-region',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/clip_region_test.ps') ],
  suite: cost_slow,
  # a slow test on an idle machine is a marginal one on a busy machine:
  # this needs about a third of the default budget alone and has been
  # killed at the default while the rest of the suite ran beside it
  timeout: 120,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A clip region can end up covering no pixels at all. PLRM 7.5.1 makes
# what a painting operation marks the intersection of two sets of pixels,
# so an empty one is an answer and not a fault: every painter paints
# nothing and none of them reports anything. Each is asked twice, once
# under a region that covers it, so "marked nothing" is the region's doing
# and not the operator declining to work.
test('empty-clip',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/empty_clip_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Text meets the clip region too. A glyph reaches the page as a raster
# plotted straight through the device's pixel methods, never through the
# fill pipeline where a region is met, so the region has to be met on
# that route -- and met as the same pixels, which a solid fill of the
# page under the region settles for every operator of the family.
test('show-clip',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/show_clip_test.ps') ],
  suite: cost_slow,
  timeout: 120,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A pattern colour is not a colour a pixel can be set to, so text painted
# in one leaves the raster route for its outlines and the ordinary fill.
# Every operator of the family is held to the project's rule for
# patterns: a pattern fill covers exactly the pixels a solid fill of the
# same path covers, which makes the oracle the same glyphs sent round by
# charpath and filled with the same pattern.
test('show-pattern',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/show_pattern_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# An outline font's glyphs are its character outlines stroked with
# StrokeWidth, not the filled coverage the glyph raster route plots, so
# text in one leaves that route for its outlines and the ordinary
# stroke. Every operator of the family is held to the same glyphs sent
# round by charpath and stroked with the same width.
test('outline-font',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/outline_font_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A composite font walked by a CMap reaches its glyphs by index, a route
# to the page of its own, and it makes the same choices the operators of
# a simple font make: text the glyph raster cannot carry leaves that
# route for its outlines. The oracle is the same glyphs of the same font
# program reached through a simple font beside it.
test('cid-show',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/cid_show_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What an operator leaves on the operand stack when it refuses. An error
# raised in PostScript with signalerror ends in a stop and never reaches
# the interpreter's handler, so the unwinding every other error gets has
# to be asked for on that path -- without it an operator's body left its
# working values behind, and for the machinery's own operators those are
# structures a program then holds.
test('error-unwind',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/error_unwind_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Graphics regression checks: operator wrapping and the wrapped-operator
# error posture, asserted through the interpreter's private operators.
test('graphics-regression',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/graphics_regression_test.ps') ],
  suite: cost_slow,
  timeout: 120,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Render classes: Type 3 and composite fonts, the shading types, tiling and
# shading patterns, and screen/halftone setup. Standard PostScript that prints
# SUCCESS on the reference interpreters too, so it stands as a conformance test.
test('render-classes',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/render_classes_test.ps') ],
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every painting operator through every device that runs without a display.
# Which methods a device must implement itself depends on how it keeps its
# raster, so a device can fill and stroke for years while being unable to
# paint a glyph -- which is what three of them were doing.
test('device-features',
  find_program('tests/run-device-features-test.sh'),
  args: [ xpost_exe, files('tests/device_features_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_slow,
  timeout: 180)

# wrapper requires the output to be exactly SUCCESS.
test('raster-class',
  find_program('tests/run-strict-ps-test.sh'),
  args: [ xpost_exe, files('tests/raster_class_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Form cache on a raster device: caching an execform builds an image device and
# clears it with the rect-fill C operators, run through the ppm device. Guards
# the regression where those operators, relocated into .internaldict, could no
# longer be fetched by name.
test('execform-cache',
  find_program('tests/run-ppm-test.sh'),
  args: [ xpost_exe, files('tests/execform_cache_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Bounding-box device: render a known fill through -d bbox and check the box.
test('bbox-device',
  find_program('tests/run-bbox-test.sh'),
  args: [ xpost_exe, files('tests/bbox_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# PDF device: write a PDF and check the document against itself -- its
# structure, its cross-reference geometry, the colour spaces its marks
# reach the content stream in, its page tree and its metadata. The input
# is a single fill large enough to overflow the content buffer,
# exercising the flush path.
test('pdf-device',
  find_program('tests/run-pdf-test.sh'),
  args: [ xpost_exe, files('tests/pdf_bigfill_test.ps') ],
  suite: cost_slow,
  timeout: 120,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The same PDFs read by consumers that did not write them: qpdf parses the
# document as a reader does, and poppler images it so that what it paints
# can be compared with what the interpreter paints from the same drawing.
# A convention held consistently and wrongly at both ends of our own code
# passes every self-contained check and fails here. Skips where neither
# consumer is installed; neither is a build dependency.
test('pdf-consumer',
  find_program('tests/run-pdf-consumer-test.sh'),
  args: [ xpost_exe ],
  suite: cost_slow,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# PDF content appends under memory pressure: a FillPoly whose content
# append fails must raise rather than leave a truncated page that
# reported success. Skips where the shell cannot impose the limit.
test('pdf-append-fail',
  find_program('tests/run-append-fail-test.sh'),
  args: [ xpost_exe, files('tests/pdf_append_fail_test.ps'), 'pdfwrite' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# The same append-completeness contract on the svgwrite device, whose
# FillPoly emits the fill colour through the same unchecked hot loop.
test('svg-append-fail',
  find_program('tests/run-append-fail-test.sh'),
  args: [ xpost_exe, files('tests/svg_append_fail_test.ps'), 'svgwrite' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# Access attributes in the device blitters: painting into a raster row a
# program restocked with a read-only string raises invalidaccess and
# leaves the read-only bytes untouched (PLRM 3.3.2).
test('blit-access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/blit_access_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The counts a blit dictionary carries index fixed per-component and
# per-native-byte tables inside the operator, so a count outside what
# those tables hold is a rangecheck rather than a read past them.
test('blit-range',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/blit_range_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The current path is a packed string in a program-reachable dictionary,
# so the extent and the two element offsets its header carries are
# program data: a field naming more than the string holds is a
# rangecheck rather than a walk past it.
test('path-bounds',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/path_bounds_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# What a device instance dictionary holds under /Private is a handle on a
# block of state outside virtual memory, so a value of the program's own
# -- or a copy of a genuine handle -- names no block and is reported
# rather than followed.
test('device-private',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/device_private_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A screening device's threshold cell is addressed by two dimensions
# carried beside it in the device dictionary, so a pair of dimensions
# the cell does not hold leaves the fill unscreened rather than reading
# past the cell.
test('halftone-cell',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/halftone_cell_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A captured glyph mask is copied into the cache at the dimensions its
# dictionary states, so dimensions the coverage bytes do not hold are a
# rangecheck rather than a copy past them.
test('mask-cache-range',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/mask_cache_range_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# .swap exchanges two elements by index, and an index outside the array
# names an element of whatever else the backing entity holds, so it is a
# rangecheck at both ends.
test('array-swap-range',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/array_swap_range_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Releasing a wrapped call clears the saved-operand array back to the
# mark its run began at, bounded by what that array holds rather than by
# the size the interpreter's own array is built at.
test('wrapped-save',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/wrapped_save_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# SVG device: render fills, strokes and glyph outlines through
# setpagedevice and require the document's structural landmarks,
# including resolution-independent point sizing.
test('svg-device',
  find_program('tests/run-svg-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# pngalpha device: RGBA output where the erased page is transparent and
# marks (including explicit white) are opaque; png stays RGB.
test('pngalpha-device',
  find_program('tests/run-pngalpha-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Save/restore VM: drive the entity count past 65536 and require dicts and
# arrays to revert exactly -- guards the saverec entity-width truncation.
test('save-restore',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Local/global VM integrity: the put operators reject a local value entering a
# global container and accept the reverse; a characterisation guard records
# that literal construction ([ ] and { }) skips the check.
test('vm',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/vm_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# roll operator: correctness (checked against Ghostscript) plus a large-roll
# performance guard -- an O(n^2) roll on a deep stack times the test out.
test('roll',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/roll_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# counttomark and the [ ] / << >> constructors built on it: correctness plus
# a performance guard -- an O(n^2) counttomark on a deep stack times the test out.
test('counttomark',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/counttomark_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# In-RIP trapping procedure set (PLRM 6.3): settrapparams/currenttrapparams
# maintain the trapping-parameter state, settrapzone accepts a zone.
test('trapping',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/trapping_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Arithmetic/math operators (PLRM 8.2): zero-divisor mod/idiv are undefinedresult
# not a SIGFPE, sqrt of a negative is rangecheck, and atan/sin/cos hit the PLRM
# examples (a truncated RAD_PER_DEG had skewed them). From the 8.2 compliance campaign.
test('op-math',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_math_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# writehexstring over the whole byte range: every byte must encode as two
# hexadecimal digits and read back identically, including above 0x7F where a
# signed character index would run off the front of the digit table.
test('op-hexstring',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_hexstring_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Execution-context operators, an extension to the language: fork, join, detach, yield and
# currentcontext. A forked context is never scheduled, so join reports in
# bounded time instead of waiting forever, and a fork with no free table
# slot is an error rather than an index into an unrelated slot.
test('op-context',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_context_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 60)

# Control operators (PLRM 8.2): repeat of a negative count is rangecheck (PLRM: the
# count is a nonnegative integer), plus if/ifelse/for/loop/exit/stopped behaviour.
test('op-control',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_control_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# exit across every PLRM looping context, including filenameforall, whose loop
# frame must carry the sentinel exit searches for so an exit inside its
# procedure ends that loop (and only that loop) and releases the glob results.
test('loop-exit',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/loop_exit_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Matrix/coordinate and graphics-state operators (PLRM 8.2): a bad-length matrix
# is rangecheck (was an out-of-bounds read), setlinecap/join outside 0..2 and
# setmiterlimit below 1 are rangecheck, and rotate uses full-precision degrees.
test('op-matrix-gstate',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_matrix_gstate_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A wrong-typed operand is a typecheck, not a stackunderflow, when a higher-arity
# signature's operands are absent (PLRM/Distiller; gs differs for the matrix
# builders, so this one is xpost-only).
test('op-dispatch-typecheck',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_dispatch_typecheck_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Path-construction operators (PLRM 8.2): currentpoint tracking, nocurrentpoint,
# and arc endpoints on the axes (full-precision degrees).
test('op-path',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_path_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Rectangle operators (PLRM 8.2): rectfill/rectstroke/rectclip accept the
# numarray and numstring forms and rectstroke an optional trailing matrix.
test('op-rect',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_rect_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Resource and user-object operators (PLRM 8.2): resourcestatus on a category
# that does not exist raises undefined (was false); the UserObjects rules.
test('op-resource',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_resource_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# status operator (PLRM 8.2): the (filename) form returns pages/bytes/referred/
# created/true or false, alongside the existing file-object boolean form.
test('op-status',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_status_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# User/system parameter operators (PLRM 8.2): current* return a parameter
# dictionary and set* apply the parameters they recognise.
test('op-userparams',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_userparams_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# bytesavailable (PLRM 8.2): remaining bytes for a seekable file, -1 for a filter.
test('op-bytesavailable',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_bytesavailable_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Operand restoration on error (PLRM 3.11): an operator error restores the
# operands, including the integer types the program pushed.
test('op-error-restore',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_error_restore_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Array packing mode (PLRM 8.2): setpacking/currentpacking and read-only procs.
test('op-packing',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_packing_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# bind (PLRM 8.2): bind rewrites packed arrays and descends into nested ones,
# but leaves ordinary read-only arrays untouched -- and freezes the operators.
test('bind-packed',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/bind_packed_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# vmreclaim (PLRM 8.2): 2 (local and global) reclaims local VM like 1 (local).
test('op-vmreclaim',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_vmreclaim_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Integer-literal range (PLRM 3.3.2): an over-range constant becomes a real.
test('op-intoverflow',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_intoverflow_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# SIGINT raises the PostScript interrupt error between evaluation steps;
# errordict's handler stops the job and the interpreter exits. POSIX-only.
test('interrupt',
  find_program('tests/run-interrupt-test.sh'),
  args: [ xpost_exe, files('tests/interrupt_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Composite-size limits: the small-object build holds Appendix B.1's 65,535
# ceiling with limitcheck; the large-object build carries composites to
# memory. The test probes the build and holds it to its own contract.
# The object width sizes the integer and the real as well as the
# composite, so the same program is arithmetic of one precision in one
# build and another in the other. That is deliberate and PLRM settles
# neither -- the appendix stating both ranges says it describes what
# implementations typically do -- but a test that reads whatever a build
# produces and asks only whether it equals itself passes under both and
# reports nothing. This writes each build's answers down, and probes the
# three faces of the width apart so a build that widens one and not the
# others names which.
test('build-personality',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/build_personality_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast)

test('large-object',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/large_object_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# The other limit the object's width sets: how many composites there may
# be at once, each taking one entity number of the memory table whatever
# its size. It is an implementation limit like the composite size above,
# so it is limitcheck, and a program can be inside every limit PLRM names
# and still reach it. The whole of the contract is what happens there --
# the error is a program's to catch, and the interpreter that raised it
# can still name it, run the next statement and report -- because
# reporting wants entity numbers of its own.
test('entity-limit',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/entity_limit_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# The same ceiling reached along every path that builds a composite and
# every path that extends one, at the last size the size field describes
# and at one past it. A constructor that counts what it is given in
# units other than the ones the field holds refuses a size its own
# operator equivalent accepts, or accepts one the field then truncates;
# neither is visible from a composite of the right length, so each path
# is asked for its length as well as for its error.
test('composite-limit',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/composite_limit_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# The capacity a dictionary reports against the length it reports (PLRM
# 8.2 maxlength, "at least as large as that returned by length"). A
# dictionary's table is over-allocated from the size asked for, so it
# takes entries past that size before it grows; a capacity that stayed
# at the size asked for would be smaller than the dictionary's own
# length for every entry in that window, at every size.
test('dict-capacity',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_dict_capacity_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# Unterminated string literals: end of input inside (...), <...> or <~...~>
# is a syntaxerror on both scan paths (PLRM is silent; Distiller and gs agree).
test('scanner-eof',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/scanner_eof_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Radix number range (PLRM 3.2): unsigned twos-complement in the integer width,
# limitcheck beyond it. Integer-width-specific, so xpost-only (see the file).
test('op-radix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_radix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvi/cvr number conversion (PLRM 8.2): reject non-PostScript syntax; cvi of an
# over-range real is rangecheck, not a wrap.
test('op-cvi',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvi_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Graphics-state number parameters are reals (PLRM), matching Distiller and gs.
test('op-gstate-types',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_gstate_types_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvs yields --nostringval-- for every type outside the string-representable set.
test('op-cvs',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvs_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# array and string get/put require an integer index (a real is a typecheck).
test('op-index-type',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_index_type_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# reading from a closed file reports end-of-data rather than raising an error.
test('op-closed-read',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_closed_read_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# save/restore (and gsave/grestore) must revert the CTM. scale/concat/rotate
# write the current-matrix array in place; that write must be backed up for
# save/restore. Guards the regression where it was not -- a transform set inside
# a save leaked past the restore, so dvips documents (casselman) rendered text
# at a runaway scale and overflowed the operand stack.
test('save-restore-ctm',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_ctm_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Interpreter isolation: internal helper procedures hide in .xpostsys, so their
# names neither pollute nor obscure a program's own, and a program may redefine
# them without disturbing the graphics machinery (a clash that broke real
# programs on xpost). Also covers surviving a program shadowing get/exec.
test('isolation',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/isolation_test.ps') ],
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# String data sources for filters (PLRM 3.8.4): a filter may read from a string.
test('op-string-filter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_string_filter_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# readstring (PLRM 8.2): verbatim transfer, the boolean result at end of
# file, and the rangecheck a zero-length string operand raises.
test('op-readstring',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_readstring_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Boundaries of the file operators (PLRM 3.8, 8.2): empty operands, end of
# data, closed files, files that do not exist, and the operators whose
# answer on a filter differs from a positionable disk file.
test('op-file-edges',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_file_edges_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every file operator against every filter. Which methods a filter carries
# itself depends on how it holds its data, so it can read perfectly while
# being unable to be reset or closed -- one operator at a time is what a
# per-operator test does not catch.
test('filter-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The predictor stage over a compressed stream: a stage that did nothing
# would return the differences, which read as an image rather than as an
# error.
test('filter-predictor',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_predictor_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every mutating operator against restore. Whether an operator records
# what it overwrites is a property of the operator, not of the type, so
# one reverting says nothing about the next.
test('save-restore-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/save_restore_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every graphics state parameter through gsave and grestore. A parameter
# left out of the copy leaks across the grestore and changes how
# everything painted afterwards looks.
test('gstate-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gstate_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The gstate object: gstate returns a copy of the current graphics state,
# and setgstate takes it back out. A copy holding the defaults instead
# answers every parameter with the default and looks like a state that
# was reset -- and the default clip region admits no ink at all.
test('gstate-object',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/gstate_object_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every colour space family through every colour operator. Each family
# decides for itself how many components it takes and how a colour in it
# is recorded, so one working says little about the next.
test('colorspace-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/colorspace_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Program code is called out to through one bracket. This holds the tree
# to it: every graphicsdict slot is registered as one a bracket saves or
# one the render owns outright, and the dictionary-scope escape has one
# spelling. Both failures are silent until a program re-enters or raises.
test('callout-bracket',
  find_program('tests/check-callout-bracket.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# Every procedure the graphics machinery calls out to, asked whether the
# caller's state survives the call. A callback is the one moment the
# machinery is half way through its own work with the program holding
# the interpreter, and each site decides for itself what it saves.
test('callback-reentry',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/callback_reentry_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every text-showing operator against both kinds of font. Each operator
# walks its string its own way, so one positioning a glyph correctly says
# nothing about the next.
test('showop-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/showop_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every function type against every shading that can take one. A branch
# per type means one evaluating correctly says nothing about the next.
test('function-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/function_matrix_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# Every resource operator against every category the interpreter admits
# to, asked for rather than listed, so a category added later is held to
# the same contract without editing the test.
test('resource-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/resource_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every way of presenting an image against every way of painting one:
# sample depth, data source, operator and transform are each decoded by
# a branch of their own.
test('image-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/image_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every halftone dictionary fault against the error it is defined to
# raise: the type, the missing entry and the wrongly-typed entry each
# have their own answer, and raising something is not raising the right
# thing.
test('halftone-matrix',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/halftone_matrix_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every setter of a transfer function or a spot function, asked whether
# it calls the procedure it is given. Setting one is what calls it, and
# programs lean on the calls, so a setter that only recorded would be
# indistinguishable from a working one until something counted.
test('colour-sampling',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/colour_sampling_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The ordered comparisons take two numbers or two strings, and refusing
# anything else is what separates them from eq and ne.
test('op-compare',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_compare_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Operators handed an operand of a type they cannot want. Accepting one
# is not a lenient failure: it answers a question with no answer, or
# stores what can never be read back.
test('op-operand-types',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_operand_types_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# An operator written in PostScript stating the operands it takes, so
# the dispatcher enforces them as it does for one written in C.
test('op-wrapsig',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_wrapsig_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The two font directories hold different populations: the local one goes
# away with the save level that defined its fonts, the global one does not.
test('font-directory',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_directory_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Where a pattern's tiles land: the tiling is anchored in pattern space,
# so the path being filled moves neither its phase nor its period.
test('pattern-phase',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/pattern_phase_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Where every group of names ends up: which dictionary, which VM, and
# what a program is allowed to do to it.
test('startup-surface',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/startup_surface_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Standard operators written in PostScript are operator objects, so bind
# reaches them and a prolog redefining one in terms of itself terminates.
test('op-bind',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_bind_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The operators taking more than one shape of operand list: every shape
# the language defines is reached, and one matching none is refused.
test('op-operand-shapes',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_operand_shapes_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Names implemented twice, in C and in PostScript: which of the two wins
# is settled while the language loads and nothing reports it, so the set
# is declared and checked.
test('check-shadowed-operators',
  find_program('tests/check-shadowed-operators.sh'),
  args: [ meson.current_source_dir() / 'src/lib',
          meson.current_source_dir() / 'data',
          files('tests/shadowed_operators.golden') ],
  suite: cost_fast)

# A standard operator written in PostScript holds the operators it calls,
# not their names: a name is resolved when it runs, so one left in a body
# is a program's redefinition waiting to be picked up. The declared list
# is what an already-bound body put out of bind's reach, and only shrinks.
test('check-wrapped-bind',
  find_program('tests/check-wrapped-bind.sh'),
  suite: cost_fast,
  args: [ xpost_exe, meson.current_source_dir(), files('tests/wrapped_bind.golden') ])

# The same invariant as a program sees it: redefine what a standard
# operator calls, and watch whether the operator picks the redefinition up.
test('wrapped-bind',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/wrapped_bind_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# initgraphics on its own: the fault it guards against is invisible in a
# file that has already been drawing, since the earlier drawing can
# empty the path by itself.
test('op-initgraphics',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_initgraphics_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The showing operators and the state each requires.
test('op-showstate',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_showstate_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The printer's descent into a structure that contains itself.
test('op-printdepth',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_printdepth_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 60)

# What the error dictionary carries and what the standard handler does
# to it: a program handling its own errors reads both.
test('error-dict',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/error_dict_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# An error is named in C, by the ERRORS enumeration, and in PostScript, by
# the list errordict's standard handlers are built from. The two sets have
# to be the same one: a C name with no handler is reported through the
# interpreter's fallback instead of through errordict, and a handler for a
# name C never returns reads as coverage it is not.
test('check-error-names',
  find_program('tests/check-error-names.sh'),
  suite: cost_fast,
  args: [ xpost_exe, meson.current_source_dir() ])

# Nothing in the environment ends the process or takes a guarantee away
# from a run. The library is embedded, so an error belongs to the run that
# caused it and not to the host holding it; and stop with no enclosing
# stopped context is specified behaviour (PLRM 8.2: a message and a quit),
# reached through the one part of a job that lies outside every stopped
# context. Held from both sides: the run is made to take that path under
# the variables and without them, and the sources are read for an ending a
# getenv decides. The collector's reclamation is held the same way, off a
# run that allocates far more than it keeps -- a variable that could stop
# a sweep reclaiming is unbounded growth in a long-running host, so it is
# compiled in only for a build that asks for it.
test('check-env-traps',
  find_program('tests/check-env-traps.sh'),
  suite: cost_fast,
  args: [ xpost_exe, meson.current_source_dir() ])

# The binary form of the error report, which has to be checked on the
# bytes rather than from inside the interpreter.
test('error-binary',
  find_program('tests/run-error-binary-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The virtual-memory parameter operators: vmstatus and globalvmstatus report
# the save level and memory consumption, and vmreclaim takes the five values
# PLRM 8.2 defines for it and refuses the rest.
test('op-vm-param',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_vm_param_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# cvn interns a name from characters that live in the memory file the
# interning allocates in, so the interning must not read them from where they
# used to be. Many names never seen before, interned amid allocation.
test('op-cvn',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_cvn_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Exceeding the number of files a context may hold open is a limitcheck
# (PLRM 8.2, file). Run with the process's allowance lowered so the limit is
# reached at once.
test('op-file-limit',
  find_program('tests/run-file-limit-test.sh'),
  args: [ xpost_exe, files('tests/op_file_limit_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# File lifetime against a small open-file allowance: eight hundred files ended
# by restore, by the program in either order, and as reusable streams, with a
# collection asked for in every round. A descriptor not released shows up as a
# refusal to open rather than as a leak nobody notices.
test('file-lifetime',
  find_program('tests/run-file-lifetime-test.sh'),
  args: [ xpost_exe, files('tests/file_lifetime_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Enumerating a directory holding more names than a composite object's own
# fields count. How many names there are is a property of the directory, not
# of the build, so both personalities enumerate the same two directories --
# one of 65535 names and one of 65536 -- and agree on every count. The
# wrapper makes the directory and skips with its reason where the file
# system will not take that many names in one place.
#
# Making and unmaking that directory is more file-system work than the
# rest of the suite put together, and run alongside the others it slows
# them enough to push one off its own timeout. It therefore runs on its
# own, where what it costs is its own to pay.
test('filenameforall-large',
  find_program('tests/run-filenameforall-large-test.sh'),
  args: [ xpost_exe, files('tests/filenameforall_large_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  is_parallel: false,
  suite: cost_slow,
  timeout: 120)

# An encoded number string is decoded into an array made while the string
# is being read, and making it grows -- and so moves -- the memory file:
# the bytes are reached through the string's entity each time round.
test('numstring-decode',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/numstring_decode_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The interpreter's private machinery dictionary is recorded in the
# context once, and is local: a global one is refused every write the
# machinery makes, and no restore puts back a displaced record.
test('privatedict-install',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/privatedict_install_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The font cache parameter operators: cachestatus reports what setcachelimit
# and setcacheparams set, within whatever range the implementation achieves.
test('op-fontcache',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_fontcache_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The clock operators: realtime counts real time, usertime the time the
# interpreter has spent executing, both in milliseconds from an arbitrary
# origin. Each loop runs until its clock ticks rather than a fixed count.
test('op-time',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_time_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The environment operators: a variable set by putenv has to survive the call,
# and getenv answers its value or reports there is none.
test('op-env',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_env_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# String and number literals as the scanner reads them (PLRM 3.2): the escapes,
# the end-of-line markers inside a string, the hexadecimal form and its missing
# final digit, and the number that will not fit an integer.
test('op-literal',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_literal_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The command line: the geometry option sets the page size and a geometry that
# does not parse is refused, the three reporting options report, -D defines a
# name in userdict before the program starts and -I adds a directory the
# resource machinery searches. Each of the two value-carrying options is asked
# for in both of its spellings and once with the option left out.
test('cli-options',
  find_program('tests/run-cli-options-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The dictionary stack: begin and end above the three permanent entries,
# cleardictstack back down to them, and dictstack's copy of the whole stack.
test('op-dictstack',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_dictstack_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The loop operators resume through continuations whose state sits on the
# execution stack, which is held in segments. Each has a fast path for a frame
# inside one segment and a fallback for a frame astride two; ordinary programs
# run shallow, so only the fast one is otherwise taken.
test('op-loop-depth',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_loop_depth_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A JPEG stream that stops early, or was never a JPEG, must end the decode
# rather than leave it waiting for input that will never come; and a marker
# whose payload the decoder does not read is stepped over rather than parsed.
test('dct-truncated',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/dct_truncated_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 60)

# The other end of the same library: a page it will not compress. Its fatal
# condition ends the process unless the handler installed over it does not
# return, so a page of no height has to come back as a PostScript error the
# program catches, with the interpreter still running afterwards.
test('jpeg-refusal',
  find_program('tests/run-jpeg-refusal-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The interpreter's own debugging printers, which live in the private
# dictionary and which nothing in ordinary use reaches: they walk the memory
# table and the name stacks, so they stop working when either changes shape.
test('internal-debug-ops',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/internal_debug_ops_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# %statementedit gathers standard input until the statement is complete, over
# as many lines as that takes. Nothing else in the suite supplies input, so
# none of the nesting it tracks had ever run.
test('statementedit',
  find_program('tests/run-statementedit-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The collector's own diagnostics, which no ordinary run reaches: an
# independent reachability verifier and the cross-bank scan that holds global
# containers to PLRM 3.7.2. A collection-heavy job must leave both silent.
test('gc-diagnostics',
  find_program('tests/run-gc-diagnostics-test.sh'),
  args: [ xpost_exe, files('tests/gc_diagnostics_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A collection between two pieces of vector output must leave the device's
# open output file alone: the file lives in local memory whatever dictionary
# holds it, and the record holding it is global.
test('vecout-gc',
  find_program('tests/run-vecout-gc-test.sh'),
  args: [ xpost_exe, files('tests/vecout_gc_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The file operator's access strings (PLRM 3.8.1, Table 3.5): all six of
# r/w/a with and without +, the access attribute each confers, and the
# refusal of an access string outside the table.
test('op-file-access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_file_access_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# readline (PLRM 8.2): terminator handling for CR, LF and CRLF, the boolean
# result at end of file, and the rangecheck boundary -- a line that exactly
# fills the string has been read, not overrun.
test('op-readline',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_readline_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# filenameforall enumerates the files matching a template (PLRM 8.2), and a
# template that matches none of them enumerates nothing rather than failing.
test('op-filenameforall',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_filenameforall_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# A filter and the stream beneath it may be closed in either order: the
# stream a filter reads from (or writes to) outlives the file object that
# opened it, and a filter over a closed stream reports end of data.
test('filter-source-life',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_source_life_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Procedure data sources for filters (PLRM 3.8.4).
test('op-proc-filter',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_proc_filter_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# setcachedevice/setcharwidth are valid only within a glyph description (PLRM 8.2).
test('op-glyphcontext',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/op_glyphcontext_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

test('tamper-dispatch',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/tamper_dispatch_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Startup namespace cleanliness: after startup userdict and globaldict are empty
# and systemdict exposes precisely the golden key set, so no internal operator or
# procedure can leak into a userland-visible dictionary unnoticed. A deliberate
# change to systemdict's public surface fails this until the golden set is
# regenerated (tools/gen_namespace_test.py), making every such change reviewed.
test('namespace',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/namespace_test.ps') ],
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Filter EOD consumption: a decode filter must swallow its own end-of-data marker
# so that the next fresh filter reading the same currentfile stream stays in sync
# -- the dvips/colorimage per-scanline idiom. Guards the regressions where
# ASCII85Decode ('z' zero group), FlateDecode (output a multiple of its buffer),
# LZWDecode (the 257 code) and the others stranded their terminator, desynchronising
# every following scanline. The scanlines are generated, and the test carries them
# whole, so it reads its own encoded data and needs nothing to produce it.
test('filter-eod',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_eod_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# The write-side counterpart: closefile on an encoding filter writes the
# coding's end-of-data marker, and a target that cannot accept it leaves a
# stream that stops short of its own terminator. Needs a target that
# refuses data, so it skips where the platform has none.
test('filter-close-eod',
  find_program('tests/run-refusing-target-test.sh'),
  args: [ xpost_exe, files('tests/filter_close_eod_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# The partial unit an encoding filter carries between writes: the leftover
# group, code or scanline is padded out at closefile so the matching decode
# filter reads the original bytes back, and the counter measuring it stays
# inside the width the padding shift, the digit array and the row buffer are
# sized by -- including after a data target has refused a byte mid-unit.
test('filter-encode-pad',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/filter_encode_pad_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# A filter releases the stream it was given to wrap: it drops the reference
# it took, and closes and frees a stream the machinery made for it alone --
# the in-memory file behind the string form, and the decompressor a
# predictor stage stands in front of. Nothing in PostScript can observe
# that, so the run happens under a leak checker, and skips without one.
test('filter-leak',
  find_program('tests/run-leak-test.sh'),
  args: [ xpost_exe, files('tests/filter_leak_test.ps'),
          files('glibc.supp') ],
  suite: cost_slow + ['memacct'],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)
# A stream nothing names any longer is reached by collection, and a stream
# still named at the end is reached by teardown. Both have to take the
# struct outside virtual memory with the entity inside it, which no
# PostScript program can observe, so this too runs under a leak checker.
test('filter-abandon',
  find_program('tests/run-leak-test.sh'),
  args: [ xpost_exe, files('tests/filter_abandon_test.ps'),
          files('glibc.supp') ],
  suite: cost_slow + ['memacct'],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)
# A face built over a font program in memory reads the program where it
# lies for as long as it might build another glyph, so the program is the
# face's to hold and to give up -- on a replacement, and at teardown for a
# font still defined. Neither end shows in PostScript, so this runs under
# a leak checker too.
test('font-leak',
  find_program('tests/run-leak-test.sh'),
  args: [ xpost_exe, files('tests/font_leak_test.ps'),
          files('glibc.supp') ],
  suite: cost_slow + ['memacct'],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  timeout: 300)
test('stream-exec',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/stream_exec_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })


# Access-attribute enforcement: readonly, executeonly and noaccess reduce the
# value and execution access of composite objects, and the standard operators
# raise invalidaccess (or the access operators typecheck) accordingly, across
# arrays, strings and dictionaries. Standard PostScript that prints SUCCESS on
# a conforming interpreter too, so it is a conformance test rather than an xpost tautology.
test('access',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/access_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Every output device renders: the rest of the suite runs under -d null,
# which never loads the graphics/device stack, so a device that fails to
# initialise or emit a page would otherwise be invisible. Render a trivial
# page through each built device and require non-empty output.
test('devices',
  find_program('tests/run-devices-test.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_slow,
  timeout: 300)

# Relocation stress: every memory grow forced to relocate, so a pointer
# derived before an allocating call and used after it reads freed memory.
# Covers string-to-name conversion, encoded number strings, path filling and
# image blitting; dictionary rehash is covered by the dict-grow C test.
test('reloc-stress',
  find_program('tests/run-reloc-stress-test.sh'),
  args: [ xpost_exe, files('tests/reloc_stress_test.ps'), 'pgm' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# The same forced relocation, underneath the operators fused into the
# procedure-execution loop. That loop resolves the running array's
# storage to a pointer once and re-derives it whenever a memory file has
# moved; a fused def can grow the dictionary it writes and a fused array
# store can copy the array a save owes a copy of, and either allocation
# can extend the file. The global half of the check is what this adds:
# global VM is never collected but still grows, and nothing else in the
# suite moves it while a fused operator runs. The test fills the global
# file to within less than the dictionary table its next growth asks for,
# then grows that dictionary from inside a procedure that is itself in
# global VM, and reads back that both the growth and the extension
# happened.
test('reloc-fused',
  find_program('tests/run-reloc-stress-test.sh'),
  args: [ xpost_exe, files('tests/reloc_fused_test.ps'), 'null' ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 120)

# The library's exported symbol set is a register, not a side effect. A
# declaration interposed between `static` and a function silently gives it
# external linkage; the compiler reports only a useless-storage-class note.
test('check-exported-symbols',
  find_program('tests/check-exported-symbols.sh'),
  suite: cost_fast,
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
test('dsc-parse', dsc_parse_exe, suite: cost_fast)

# A test must be able to fail. Each rule here encodes a defect this suite
# actually carried: a failure counter incremented with def inside a scratch
# dictionary (the increment is discarded, so the suite prints FAIL lines and
# reports SUCCESS); a wrapper that ignores the interpreter's exit status (a
# crash after SUCCESS passes); a guard that accepts an empty golden file
# (it then compares nothing).
test('check-test-quality',
  find_program('tests/check-test-quality.sh'),
  args: [ meson.current_source_dir() / 'tests' ],
  suite: cost_fast,
  timeout: 120)

# Every test declares what it costs, where it is registered. A test that
# names no cost joins the quick profile by omission, and a profile that
# slowly fills with minute-long checks is a profile nobody runs.
test('check-test-cost',
  find_program('tests/check-test-cost.sh'),
  args: [ meson.current_source_dir() ],
  suite: cost_fast)

# The distribution lists must cover every built file: a header present in the
# meson build but missing from the autotools lists yields a `make dist` tarball
# that does not compile, which no in-tree build would ever reveal.
test('check-dist-lists',
  find_program('tests/check-dist-lists.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# A quiet run must be silent: the interpreter's own self-checks report only
# when something is actually wrong, so stderr stays meaningful.
test('clean-startup',
  find_program('tests/run-clean-startup-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Fused-execution equivalence: the interpreter inlines the hottest operators
# while walking a procedure, and those copies must behave exactly as the
# operators they stand in for -- same results, same errors, same access
# enforcement. Each case runs both ways and the outcomes must match.
test('fused-equivalence',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/fused_equivalence_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# ... and the complement of the op-references test: that one shows the rule
# holding where it was broken, this one shows it cannot be broken again.
# No name pushed for execution, the reference table reached only through its
# accessors, and every surviving by-string lookup on a register that only
# shrinks.
test('check-op-references',
  find_program('tests/check-op-references.sh'),
  args: [ meson.current_source_dir() ],
  suite: cost_fast,
  timeout: 120)

# The address of a special entity is reached through the accessor named for
# it, so the enumerators appear only in the header that defines them and in
# the five constructors that build the entities. A lookup that cannot refuse
# has no refusal to ignore.
test('check-vm-address',
  find_program('tests/check-vm-address.sh'),
  args: [ meson.current_source_dir() ],
  suite: cost_fast,
  timeout: 120)

# ... and every fused operator must actually have those cases: adding one
# to the fast path without equivalence coverage fails here.
test('check-fused-ops',
  find_program('tests/check-fused-ops.sh'),
  args: [ meson.current_source_dir() / 'src' / 'lib',
          files('tests/fused_equivalence_test.ps') ],
  suite: cost_fast)

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
  suite: cost_fast,
  timeout: 300)

# The other half of that: the manifest above compares one run against a
# record, so it assumes what it never checks -- that a device renders the
# same bytes on two runs of one tree. tests/render-differ.sh renders any
# programs through two of this interpreter's own trees at once and reports
# what moved between them, which is the question a change asks and cannot
# answer from its diff; pointed at one tree twice, the answer is known in
# advance and holds the determinism the manifest rests on. It also keeps
# the instrument itself in working order, which is why it is registered
# rather than left to be rebuilt from nothing by whoever next needs it.
# Naming programs is what the harness takes; the corpus is its default,
# and reaching that is `tests/render-differ.sh <before> <after>`.
test('render-differ',
  find_program('tests/run-render-differ-test.sh'),
  args: [ xpost_exe, meson.current_source_dir(),
          files('tests/golden_page.ps') ],
  suite: cost_fast,
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
  suite: cost_slow,
  timeout: 300)

# What a fill meant to cover a whole surface covers. A marking method's
# extent operands name an inclusive pixel span, so a caller holding a
# surface's size hands the far corner, one less than the size. A raster
# device clips a surplus column and row away and reads the same either way;
# a vector writer emits the rectangle, so the surplus reaches the document.
# Asked of the operands on every marking device, and of the documents the
# vector writers wrote.
test('page-cover',
  find_program('tests/run-page-cover-test.sh'),
  args: [ xpost_exe, files('tests/page_cover_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# Values outside the range their contract names: a colour component from a
# transfer function or a tint transform, and a blend coverage from a glyph
# rasteriser. Each must be folded to the nearest valid value on its way to a
# device, not wrapped -- a wrapped channel is a different colour, a wrapped
# coverage is transparent ink, and a vector writer prints what it is handed.
test('colour-range',
  find_program('tests/run-colour-range-test.sh'),
  args: [ xpost_exe, files('tests/colour_range_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# Device teardown discipline: Destroy is called at setpagedevice, at job end,
# and possibly by the program itself, so every device's Destroy must be
# idempotent -- clear the stored resource handle, never free it twice -- and
# a device-owned framebuffer must be released unless handed to the client.
test('device-destroy',
  find_program('tests/run-device-destroy-test.sh'),
  args: [ xpost_exe, files('tests/device_destroy_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# A restore back past a setpagedevice makes the retired device current again --
# PLRM 6.1 keeps the device in the graphics state, so a saved state still names
# it -- and the interpreter then paints on an instance whose buffer was released
# when it was retired. The recorded dimensions outlive the buffer, so the bounds
# checks pass and nothing else stands between the mark and the freed memory.
test('device-restore-retired',
  find_program('tests/run-device-restore-test.sh'),
  args: [ xpost_exe, files('tests/device_restore_retired_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_slow,
  timeout: 300)

# The other half of that: the device the restore displaces is retired there,
# because nothing else can retire it. Its raster or its content accumulator is
# not virtual memory, so the collector neither reaches it nor owns it, and the
# graphics state has stopped naming it. A restore that leaves the install
# standing must retire nothing, and which of two releases a device gets is
# fixed when it is installed rather than read back from a slot the program can
# write to.
test('device-retire-restored',
  find_program('tests/run-device-restore-test.sh'),
  args: [ xpost_exe, files('tests/device_retire_restored_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# GetPix is callable on a device the program has already destroyed: Destroy
# releases the raster but leaves the instance dictionary, and the contract makes
# Destroy something a program may call for itself. A device carrying its own
# buffer must test the handle rather than follow the cleared pointer, reading a
# released raster as the ground.
test('device-getpix-destroyed',
  find_program('tests/run-device-getpix-test.sh'),
  args: [ xpost_exe, files('tests/device_getpix_destroyed_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# quit ends the run (PLRM 8.2), and what happens next is the device's
# teardown. A device whose Destroy is a PostScript procedure is torn down
# by re-entering the interpreter, which is where the abandoned program is
# within reach; every device is held to stopping where it was told to.
test('device-quit',
  find_program('tests/run-device-quit-test.sh'),
  args: [ xpost_exe, files('tests/quit_run_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_fast,
  timeout: 300)

# A setpagedevice whose maker fails at run time must leave the incumbent device
# in the graphics state, whole and painting: the program is entitled to catch
# that error and carry on. Guards the ordering -- the replacement is built
# before the incumbent is retired -- without which a caught failure leaves the
# graphics state naming a device whose buffers are freed.
test('setpagedevice-failure',
  find_program('tests/run-setpagedevice-failure-test.sh'),
  args: [ xpost_exe, files('tests/setpagedevice_failure_test.ps') ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: cost_slow,
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
  suite: cost_fast,
  timeout: 300)

# Embedded Type 1 with a no-access Private: definefont must reach the loader and
# read Subrs out of Private in C, not raise invalidaccess at a PostScript-level
# read. Guards the regression where access enforcement broke every embedded
# Type 1 font (the whole casselman corpus).
test('font-type1-noaccess',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_type1_noaccess_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Embedded Type 1 with an over-allocated /Subrs array -- more slots declared than
# filled, leaving null entries. definefont must still load the font. Guards the
# regression where .loadfont1 rejected the whole font on the first null Subr,
# which is why xpost rendered 0/13 of the (dvips Computer Modern) casselman corpus
# while Ghostscript rendered all of it -- CMR10 declares 38 Subrs and fills 15.
test('font-type1-sparse-subrs',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/font_type1_sparse_subrs_test.ps') ],
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The bounds the anti-aliased stencil painter puts on its mask. Every byte
# the sampling walk reads is addressed from the declared width and height,
# so a mask too big for its buffer has to be refused before the walk. The
# dimensions are a program's to choose and the byte count they multiply to
# is far wider than either, so the masks here include ones whose count
# lands on a multiple of 2^32 and one whose count is negative in 32 bits.
test('stencil-mask-size',
  find_program('tests/run-ps-test.sh'),
  args: [ xpost_exe, files('tests/stencil_mask_size_test.ps') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Differential corpus: render whatever real-program corpora the developer has
# fetched (tests/corpus/fetch.sh) through xpost and Ghostscript and report the
# per-page difference. Skips when no corpus is present or the reference tools
# are absent, so it is never a build-time dependency; fails when xpost crashes
# or hangs on a program, and when a corpus was named and only part of it
# evaluated.
# One test per corpus, so that meson overlaps them instead of running them end
# to end; each skips on its own when its corpus has not been fetched.
#
# Each carries its own cost, which is the point of costing a test apart
# from saying what it is about: two of these corpora take minutes and two
# do not, and the cheap pair is worth running far more often than the
# expensive one. The adobe corpus is costed as the others are rather than
# as the skip it is until its programs are fetched.
foreach corpus : [ [ 'ghostscript', 'slow' ],
                   [ 'casselman', 'veryslow' ],
                   [ 'bwipp', 'veryslow' ],
                   [ 'adobe', 'slow' ] ]
  test('corpus-' + corpus[0],
    find_program('tests/corpus/run-corpus-test.sh'),
    args: [ xpost_exe, corpus[0] ],
    env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
    suite: [ 'corpus', corpus[1] ],
    timeout: 1800)
endforeach

# A downstream consumer's own suite: Barcode Writer in Pure PostScript,
# a hundred and thirty programs of real-world PostScript run to verdicts
# their author wrote, out of a checkout kept beside this one. Skips when
# there is no checkout or it has not been built, so it is never a
# build-time dependency; fails on a test the consumer's suite failed and
# on a test file that reached no verdict at all.
#
# It is in a suite of its own for the reason the corpus is: it needs
# something this tree does not carry, so on most machines it is a skip,
# and a skip inside a cost profile makes that profile say one thing here
# and another there. It also runs its files several at a time, as the
# corpus evaluator does, so it wants the machine rather than a share of
# it.
#
# The cost is the wall time of the whole thing, its own parallelism
# included, which is how the corpora above are costed too: thirty-eight
# seconds over four processes on the machine this was measured on, about
# two minutes of processor time spread over whatever the machine has.
test('vendor-bwipp',
  find_program('tests/run-vendor-bwipp.sh'),
  args: [ xpost_exe ],
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' },
  suite: [ 'vendor', 'slow' ],
  timeout: 1800)

# Binary tokens (PLRM 3.14): the conformance corpus against a golden
# record generated from the reference interpreter, which revalidates
# the golden when a reference interpreter is present.
test('binary-tokens',
  find_program('tests/run-bintok-test.sh'),
  args: [ xpost_exe,
          files('tests/binary_token_test.ps'),
          files('tests/binary_token_test.expected') ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# Error reporting: a top-level job error prints the standard Adobe error line
# and flush notice; a clean quit or a caught error prints neither.
test('error-format',
  find_program('tests/run-error-format-test.sh'),
  args: [ xpost_exe ],
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })
# Single-opener invariant: fopen is named only inside xpost_raw_fopen(),
# which xpost_diskfile_fopen() reaches through, so every disk file the
# project opens -- the library's and the programs' alike -- passes one
# enforcement point.
test('fopen-funnel',
  find_program('tests/check-fopen-funnel.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# Device-driver skeleton: the compiled-buffer device fleet reaches its
# private struct, operand folding and FillRect extent arithmetic through
# xpost_dev_driver.h -- the contract stays written in one place.
test('check-device-skeleton',
  find_program('tests/check-device-skeleton.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# Destroy is idempotent. A device is retired at setpagedevice, at job end
# and by a restore past the change that installed it, so every Destroy
# clears the handles it releases and stores the private struct back --
# checked over a population derived from the method tables and the classes
# rather than listed, so a device added later joins it that day.
test('check-device-destroy',
  find_program('tests/check-device-destroy.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# One device roster. -d and setpagedevice are two spellings of one
# selection, so the option parser's list, the interpreter's maker table
# and the .devicemakers dictionary name the same devices -- and the test
# wrappers, which carry rosters of their own, run every one of them.
test('check-device-roster',
  find_program('tests/check-device-roster.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# A function that can refuse returns that fact and no caller may ignore
# it. The compiler enforces that wherever XPOST_MUST_CHECK is applied;
# this holds the set it is applied to, so the enforcement cannot be
# undone by dropping a mark, and counts the sites that claim a refusal
# cannot arise.
test('must-check',
  find_program('tests/check-must-check.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# Machinery stays born in its final home: every registered private-namespace
# member keeps its dictionary and every graphics-state template slot survives,
# per the register in tests/dict_homes.golden.
test('dict-homes',
  find_program('tests/check-dict-homes.sh'),
  args: [ meson.current_source_dir() ],
  suite: cost_fast,
  timeout: 120)

# The guards read data/ and tests/ under a source root they are given, and
# the interpreter finds the working tree by other routes when that root is
# wrong -- so a misdirected guard reports a true result about a tree nobody
# asked about. Each one must refuse a path that is not a source root.
test('guard-paths',
  find_program('tests/check-guard-paths.sh'),
  args: [ meson.current_source_dir(), xpost_exe, libxpost_lib.full_path() ],
  suite: cost_slow,
  timeout: 300)

# An operator's operand statement is recorded where the operator is
# defined, through the one mechanism that files it. Four operators once
# could not reach it and had their statements written by hand instead.
test('opsigs-writer',
  find_program('tests/check-opsigs-writer.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# The complement of dict-homes: that guards a registered member against
# moving, this guards a reference against landing nowhere. Every bare
# fetch from a private namespace must name something the namespace holds,
# checked against a live startup rather than a second set of patterns.
test('private-refs',
  find_program('tests/check-private-refs.sh'),
  suite: cost_fast,
  args: [ xpost_exe, meson.current_source_dir() ])

# The isolation tests assert that a name is not reachable, so a register
# entry naming something the interpreter does not have passes because
# nothing defines it -- an entry that cannot fail, and does not check
# what it was written to check. Every name those registers carry is held
# to a live startup here.
test('name-registers',
  find_program('tests/check-name-registers.sh'),
  suite: cost_fast,
  args: [ xpost_exe, meson.current_source_dir() ])

# The raster classes stay a single generated suite: pgm/ppm are parameter
# files over .makerasterclass, pbm/tiff dict-copies overriding emission,
# and every shared method is defined exactly once, in the prototype.
test('raster-classes-derivation',
  find_program('tests/check-raster-classes.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# The vector writers share one base: exactly one two-decimal formatter
# (.num2str) and one page-to-points unscale (.devres-to-points) exist,
# each writer reaches them by reference, and the DSC device stays a
# dict-copy derivation of the PDF device.
test('vecbase',
  find_program('tests/check-vecbase.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# The filter base: what every filter shares is declared once, every filter
# struct begins with one of the two bases, and what a filter is a filter
# over is stated in the one call it is constructed through rather than
# looked up in a hand-maintained list of the method tables.
test('filter-base',
  find_program('tests/check-filter-base.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# One growable byte buffer for the whole tree: the builders that assemble
# a byte stream of unknown final size grow it through xpost_strbuf.h, none
# of them carries a grow loop or a pointer/length/capacity triple of its
# own, and none reads the answer as a success flag.
test('one-bytebuf',
  find_program('tests/check-one-bytebuf.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# A cursor is held in a slot as wide as the bound it is checked against.
# A word is sixteen bits in the default build, so a cursor of that width
# reaches the end of what it counts only where the bound is the size
# field itself; against a derived one it wraps and the loop does not end.
test('narrow-cursors',
  find_program('tests/check-narrow-cursors.sh'),
  suite: cost_slow,
  args: [ meson.current_source_dir() ],
  timeout: 120)

# An encoder gives up a part-finished unit before offering it to the
# target, so a refused byte cannot leave the unit open and its counter
# running past the width the shifts and subscripts around it read it as.
test('encoder-units',
  find_program('tests/check-encoder-units.sh'),
  suite: cost_fast,
  args: [ meson.current_source_dir() ])

# Every route a program has to the glyph raster resolves the clipping
# region first. The raster route plots a glyph straight through the
# device's pixel methods and takes the region out of the clip's cache
# holder, so an operator that reaches it without filling the holder
# paints wherever the glyphs fall and nothing says so.
test('show-clip-routes',
  find_program('tests/check-show-clip.sh'),
  suite: cost_fast,
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
  test('path-confine', path_confine_exe, suite: cost_fast)
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
  suite: cost_fast,
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
  suite: cost_fast,
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
  suite: cost_fast,
  env: { 'XPOST_DATA_DIR': meson.current_source_dir() / 'data' })

# The driver contract's rectangle definition: xpost_dev_rect_normalize()
# is the one statement of which pixels FillRect paints (inclusive far
# corner, negative extents reflected, clipped by the source the caller
# supplies), Xpost_Dev_Line the one statement of which pixels DrawLine
# paints, and the folding helpers truncate and clamp. Every device
# routed through the driver header inherits exactly these semantics.
dev_rect_exe = executable('dev_rect_test',
  'tests/dev_rect_test.c',
  c_args: '-DHAVE_CONFIG_H',
  dependencies: [ libxpost_dep, m_dep ],
  link_args: internals_link_args,
  include_directories: [ config_dir, include_directories('src/lib') ],
  install: false)
test('dev-rect', dev_rect_exe, suite: cost_fast)
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
