param([int]$Port = 8766, [string]$Root = 'C:\Users\Administrator\Documents\Arduino\ESP32 Project\MANUAL')
# helper for the manual build tools: GET /m/<path> reads a file under the manual folder, POST /save/<path> writes one (the body is the file), GET anything else under /m is the same
$l = New-Object Net.HttpListener; $l.Prefixes.Add("http://localhost:$Port/"); $l.Start(); "listening $Port"
$mime = @{ '.html'='text/html; charset=utf-8'; '.js'='application/javascript'; '.css'='text/css'; '.png'='image/png'; '.jpg'='image/jpeg'; '.json'='application/json'; '.svg'='image/svg+xml'; '.txt'='text/plain' }
while ($l.IsListening) {
  $c = $l.GetContext(); $rq = $c.Request; $rs = $c.Response
  try {
    $p = [Uri]::UnescapeDataString($rq.Url.AbsolutePath)
    if ($p -match '\.\.') { $rs.StatusCode = 400; $b = [Text.Encoding]::UTF8.GetBytes('bad path') }
    elseif ($rq.HttpMethod -eq 'POST' -and $p.StartsWith('/save/')) {
      $rel = $p.Substring(6).Replace('/', '\'); $dest = Join-Path $Root $rel
      $dir = Split-Path $dest -Parent; if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
      $ms = New-Object IO.MemoryStream; $rq.InputStream.CopyTo($ms); [IO.File]::WriteAllBytes($dest, $ms.ToArray())
      $b = [Text.Encoding]::UTF8.GetBytes("saved $rel ($($ms.Length) bytes)")
    }
    elseif ($p.StartsWith('/m/')) {
      $f = Join-Path $Root $p.Substring(3).Replace('/', '\')
      if (Test-Path $f -PathType Leaf) { $b = [IO.File]::ReadAllBytes($f); $e = [IO.Path]::GetExtension($f).ToLower(); $rs.ContentType = $(if ($mime[$e]) { $mime[$e] } else { 'application/octet-stream' }) }
      else { $rs.StatusCode = 404; $b = [Text.Encoding]::UTF8.GetBytes('not found') }
    }
    else { $rs.StatusCode = 404; $b = [Text.Encoding]::UTF8.GetBytes('not found') }
    $rs.Headers.Add('Access-Control-Allow-Origin', '*')
    $rs.ContentLength64 = $b.Length; $rs.OutputStream.Write($b, 0, $b.Length)
  } catch { try { $rs.StatusCode = 500 } catch {} }
  finally { try { $rs.Close() } catch {} }
}
