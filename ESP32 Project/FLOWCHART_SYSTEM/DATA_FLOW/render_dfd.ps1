# Makes JollyHelmet_DataFlow.png (3800 x 2520) and JollyHelmet_DataFlow.pdf from JollyHelmet_DataFlow.html with Microsoft Edge (no internet needed).
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'JollyHelmet_DataFlow.html'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgedfd_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$png = Join-Path $here 'JollyHelmet_DataFlow.png'; if (Test-Path $png) { Remove-Item $png -Force }
RunEdge @('--window-size=1900,1260', '--force-device-scale-factor=2', '--virtual-time-budget=8000', "--screenshot=$png", $url)
$pdf = Join-Path $here 'JollyHelmet_DataFlow.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=8000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
'png: ' + $(if (Test-Path $png) { (Get-Item $png).Length } else { 'MISSING' }) + '   pdf: ' + $(if (Test-Path $pdf) { (Get-Item $pdf).Length } else { 'MISSING' })
