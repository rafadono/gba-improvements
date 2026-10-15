<#
.SYNOPSIS
  Generic headless capture run for a host executable (see docs/CAPTURE_HARNESS.md).
  The host reads variables named <Prefix>_CAPTURE_DIR, <Prefix>_CAPTURE_FRAMES, ... where Prefix is whatever it
  passed as CaptureConfig::env_prefix.
.EXAMPLE
  .\run_capture.ps1 -Prefix MYGAME -Exe .\build\mygame.exe -Rom roms\game.gba -Dir captures\t1 -Frames "300,600-610" `
      -Script first_dialogue.txt -Window 852x480 -Set "screen_mask=0" -Mode output -Fast
#>
param(
    [Parameter(Mandatory = $true)][string]$Prefix,
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Rom = "",
    [string]$Bios = "",
    [string]$Dir = "captures\run",          # output directory (PNG files)
    [string]$Frames = "",                   # e.g. "300,600-610,1200" (emulated frames)
    [string]$Script = "",                   # input script file or inline "120-125: A; 200: START"
    [string]$Set = "",                      # "key=value;key=value" (host settings)
    [string]$Window = "",                   # "WxH" output size
    [string]$Variants = "",                 # "852x480 key=0|960x640": one output image per entry
    [ValidateSet("output","native","both")][string]$Mode = "output",
    [string]$LoadState = "",
    [string]$SaveState = "",                # "<pump>:<slot>" (pump count since process start)
    [string]$SavePpu = "",                  # "300:1,900:2": save slots at absolute PPU frames
    [string]$LoadPpuFrame = "",             # PPU frame of -LoadState (default: its .meta sidecar; 0 = boot)
    [switch]$Fast,
    [string]$ExtraArgs = "",
    [int]$ExitFrame = 0,
    [int]$TimeoutSec = 240,
    [string]$Log = ""
)
$ErrorActionPreference = "Stop"
$Prefix = $Prefix.TrimEnd('_')
if (-not (Test-Path $Exe)) { throw "$Exe not found" }
$exeFull = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$absDir = (Resolve-Path $Dir).Path
function Set-HostVar($name, $value) { Set-Item -Path "Env:${Prefix}_$name" -Value $value }

$env:SDL_VIDEODRIVER = "dummy"; $env:SDL_AUDIODRIVER = "dummy"
Set-HostVar CAPTURE_DIR $absDir
Set-HostVar CAPTURE_MODE $Mode
if ($Frames) { Set-HostVar CAPTURE_FRAMES $Frames; Set-HostVar EXIT_AFTER_CAPTURE "1" }
if ($Script) {
    if (Test-Path $Script) { Set-HostVar INPUT_SCRIPT (Resolve-Path $Script).Path } else { Set-HostVar INPUT_SCRIPT $Script }
}
if ($Set) { Set-HostVar SET $Set }
if ($Window) { Set-HostVar WINDOW_SIZE $Window }
if ($Variants) { Set-HostVar CAPTURE_VARIANTS $Variants }
if ($LoadState) { Set-HostVar SAVESTATE_LOAD (Resolve-Path $LoadState).Path }
if ($SaveState) { Set-HostVar SAVESTATE_SAVE $SaveState }
if ($SavePpu) { Set-HostVar SAVESTATE_SAVE_PPU $SavePpu }
if ($LoadPpuFrame) { Set-HostVar SAVESTATE_LOAD_PPU_FRAME $LoadPpuFrame }
# Where the runtime writes the slots (<rom>.state<N>): lets the host write the .meta sidecars.
if ($SavePpu -and $Rom -and (Test-Path $Rom)) { Set-HostVar SAVESTATE_ROM (Resolve-Path $Rom).Path }
if ($Fast) { Set-HostVar FAST "1" }
if ($ExitFrame -gt 0) { Set-HostVar EXIT_FRAME "$ExitFrame" }

if (-not $Log) { $Log = Join-Path $absDir "run.log" }
$errLog = "$Log.err"
$argStr = ""
if ($Rom) { $argStr += "--rom `"$Rom`" " }
if ($Bios) { $argStr += "--bios `"$Bios`" " }
if ($ExtraArgs) { $argStr += $ExtraArgs }
$startArgs = @{ FilePath = $exeFull; WorkingDirectory = (Get-Location).Path; RedirectStandardOutput = $Log;
                RedirectStandardError = $errLog; PassThru = $true; WindowStyle = "Hidden" }
if ($argStr.Trim()) { $startArgs.ArgumentList = $argStr.Trim() }
$p = Start-Process @startArgs
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Write-Host "timeout after ${TimeoutSec}s, killing PID $($p.Id)"
    $p.Kill(); $p.WaitForExit()
}
Write-Host "exit code: $($p.ExitCode)  (logs: $Log, $errLog)"
Get-ChildItem $absDir -Filter *.png | Sort-Object Name | ForEach-Object { Write-Host $_.FullName }
