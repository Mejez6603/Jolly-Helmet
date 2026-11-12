# Makes one PNG per diagram (3960 x 2680) and ONE PDF with all the pages, from JollyHelmet_DFD_L2_L3.html with Microsoft Edge (no internet needed).
# usage:  .\render_dfd23.ps1            all diagrams
#         .\render_dfd23.ps1 L2-1 L2-3  only those PNGs (no PDF)
param([string[]]$Only)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = Join-Path $here 'JollyHelmet_DFD_L2_L3.html'
$base = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgedfd23_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 600; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$names = [ordered]@{
  'L2-1' = 'DFD_L2_1_Box_main_controller'
  'L2-2' = 'DFD_L2_2_Refilling_Station'
  'L2-3' = 'DFD_L2_3_Local_Server'
  'L2-4' = 'DFD_L2_4_Online_Server'
  'L2-5' = 'DFD_L2_5_Website'
  'L3-1' = 'DFD_L3_1_Run_the_cycle_1.4'
  'L3-2' = 'DFD_L3_2_Control_the_valves_2.3'
}
$ids = if ($Only) { $Only } else { $names.Keys }
$out = Join-Path $here 'png'; if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }
foreach ($id in $ids) {
  if (-not $names.Contains($id)) { continue }
  $png = Join-Path $out ($names[$id] + '.png'); if (Test-Path $png) { Remove-Item $png -Force }
  RunEdge @('--window-size=1980,1340', '--force-device-scale-factor=2', '--virtual-time-budget=6000', "--screenshot=$png", ($base + '?d=' + $id))
  $id + ': ' + $(if (Test-Path $png) { (Get-Item $png).Length } else { 'MISSING' })
}
if (-not $Only) {
  $pdf = Join-Path $here 'JollyHelmet_DFD_Level2_Level3.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
  RunEdge @('--virtual-time-budget=9000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", ($base + '?d=all'))
  'PDF: ' + $(if (Test-Path $pdf) { (Get-Item $pdf).Length } else { 'MISSING' })
}
