# Makes the finished files from BOX 3 STEP.txt (or the file given with -Json): BOX3_Flow_TFT.pdf, BOX3_Flow_TFT.png and screens\NN_STEP.png (every TFT screen, 640 x 480).
param([string]$Json = '..\BOX 3 STEP.txt')     # which Designer export to draw (default: the file next to the project folder)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
& (Join-Path $here 'build_flow.ps1') -Json $Json | Out-Null
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$page = 'file:///' + ((Join-Path $here 'box3_flow.html') -replace '\\', '/').Replace(' ', '%20')
function RunEdge([string[]]$a) {
  $prof = Join-Path $env:TEMP ('edgeflow_' + [Guid]::NewGuid().ToString('N'))
  $all = @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }   # paths with a space must be quoted
  $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(90000)) { $p.Kill() }
  Start-Sleep -Milliseconds 600; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
$png = Join-Path $here 'BOX3_Flow_TFT.png'; $pdf = Join-Path $here 'BOX3_Flow_TFT.pdf'
foreach ($f in $png, $pdf) { if (Test-Path $f) { Remove-Item $f -Force } }
RunEdge @('--window-size=1680,2640', '--force-device-scale-factor=2', '--virtual-time-budget=15000', "--screenshot=$png", $page)
RunEdge @('--virtual-time-budget=15000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $page)
# every screen, cut from one contact sheet
$json = (Get-Content -Raw -Encoding UTF8 (Resolve-Path (Join-Path $here $Json)).Path | ConvertFrom-Json)
$n = $json.steps.Count; $rows = [Math]::Ceiling($n / 4)
$sheet = Join-Path $here 'screens\_sheet.png'; if (Test-Path $sheet) { Remove-Item $sheet -Force }
for ($try = 1; $try -le 3 -and -not (Test-Path $sheet); $try++) {      # Edge sometimes leaves no file on the first try: ask again
  RunEdge @("--window-size=2560,$($rows * 480)", '--force-device-scale-factor=1', '--virtual-time-budget=15000', "--screenshot=$sheet", ($page + '?sheet=1'))
}
Add-Type -AssemblyName System.Drawing
$img = [Drawing.Image]::FromFile($sheet)
Get-ChildItem (Join-Path $here 'screens') -Filter '*.png' | Where-Object { $_.Name -ne '_sheet.png' } | Remove-Item -Force
for ($i = 0; $i -lt $n; $i++) {
  $bm = New-Object Drawing.Bitmap(640, 480); $g = [Drawing.Graphics]::FromImage($bm)
  $sx = [int](($i % 4) * 640); $sy = [int]([Math]::Floor($i / 4) * 480)
  $dst = New-Object Drawing.Rectangle -ArgumentList 0, 0, 640, 480; $srcR = New-Object Drawing.Rectangle -ArgumentList $sx, $sy, 640, 480
  $g.DrawImage($img, $dst, $srcR, [Drawing.GraphicsUnit]::Pixel); $g.Dispose()
  $name = ('{0:D2}_{1}.png' -f ($i + 1), (($json.steps[$i].name -replace '[^A-Za-z0-9]+', '_').Trim('_')))
  $bm.Save((Join-Path $here "screens\$name"), [Drawing.Imaging.ImageFormat]::Png); $bm.Dispose()
}
$img.Dispose(); Remove-Item $sheet -Force
"done: $(if (Test-Path $pdf) { 'pdf ' + [math]::Round((Get-Item $pdf).Length / 1KB) + ' KB' } else { 'NO PDF' }), $(if (Test-Path $png) { 'png ' + [math]::Round((Get-Item $png).Length / 1KB) + ' KB' } else { 'NO PNG' }), $((Get-ChildItem (Join-Path $here 'screens') -Filter '*.png').Count) screens"