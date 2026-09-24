param([string]$in, [string]$out, [int]$x, [int]$y, [int]$w, [int]$h, [int]$scale = 4)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($in)
$bmp = New-Object System.Drawing.Bitmap ($w * $scale), ($h * $scale)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$dst = New-Object System.Drawing.Rectangle 0, 0, ($w * $scale), ($h * $scale)
$srcRect = New-Object System.Drawing.Rectangle $x, $y, $w, $h
$g.DrawImage($src, $dst, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$src.Dispose(); $bmp.Dispose()
"cropped $out"
