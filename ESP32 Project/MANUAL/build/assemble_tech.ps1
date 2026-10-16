# Builds the two hidden flows of the manual and splices them into JollyHelmet_UserManual.html:
#   tech_flow.src.html -> between <!--TECH-FLOW-START--> and <!--TECH-FLOW-END-->      (the Technician guide)
#   ref_flow.src.html  -> between <!--REF-FLOW-START-->  and <!--REF-FLOW-END-->       (the Reference section)
# Figures:  @@FIG:name@@ = the svg of that name in figs.html or figs_ref.html (figs_ref.html is written by flowgen.html)
# Pictures: @@CIRKIT:file|caption@@ = images\cirkit\<file>.png/.jpg if it exists, else a dashed slot that says which file to save
# Facts:    @@FWDATE:folder@@  @@LIB:name@@  @@MD5:path@@  @@TODAY@@  @@INDEXTERMS@@ are read from this computer at build time.
# Re-run after editing any source file or after adding a Cirkit picture, then run build_pdf.ps1 (or just run ..\rebuild_all.ps1).
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$man  = Join-Path (Split-Path -Parent $here) 'JollyHelmet_UserManual.html'
$proj = Split-Path -Parent (Split-Path -Parent $here)          # the ESP32 Project folder (the sketches)
$enc  = New-Object System.Text.UTF8Encoding($false)
$read = { param($n) [System.IO.File]::ReadAllText((Join-Path $here $n), $enc) }

$figs = (& $read 'figs.html')
$refFigs = Join-Path $here 'figs_ref.html'
if (Test-Path $refFigs) { $figs += "`n" + (& $read 'figs_ref.html') }
$rx = [regex]'(?s)<div class="f" id="fig-([a-z0-9]+)">\s*(<svg.*?</svg>)\s*</div>'
$svg = @{}
foreach ($m in $rx.Matches($figs)) { $svg[$m.Groups[1].Value] = $m.Groups[2].Value }

function Expand([string]$src) {
  foreach ($k in $svg.Keys) { $src = $src.Replace('@@FIG:' + $k + '@@', $svg[$k]) }
  # Cirkit pictures
  $cdir = Join-Path (Split-Path -Parent $here) 'images\cirkit'
  $src = [regex]::Replace($src, '@@CIRKIT:([A-Za-z0-9_]+)\|([^@]*)@@', {
    param($m)
    $name = $m.Groups[1].Value; $cap = $m.Groups[2].Value
    foreach ($ext in 'png','jpg','jpeg') {
      if (Test-Path (Join-Path $cdir "$name.$ext")) { return '<figure class="fig cirkit"><img src="images/cirkit/' + $name + '.' + $ext + '" alt="' + $cap + '"><figcaption class="cap">' + $cap + '</figcaption></figure>' }
    }
    return '<figure class="fig cirkit"><div class="ph" style="height:250px">Cirkit Designer picture goes here<br>Save the export as images\cirkit\' + $name + '.png, then rebuild</div><figcaption class="cap">' + $cap + '</figcaption></figure>'
  })
  # file dates of the sketches
  $src = [regex]::Replace($src, '@@FWDATE:([A-Za-z0-9_]+)@@', {
    param($m)
    $f = Join-Path $proj ($m.Groups[1].Value + '\' + $m.Groups[1].Value + '.ino')
    if (Test-Path $f) { (Get-Item $f).LastWriteTime.ToString('yyyy-MM-dd') } else { 'not found' }
  })
  # library versions
  $lib = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries'
  $src = [regex]::Replace($src, '@@LIB:([A-Za-z0-9_]+)@@', {
    param($m)
    $n = $m.Groups[1].Value
    if ($n -eq 'core') {
      $d = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32'
      $v = Get-ChildItem $d -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
      if ($v) { return $v.Name } else { return 'not found' }
    }
    $p = Join-Path $lib ($n + '\library.properties')
    if (Test-Path $p) { $l = (Get-Content $p | Where-Object { $_ -like 'version=*' } | Select-Object -First 1); if ($l) { return $l.Substring(8) } }
    return 'not found'
  })
  # checksum (first 8 hex) of a shared file
  $src = [regex]::Replace($src, '@@MD5:([^@]+)@@', {
    param($m)
    $f = Join-Path $proj ($m.Groups[1].Value -replace '/', '\')
    if (Test-Path $f) { (Get-FileHash $f -Algorithm MD5).Hash.Substring(0, 8).ToLower() } else { 'not found' }
  })
  $src = $src.Replace('@@TODAY@@', (Get-Date).ToString('yyyy-MM-dd'))
  $it = Join-Path $here 'index_terms.json'
  if (Test-Path $it) { $src = $src.Replace('@@INDEXTERMS@@', (([System.IO.File]::ReadAllText($it, $enc)) -replace '\s*\r?\n\s*', ' ').Trim()) }
  if ($src -match '@@[A-Z]+[:@]') { throw ('a placeholder was not replaced: ' + $Matches[0]) }
  return $src
}

$html = [System.IO.File]::ReadAllText($man, $enc)
function Splice([string]$html, [string]$tag, [string]$srcName) {
  $src = Expand (& $read $srcName)
  $a = $html.IndexOf("<!--$tag-START-->"); $b = $html.IndexOf("<!--$tag-END-->")
  if ($a -lt 0 -or $b -lt 0) { throw "markers $tag not found in the manual html" }
  Write-Host ("spliced $tag : " + $src.Length + " chars")
  return $html.Substring(0, $a) + "<!--$tag-START-->`n" + $src.Trim() + "`n" + $html.Substring($b)
}
$html = Splice $html 'TECH-FLOW' 'tech_flow.src.html'
$html = Splice $html 'REF-FLOW' 'ref_flow.src.html'
[System.IO.File]::WriteAllText($man, $html, $enc)
Write-Host "written $man"
