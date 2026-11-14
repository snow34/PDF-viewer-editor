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
    out = bytearray(b"%PDF-1.3\n%\xe2\xe3\xcf\xd3\n")
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


# ---------------------------------------------------------------- preflight
# x1a_ok.pdf passes every PDF/X-1a:2001 check the app makes. Every other
# x1a_*.pdf is the same file with exactly one thing wrong.

def pdfx(name, content=b"0 0 0 1 k 50 50 200 200 re f", boxes=b"/TrimBox [10 10 290 290]",
         resources=b"", page_extra=b"", catalog_extra=b"", intent=True,
         info=b"/Title (Test) /CreationDate (D:20240101000000Z) /ModDate (D:20240101000000Z) /Trapped /False",
         marker=b"/GTS_PDFXVersion (PDF/X-1:2001) /GTS_PDFXConformance (PDF/X-1a:2001)", extra=()):
    objs = [
        b"<< /Type /Catalog /Pages 2 0 R " + (INTENT if intent else b"") + b" " + catalog_extra + b" >>",
        PAGES,
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] " + boxes + b" /Contents 4 0 R"
        b" /Resources << " + resources + b" >> " + page_extra + b" >>",
        stream(content),
        b"<< " + info + b" " + marker + b" >>",
    ] + list(extra)
    write(out_dir + "/" + name, objs, b"/Info 5 0 R")


pdfx("x1a_ok.pdf")
pdfx("x1a_rgb.pdf", content=b"0 0 0 1 k 50 50 200 200 re f 1 0 0 rg 100 100 50 50 re f")
pdfx("x1a_no_trim.pdf", boxes=b"")
pdfx("x1a_trim_and_art.pdf", boxes=b"/TrimBox [10 10 290 290] /ArtBox [20 20 280 280]")
pdfx("x1a_trim_outside.pdf", boxes=b"/TrimBox [10 10 290 290] /BleedBox [20 20 280 280]")
pdfx("x1a_opacity.pdf", content=b"0 0 0 1 k 50 50 200 200 re f /GS0 gs 1 0 0 0 k 100 100 50 50 re f",
     resources=b"/ExtGState << /GS0 << /Type /ExtGState /ca 0.5 >> >>")
pdfx("x1a_font.pdf", content=b"BT /F1 12 Tf 0 0 0 1 k 30 250 Td (Hello Prepress) Tj ET",
     resources=b"/Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >>")
pdfx("x1a_annot.pdf", page_extra=b"/Annots [<< /Type /Annot /Subtype /Text /Rect [100 100 120 120]"
                                 b" /Contents (note) >>]")
pdfx("x1a_annot_outside.pdf", boxes=b"/TrimBox [50 50 250 250]",
     page_extra=b"/Annots [<< /Type /Annot /Subtype /Text /Rect [5 5 25 25] /Contents (note) >>]")
pdfx("x1a_no_meta.pdf", info=b"/CreationDate (D:20240101000000Z) /ModDate (D:20240101000000Z)")
pdfx("x1a_no_intent.pdf", intent=False)
pdfx("x1a_javascript.pdf", catalog_extra=b"/OpenAction << /S /JavaScript /JS (app.alert\\(1\\)) >>")
pdfx("x1a_lowres.pdf", content=b"q 144 0 0 144 30 120 cm /Im1 Do Q", resources=b"/XObject << /Im1 6 0 R >>",
     extra=[b"<< /Type /XObject /Subtype /Image /Width 20 /Height 20 /ColorSpace /DeviceGray"
            b" /BitsPerComponent 8 /Length 400 >>\nstream\n" + b"\x80" * 400 + b"\nendstream"])

# x4_no_profile.pdf: marked PDF/X-4 in XMP, 50 % opacity (allowed in X-4), but
# the output intent has no embedded profile, which X-4 requires.
XMP4 = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">'
        b'<rdf:Description rdf:about="" xmlns:pdfxid="http://www.npes.org/pdfx/ns/id/"'
        b' xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:xmp="http://ns.adobe.com/xap/1.0/"'
        b' xmlns:pdf="http://ns.adobe.com/pdf/1.3/">'
        b"<pdfxid:GTS_PDFXVersion>PDF/X-4</pdfxid:GTS_PDFXVersion>"
        b'<dc:title><rdf:Alt><rdf:li xml:lang="x-default">Test</rdf:li></rdf:Alt></dc:title>'
        b"<xmp:CreateDate>2024-01-01T00:00:00Z</xmp:CreateDate><xmp:ModifyDate>2024-01-01T00:00:00Z</xmp:ModifyDate>"
        b"<pdf:Trapped>False</pdf:Trapped>"
        b"</rdf:Description></rdf:RDF></x:xmpmeta>")
write(out_dir + "/x4_no_profile.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R /Metadata 5 0 R " + INTENT + b" >>",
    PAGES,
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /TrimBox [10 10 290 290] /Contents 4 0 R"
    b" /Resources << /ExtGState << /GS0 << /Type /ExtGState /ca 0.5 >> >> >> >>",
    stream(b"/GS0 gs 0 0 0 1 k 50 50 200 200 re f"),
    b"<< /Type /Metadata /Subtype /XML /Length %d >>\nstream\n" % len(XMP4) + XMP4 + b"\nendstream",
])

# x1a_two_pages.pdf: page 1 is clean, page 2 has an RGB square at (100,100) 50 x 50.
write(out_dir + "/x1a_two_pages.pdf", [
    b"<< /Type /Catalog /Pages 2 0 R " + INTENT + b" >>",
    b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 >>",
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /TrimBox [10 10 290 290] /Contents 4 0 R >>",
    stream(b"0 0 0 1 k 50 50 200 200 re f"),
    b"<< /Title (Test) /CreationDate (D:20240101000000Z) /ModDate (D:20240101000000Z) /Trapped /False"
    b" /GTS_PDFXVersion (PDF/X-1:2001) /GTS_PDFXConformance (PDF/X-1a:2001) >>",
    b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /TrimBox [10 10 290 290] /Contents 7 0 R >>",
    stream(b"1 0 0 rg 100 100 50 50 re f"),
], b"/Info 5 0 R")
