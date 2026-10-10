# Builds the technician flow (tech_flow.src.html + the svg figures in figs.html) and splices it into JollyHelmet_UserManual.html
# between <!--TECH-FLOW-START--> and <!--TECH-FLOW-END-->.  Re-run after editing either source file.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$man  = Join-Path (Split-Path -Parent $here) 'JollyHelmet_UserManual.html'
$enc  = New-Object System.Text.UTF8Encoding($false)
$figs = [System.IO.File]::ReadAllText((Join-Path $here 'figs.html'), $enc)
$src  = [System.IO.File]::ReadAllText((Join-Path $here 'tech_flow.src.html'), $enc)
$rx = [regex]'(?s)<div class="f" id="fig-([a-z]+)">\s*(<svg.*?</svg>)\s*</div>'
foreach ($m in $rx.Matches($figs)) { $src = $src.Replace('@@FIG:' + $m.Groups[1].Value + '@@', $m.Groups[2].Value) }
if ($src -match '@@FIG:') { throw 'a figure placeholder was not replaced' }
$html = [System.IO.File]::ReadAllText($man, $enc)
$a = $html.IndexOf('<!--TECH-FLOW-START-->'); $b = $html.IndexOf('<!--TECH-FLOW-END-->')
if ($a -lt 0 -or $b -lt 0) { throw 'markers not found in the manual html' }
$new = $html.Substring(0, $a) + "<!--TECH-FLOW-START-->`n" + $src.Trim() + "`n" + $html.Substring($b)
[System.IO.File]::WriteAllText($man, $new, $enc)
"spliced " + $src.Length + " chars into " + $man
