# Rebuilds JollyHelmet_UserManual.pdf from JollyHelmet_UserManual.html with Microsoft Edge (headless). Run it after editing the HTML.
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path $edge)) { $edge = 'C:\Program Files\Microsoft\Edge\Application\msedge.exe' }
$here = $PSScriptRoot
$html = Join-Path $here 'JollyHelmet_UserManual.html'
$pdf  = Join-Path $here 'JollyHelmet_UserManual.pdf'
$prof = Join-Path $env:TEMP 'jolly_manual_edge_profile'
$url  = 'file:///' + ($html -replace '\\','/').Replace(' ','%20')
$args = @('--headless=new','--disable-gpu','--no-pdf-header-footer','--virtual-time-budget=15000',"--user-data-dir=`"$prof`"","--print-to-pdf=`"$pdf`"","`"$url`"")
Start-Process -FilePath $edge -ArgumentList $args -Wait -WindowStyle Hidden
if (Test-Path $pdf) { "Done: $pdf" } else { "Failed - is Microsoft Edge installed?" }