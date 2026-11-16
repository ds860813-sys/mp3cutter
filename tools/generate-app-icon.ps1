$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$assetDir = Join-Path $PSScriptRoot "..\assets"
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null

function New-RoundedRectPath {
    param(
        [System.Drawing.RectangleF]$Rect,
        [single]$Radius
    )

    $diameter = $Radius * 2
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($Rect.X, $Rect.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($Rect.Right - $diameter, $Rect.Bottom - $diameter,
                 $diameter, $diameter, 0, 90)
    $path.AddArc($Rect.X, $Rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconPng {
    param([int]$Size)

    $bitmap = [System.Drawing.Bitmap]::new(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint =
        [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $inset = [single]([Math]::Max(1, $Size * 0.035))
    $rect = [System.Drawing.RectangleF]::new(
        $inset, $inset, $Size - 2 * $inset, $Size - 2 * $inset)
    $radius = [single]([Math]::Max(2, $Size * 0.19))
    $path = New-RoundedRectPath $rect $radius

    $topColor = [System.Drawing.ColorTranslator]::FromHtml("#172027")
    $bottomColor = [System.Drawing.ColorTranslator]::FromHtml("#2B373E")
    $gradient = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $rect, $topColor, $bottomColor, 90.0)
    $graphics.FillPath($gradient, $path)

    $borderWidth = [single]([Math]::Max(1, $Size * 0.018))
    $border = [System.Drawing.Pen]::new(
        [System.Drawing.ColorTranslator]::FromHtml("#47565D"), $borderWidth)
    $graphics.DrawPath($border, $path)

    $mint = [System.Drawing.ColorTranslator]::FromHtml("#69DB91")
    $textColor = [System.Drawing.ColorTranslator]::FromHtml("#E3EBE7")
    $center = [System.Drawing.StringFormat]::new()
    $center.Alignment = [System.Drawing.StringAlignment]::Center
    $center.LineAlignment = [System.Drawing.StringAlignment]::Center
    $center.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip

    $numberFont = [System.Drawing.Font]::new(
        "Segoe UI", [single]($Size * 0.59), [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel)
    $numberRect = [System.Drawing.RectangleF]::new(
        0, $Size * 0.005, $Size, $Size * 0.64)
    $graphics.DrawString("3", $numberFont, [System.Drawing.SolidBrush]::new($mint),
                         $numberRect, $center)

    $mpFont = [System.Drawing.Font]::new(
        "Segoe UI", [single]($Size * 0.20), [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel)
    $mpRect = [System.Drawing.RectangleF]::new(
        0, $Size * 0.69, $Size, $Size * 0.20)
    $graphics.DrawString("mp", $mpFont, [System.Drawing.SolidBrush]::new($textColor),
                         $mpRect, $center)

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()

    if ($Size -eq 1024) {
        [System.IO.File]::WriteAllBytes(
            (Join-Path $assetDir "Mp3cutter-icon.png"), $bytes)
    }

    $stream.Dispose()
    $mpFont.Dispose()
    $numberFont.Dispose()
    $center.Dispose()
    $border.Dispose()
    $gradient.Dispose()
    $path.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    return ,$bytes
}

function New-IconDib {
    param([int]$Size)

    $png = New-IconPng $Size
    $pngStream = [System.IO.MemoryStream]::new($png)
    $bitmap = [System.Drawing.Bitmap]::FromStream($pngStream)
    $maskStride = [int]([Math]::Floor(($Size + 31) / 32) * 4)
    $mask = [byte[]]::new($maskStride * $Size)
    $pixels = [byte[]]::new($Size * $Size * 4)

    for ($y = 0; $y -lt $Size; ++$y) {
        for ($x = 0; $x -lt $Size; ++$x) {
            $color = $bitmap.GetPixel($x, $y)
            $destination = (($Size - 1 - $y) * $Size + $x) * 4
            $pixels[$destination] = $color.B
            $pixels[$destination + 1] = $color.G
            $pixels[$destination + 2] = $color.R
            $pixels[$destination + 3] = $color.A
            if ($color.A -eq 0) {
                $maskIndex = ($Size - 1 - $y) * $maskStride + [Math]::Floor($x / 8)
                $mask[$maskIndex] = $mask[$maskIndex] -bor (0x80 -shr ($x % 8))
            }
        }
    }

    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]40)
    $writer.Write([int32]$Size)
    $writer.Write([int32]($Size * 2))
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]0)
    $writer.Write([uint32]($Size * $Size * 4))
    $writer.Write([int32]0)
    $writer.Write([int32]0)
    $writer.Write([uint32]0)
    $writer.Write([uint32]0)
    $writer.Write($pixels)
    $writer.Write($mask)
    $result = $stream.ToArray()

    $writer.Dispose()
    $stream.Dispose()
    $bitmap.Dispose()
    $pngStream.Dispose()
    return ,$result
}

$null = New-IconPng 1024
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$images = @{}
foreach ($size in $sizes) {
    $images[$size] = New-IconDib $size
}

$icoPath = Join-Path $assetDir "Mp3cutter.ico"
$file = [System.IO.File]::Open(
    $icoPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
foreach ($size in $sizes) {
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$size].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$size].Length
}
foreach ($size in $sizes) {
    $writer.Write([byte[]]$images[$size])
}
$writer.Dispose()
$file.Dispose()

$validationIcon = [System.Drawing.Icon]::new($icoPath)
Write-Host "Validated icon: $($validationIcon.Width)x$($validationIcon.Height)"
$validationIcon.Dispose()
Write-Host "Created $icoPath"
