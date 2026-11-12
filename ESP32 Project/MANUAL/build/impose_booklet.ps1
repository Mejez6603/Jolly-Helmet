param(
  [Parameter(Mandatory)][string]$Pdf,            # the book PDF in reading order (page count must be a multiple of 4; blanks are added if not)
  [Parameter(Mandatory)][string]$OutPdf,         # the booklet PDF (landscape sheets, 2 book pages per sheet side)
  [double]$SheetW = 792, [double]$SheetH = 612,  # sheet in points, LANDSCAPE: Letter 792 x 612, A4 842 x 595, Long bond (8.5 x 13) 936 x 612
  [int]$RenderWidth = 1700,                      # pixels across one book page before it is scaled down onto the sheet
  [int]$Quality = 90                             # JPEG quality
)
# BOOKLET IMPOSITION (2026-11-04). Reading-order PDF in, "saddle-stitch" booklet PDF out:
#   sheet 1 front = last page | page 1      sheet 1 back = page 2 | second-to-last page
#   sheet 2 front = N-2 | page 3            sheet 2 back = page 4 | N-3     ... and so on.
# Print the result DOUBLE-SIDED, FLIP ON THE SHORT EDGE, 100 % / actual size, then fold the stack in half (staple on the fold).
# No external tools: pages are drawn with Windows.Data.Pdf, stored as JPEG, and the PDF file is written by hand.
$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture
Add-Type -AssemblyName System.Runtime.WindowsRuntime
Add-Type -AssemblyName System.Drawing
$null = [Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType=WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Storage.Streams, ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType=WindowsRuntime]
$asOp = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asAct = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]
function AwaitOp($op, $type) { $t = $asOp.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }

$Pdf = (Resolve-Path $Pdf).Path
$file = AwaitOp ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Pdf)) ([Windows.Storage.StorageFile])
$doc = AwaitOp ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
$n = [int]$doc.PageCount
$np = [int]([math]::Ceiling($n / 4.0) * 4)          # padded page count
$sheets = $np / 4
"source pages: $n ; padded to: $np ; sheets (paper): $sheets ; sides: $($sheets * 2)"

# 1. every page -> white-backed JPEG
$codec = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$ep = New-Object Drawing.Imaging.EncoderParameters(1)
$ep.Param[0] = New-Object Drawing.Imaging.EncoderParameter([Drawing.Imaging.Encoder]::Quality, [long]$Quality)
$jpg = New-Object 'object[]' ($np + 1)               # 1-based
$pw = New-Object 'int[]' ($np + 1); $ph = New-Object 'int[]' ($np + 1); $aspect = New-Object 'double[]' ($np + 1)
for ($p = 1; $p -le $n; $p++) {
  $page = $doc.GetPage($p - 1)
  $stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
  $opts = New-Object Windows.Data.Pdf.PdfPageRenderOptions; $opts.DestinationWidth = $RenderWidth
  $t = $asAct.Invoke($null, @($page.RenderToStreamAsync($stream, $opts))); $t.Wait(-1) | Out-Null
  $reader = New-Object Windows.Storage.Streams.DataReader($stream.GetInputStreamAt(0))
  $null = AwaitOp ($reader.LoadAsync([uint32]$stream.Size)) ([uint32])
  $bytes = New-Object byte[] ([int]$stream.Size); $reader.ReadBytes($bytes)
  $page.Dispose()
  $src = [Drawing.Image]::FromStream((New-Object IO.MemoryStream(, $bytes)))
  $bmp = New-Object Drawing.Bitmap($src.Width, $src.Height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [Drawing.Graphics]::FromImage($bmp); $g.Clear([Drawing.Color]::White); $g.DrawImage($src, 0, 0, $src.Width, $src.Height); $g.Dispose()
  $o = New-Object IO.MemoryStream; $bmp.Save($o, $codec, $ep)
  $jpg[$p] = $o.ToArray(); $pw[$p] = $bmp.Width; $ph[$p] = $bmp.Height; $aspect[$p] = $bmp.Width / [double]$bmp.Height
  $bmp.Dispose(); $src.Dispose()
}
$totalJpg = 0; for ($p = 1; $p -le $n; $p++) { $totalJpg += $jpg[$p].Length }
"rendered $n pages, JPEG total $([math]::Round($totalJpg / 1MB, 1)) MB"

# 2. booklet order: list of (left, right) page numbers per side, 0 = blank
$sides = New-Object System.Collections.Generic.List[object]
for ($s = 0; $s -lt $sheets; $s++) {
  $sides.Add(@(($np - 2 * $s), (1 + 2 * $s)))              # front: last | first
  $sides.Add(@((2 + 2 * $s), ($np - 1 - 2 * $s)))          # back : second | second-to-last
}
foreach ($sd in $sides) { for ($k = 0; $k -lt 2; $k++) { if ($sd[$k] -gt $n) { $sd[$k] = 0 } } }

# 3. write the PDF by hand
$ms = New-Object IO.MemoryStream
function W([string]$s) { $b = [Text.Encoding]::GetEncoding(28591).GetBytes($s); $ms.Write($b, 0, $b.Length) }
function WB([byte[]]$b) { $ms.Write($b, 0, $b.Length) }
function F([double]$v) { return $v.ToString('0.###', $inv) }
$offs = New-Object 'long[]' (3 + $n + $sides.Count * 2)   # objects 1 .. last
$firstImg = 3; $firstPage = 3 + $n
W "%PDF-1.4`n"; WB ([byte[]](0x25, 0xE2, 0xE3, 0xCF, 0xD3, 0x0A))
$offs[1] = $ms.Position; W "1 0 obj`n<< /Type /Catalog /Pages 2 0 R >>`nendobj`n"
$kids = (0..($sides.Count - 1) | ForEach-Object { "$($firstPage + 2 * $_) 0 R" }) -join ' '
$offs[2] = $ms.Position; W "2 0 obj`n<< /Type /Pages /Kids [ $kids ] /Count $($sides.Count) >>`nendobj`n"
for ($p = 1; $p -le $n; $p++) {
  $id = $firstImg + $p - 1; $offs[$id] = $ms.Position
  W "$id 0 obj`n<< /Type /XObject /Subtype /Image /Width $($pw[$p]) /Height $($ph[$p]) /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length $($jpg[$p].Length) >>`nstream`n"
  WB $jpg[$p]; W "`nendstream`nendobj`n"
}
$slotW = $SheetW / 2
for ($i = 0; $i -lt $sides.Count; $i++) {
  $pg = $firstPage + 2 * $i; $ct = $pg + 1
  $res = ''; $cs = ''
  for ($k = 0; $k -lt 2; $k++) {
    $pn = $sides[$i][$k]; if ($pn -le 0) { continue }
    $dw = [math]::Min($slotW, $SheetH * $aspect[$pn]); $dh = $dw / $aspect[$pn]
    $x = $k * $slotW + ($slotW - $dw) / 2; $y = ($SheetH - $dh) / 2
    $res += "/Im$k $($firstImg + $pn - 1) 0 R "
    $cs += "q $(F $dw) 0 0 $(F $dh) $(F $x) $(F $y) cm /Im$k Do Q`n"
  }
  $offs[$pg] = $ms.Position
  W "$pg 0 obj`n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 $(F $SheetW) $(F $SheetH)] /Resources << /XObject << $res>> >> /Contents $ct 0 R >>`nendobj`n"
  $offs[$ct] = $ms.Position
  W "$ct 0 obj`n<< /Length $($cs.Length) >>`nstream`n$cs`nendstream`nendobj`n"
}
$xref = $ms.Position; $count = $offs.Length
W "xref`n0 $count`n0000000000 65535 f `n"
for ($id = 1; $id -lt $count; $id++) { W ("{0:0000000000} 00000 n `n" -f $offs[$id]) }
W "trailer`n<< /Size $count /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
[IO.File]::WriteAllBytes($OutPdf, $ms.ToArray())
"wrote $OutPdf : $($sides.Count) landscape sides ($sheets sheets, $([math]::Round((Get-Item $OutPdf).Length / 1MB, 1)) MB)"
$order = ($sides | ForEach-Object { "[$($_[0])|$($_[1])]" }) -join ' '
"order: $order"
