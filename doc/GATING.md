# What to run, and when

The suite is three hundred and eleven tests at two object widths. Run
whole, at both widths, it costs about ten minutes of wall clock and
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
| `suite` | a test's own source; the test itself is added from its registration | 4 |
| `corpus` | the fetched programs and their harnesses | 10 |
| `host` | what the interpreter asks of the platform, and the program a user starts | 13 |
| `font` | glyphs, the cache, the files they come from | 18 |
| `filter` | files, filters, the scanner's reading | 38 |
| `guards` | the checks over the tree's own shape, and the path helper they share | 43 |
| `graphics` | paths, paint, colour, clipping, images | 49 |
| `record` | the recorded page, its spans, the band devices | 29 |
| `device` | what a page is painted into and written out as | 63 |
| `language` | operators, errors, names, the programs that install them | 103 |
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

Measured on sixteen cores of a machine also doing other work, so read
the ratios rather than the seconds. The old rule was the whole suite at
both widths whatever the change was, which is the first row.

| gate | narrow | wide | both |
| --- | --- | --- | --- |
| everything, both widths | 5m04s | 5m12s | 10m16s |
| a change to doc/ | 2s | 7s | 9s |
| a change to one device | 32s | 15s | 47s |
| a change to the object | 5m04s | 5m12s | 10m05s |

So a doc change costs a hundredth of what it used to and a device change
a fourteenth, and a change to the object costs exactly what it did,
which is the point: what was dropped is the re-verification of things
the change could not reach.

The whole-suite figure is set by one test. The banding campaign takes
five minutes of it and everything else fits alongside it several times
over -- the same suite without it runs in one minute forty-three. It is
reached first so that a run does not spend its opening minute doing
something else, and it is outside every selection that cannot see it.
