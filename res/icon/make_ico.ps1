# 把 icon.png 转成多尺寸 icon.ico（含黑底转透明）
# 用法：powershell -ExecutionPolicy Bypass -File .\make_ico.ps1
param(
    [string]$Source = "$PSScriptRoot\icon.png",
    [string]$Output = "$PSScriptRoot\icon.ico"
)

Add-Type -AssemblyName System.Drawing
$src = New-Object System.Drawing.Bitmap $Source
if ($src.Width -ne $src.Height) { throw "icon.png 必须是正方形，当前 $($src.Width)x$($src.Height)" }

$w = $src.Width
$h = $src.Height
$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h

# 角落采样：接近纯黑才做黑底转透明，否则原图直用
$corner = $src.GetPixel(0, 0)
$clean = $src
if (($corner.R -band $corner.G -band $corner.B) -lt 16) {
    $clean = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $sd = $src.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $dd = $clean.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $len = $sd.Stride * $h
    $buf = New-Object byte[] $len
    [System.Runtime.InteropServices.Marshal]::Copy($sd.Scan0, $buf, 0, $len)

    # 黑色底等价于预乘图：alpha 取 RGB 最大分量，颜色除回 alpha，边缘平滑无黑边
    for ($i = 0; $i -lt $len; $i += 4) {
        $r = $buf[$i + 2]; $g = $buf[$i + 1]; $b = $buf[$i]
        $a = [Math]::Max($r, [Math]::Max($g, $b))
        if ($a -gt 0) {
            $buf[$i]     = [Math]::Min(255, $b * 255 / $a)
            $buf[$i + 1] = [Math]::Min(255, $g * 255 / $a)
            $buf[$i + 2] = [Math]::Min(255, $r * 255 / $a)
            $buf[$i + 3] = $a
        }
    }
    [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $dd.Scan0, $len)
    $src.UnlockBits($sd)
    $clean.UnlockBits($dd)
}

# 逐尺寸缩放并压成 PNG 帧
$frames = @()
foreach ($s in @(16, 24, 32, 48, 64, 128, 256)) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $ia = New-Object System.Drawing.Imaging.ImageAttributes
    $ia.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $dest = New-Object System.Drawing.Rectangle 0, 0, $s, $s
    $g.DrawImage($clean, $dest, 0, 0, $w, $h, [System.Drawing.GraphicsUnit]::Pixel, $ia)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @($s, $ms.ToArray())
    $bmp.Dispose()
}

# 写 ICO 容器：目录 + 每帧一个 PNG 条目（Vista 起支持 PNG 帧）
$fs = [System.IO.File]::Create($Output)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $size = [byte]($(if ($f[0] -ge 256) { 0 } else { $f[0] }))
    $bw.Write($size); $bw.Write($size)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$f[1].Length)
    $bw.Write([uint32]$offset)
    $offset += $f[1].Length
}
foreach ($f in $frames) { $bw.Write($f[1]) }
$bw.Close()

"icon.ico 生成完成：$((Get-Item $Output).Length) 字节，$($frames.Count) 个尺寸"
