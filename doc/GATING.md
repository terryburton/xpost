# What to run, and when

The suite is three hundred and eleven tests at two object widths. Run
whole, at both widths, it costs about twelve minutes of wall clock and
sixteen cores. That is the right price for a verdict on the tree and the
wrong price for an edit, and paying it for every edit is how a suite
stops being run at all.

This page says which run answers which question, so that a piece of work
can name one rather than describe one.

## The per-change gate

    tests/gate.sh --narrow build --wide blarge

Reads what the working tree has changed, works out which areas of the
suite answer for it, runs those in the narrow build, and runs the tests
that read the object width in the wide one. The mapping from a path to
an area, and from an area to its tests, is `tests/gate-map`; the areas
below are its section headings and are the names to use.

    --area NAME     gate against an area outright, whatever changed
    --since REF     take the change from what REF does not have
    --list          say what would run, and stop
    --batch         every test, both widths
    -j N            tests at once (16 is the cap and the default)

Naming paths on the command line gates against those instead of against
the working tree, which is how to ask what a change would cost before
making it.

## The areas

| area | what a change to it reaches | tests |
| --- | --- | --- |
| `doc` | prose no program reads | 5 |
| `suite` | a test's own source; the test itself is added from its registration | 5 |
| `corpus` | the fetched programs and their harnesses | 10 |
| `host` | what the interpreter asks of the platform, and the program a user starts | 14 |
| `font` | glyphs, the cache, the files they come from | 21 |
| `filter` | files, filters, the scanner's reading | 39 |
| `guards` | the checks over the tree's own shape, and the path helper they share | 43 |
| `graphics` | paths, paint, colour, clipping, images | 49 |
| `record` | the recorded page, its spans, the band devices | 29 |
| `device` | what a page is painted into and written out as | 63 |
| `language` | operators, errors, names, the programs that install them | 129 |
| `vm`, `build` | the object and its memory; the build description | all 311, both widths |

Counts include the four guards every gate runs whatever was touched, and
overlap: a test may answer for several areas.

A path that no rule classifies falls through to a catch-all and selects
the whole suite at both widths. The failure mode of an incomplete map is
a slow gate, never a small one, and adding a source file without
classifying it costs a saving rather than a test.

## The two widths

The narrow build is primary and the wide build is equally important.
Neither is ever dropped. What varies is how much of the wide build one
change is gated against:

* a change reaching the object, the memory it lives in, or the build
  description runs its whole selection at both widths;
* every other change runs the wide build over the tests that read the
  width directly -- twenty-seven of them, about fifteen seconds.

Those twenty-seven are a tripwire and not a verdict. They were derived
by running the whole suite at both widths and comparing the two records
test by test, and they are stated in the `width` area of the map,
together with the three banding tests that also differ by width and are
too expensive to run on every edit. The wide run of everything is the
batch gate.

## The batch gate

    tests/gate.sh --batch --narrow build --wide blarge

Every test, both widths. This is what a branch is held to before it goes
anywhere, and it is one run per batch of branches rather than one per
branch per rebase: accumulate two or three green branches, rebase them
together, gate once.

A rebase whose commits touch no file the other branch touched does not
need re-gating, but the disjointness has to be shown -- `git diff
--name-only` over both, with no name in common -- rather than assumed.

## The cost profiles

These select on what a test costs rather than on what it is about. They
answer "how much of the suite ran", which is a different question from
"could what ran have noticed what changed".

    ninja -C build quick        the tests under five seconds
    ninja -C build full         every test the tree runs out of itself
    ninja -C build corpus       the corpora, with the programs fetched
    ninja -C build vendor       the consumer suite, with its checkout
    ninja -C build everything   all of it, and nothing skipped

`everything` is the only one that is a verdict on the tree: it refuses
to pass while any test merely skipped. `full` is every test but the ones
needing something the tree does not carry, and a green `full` says
nothing whatever about the corpus or the consumer suite.

## What each costs

Measured on sixteen cores, this tree, both builds present.

| run | narrow | wide | both |
| --- | --- | --- | --- |
| whole suite | 4m24s | 6m00s | 10m24s |
| a change to doc/ | 1s | 15s | 16s |
| a change to one device | 1m24s | 15s | 1m39s |
| a change to the object | 4m24s | 6m00s | 10m24s |

The whole-suite figure is set by one test: the banding campaign takes
four minutes of a narrow run and six of a wide one, and everything else
fits alongside it several times over. It is reached first so that a run
does not spend its first minute doing something else, and it is left out
of any selection that cannot see it.
