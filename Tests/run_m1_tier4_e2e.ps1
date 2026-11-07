# PowerShell script for Milestone 1 Tier 4 Real-World E2E Verification
# Usage: powershell -ExecutionPolicy Bypass -File G:\MQEngine\MQEngine\Tests\run_m1_tier4_e2e.ps1

$ErrorActionPreference = "Stop"

Write-Host "============================================================================" -ForegroundColor Cyan
Write-Host "  MQEngine Milestone 1: Tier 4 Real-World CLI Screenshot E2E Verification" -ForegroundColor Cyan
Write-Host "============================================================================" -ForegroundColor Cyan

$WorkspaceRoot = "G:\MQEngine\MQEngine"
$BuildDir = "$WorkspaceRoot\Output\build-windows-debug"
$EditorCliExe = "$BuildDir\Applications\EditorCli\EditorCli.exe"
if (-not (Test-Path $EditorCliExe)) {
    $EditorCliExe = "$BuildDir\EditorCli.exe"
}

if (-not (Test-Path $EditorCliExe)) {
    Write-Host "[ERROR] EditorCli.exe not found at: $EditorCliExe" -ForegroundColor Red
    Write-Host "Please build the EditorCli target first via cmake." -ForegroundColor Yellow
    exit 1
}

$ScreenshotFile = "$WorkspaceRoot\test_m1.png"
if (Test-Path $ScreenshotFile) {
    Remove-Item -Force $ScreenshotFile
    Write-Host "[INFO] Removed existing screenshot artifact: $ScreenshotFile"
}

$Arguments = @("--screenshot=test_m1.png", "--test_frames=30")
Write-Host "[RUNNING] Invoking: $EditorCliExe $($Arguments -join ' ')" -ForegroundColor Green

$ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
$ProcessInfo.FileName = $EditorCliExe
$ProcessInfo.Arguments = $Arguments -join " "
$ProcessInfo.WorkingDirectory = $WorkspaceRoot
$ProcessInfo.UseShellExecute = $false
$ProcessInfo.RedirectStandardOutput = $true
$ProcessInfo.RedirectStandardError = $true

$Process = [System.Diagnostics.Process]::Start($ProcessInfo)
$Stdout = $Process.StandardOutput.ReadToEnd()
$Stderr = $Process.StandardError.ReadToEnd()
$Process.WaitForExit(60000) # 60s timeout

$ExitCode = $Process.ExitCode
Write-Host "[PROCESS] Process exited with code: $ExitCode"

if ($ExitCode -ne 0) {
    Write-Host "[FAIL] EditorCli exited with non-zero code: $ExitCode" -ForegroundColor Red
    if ($Stderr) {
        Write-Host "Stderr output:" -ForegroundColor Yellow
        Write-Host $Stderr
    }
    exit $ExitCode
}

# 验证物理文件是否存在
if (-not (Test-Path $ScreenshotFile)) {
    Write-Host "[FAIL] Screenshot artifact does not exist: $ScreenshotFile" -ForegroundColor Red
    exit 2
}

# 验证物理文件大小 > 0
$FileItem = Get-Item $ScreenshotFile
$FileSize = $FileItem.Length
if ($FileSize -le 0) {
    Write-Host "[FAIL] Screenshot file size is 0 bytes!" -ForegroundColor Red
    exit 3
}

# 验证 PNG 8 字节魔数头
$FileBytes = [System.IO.File]::ReadAllBytes($ScreenshotFile)
if ($FileBytes.Length -lt 8) {
    Write-Host "[FAIL] Screenshot file is smaller than 8 bytes!" -ForegroundColor Red
    exit 4
}

$ExpectedMagic = @(0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
for ($i = 0; $i -lt 8; $i++) {
    if ($FileBytes[$i] -ne $ExpectedMagic[$i]) {
        Write-Host "[FAIL] File header byte $i is 0x$($FileBytes[$i].ToString('X2')), expected 0x$($ExpectedMagic[$i].ToString('X2'))" -ForegroundColor Red
        exit 5
    }
}

Write-Host "============================================================================" -ForegroundColor Green
Write-Host "  [PASS] Tier 4 Real-World Screenshot E2E Verification SUCCESSFUL!" -ForegroundColor Green
Write-Host "  Artifact: $ScreenshotFile ($FileSize bytes, Valid PNG Header)" -ForegroundColor Green
Write-Host "============================================================================" -ForegroundColor Green
exit 0
