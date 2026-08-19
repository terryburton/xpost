doxygen = find_program('doxygen', required: false)
sh = find_program('sh', required: false)
if doxygen.found() and sh.found()
  # every path in the Doxyfile is written relative to the tree root
  # (INPUT, IMAGE_PATH, OUTPUT_DIRECTORY), which is also where the
  # autotools doc target runs doxygen; run it from the same place
  custom_target('doc',
    input: 'Doxyfile',
    output: '.',
    command: [ sh, '-c', 'cd "$0" && "$1" doc/Doxyfile',
               meson.project_source_root(), doxygen ],
    install: false
  )
endif
