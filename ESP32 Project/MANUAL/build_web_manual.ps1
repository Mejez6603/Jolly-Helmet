# ---------------------------------------------------------------------------------------------------------------------------------
# build_web_manual.ps1  -  turns the EDITED WORD FILE into (1) a PDF next to it and (2) the Manual tab of the online web page.
#
#   Right-click > Run with PowerShell.  It asks for the TEAM PASSWORD (type one, you need it again to unlock the team chapters on the web page).
#   TWO PHASES (added 2026-10-16, so Word is only needed once and the password can come later):
#      -PdfOnly   no password: Word exports the PDF + the public customer files, and the page texts for the team copy are SAVED (build\_web_cache).
#      -TeamOnly  asks for the team password and encrypts the saved texts + screen PDF into team_manual.js. Word is NOT opened.
#   Without either switch it does both in one go, as before.
#   Needs Microsoft Word on this PC. Word may stay open with your file: only a COPY is opened (hidden), nothing in your file is changed.
#
#   Writes   JollyHelmet_UserManual_v4.pdf                    print quality PDF of the newest Word file (same folder as the Word file)
#            ..\ONLINE_WEB\manual\customer_guide.pdf          the customer chapters only. Public: anybody who opens the web page can read it.
#            ..\ONLINE_WEB\manual\customer_index.js           chapter list and search text of those pages (public)
#            ..\ONLINE_WEB\manual\team_manual.js              the WHOLE manual (admin, technician, reference with the Wi-Fi passwords),
#                                                             encrypted with the team password (AES-256 + HMAC, PBKDF2 150000 rounds).
#                                                             Without the password the file is unreadable, so it is safe on a public web site.
# ---------------------------------------------------------------------------------------------------------------------------------
param(
  [string]$Docx,                       # default: the newest JollyHelmet_UserManual_v*.docx in this folder
  [string]$Password,                   # default: asked
  [string]$WebDir = (Join-Path $PSScriptRoot '..\ONLINE_WEB\manual'),
  [int]$Rounds = 150000,
  [switch]$UpdateFields,              # also update the contents and index fields in the copy before exporting
  [switch]$PdfOnly,                   # phase 1: PDF + public customer files + save the team data, no password asked
  [switch]$TeamOnly                   # phase 2: only the encrypted team copy, from what phase 1 saved (no Word)
)
$ErrorActionPreference = 'Stop'
function Log($m) { Write-Host ((Get-Date).ToString('HH:mm:ss') + '  ' + $m) }
$here = $PSScriptRoot
if (-not $Docx) {
  $Docx = (Get-ChildItem $here -Filter 'JollyHelmet_UserManual_v*.docx' | Where-Object { $_.Name -notlike '~$*' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not $Docx -or -not (Test-Path $Docx)) { throw 'No Word file found.' }
if (-not $Password -and -not $PdfOnly) {
  $sec = Read-Host 'Team password for the admin, technician and reference chapters (keep it; the web page asks for it)' -AsSecureString
  $Password = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
}
if (-not $PdfOnly -and $Password.Length -lt 6) { throw 'Use a team password of at least 6 characters.' }
$WebDir = [IO.Path]::GetFullPath($WebDir)
New-Item -ItemType Directory -Force $WebDir | Out-Null
$tmp = Join-Path $env:TEMP 'jh_web_manual'; New-Item -ItemType Directory -Force $tmp | Out-Null
$copy = Join-Path $tmp 'manual_copy.docx'
Copy-Item $Docx $copy -Force
$pdfPrint = [IO.Path]::ChangeExtension($Docx, '.pdf')
$pdfScreen = Join-Path $tmp 'full_screen.pdf'
$pdfCust = Join-Path $tmp 'customer_screen.pdf'
"Word file : $Docx"
$enc = New-Object System.Text.UTF8Encoding($false)
$cache = Join-Path $here 'build\_web_cache'
$cacheMeta = "$Docx|" + (Get-Item $Docx).LastWriteTimeUtc.Ticks
if ($TeamOnly) {
  if (-not (Test-Path (Join-Path $cache 'meta.txt')) -or ([IO.File]::ReadAllText((Join-Path $cache 'meta.txt')) -ne $cacheMeta)) { throw 'The saved team data is not for this Word file (or there is none). Run with -PdfOnly first (this opens Word once).' }
  $pdfScreen = Join-Path $cache 'full_screen.pdf'
  $teamIdx = [IO.File]::ReadAllText((Join-Path $cache 'team_idx.json'), $enc)
  Log 'Using the saved team data (no Word)'
}
if (-not $TeamOnly) {


# ---------- Word: export the PDFs and read the headings and page texts (in a separate process with a time limit)
# Word may already be open with your file. Only the COPY is opened (and closed again). Fields are NOT updated unless you add -UpdateFields:
# so press Ctrl+A, F9 in Word (contents and index) and save before you run this, if you changed the text.
$wordWork = {
  param($copy, $pdfPrint, $pdfScreen, $pdfCust, $update)
  $ErrorActionPreference = 'Stop'
  $running = $false
  try { $word = [Runtime.InteropServices.Marshal]::GetActiveObject('Word.Application'); $running = $true } catch { $word = New-Object -ComObject Word.Application; $word.Visible = $true; $word.WindowState = 2 }   # a new Word needs a (minimized) window, or Range.Information crashes it
  $m = [Type]::Missing
  $oldAlerts = $word.DisplayAlerts; $word.DisplayAlerts = 0     # no questions (a file made with "update fields on opening" would otherwise wait for a click in the hidden window)
  $doc = $word.Documents.Open($copy, $false, $false, $false, $m, $m, $false, $m, $m, $m, $m, (-not $running))
  $res = @{ log = @() }
  try {
    if ($update) { $doc.Fields.Update() | Out-Null; foreach ($t in $doc.TablesOfContents) { $t.Update() }; foreach ($i in $doc.Indexes) { $i.Update() }; $res.log += 'fields updated' }
    $n = $doc.ComputeStatistics(2); $res.n = $n; $res.log += "pages $n"
    # the PDFs: print quality (everything), screen quality (everything)
    $doc.ExportAsFixedFormat($pdfPrint, 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false); $res.log += 'print pdf'
    $doc.ExportAsFixedFormat($pdfScreen, 17, $false, 1, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false); $res.log += 'screen pdf'
    # headings (outline levels 1-4) with their page
    $toc = @()
    foreach ($p in $doc.Paragraphs) {
      $lv = $p.OutlineLevel
      if ($lv -ge 1 -and $lv -le 4) {
        $txt = ($p.Range.Text -replace '[\r\a\f]', '').Trim()
        if ($txt) { $toc += [pscustomobject]@{ t = $txt; l = [int]$lv; p = [int]$p.Range.Information(3) } }
      }
    }
    $res.toc = $toc; $res.log += ('headings ' + $toc.Count)
    # the customer chapters = first level-1 heading up to the page before the second level-1 heading (Customer guide ... Admin guide)
    $h1 = @($toc | Where-Object { $_.l -eq 1 })
    if ($h1.Count -lt 2) { throw 'Expected the level-1 headings Customer guide and Admin guide.' }
    $first = $h1[0].p; $last = $h1[1].p - 1
    $res.first = $first; $res.last = $last; $res.log += "customer pages $first-$last"
    $doc.ExportAsFixedFormat($pdfCust, 17, $false, 1, 3, $first, $last, 0, $true, $true, 1, $true, $true, $false); $res.log += 'customer pdf'
    # the text of every page, for the search box
    $starts = @()
    for ($k = 1; $k -le $n; $k++) { $starts += $doc.GoTo(1, 1, $k).Start }
    $endDoc = $doc.Content.End
    $pages = @()
    for ($k = 0; $k -lt $n; $k++) {
      $e = if ($k + 1 -lt $n) { $starts[$k + 1] } else { $endDoc }
      $txt = $doc.Range($starts[$k], $e).Text
      $pages += (($txt -replace '[\r\n\a\f\x0B\x01\x13\x14\x15]', ' ' -replace '\s+', ' ').Trim())
    }
    $res.pages = $pages; $res.log += 'page texts'
  } finally {
    $doc.Close($false); $word.DisplayAlerts = $oldAlerts
    if (-not $running) { $word.Quit() }
  }
  return $res
}
Log 'Word is exporting the PDFs (this takes about a minute)'
$job = Start-Job -ScriptBlock $wordWork -ArgumentList $copy, $pdfPrint, $pdfScreen, $pdfCust, ($UpdateFields.IsPresent)
if (-not (Wait-Job $job -Timeout 300)) {
  Stop-Job $job
  try { $w = [Runtime.InteropServices.Marshal]::GetActiveObject('Word.Application'); foreach ($d in @($w.Documents)) { if ($d.FullName -eq $copy) { $d.Close($false) } } } catch {}
  throw 'Word did not answer within 5 minutes (a dialog may be open in Word). Close any dialog in Word and run the script again.'
}
$r = Receive-Job $job; Remove-Job $job
$r.log | ForEach-Object { Log ('  ' + $_) }
$toc = @($r.toc); $pages = @($r.pages); $first = [int]$r.first; $last = [int]$r.last; $n = [int]$r.n
if ($pages.Count -ne $n) { throw "Read $($pages.Count) page texts for $n pages." }
"PDF       : $pdfPrint (" + [math]::Round((Get-Item $pdfPrint).Length / 1MB, 1) + ' MB)'

# ---------- the public part: customer_guide.pdf + customer_index.js
Copy-Item $pdfCust (Join-Path $WebDir 'customer_guide.pdf') -Force
$pub = [ordered]@{
  first = $first; last = $last
  pages = @($pages[($first - 1)..($last - 1)])
  toc   = @($toc | Where-Object { $_.p -ge $first -and $_.p -le $last })
}
$json = $pub | ConvertTo-Json -Depth 5 -Compress
[IO.File]::WriteAllText((Join-Path $WebDir 'customer_index.js'), "/* generated by build_web_manual.ps1 - public (customer chapters only) */`nwindow.JH_MANUAL_PUBLIC = $json;`n", $enc)
# what the team copy needs, saved so the password step can run later without Word
$teamIdx = [ordered]@{ pages = @($pages); toc = @($toc) } | ConvertTo-Json -Depth 5 -Compress
New-Item -ItemType Directory -Force $cache | Out-Null
Copy-Item $pdfScreen (Join-Path $cache 'full_screen.pdf') -Force
[IO.File]::WriteAllText((Join-Path $cache 'team_idx.json'), $teamIdx, $enc)
[IO.File]::WriteAllText((Join-Path $cache 'meta.txt'), $cacheMeta, $enc)
Log 'Saved the team data for the password step'
}   # end of: if (-not $TeamOnly)
if ($PdfOnly) {
  Get-ChildItem $WebDir | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } } | Format-Table -AutoSize
  "PDF and the public customer files are done. The team copy (team_manual.js) still has the OLD text: run again with -TeamOnly and the team password."
  return
}

# ---------- the team part: everything, encrypted
function Protect([byte[]]$data, [string]$pwd, [int]$rounds) {
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  $salt = New-Object byte[] 16; $rng.GetBytes($salt); $iv = New-Object byte[] 16; $rng.GetBytes($iv)
  $kdf = New-Object Security.Cryptography.Rfc2898DeriveBytes($pwd, $salt, $rounds, [Security.Cryptography.HashAlgorithmName]::SHA256)
  $keys = $kdf.GetBytes(64)
  $aesKey = New-Object byte[] 32; $macKey = New-Object byte[] 32
  [Array]::Copy($keys, 0, $aesKey, 0, 32); [Array]::Copy($keys, 32, $macKey, 0, 32)
  $aes = [Security.Cryptography.Aes]::Create(); $aes.Mode = 'CBC'; $aes.Padding = 'PKCS7'; $aes.Key = $aesKey; $aes.IV = $iv
  $ct = $aes.CreateEncryptor().TransformFinalBlock($data, 0, $data.Length)
  $ms = New-Object IO.MemoryStream; $ms.Write($iv, 0, 16); $ms.Write($ct, 0, $ct.Length)
  $mac = (New-Object Security.Cryptography.HMACSHA256(, $macKey)).ComputeHash($ms.ToArray())
  $out = New-Object IO.MemoryStream
  $out.Write([Text.Encoding]::ASCII.GetBytes('JHM1'), 0, 4)
  $r = [BitConverter]::GetBytes([uint32]$rounds); [Array]::Reverse($r); $out.Write($r, 0, 4)       # rounds, big endian
  $out.Write($salt, 0, 16); $out.Write($iv, 0, 16); $out.Write($mac, 0, 32); $out.Write($ct, 0, $ct.Length)
  return $out.ToArray()
}
$idxEnc = [Convert]::ToBase64String((Protect $enc.GetBytes($teamIdx) $Password $Rounds))
$pdfEnc = [Convert]::ToBase64String((Protect ([IO.File]::ReadAllBytes($pdfScreen)) $Password $Rounds))
[IO.File]::WriteAllText((Join-Path $WebDir 'team_manual.js'), "/* generated by build_web_manual.ps1 - encrypted with the team password (AES-256-CBC + HMAC-SHA256, PBKDF2-SHA256) */`nwindow.JH_MANUAL_TEAM = { idx: `"$idxEnc`", pdf: `"$pdfEnc`" };`n", $enc)
Get-ChildItem $WebDir | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } } | Format-Table -AutoSize
"Done. Open ONLINE_WEB\index.html#manual"
