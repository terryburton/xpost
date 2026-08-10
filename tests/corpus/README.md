Corpora of real programs
========================

Sets of real PostScript programs, rendered to catch the faults the unit
suite does not reach, because a page of real PostScript is not a test
and does things no test thought to. Most of them are rendered through
xpost and through a reference interpreter and compared, which is what
the rest of this file is about; `eps` is the one held to what this
interpreter alone must do with it, by a test of its own that asks no
other engine anything. The programs themselves are **not** kept
in this repository: they belong to other people, or are generated, and
committing them would raise a licensing question and bloat the tree.
Instead each corpus is a directory here that holds only what is ours --
a small compatibility prelude where one is needed -- and its programs
are fetched or copied into it on demand. Every step degrades to a skip
when a corpus is absent, so none of it is a build-time dependency.

    fetch.sh [name ...]      populate the corpora from their own sources
    evaluate.sh [name ...]   render and compare whatever is present

Run them from anywhere; both locate the repository themselves. With no
arguments each acts on every corpus it knows.

The build wires the evaluation in as a test, so once a corpus is fetched

    meson test -C build corpus

renders it and reports the per-page difference; a plain `meson test`
runs it as part of the suite. It skips when no corpus is present or the
comparison tools are absent, and fails when xpost crashes or hangs on a
program -- a rendering difference is a lead, not a failure (see
Evaluation, below).

Each corpus closes with a count of the programs it evaluated and the ones
it held out -- a corpus names those in a `heldout` file, one basename per
line, each with the reason it is not run -- and a program the run named
and did not reach is reported as one and fails the test. The corpora are evaluated at once, one test each,
so a run that quietly did a fraction of the work would otherwise report
the same success as one that did all of it.

Reaching a program is not the same as rendering one, so the count closes
with the number of pages actually compared, and what drew no page is
held to a declared set. A corpus names in a `nopage` file every program
that produces no page and every page of a program that produces some,
each with the reason -- a basename on its own line for a whole program,
a basename and ` pN` for a single page. A run that draws nothing
otherwise reports exactly what a run that drew everything reports: it
reached every program either way. Both directions fail the test. An
absence nobody declared is the corpus quietly comparing less than it was
asked to; a declared absence that rendered is an entry whose reason has
lapsed, and a file saying something untrue about what it holds is worse
than no file, because it is read as a known cost and stops anyone
looking.

A page one engine drew is not the same as a page there was to draw. The
run compares the pages both engines wrote and can see no further than
the further of the two, so a page neither of them reached is not in the
comparison and is not missing from either side of it: a program that
stops emitting halfway through matches on every page it did draw and is
silent about the rest. So each corpus declares in a `pages` file how
many pages each of its programs has, a basename and a count per line,
and the run reaches that far whatever the engines wrote. This is held
from both ends too -- a program with no count fails the corpus, so one
cannot be added without a count, and a count naming a program the
corpus does not hold fails it as well.

The counts are not a record of what this tree draws, which would make
whatever it draws today correct by definition and lock in the next
defect the moment it appeared. Each file says where its numbers come
from, and in every case that is something other than xpost: the
producer's own `%%Pages:` line corroborated by the reference engine,
the single `showpage` a drawing ends with, or the arithmetic of the
data a program paginates. The naive readings of the same comments are
not sources and were measured not to be -- an embedded EPS is
paginated too, so counting `%%Page:` lines overstates three of the
casselman chapters and finds none at all in two thirds of the corpus,
and `%%Pages: (atend)` says nothing until the trailer.

Each corpus therefore closes with three numbers: the pages declared,
the pages compared, and the pages absent. The wrapper that meson runs
works the same three out for itself from the corpus directory and its
registers, and fails when the report does not meet them. The evaluator's
summary is otherwise the evaluator's account of itself -- it agrees with
the work the run did and says nothing about the work the run was given,
so a run that named half the programs reports honestly on that half.

The corpora
-----------

  ghostscript   Ghostscript's own examples/ -- tiger, colorcir,
                doretree, escher, snowflake, and the rest. Dense
                real-world artwork: the classic interpreter torture
                pages. Fetched from the Artifex ghostpdl repository
                (AGPL); not redistributed here.

  casselman     The PostScript chapters of Bill Casselman's
                "Mathematical Illustrations: A Manual of Geometry and
                PostScript" (Cambridge University Press), typeset in
                PostScript by their author. dvips output with embedded
                Type 1 fonts: a real-document stress test. Fetched from
                personal.math.ubc.ca; copyright the author, not
                redistributed here.

  eps           Twelve encapsulated illustrations from John Burkardt's
                sample collection (LGPL) -- graph drawings, plotted
                figures, and institutional logos wrapped around a
                photograph. Four of them carry no `showpage`, which is
                the ordinary shape of a file written to be placed in
                another document: the document supplies it. Fetched from
                people.sc.fsu.edu; not redistributed here.

                This corpus is held by a test of its own rather than by
                the differential run, and that test asks no other engine
                anything: each program must finish without an error,
                draw the pages the corpus declares, and put ink on each
                of them. The four that ask for no page have nothing else
                holding them -- without the page a job's end supplies
                they render blank -- so an `unasked` register beside the
                other two names them, held against the files both ways.
                Byte-exact goldens are deliberately not used: the
                programs are fetched from a source free to revise them,
                and a hash would turn the gate red over a change that is
                not this tree's.

  bwipp         The variable-data examples from BWIPP (Terry Burton's
                barcode writer, MIT), driven off a local checkout. They
                repeat a compute-intensive logo and barcode across many
                pages through PostScript forms, so they exercise the
                form cache; the packaged resource loads its data through
                125 ASCII85Decode filters, so it exercises the decode
                path too. The monolithic resource is copied in as this
                corpus's prelude (large and generated, so not committed).
                Point BWIPP=/path/to/postscriptbarcode at your checkout
                (default ~/src/postscriptbarcode) and build its
                monolithic resource first.

  adobe         The sample code of Adobe's PostScript books -- the Blue
                Book (Tutorial and Cookbook) and Green Book (Program
                Design) listings -- and the DeviceN, halftone and
                masked-image technical-note examples. Each listing
                targets one named feature of a specification section, so
                a divergence points straight at the operator
                responsible; this is the compliance workhorse. Adobe
                holds the copyright and no canonical download survives,
                so this corpus is NOT fetched: place your own copy under
                adobe/ (see SOURCES) and the evaluator will pick it up.

Not included
------------

  Real World PostScript (Roth, Addison-Wesley 1988) has no surviving
  example-code distribution -- only the scanned book -- so there is
  nothing to fetch.

Preludes
--------

Where a corpus assumes something outside the language -- Ghostscript's
examples use the min/max operators and their internal dot-forms, which
Ghostscript provides but the PLRM does not; the BWIPP examples need the
barcode resource loaded first -- a file named `prelude` in the corpus
directory is prepended to every program of that corpus. It is prepended
to *both* engines, so the compared input stays identical and the shim
is not itself under test. A small prelude that is ours (ghostscript's)
is committed; a large generated one (bwipp's barcode resource) is not.

A corpus that needs a prelude and has not got one cannot run: nothing
is prepended, every program of it fails, and the run compares no page.
Which of two things that is depends on where the prelude comes from. A
committed prelude is in every checkout, so its absence is a broken tree
and the programs failing for want of it is the report. A prelude that
is fetched is absent wherever the corpus has not been fetched, and the
corpus is skipped -- named, with what the prelude is and how to obtain
it -- rather than run into a wall of failures. The corpus says which it
has by a `prelude.fetched` file beside the prelude, committed where the
prelude is not, whose text is what the skip prints.

Evaluation
----------

Each program renders at 72 dpi on a letter page through xpost and
through Ghostscript, and the two rasters are compared page by page.
Colour pages compare by pixel count at a small fuzz (structural
difference, tolerant of sub-pixel edges); halftone and pattern pages
compare by tint at a coarse resize, because two correct screens differ
dot for dot but hold the same tone.

A difference is a lead, not a verdict. Ghostscript is informative;
where a deviation matters, Adobe Distiller is authoritative. Some
divergences are expected and not defects:

  - rand/srand pages (Ghostscript's snowflak and vasarely) draw
    different figures in every interpreter, the PLRM leaving the
    generator implementation-defined;
  - CMYK and DeviceN colour differs between Ghostscript's colour-managed
    render and the PLRM arithmetic xpost follows;
  - masked images under /Interpolate differ by design across
    implementations.

SOURCES
-------

  ghostscript  https://github.com/ArtifexSoftware/ghostpdl  (examples/)
  casselman    https://personal.math.ubc.ca/~cass/graphics/manual/
  eps          https://people.sc.fsu.edu/~jburkardt/data/eps/  (eps.html)
  bwipp        https://github.com/bwipp/postscriptbarcode  (contrib/Examples,
               build/monolithic_package/barcode.ps)
  adobe        Adobe's "PostScript Language Tutorial and Cookbook" (Blue
               Book) and "PostScript Language Program Design" (Green
               Book) sample code, and the DeviceN / halftone /
               masked-image Technical Notes. Historically on Adobe's
               developer FTP; obtain from an archive you trust and
               arrange under adobe/ as flat *.ps files.
