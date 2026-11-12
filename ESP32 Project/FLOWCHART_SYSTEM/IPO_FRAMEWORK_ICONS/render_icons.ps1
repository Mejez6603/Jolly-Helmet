# Makes JollyHelmet_Framework_Icons.png (with the "Figure 2" caption), JollyHelmet_Framework_Icons_no_caption.png and JollyHelmet_Framework_Icons.pdf
# from JollyHelmet_Framework_Icons.html with Microsoft Edge (no internet needed). The figure number / caption text is at the top of the HTML (id="capn" / id="capt").
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'JollyHelmet_Framework_Icons.html'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgeico_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
# caption variant: T = 118  -> page height 118 + 1850 + 8 = 1976 ; no caption: T = 8 -> 1866   (FH = 1850 now holds the Feedback box)
$png = Join-Path $here 'JollyHelmet_Framework_Icons.png'; if (Test-Path $png) { Remove-Item $png -Force }
RunEdge @('--window-size=1800,1976', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png", $url)
$png2 = Join-Path $here 'JollyHelmet_Framework_Icons_no_caption.png'; if (Test-Path $png2) { Remove-Item $png2 -Force }
RunEdge @('--window-size=1800,1866', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png2", ($url + '?nocap'))
$pdf = Join-Path $here 'JollyHelmet_Framework_Icons.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=6000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
foreach ($f in $png, $png2, $pdf) { (Split-Path $f -Leaf) + ': ' + $(if (Test-Path $f) { (Get-Item $f).Length } else { 'MISSING' }) }
