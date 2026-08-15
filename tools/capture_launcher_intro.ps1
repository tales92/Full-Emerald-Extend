[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Rom,
    [Parameter(Mandatory = $true)][string]$Bios,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(1, 600)][int]$StepFrames = 60,
    [ValidateRange(1, 5000)][int]$SampleCount = 40
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

foreach ($path in @($Executable, $Rom, $Bios)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
if ([IO.Path]::GetPathRoot($output) -ine 'D:\') {
    throw "Capture output must stay on D: (resolved '$output')."
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class FullEmeraldCaptureImage {
    public static byte[] DecodeHex(string value) {
        if (value == null || (value.Length & 1) != 0)
            throw new ArgumentException("Invalid hexadecimal framebuffer.");
        byte[] bytes = new byte[value.Length / 2];
        for (int i = 0; i < bytes.Length; ++i)
            bytes[i] = Convert.ToByte(value.Substring(i * 2, 2), 16);
        return bytes;
    }

    public static void SaveRgbPng(byte[] rgb, int width, int height,
                                  string path) {
        if (rgb == null || rgb.Length != width * height * 3)
            throw new ArgumentException("Framebuffer size does not match.");
        using (Bitmap bitmap = new Bitmap(width, height,
                                           PixelFormat.Format24bppRgb)) {
            Rectangle rect = new Rectangle(0, 0, width, height);
            BitmapData data = bitmap.LockBits(rect, ImageLockMode.WriteOnly,
                                               PixelFormat.Format24bppRgb);
            try {
                byte[] row = new byte[Math.Abs(data.Stride)];
                for (int y = 0; y < height; ++y) {
                    Array.Clear(row, 0, row.Length);
                    for (int x = 0; x < width; ++x) {
                        int source = (y * width + x) * 3;
                        int target = x * 3;
                        row[target + 0] = rgb[source + 2];
                        row[target + 1] = rgb[source + 1];
                        row[target + 2] = rgb[source + 0];
                    }
                    Marshal.Copy(row, 0,
                        IntPtr.Add(data.Scan0, y * data.Stride), row.Length);
                }
            } finally {
                bitmap.UnlockBits(data);
            }
            bitmap.Save(path, ImageFormat.Png);
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

$listener = [Net.Sockets.TcpListener]::new(
    [Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()

$save = Join-Path $output 'capture.sav'
$stdout = Join-Path $output 'worker.stdout.log'
$stderr = Join-Path $output 'worker.stderr.log'
$arguments = @(
    '--rom', [IO.Path]::GetFullPath($Rom),
    '--bios', [IO.Path]::GetFullPath($Bios),
    '--tcp', [string]$port, '--no-window', '--quiet', '--save', $save
)
$process = Start-Process -FilePath ([IO.Path]::GetFullPath($Executable)) `
    -ArgumentList $arguments -WorkingDirectory $output -WindowStyle Hidden `
    -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr

$client = [Net.Sockets.TcpClient]::new()
try {
    $connected = $false
    for ($attempt = 0; $attempt -lt 200 -and -not $process.HasExited; ++$attempt) {
        try {
            $client.Connect([Net.IPAddress]::Loopback, $port)
            $connected = $true
            break
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    if (-not $connected) {
        $tail = if (Test-Path -LiteralPath $stderr) {
            Get-Content -LiteralPath $stderr -Raw
        } else { '' }
        throw "Intro capture worker did not open TCP port $port. $tail"
    }

    $stream = $client.GetStream()
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $false,
                                     262144, $true)
    $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false),
                                     262144, $true)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true
    function Invoke-Worker([string]$Command) {
        $writer.WriteLine($Command)
        $line = $reader.ReadLine()
        if (-not $line) { throw "Capture worker closed its connection." }
        return $line
    }

    [void](Invoke-Worker '{"ping":true}')
    for ($index = 0; $index -lt $SampleCount; ++$index) {
        $step = Invoke-Worker (('{{"run_frames":true,"n":{0},"keyinput":1023}}' -f
                               $StepFrames)) | ConvertFrom-Json
        if (-not $step.ok) { throw "Frame stepping failed at sample $index." }
        $shot = Invoke-Worker '{"screenshot":true}' | ConvertFrom-Json
        if (-not $shot.ok) { throw "Screenshot failed at sample $index." }
        $bytes = [FullEmeraldCaptureImage]::DecodeHex([string]$shot.data)
        $frame = [int]$step.frame
        $path = Join-Path $output ('sample-{0:D5}.png' -f $frame)
        [FullEmeraldCaptureImage]::SaveRgbPng(
            $bytes, [int]$shot.w, [int]$shot.h, $path)
        Write-Host ("Captured frame {0} -> {1}" -f $frame, $path)
    }
    [void](Invoke-Worker '{"quit":true}')
    $reader.Dispose()
    $writer.Dispose()
} finally {
    $client.Dispose()
    if (-not $process.HasExited) {
        if (-not $process.WaitForExit(5000)) { $process.Kill() }
    }
    $process.Dispose()
}
