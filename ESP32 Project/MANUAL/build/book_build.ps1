param([Parameter(Mandatory)][ValidateSet('customer','admin','tech')][string]$Book, [Parameter(Mandatory)][string]$Docx, [Parameter(Mandatory)][string]$OutDocx, [Parameter(Mandatory)][string]$OutPdf)
# BOOK STYLE (2026-11-04): makes one of the three printable books from the full manual (the full manual is never changed; a COPY is opened in Word).
#   customer = Customer guide | admin = Admin guide + Safety, Settings reference, Policies, Glossary, Index | tech = Technician guide + Reference (Safety, charts, trees, settings, schematics, glossary, version, index)
# Book layout: cover (page 1), colophon on the inside cover (page 2), contents on page 3, every chapter starts on a right-hand (odd) page, mirrored margins with a binding gutter (same text width as before),
# the page number on the OUTER edge, the total number of pages is a multiple of 4 (blank pages are added before the back cover) so the PDF can be printed double-sided or folded into a booklet.
$cfg = @{
  customer = @{ Title = 'CUSTOMER GUIDE'; Name = 'Customer guide'; Keep1 = @('Customer guide'); Keep2 = @() }
  admin    = @{ Title = 'ADMIN GUIDE';    Name = 'Admin guide (Control Center)'; Keep1 = @('Admin guide (Control Center)', 'Reference (Safety, charts, settings and index)'); Keep2 = @('Safety and emergencies', 'Settings reference', 'Policies', 'Glossary', 'Index') }
  tech     = @{ Title = 'TECHNICIAN GUIDE'; Name = 'Technician guide'; Keep1 = @('Technician guide (Hardware, online monitoring and maintenance)', 'Reference (Safety, charts, settings and index)'); Keep2 = @('Safety and emergencies', 'Box flowcharts', 'Troubleshooting decision trees', 'Settings reference', 'Circuit schematics', 'Glossary', 'Version and revisions', 'Index') }
}[$Book]
$tmp = Join-Path $env:TEMP ('jh_book_' + [guid]::NewGuid().ToString('N') + '.docx'); Copy-Item $Docx $tmp
$w = New-Object -ComObject Word.Application; $w.Visible = $false; $w.DisplayAlerts = 0
function Headings($d, [int]$style) {
  $res = New-Object System.Collections.Generic.List[object]
  $r = $d.Content; $f = $r.Find; $f.ClearFormatting(); $f.Style = $d.Styles.Item($style); $f.Text = ''; $f.Forward = $true; $f.Wrap = 0; $f.Format = $true
  while ($f.Execute()) { $res.Add([pscustomobject]@{ Start = $r.Start; Text = ($r.Text -replace '[\r\n\a]', '').Trim() }); $r.Collapse(0) }
  return $res
}
function ReplaceAll($d, [string]$from, [string]$to) {
  $r = $d.Content; $f = $r.Find; $f.ClearFormatting(); $f.Replacement.ClearFormatting()
  [void]$f.Execute($from, $true, $false, $false, $false, $false, $true, 0, $false, $to, 2)
}
try {
  $d = $w.Documents.Open($tmp, $false, $false, $false)
  $d.TrackRevisions = $false
  $body = $d.Sections.Item(2)
  $bodyEnd = $body.Range.End - 1
  # 1. take out what does not belong to this book (from the end backwards)
  $h1 = Headings $d -2; $h2 = Headings $d -3
  $cuts = New-Object System.Collections.Generic.List[object]
  for ($i = 0; $i -lt $h1.Count; $i++) {
    $s = $h1[$i].Start; $e = if ($i + 1 -lt $h1.Count) { $h1[$i + 1].Start } else { $bodyEnd }
    if ($cfg.Keep1 -notcontains $h1[$i].Text) { $cuts.Add([pscustomobject]@{ S = $s; E = $e; T = $h1[$i].Text }); continue }
    foreach ($j in 0..($h2.Count - 1)) {
      $x = $h2[$j]; if ($x.Start -lt $s -or $x.Start -ge $e) { continue }
      if ($h1[$i].Text -notlike 'Reference*') { continue }
      $xe = $e; foreach ($y in $h2) { if ($y.Start -gt $x.Start -and $y.Start -lt $e) { $xe = [math]::Min($xe, $y.Start) } }
      if ($cfg.Keep2 -notcontains $x.Text) { $cuts.Add([pscustomobject]@{ S = $x.Start; E = $xe; T = $x.Text }) }
    }
  }
  foreach ($c in ($cuts | Sort-Object S -Descending)) { $d.Range($c.S, $c.E).Delete() | Out-Null; "cut: $($c.T)" }
  # 2. words that point to other books
  if ($Book -eq 'admin') {
    ReplaceAll $d 'in the Reference chapter' 'in the Technician book (Reference chapter)'
    ReplaceAll $d 'in this reference' 'in the Technician book'
    ReplaceAll $d 'Safety, flowcharts, decision trees, settings, schematics, policies, glossary, versions and index' 'Safety, settings, policies, glossary and index'
    ReplaceAll $d 'Reference (Safety, charts, settings and index)' 'Reference (Safety, settings, policies and glossary)'
  }
  if ($Book -eq 'tech') { ReplaceAll $d 'Safety, flowcharts, decision trees, settings, schematics, policies, glossary, versions and index' 'Safety, flowcharts, decision trees, settings, schematics, glossary, versions and index' }
  ReplaceAll $d 'USER MANUAL' ('USER MANUAL - ' + $cfg.Title)
  $br = $d.Content; $bf = $br.Find; $bf.ClearFormatting(); $bf.Text = ("USER MANUAL - " + $cfg.Title); $bf.MatchCase = $true; $bf.Forward = $true; $bf.Wrap = 0; while ($bf.Execute()) { $br.Font.Spacing = 3; $br.Collapse(0) }
  # 3. page layout: mirrored margins with a gutter (same text width), numbering continues from the cover, odd/even footers
  $body = $d.Sections.Item(2); $ps = $body.PageSetup
  $ps.MirrorMargins = -1; $ps.LeftMargin = 50.4; $ps.RightMargin = 50.4; $ps.Gutter = 28.8
  $ps.OddAndEvenPagesHeaderFooter = -1
  $body.Footers.Item(1).PageNumbers.RestartNumberingAtSection = $false
  foreach ($k in 2, 3) { $body.Headers.Item($k).LinkToPrevious = $false; $body.Footers.Item($k).LinkToPrevious = $false }
  $pr = $body.Footers.Item(1).Range; $ev = $body.Footers.Item(3)
  $ev.Range.FormattedText = $pr.FormattedText
  $er = $ev.Range; $er.MoveEnd(1, -1) | Out-Null; $er.Text = ''
  $er = $ev.Range; $er.Collapse(1); $er.InsertAfter('Page '); $er.Font.Bold = $true; $er.Font.Size = 9; $er.Font.Color = 6042132
  $fr = $ev.Range; $fr.MoveEnd(1, -1) | Out-Null; $fr.Collapse(0); $fld = $d.Fields.Add($fr, 33); $fld.Result.Font.Bold = $true; $fld.Result.Font.Size = 9; $fld.Result.Font.Color = 6042132
  $sr = $ev.Range; $sr.MoveEnd(1, -1) | Out-Null; $sr.Collapse(0); $sr.InsertAfter("`tJOLLY HELMET  |  Make your Helmet Happy Again!"); $sr.Font.Bold = $false; $sr.Font.Size = 8; $sr.Font.Color = 8020054
  $ev.Range.ParagraphFormat.TabStops.ClearAll(); [void]$ev.Range.ParagraphFormat.TabStops.Add(482.4, 2)
  $body.Headers.Item(3).Range.FormattedText = $body.Headers.Item(1).Range.FormattedText
  # 4. colophon on the inside cover (page 2), contents on page 3
  $first = $body.Range; $first.Collapse(1)
  $col = "Jolly Helmet User Manual`r" + $cfg.Name + "`rRevision 11, 4 November 2026. Made by MEJEZ STUDIO.`rThis book is one of three: Customer guide, Admin guide, Technician guide (with the Reference). The full manual, with everything in one file, is JollyHelmet_UserManual_v11.pdf.`rTwo PDFs come with this book. The plain file (single pages in reading order): print double-sided, flip on the LONG edge, Letter size (A4 at 'fit'). The file whose name ends in _BOOKLET (two pages on each landscape sheet, the last page next to the first): print double-sided, flip on the SHORT edge, 100 percent (actual size), then fold the whole stack in half and staple on the fold. Every chapter starts on a right-hand page; the page count is a multiple of 4 so the booklet folds cleanly.`rQuestions or faults: MEJEZ STUDIO, 0975 791 5832.`r"
  $first.InsertBefore($col)
  $cr = $d.Range($body.Range.Start, $body.Range.Start + $col.Length)
  $cr.Style = $d.Styles.Item(-1); $cr.Font.Size = 11; $cr.ParagraphFormat.SpaceAfter = 10
  $d.Range($body.Range.Start, $body.Range.Start + 24).Font.Size = 20; $d.Range($body.Range.Start, $body.Range.Start + 24).Font.Bold = $true
  $pb = $d.Range($body.Range.Start + $col.Length, $body.Range.Start + $col.Length); $pb.InsertBreak(7)
  # 5. every chapter starts on a right-hand page
  foreach ($h in (Headings $d -2 | Sort-Object Start -Descending)) { $r = $d.Range($h.Start, $h.Start); $r.InsertBreak(5) }
  foreach ($si in @(1, $d.Sections.Count)) { $sc = $d.Sections.Item($si); foreach ($k in 1, 2, 3) { $sc.Headers.Item($k).LinkToPrevious = $false; $sc.Footers.Item($k).LinkToPrevious = $false }; foreach ($k in 1, 2, 3) { foreach ($hf in @($sc.Headers.Item($k), $sc.Footers.Item($k))) { $hr = $hf.Range; $hr.Font.Size = 1; $hr.ParagraphFormat.SpaceBefore = 0; $hr.ParagraphFormat.SpaceAfter = 0; $hr.ParagraphFormat.LineSpacingRule = 4; $hr.ParagraphFormat.LineSpacing = 1 } } }
  foreach ($t in $d.TablesOfContents) { $t.Update() }; foreach ($i in $d.Indexes) { $i.Update() }; foreach ($t in $d.TablesOfContents) { $t.Update() }
  # 6. a multiple of 4 pages in all (blank pages before the back cover)
  for ($k = 0; $k -lt 6; $k++) {
    $pages = $d.ComputeStatistics(2)
    if ($pages % 4 -eq 0) { break }
    $e2 = $d.Sections.Item($d.Sections.Count - 1).Range; $e2.MoveEnd(1, -1) | Out-Null; $e2.Collapse(0); $e2.InsertBreak(7)
  }
  foreach ($t in $d.TablesOfContents) { $t.Update() }
  $pages = $d.ComputeStatistics(2)
  $d.SaveAs2($OutDocx, 16)
  $d.ExportAsFixedFormat($OutPdf, 17)
  "book: $Book ; pages: $pages ; sections: $($d.Sections.Count) ; pdf: $((Get-Item $OutPdf).Length) bytes"
  $d.Close(0)
} finally { try { $w.Quit() } catch {}; Remove-Item $tmp -ErrorAction SilentlyContinue }
