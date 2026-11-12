# Rebuilds the three printable books from the FULL manual and makes the BOOKLET (folding-order, landscape) PDF of each. Output goes to MANUAL\BOOKS.
# Edit JollyHelmet_UserManual_v11.docx first, then run:  powershell -NoProfile -ExecutionPolicy Bypass -File rebuild_books.ps1
$b = $PSScriptRoot; $m = Split-Path $b -Parent; $out = Join-Path $m 'BOOKS'
foreach ($k in @(@('customer', 'Customer_Guide'), @('admin', 'Admin_Guide'), @('tech', 'Technician_Guide'))) {
  $nm = "JollyHelmet_$($k[1])"; "=== $($k[0]) ==="
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$b\book_build.ps1" -Book $k[0] -Docx "$m\JollyHelmet_UserManual_v11.docx" -OutDocx "$out\$nm.docx" -OutPdf "$out\$nm.pdf"
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$b\impose_booklet.ps1" -Pdf "$out\$nm.pdf" -OutPdf "$out\${nm}_BOOKLET.pdf"   # add -SheetW 936 -SheetH 612 for long bond, -SheetW 842 -SheetH 595 for A4
}
