# Sourced by the wrappers that run one workload once per device: the
# roster of every device, and the two subsets a cross-product runs.
#
# DEVICE_FLEET_ALL names every device the interpreter can make without a
# display. tests/check-device-roster.sh holds it to the interpreter's
# maker table, so a device added there and left out here fails, and
# run-devices-test.sh renders a page through all of it -- no device is
# built, selectable and never once exercised.
#
# A cross-product wrapper asks one question of every device, and two
# devices that answer it out of the same code are the same run twice.
# Which two those are depends on the question, so there are two subsets
# rather than one, each named for what its members implement separately:
#
#   DEVICE_FLEET_LIFETIME   what a device owns and when it releases it.
#                           Create, Destroy, GetPix after a Destroy, and
#                           the retirement a page-device change makes.
#   DEVICE_FLEET_MARKING    what a device does with a mark: the marking
#                           methods, the colour components they take and
#                           the raster (or the record) they leave.
#
# The membership is read off the sources rather than chosen by count:
#
#   pgm ppm pbm tiff  one class, built by .makerasterclass (data/image.ps)
#                     over a parameter set. Create, Destroy, GetPix and
#                     the Emit dispatch are the prototype's on all four,
#                     so one of them stands for the lifetime of all four.
#                     They diverge where they mark: pgm carries one grey
#                     component, ppm three colour ones, and pbm has a
#                     PutPix of its own that screens and thresholds to
#                     bilevel. tiff is data/ppmimage.ps with the page
#                     writer replaced, and the page writer is not a mark:
#                     golden-render, raster-formats and multipage hold
#                     its bytes.
#   pdfwrite dscwrite svgwrite
#                     the vector writers. dscwrite is data/pdfwrite.ps
#                     copied with its three page writers replaced --
#                     Create, Destroy, the Emit dispatch and every
#                     marking method are inherited whole -- so pdfwrite
#                     stands for it in both subsets, and pdf-device,
#                     golden-render and multipage hold what it writes.
#                     svgwrite implements its own, and stands for itself.
#   raster bgr png pngalpha jpeg
#                     the devices whose raster is a buffer outside the
#                     PostScript virtual machine, which is the class a
#                     released buffer can be marked through. Four
#                     implementations: raster, bgr and jpeg one each,
#                     and png with pngalpha, which is the same C with an
#                     alpha flag -- the same Create, Destroy and GetPix,
#                     and an alpha branch through the blend that only
#                     the marking subset has anything to ask about.
#   null bbox         the devices that paint nothing, which is the
#                     opposite fault: a method that does something is as
#                     wrong as one that does not. Neither owns a raster
#                     and neither derives from the other.
#
# A device leaves a subset only where every line it would run there is
# another member's too. Adding one is free; taking one out is a claim
# about the sources, and the claim is written above.

# DEVICE_FLEET_OPTIONAL names the members that need a library the build
# may not have, and so the only ones that may legitimately answer "wrong
# device". It is what lets a wrapper hold itself to a floor: a device
# that is not built in skips, a roster that skipped from end to end
# leaves every verdict untaken, and a wrapper with nothing to say prints
# the same SUCCESS as one that asked its question of everything. The
# floor is the roster less this list, so it follows the roster instead
# of being a number typed beside it.
DEVICE_FLEET_ALL='pgm ppm pbm tiff null bbox raster bgr png pngalpha
                  pdfwrite svgwrite dscwrite jpeg'

DEVICE_FLEET_OPTIONAL='png pngalpha jpeg'

DEVICE_FLEET_LIFETIME='pgm null bbox raster bgr png jpeg pdfwrite svgwrite'

DEVICE_FLEET_MARKING='pgm ppm pbm null bbox raster bgr png pngalpha jpeg
                      pdfwrite svgwrite'
