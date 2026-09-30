param([string]$TestExecutable = 'build/cmake/Release/LoliComparisonTests.exe')
$ErrorActionPreference = 'Stop'
$existing = @(Get-Process -Name LoliProfilerCompare -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$launcher = Start-Process -FilePath (Resolve-Path -LiteralPath $TestExecutable).Path -ArgumentList '--launch-compare' -PassThru -WindowStyle Hidden
$launcher.WaitForExit()
if ($launcher.ExitCode -ne 0) { throw 'Comparison launcher failed' }
$comparison = $null
try {
    # Wait for SFML to initialize its native window, then verify independence.
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        $created = @(Get-Process -Name LoliProfilerCompare -ErrorAction SilentlyContinue | Where-Object { $_.Id -notin $existing })
        if ($created.Count -eq 1 -and $created[0].MainWindowHandle -ne 0) {
            $comparison = $created[0]
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if ($null -eq $comparison) { throw 'Detached comparison window did not initialize' }
    if (-not $launcher.HasExited -or $comparison.HasExited) { throw 'Comparison did not outlive its launcher' }
    Write-Output "PASS comparison PID $($comparison.Id) outlived launcher PID $($launcher.Id)"
} finally {
    if ($null -ne $comparison -and -not $comparison.HasExited) {
        if (-not $comparison.CloseMainWindow() -or -not $comparison.WaitForExit(10000)) {
            throw 'Test comparison window did not close gracefully'
        }
    }
}
