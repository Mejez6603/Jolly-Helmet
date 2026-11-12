# Makes JollyHelmet_DFD_Level1.png (3960 x 2680) and JollyHelmet_DFD_Level1.pdf from JollyHelmet_DFD_Level1.html with Microsoft Edge (no internet needed).
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'JollyHelmet_DFD_Level1.html'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgedfd1_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$png = Join-Path $here 'JollyHelmet_DFD_Level1.png'; if (Test-Path $png) { Remove-Item $png -Force }
RunEdge @('--window-size=1980,1340', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png", $url)
$pdf = Join-Path $here 'JollyHelmet_DFD_Level1.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=6000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
foreach ($f in $png, $pdf) { (Split-Path $f -Leaf) + ': ' + $(if (Test-Path $f) { (Get-Item $f).Length } else { 'MISSING' }) }
