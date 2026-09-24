param([string]$in, [string]$out, [int]$scale = 3, [int]$tile = 16)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($in)
$w = $src.Width * $scale; $h = $src.Height * $scale
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
# checkerboard so transparency is visible
$c1 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 40, 40, 48))
$c2 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 70, 70, 80))
for ($y = 0; $y -lt $h; $y += 12) { for ($x = 0; $x -lt $w; $x += 12) {
    $b = if ((($x / 12) + ($y / 12)) % 2 -eq 0) { $c1 } else { $c2 }
    $g.FillRectangle($b, $x, $y, 12, 12) } }
$g.DrawImage($src, 0, 0, $w, $h)
$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(160, 255, 0, 255)), 1
$font = New-Object System.Drawing.Font "Consolas", ([float]($scale * 3.2))
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::Yellow)
$shadow = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::Black)
$cols = [Math]::Floor($src.Width / $tile); $rows = [Math]::Floor($src.Height / $tile)
for ($c = 0; $c -le $cols; $c++) { $g.DrawLine($pen, $c * $tile * $scale, 0, $c * $tile * $scale, $h) }
for ($r = 0; $r -le $rows; $r++) { $g.DrawLine($pen, 0, $r * $tile * $scale, $w, $r * $tile * $scale) }
for ($r = 0; $r -lt $rows; $r++) { for ($c = 0; $c -lt $cols; $c++) {
    $t = "$c,$r"
    $x = $c * $tile * $scale + 2; $y = $r * $tile * $scale + 1
    $g.DrawString($t, $font, $shadow, $x + 1, $y + 1)
    $g.DrawString($t, $font, $brush, $x, $y) } }
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
"$out  ($cols x $rows tiles)"
