# Builds system_flowcharts.html from diagrams\*.mmd, then makes SYSTEM_Flowcharts.pdf and png\NN_name.png.   Needs Microsoft Edge + internet (Mermaid is loaded from cdn.jsdelivr.net).
param([switch]$NoPng)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$enc = New-Object Text.UTF8Encoding($false)
$items = Get-ChildItem (Join-Path $here 'diagrams') -Filter '*.mmd' | Sort-Object Name | ForEach-Object { [ordered]@{ id = $_.BaseName; code = [IO.File]::ReadAllText($_.FullName, $enc) } }
$json = ConvertTo-Json -InputObject @($items) -Depth 4 -Compress
$json = $json.Replace('</', '<\/')
$t = [IO.File]::ReadAllText((Join-Path $here 'render.template.html'), $enc).Replace('/*DIAGRAMS_JSON*/', $json)
$page = Join-Path $here 'system_flowcharts.html'
[IO.File]::WriteAllText($page, $t, $enc)
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$url = 'file:///' + ($page -replace '\\', '/').Replace(' ', '%20')
function Quote([string[]]$a) { $a | ForEach-Object { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } } }
function RunEdge([string[]]$a, [string]$stdout) {
  $prof = Join-Path $env:TEMP ('edgesys_' + [Guid]::NewGuid().ToString('N'))
  $all = Quote (@('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "--user-data-dir=$prof") + $a)
  if ($stdout) { $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout }
  else { $p = Start-Process -FilePath $edge -ArgumentList $all -PassThru -WindowStyle Hidden }
  if (-not $p.WaitForExit(120000)) { $p.Kill() }
  Start-Sleep -Milliseconds 700; Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue
}
# the whole set as one PDF (one diagram per A3 landscape page)
$pdf = Join-Path $here 'SYSTEM_Flowcharts.pdf'; if (Test-Path $pdf) { Remove-Item $pdf -Force }
RunEdge @('--virtual-time-budget=30000', '--no-pdf-header-footer', "--print-to-pdf=$pdf", $url)
# one PNG per diagram, at the size Mermaid drew it (x2)
if (-not $NoPng) {
  foreach ($it in $items) {
    $u = $url + '?d=' + $it.id + '&bare=1'
    $dump = Join-Path $env:TEMP ('dump_' + $it.id + '.html'); if (Test-Path $dump) { Remove-Item $dump -Force }
    RunEdge @('--virtual-time-budget=30000', '--dump-dom', $u) $dump
    $dom = [IO.File]::ReadAllText($dump, $enc)
    $tm = [regex]::Match($dom, '\[done (\d+) bad (\d+)\]'); if (-not $tm.Success -or $tm.Groups[2].Value -ne '0') { Write-Warning ("{0}: Mermaid reports a syntax error (see class err in the page)" -f $it.id); continue }
    $m = [regex]::Match($dom, '<svg[^>]*viewBox="([\d\.\-]+) ([\d\.\-]+) ([\d\.]+) ([\d\.]+)"')
    if (-not $m.Success) { Write-Warning ("{0}: no svg found" -f $it.id); continue }
    $w = [int][Math]::Ceiling([double]$m.Groups[3].Value) + 30; $h = [int][Math]::Ceiling([double]$m.Groups[4].Value) + 30
    $png = Join-Path $here ('png\' + $it.id + '.png'); if (Test-Path $png) { Remove-Item $png -Force }
    RunEdge @("--window-size=$w,$h", '--force-device-scale-factor=2', '--virtual-time-budget=30000', "--screenshot=$png", $u)
    Remove-Item $dump -Force -ErrorAction SilentlyContinue
    '{0}: {1} x {2}  {3}' -f $it.id, $w, $h, $(if (Test-Path $png) { 'png ok' } else { 'NO PNG' })
  }
}
'done'