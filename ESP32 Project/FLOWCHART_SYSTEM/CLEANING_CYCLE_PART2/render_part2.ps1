# Makes Customer_Cleaning_Cycle_Part2.png (1700 x 2416) and Customer_Cleaning_Cycle_Part2.pdf from Customer_Cleaning_Cycle_Part2.html with Microsoft Edge (no internet needed).
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'Customer_Cleaning_Cycle_Part2.html'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgepart2_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$png = Join-Path $here 'Customer_Cleaning_Cycle_Part2.png'; if (Test-Path $png) { Remove-Item $png -Force }
RunEdge @('--window-size=425,604', '--force-device-scale-factor=4', '--virtual-time-budget=4000', "--screenshot=$png", $url)
$pdf = Join-Path $here 'Customer_Cleaning_Cycle_Part2.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=4000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
foreach ($f in $png, $pdf) { (Split-Path $f -Leaf) + ': ' + $(if (Test-Path $f) { (Get-Item $f).Length } else { 'MISSING' }) }
