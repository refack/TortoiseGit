# TortoiseGit - a Windows shell extension for easy version control
#
# Copyright (C) 2026 - TortoiseGit
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

<#
.SYNOPSIS
    Runs ShellTest.exe across every cache mode and reports the matrix.

.DESCRIPTION
    ShellCache reads HKCU\Software\TortoiseGit\CacheType once and then caches it
    behind a ticker, so a mode switch is only honoured by a fresh process. This
    script therefore launches one ShellTest.exe per (mode, repo-set) rather than
    looping inside the harness.

    It saves and restores the caller's real CacheType value: the benchmark writes
    to the same key the installed TortoiseGit reads, so leaving it changed would
    silently reconfigure the user's shell.

.EXAMPLE
    .\Run-ShellBench.ps1 -Repo E:\3party\ffmpeg,E:\3party\bun -Configuration Release
#>
[CmdletBinding()]
param(
    [string[]]$Repo = @('E:\3party\ffmpeg', 'E:\3party\bun', 'E:\3party\explorerplusplus'),
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [int]$Limit = 5000,
    [int]$Rounds = 40,
    # 0 = none, 1 = exe (TGitCache), 2 = dll, 3 = dllFull
    [int[]]$CacheType = @(0, 2, 3, 1)
)

$ErrorActionPreference = 'Stop'

$binDir = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "bin\$Configuration`64\bin"
$exe = Join-Path $binDir 'ShellTest.exe'
if (-not (Test-Path $exe)) { throw "not built: $exe" }

$key = 'HKCU:\Software\TortoiseGit'
$saved = (Get-ItemProperty -Path $key -Name CacheType -ErrorAction SilentlyContinue).CacheType
if ($null -eq $saved) {
    Write-Host "note: HKCU\Software\TortoiseGit\CacheType was unset (TortoiseGit's own default is 'exe'); it will be removed again afterwards." -ForegroundColor DarkGray
}

$argList = @()
foreach ($r in $Repo) { $argList += @('--repo', $r) }
$argList += @('--limit', $Limit, '--rounds', $Rounds)

try {
    foreach ($ct in $CacheType) {
        Write-Host ("=" * 78) -ForegroundColor DarkCyan
        & $exe @argList --cachetype $ct
        Write-Host ''
    }
}
finally {
    if ($null -eq $saved) {
        Remove-ItemProperty -Path $key -Name CacheType -ErrorAction SilentlyContinue
    }
    else {
        Set-ItemProperty -Path $key -Name CacheType -Value $saved -Type DWord
    }
    # The last mode measured leaves a TGitCache started under it; put the user's
    # own configuration back in charge by letting it restart on demand.
    # taskkill exits 128 when nothing matched, which is not a failure here.
    taskkill.exe /F /IM TGitCache.exe 2>&1 | Out-Null
    $global:LASTEXITCODE = 0
    Write-Host "restored HKCU\Software\TortoiseGit\CacheType" -ForegroundColor DarkGray
}
