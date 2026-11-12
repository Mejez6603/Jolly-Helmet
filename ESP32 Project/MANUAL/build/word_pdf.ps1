param([Parameter(Mandatory)][string]$Docx, [Parameter(Mandatory)][string]$Pdf, [switch]$UpdateFields)
# Opens a COPY of the Word file in Word (the original is never opened), updates the contents and the index, and exports the PDF. Run it with a timeout (Word can hang on export).
$tmp = Join-Path $env:TEMP ('jh_pdf_' + [guid]::NewGuid().ToString('N') + '.docx'); Copy-Item $Docx $tmp
$w = New-Object -ComObject Word.Application; $w.Visible = $false; $w.DisplayAlerts = 0
try {
  $d = $w.Documents.Open($tmp, $false, $false, $false)
  if ($UpdateFields) { foreach ($t in $d.TablesOfContents) { $t.Update() }; foreach ($i in $d.Indexes) { $i.Update() }; foreach ($t in $d.TablesOfContents) { $t.Update() } }
  $pages = $d.ComputeStatistics(2)
  $d.ExportAsFixedFormat($Pdf, 17)
  "pages: $pages ; toc: $($d.TablesOfContents.Count) ; indexes: $($d.Indexes.Count) ; pdf: $((Get-Item $Pdf).Length) bytes"
  $d.Close(0)
} finally { try { $w.Quit() } catch {}; Remove-Item $tmp -ErrorAction SilentlyContinue }
