# cl-shim.ps1 - a logging wrapper around MSVC's cl.exe.
#
# Runs cl with the argv it was handed, writes the complete output to a
# timestamped file, and passes everything EXCEPT the lines naming the MSVC
# system include directory through to stdout. The build stays readable; the
# full /showIncludes trail stays on disk.
#
# Usage, either shape:
#   cl-shim.ps1 <cl args...>                 - resolves cl.exe from PATH
#   cl-shim.ps1 <path\to\cl.exe> <args...>   - uses the compiler it is given
# The second shape is what CMAKE_<LANG>_COMPILER_LAUNCHER produces: CMake
# prefixes the launcher to the compiler command, so argv[0] is the compiler it
# resolved. Preferring that one over PATH matters - PATH may hold a different
# host/target architecture than the one CMake configured with.
#
# Environment:
#   CL_SHIM_LOGDIR    where logs go            (default: <this dir>\.cl-logs)
#   CL_SHIM_FILTER    substring to suppress    (default: the MSVC include dir)
#   CL_SHIM_NOFILTER  set to anything to pass the output through untouched
#
# Note on CL_SHIM_NOFILTER: under Ninja, CMake compiles with /showIncludes and
# ninja parses those very lines out of this script's stdout to build its header
# dependency database (deps = msvc). Suppressing them here also removes them
# from ninja's dependencies - which is harmless for system headers, since the
# toolset does not change under you, but it does mean ninja will not rebuild if
# you edit a header under the MSVC include directory. Set CL_SHIM_NOFILTER=1
# for a build whose dependencies must be exact.

#Requires -Version 7

# A non-zero exit from cl is data, not a PowerShell error. Without these two
# lines PowerShell 7.3+ turns the first failed compile into a terminating
# exception, and the build system sees the shim die instead of the compiler's
# diagnostics and exit code.
$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false

# No param() block, deliberately, and no $args either. Both are unusable here.
#
# This script is invoked as `pwsh -File cl-shim.ps1 <the compiler's argv>`, and
# that argv contains tokens of the form -IE:\some\path. PowerShell reads
# -Name:Value as a named parameter with an inline value, so `-IE:\3party\x`
# reaches $args as the bare string `\3party\x` - the `-I` is taken for a
# parameter named IE and silently dropped. CMake emits every include directory
# in exactly that shape, so this is not a corner case: it turns each -I into a
# stray source file and the compile fails with D9024 and C1083.
#
# [Environment]::GetCommandLineArgs() returns the argv Windows itself produced
# for this process, before PowerShell's parameter binder saw any of it, so the
# tokens come back verbatim. Everything up to and including the script path
# belongs to pwsh; the rest is the compiler's.
#
# A declared param() block would be the same trap from the other direction, and
# configuration therefore arrives through the environment instead.

$filter = if ($env:CL_SHIM_FILTER) { $env:CL_SHIM_FILTER }
          else { 'C:\bin\dev\Microsoft\2026Community\VC\Tools\MSVC\14.51.36231\include\' }
if ($env:CL_SHIM_NOFILTER) { $filter = $null }

$logDir = if ($env:CL_SHIM_LOGDIR) { $env:CL_SHIM_LOGDIR } else { Join-Path $PSScriptRoot '.cl-logs' }

$raw = [System.Environment]::GetCommandLineArgs()
$fileAt = [System.Array]::FindIndex([string[]]$raw, [Predicate[string]] { param($a) $a -ieq '-File' })
$argv = if ($fileAt -ge 0 -and $raw.Length -gt ($fileAt + 2)) { @($raw[($fileAt + 2)..($raw.Length - 1)]) } else { @() }

if ($argv.Count -gt 0 -and $argv[0] -match '(^|[\\/])cl(\.exe)?$')
{
	$cl   = $argv[0]
	$argv = @($argv | Select-Object -Skip 1)
}
else
{
	$cl = Get-Command 'cl.exe' -CommandType Application -ErrorAction SilentlyContinue |
		Select-Object -First 1 -ExpandProperty Source
}

if (-not $cl)
{
	# 9009 is cmd.exe's "command not found", which is what a missing compiler
	# would have produced without this shim in the way.
	Write-Host 'cl-shim: no compiler argument and no cl.exe on PATH'
	exit 9009
}

# Name the log after the translation unit so the directory is navigable. The
# PID keeps parallel jobs apart - each cl invocation is its own pwsh process -
# and the millisecond stamp covers PID reuse.
$tu    = $argv | Where-Object { $_ -match '\.(c|cc|cpp|cxx|ixx|cppm)$' } | Select-Object -Last 1
$tag   = if ($tu) { [System.IO.Path]::GetFileName($tu) } else { 'no-tu' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss.fff'

[void][System.IO.Directory]::CreateDirectory($logDir)
$log = Join-Path $logDir "$stamp-$PID-$tag.log"

Set-Content -Path $log -Encoding utf8 -Value @(
	"# $stamp  pid $PID"
	"# cwd: $((Get-Location).Path)"
	"# cmd: $cl $($argv -join ' ')"
	''
)

# IndexOf with OrdinalIgnoreCase rather than -like: a -like pattern would treat
# [ and ] in a user-supplied CL_SHIM_FILTER as character classes, and cl echoes
# include paths in whatever case it resolved them, which need not match the
# case in INCLUDE.
& $cl @argv 2>&1 |
	Tee-Object -FilePath $log -Append |
	Where-Object { -not $filter -or $_ -notmatch '^C:\\' }

# $LASTEXITCODE is still cl's own: every downstream stage of that pipeline is a
# cmdlet, not a native command, so nothing overwrote it. Propagating it is what
# keeps a failed compile a failed build step.
exit ([int]$LASTEXITCODE)
