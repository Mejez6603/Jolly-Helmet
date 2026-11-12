JOLLY HELMET - THE THREE PRINTABLE BOOKS (2026-11-04, revision 11)
===================================================================
JollyHelmet_Customer_Guide.pdf      12 pages   Customer guide only (for the machines' owner to hand out or post).
JollyHelmet_Admin_Guide.pdf         48 pages   Admin guide (Control Center, Refilling Station, online page) + Reference: Safety, Settings reference, Policies, Glossary, Index.
JollyHelmet_Technician_Guide.pdf    68 pages   Technician guide (hardware, wiring, firmware, online monitoring, maintenance) + Reference: Safety, charts, decision trees, settings, schematics, glossary, version, index.
The .docx next to each PDF is the same book, editable in Word. The full manual (everything in one file) stays in the folder above: JollyHelmet_UserManual_v11.docx / .pdf.

BOOK LAYOUT: cover (p.1), colophon on the inside cover (p.2), contents (p.3), every chapter starts on a RIGHT-HAND (odd) page, so there are blank pages between chapters on purpose.
Mirrored margins with a binding gutter (0.4 in), page number on the OUTER edge, and the page count is a multiple of 4 (the last page is the back cover).
TWO PDFs FOR EACH BOOK
  JollyHelmet_<name>.pdf          single pages in reading order (1, 2, 3 ...). Print on Letter, 100 % / "actual size", double-sided, flip on the LONG edge. A4: "fit to page".
  JollyHelmet_<name>_BOOKLET.pdf  BOOKLET: landscape sheets, two book pages on each side, in folding order (last page | page 1, then page 2 | second-to-last, ...).
HOW TO PRINT THE _BOOKLET FILE
  1. Paper: Letter (the file is a Letter LANDSCAPE sheet, 11 x 8.5 in). Scale: 100 % / "actual size" (NOT "fit"), orientation: landscape.
  2. Double-sided: ON, flip on the SHORT edge. (Do not use the "Booklet" option in the print dialog; the file is already arranged.)
  3. Print ONE sheet first and check: page 2 must be on the back of page 1, upright. If it is upside down, switch to flip on the LONG edge.
  4. Take the printed stack, keep the sheets in the order they came out, fold the whole stack in half and staple twice on the fold.
  Customer guide = 3 sheets (12 pages), Admin guide = 12 sheets (48 pages), Technician guide = 17 sheets (68 pages): the Technician guide is thick; staple it or hole-punch it, or print the plain file instead.
  The pages are shrunk to about 65 % to fit two on one Letter sheet (text about 7 pt). For larger text use long bond or A4 landscape: ask for another size (the script takes -SheetW / -SheetH).

HOW THEY ARE MADE: MANUAL\build\book_build.ps1 (Word, on a COPY of the full manual): book_build.ps1 -Book customer|admin|tech -Docx <full manual docx> -OutDocx <book docx> -OutPdf <book pdf>.
Edit the FULL manual (v11 docx), then run the script three times; never edit the books by hand if you want them to stay in step.
