# Builds box3_flow.html (the flowchart page) from the Designer export. Run again whenever BOX 3 STEP.txt changes (or pass -Json to use another export).
param([string]$Json = '..\BOX 3 STEP.txt')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$src = (Resolve-Path (Join-Path $here $Json)).Path
$enc = New-Object Text.UTF8Encoding($false)
$raw = [IO.File]::ReadAllText($src, $enc).TrimStart([char]0xFEFF).Trim()
$obj = $raw | ConvertFrom-Json
$meta = (@{ stamp = (Get-Item $src).LastWriteTime.ToString('yyyy-MM-dd HH:mm'); preset = [string]$obj.preset; file = (Split-Path $src -Leaf) } | ConvertTo-Json -Compress)
$t = [IO.File]::ReadAllText((Join-Path $here 'box3_flow.template.html'), $enc)
$t = $t.Replace('/*FLOW_JSON*/', $raw).Replace('/*META_JSON*/', $meta)
[IO.File]::WriteAllText((Join-Path $here 'box3_flow.html'), $t, $enc)
"built box3_flow.html from $src ($($obj.steps.Count) steps)"