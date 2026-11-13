#!/usr/bin/env python3
"""Generate the test PDFs. Usage: make_test_pdf.py <output folder>

ink_patches.pdf has patches with known ink values.

Layout (page 300 x 300 pt, origin bottom-left):
  A (20..80, 220..280)    CMYK 40/30/20/10
  B (100..160, 220..280)  spot "PANTONE 185 C" at 50 %
  C (180..240, 220..280)  CMYK 100/100/100/20  (TAC 320 %)
  D (20..100, 100..180)   100 % cyan, then
  E (60..140, 60..140)    100 % yellow with overprint on (OPM 1):
                          overlap (60..100, 100..140) must read C100 + Y100
  F (180..260, 100..180)  100 % cyan, then
  G (220..300, 60..140)   100 % yellow, overprint off: overlap knocks out cyan
"""
import sys

content = b"""
0.4 0.3 0.2 0.1 k 20 220 60 60 re f
/CS0 cs 0.5 scn 100 220 60 60 re f
1 1 1 0.2 k 180 220 60 60 re f
1 0 0 0 k 20 100 80 80 re f
q /GS0 gs 0 0 1 0 k 60 60 80 80 re f Q
1 0 0 0 k 180 100 80 80 re f
0 0 1 0 k 220 60 80 80 re f
"""

def write(path, objs, trailer_extra=b""):
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for i, body in enumerate(objs, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    out += b"trailer\n<< /Size %d /Root 1 0 R %s >>\nstartxref\n%d\n%%%%EOF\n" % (
        len(objs) + 1, trailer_extra, xref)
    with open(path, "wb") as f:
        f.write(out)


def stream(data):
    return b"<< /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


PAGES = b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
PAGE = b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Contents 4 0 R >>"
BOX = stream(b"0 0 0 1 k 50 50 200 200 re f")
INTENT = (b"/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX"
          b" /OutputConditionIdentifier (FOGRA39) /RegistryName (http://www.color.org)"
          b" /Info (Coated FOGRA39) >>]")

out_dir = sys.argv[1] if len(sys.argv) > 1 else "."

# ink_patches.pdf: see the docstring.
write(out_dir + "/ink_patches.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R >>",
    PAGES,
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /TrimBox [10 10 290 290]"
    b" /BleedBox [2 2 298 298] /Contents 4 0 R /Resources << /ColorSpace << /CS0 5 0 R >>"
    b" /ExtGState << /GS0 << /Type /ExtGState /OP true /op true /OPM 1 >> >> >> >>",
    stream(content),
    b"[/Separation /PANTONE#20185#20C /DeviceCMYK 6 0 R]",
    b"<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 0.91 0.76 0] /N 1 >>",
])

# pdfx1a_marked.pdf: claims PDF/X-1a in the Info dictionary (not a compliant file).
write(out_dir + "/pdfx1a_marked.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R " + INTENT + b" >>",
    PAGES, PAGE, BOX,
    b"<< /Title (Marked X-1a) /GTS_PDFXVersion (PDF/X-1a:2001) /GTS_PDFXConformance (PDF/X-1a:2001) >>",
], b"/Info 5 0 R")

# pdfx4_marked.pdf: claims PDF/X-4 in XMP metadata only (not a compliant file).
XMP = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
       b'<rdf:Description rdf:about="" xmlns:pdfxid="http://www.npes.org/pdfx/ns/id/">'
       b"<pdfxid:GTS_PDFXVersion>PDF/X-4</pdfxid:GTS_PDFXVersion>"
       b"</rdf:Description></rdf:RDF></x:xmpmeta>")
write(out_dir + "/pdfx4_marked.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R /Metadata 5 0 R " + INTENT + b" >>",
    PAGES, PAGE, BOX,
    b"<< /Type /Metadata /Subtype /XML /Length %d >>\nstream\n" % len(XMP) + XMP + b"\nendstream",
])

# objects.pdf: one of each kind of object for the inspector and for search.
#   text "Hello Prepress" at 12 pt in black, baseline (30, 250)
#   a 2 x 2 pixel grey image placed 72 x 72 pt at (30, 120)      -> 2 ppi
#   a spot colour square with overprint at (150, 120), 80 x 80 pt
#   an RGB square at (150, 20), 60 x 60 pt, on top of a larger CMYK square
OBJ_CONTENT = b"""
BT /F1 12 Tf 0 0 0 1 k 30 250 Td (Hello Prepress) Tj ET
q 72 0 0 72 30 120 cm /Im1 Do Q
q /GS0 gs /CS0 cs 1 scn 150 120 80 80 re f Q
0.1 0.2 0.3 0 k 140 10 100 80 re f
1 0 0 rg 150 20 60 60 re f
"""
write(out_dir + "/objects.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R >>",
    PAGES,
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Contents 4 0 R /Resources <<"
    b" /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >>"
    b" /XObject << /Im1 5 0 R >> /ColorSpace << /CS0 6 0 R >>"
    b" /ExtGState << /GS0 << /Type /ExtGState /OP true /op true /OPM 1 >> >> >> >>",
    stream(OBJ_CONTENT),
    b"<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray"
    b" /BitsPerComponent 8 /Length 4 >>\nstream\n\x00\x80\x80\xff\nendstream",
    b"[/Separation /PANTONE#20185#20C /DeviceCMYK 7 0 R]",
    b"<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [0 0.91 0.76 0] /N 1 >>",
])
