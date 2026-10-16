# One click: puts the sources together (build\assemble_tech.ps1) and rebuilds the PDF (build_pdf.ps1).
# Run it (right-click, Run with PowerShell) after you edit build\tech_flow.src.html, build\ref_flow.src.html or add a Cirkit picture to images\cirkit.
# The Word file is made separately (see README.txt).
$here = $PSScriptRoot
& (Join-Path $here 'build\assemble_tech.ps1')
& (Join-Path $here 'build_pdf.ps1')
