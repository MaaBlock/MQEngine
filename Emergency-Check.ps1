Write-Host "=== 紧急安全检查 ===" -ForegroundColor Red

# 1. 检查可疑文件夹
$suspiciousFolder = "C:\系统高速下载路径"
if (Test-Path $suspiciousFolder) {
    Write-Host "⚠ 发现可疑文件夹" -ForegroundColor Red

    # 检查内容但不执行
    $contents = Get-ChildItem $suspiciousFolder -Force -Recurse -ErrorAction SilentlyContinue
    Write-Host "文件数量: $($contents.Count)" -ForegroundColor Yellow

    # 检查可执行文件
    $executables = $contents | Where-Object { $_.Extension -in @('.exe', '.bat', '.cmd', '.scr', '.msi', '.dll') }
    if ($executables.Count -gt 0) {
        Write-Host "⚠ 发现 $($executables.Count) 个可执行文件!" -ForegroundColor Red
        $executables | Select-Object Name, Length, CreationTime | Format-Table
    }
}

# 2. 检查运行中的可疑进程
Write-Host "`n=== 可疑进程检查 ===" -ForegroundColor Yellow
Get-Process | Where-Object {
    $_.ProcessName -like "*下载*" -or
    $_.ProcessName -like "*download*" -or
    $_.ProcessName -like "*speed*" -or
    $_.Path -like "*temp*" -or
    $_.Path -like "*appdata*"
} | Select-Object Name, Id, Path, StartTime -ErrorAction SilentlyContinue

# 3. 检查网络连接
Write-Host "`n=== 可疑网络连接 ===" -ForegroundColor Yellow
Get-NetTCPConnection | Where-Object {
    $_.State -eq "Established" -and
    $_.RemoteAddress -notlike "127.*" -and
    $_.RemoteAddress -notlike "192.168.*" -and
    $_.RemoteAddress -notlike "10.*" -and
    $_.RemotePort -notin @(80, 443, 53, 993, 995, 587, 465)
} | Select-Object LocalPort, RemoteAddress, RemotePort, OwningProcess

Write-Host "`n=== 建议立即采取的行动 ===" -ForegroundColor Cyan
Write-Host "1. 断开网络连接" -ForegroundColor White
Write-Host "2. 运行完整杀毒扫描" -ForegroundColor White
Write-Host "3. 不要打开可疑文件夹中的任何文件" -ForegroundColor White
Write-Host "4. 考虑使用专业反恶意软件工具" -ForegroundColor White