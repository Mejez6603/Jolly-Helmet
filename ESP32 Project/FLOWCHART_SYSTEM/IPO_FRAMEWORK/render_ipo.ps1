# Makes JollyHelmet_IPO_Framework.png (with the "Figure 1" caption, like the template), JollyHelmet_IPO_Framework_no_caption.png and JollyHelmet_IPO_Framework.pdf
# from JollyHelmet_IPO_Framework.html with Microsoft Edge (no internet needed). The figure number / caption text is at the top of the HTML (id="cap").
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'JollyHelmet_IPO_Framework.html'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgeipo_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$png = Join-Path $here 'JollyHelmet_IPO_Framework.png'; if (Test-Path $png) { Remove-Item $png -Force }
RunEdge @('--window-size=2000,1056', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png", $url)
$png2 = Join-Path $here 'JollyHelmet_IPO_Framework_no_caption.png'; if (Test-Path $png2) { Remove-Item $png2 -Force }
RunEdge @('--window-size=2000,978', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png2", ($url + '?nocap'))
$pdf = Join-Path $here 'JollyHelmet_IPO_Framework.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=6000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
foreach ($f in $png, $png2, $pdf) { (Split-Path $f -Leaf) + ': ' + $(if (Test-Path $f) { (Get-Item $f).Length } else { 'MISSING' }) }
