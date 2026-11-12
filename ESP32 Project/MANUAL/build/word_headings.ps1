param([Parameter(Mandatory)][string]$Docx, [Parameter(Mandatory)][string]$Out)
# lists every heading (level 1-3) of a COPY of the Word file with its page number
$tmp = Join-Path $env:TEMP ('jh_head_' + [guid]::NewGuid().ToString('N') + '.docx'); Copy-Item $Docx $tmp
$w = New-Object -ComObject Word.Application; $w.Visible = $false; $w.DisplayAlerts = 0
try {
  $d = $w.Documents.Open($tmp, $false, $true, $false)
  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($p in $d.Paragraphs) {
    $lvl = $p.OutlineLevel
    if ($lvl -ge 1 -and $lvl -le 3) { $t = $p.Range.Text.Trim(); if ($t) { $lines.Add(("{0}`t{1}`t{2}" -f $p.Range.Information(3), $lvl, $t)) } }
  }
  $lines | Set-Content -Path $Out -Encoding UTF8
  "headings: $($lines.Count)"
  $d.Close(0)
} finally { try { $w.Quit() } catch {}; Remove-Item $tmp -ErrorAction SilentlyContinue }
