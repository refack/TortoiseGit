<#
.SYNOPSIS
	Runs the unit test suite across several processes.

.DESCRIPTION
	Tests.exe is single threaded and takes about two minutes, most of which is
	spent waiting on git.exe and libgit2. Splitting it across processes is safe -
	the suite was checked for cross-process interference and has none - but
	gtest's own sharding (GTEST_TOTAL_SHARDS) distributes tests round robin by
	*index*, which balances test counts rather than time. With one test costing
	a quarter of the suite that is close to useless.

	So this bin-packs by measured duration instead: longest test first into the
	currently lightest process. Durations are learned from the previous run and
	cached next to Tests.exe, so the first run is round robin and every run after
	it is balanced.

	Note that the floor is the single slowest test - no amount of processes
	splits one test. Use -Slowest to see what that floor currently is.

.PARAMETER Jobs
	How many processes. Defaults to CPU count minus two, capped at eight, in the
	same spirit as Build-Nice.ps1.

.PARAMETER Filter
	A gtest filter to select a subset, e.g. '*CGit*'.

.PARAMETER Slowest
	After the run, print the slowest tests instead of just the summary.

.EXAMPLE
	.\test\Run-Tests.ps1
	.\test\Run-Tests.ps1 -Jobs 4 -Filter '*SerializeArgv*'
#>
[CmdletBinding()]
param(
	[string]$Exe = "$PSScriptRoot\..\bin\Debug64\bin\Tests.exe",
	[int]$Jobs = 0,
	[string]$Filter = '*',
	[switch]$Slowest
)

$ErrorActionPreference = 'Stop'

$Exe = (Resolve-Path $Exe).Path
if ($Jobs -le 0) { $Jobs = [Math]::Max(2, [Math]::Min(8, [Environment]::ProcessorCount - 2)) }

$timingsPath = Join-Path (Split-Path $Exe) 'test-timings.json'

# --- enumerate the tests -------------------------------------------------
# --gtest_list_tests prints a suite line ("Foo." at column 0) followed by its
# indented test lines, each possibly carrying a "# GetParam() = ..." comment.
$listing = & $Exe --gtest_list_tests "--gtest_filter=$Filter" 2>&1
$tests = [System.Collections.Generic.List[string]]::new()
$suite = $null
foreach ($raw in $listing)
{
	$line = "$raw"
	if ($line -match '^(\S+)\.\s*(#.*)?$') { $suite = $Matches[1]; continue }
	if ($suite -and $line -match '^\s+(\S+)') { $tests.Add("$suite.$($Matches[1])") }
}
if ($tests.Count -eq 0) { Write-Error "No tests matched '$Filter'." }

# --- bin pack by known duration ------------------------------------------
$timings = @{}
if (Test-Path $timingsPath)
{
	foreach ($p in (Get-Content $timingsPath -Raw | ConvertFrom-Json).PSObject.Properties) { $timings[$p.Name] = [double]$p.Value }
}
# An unmeasured test is assumed average, which degrades to round robin on the
# first run rather than piling every unknown into one process.
$fallback = if ($timings.Count) { ($timings.Values | Measure-Object -Average).Average } else { 1.0 }

$cost = @{}
foreach ($t in $tests) { $cost[$t] = if ($timings.ContainsKey($t)) { $timings[$t] } else { $fallback } }

$bins = @(0..($Jobs - 1) | ForEach-Object { @{ Ms = 0.0; Tests = [System.Collections.Generic.List[string]]::new() } })
foreach ($t in ($tests | Sort-Object { $cost[$_] } -Descending))
{
	$bin = $bins | Sort-Object { $_.Ms } | Select-Object -First 1
	$bin.Ms += $cost[$t]
	$bin.Tests.Add($t)
}
$bins = @($bins | Where-Object { $_.Tests.Count -gt 0 })

Write-Host "$($tests.Count) tests across $($bins.Count) processes (predicted $([int](($bins | Measure-Object -Property Ms -Maximum).Maximum / 1000))s)"

# --- run ------------------------------------------------------------------
$sw = [Diagnostics.Stopwatch]::StartNew()
$running = @()
for ($i = 0; $i -lt $bins.Count; ++$i)
{
	$running += Start-Job -ArgumentList $Exe, ($bins[$i].Tests -join ':'), $i -ScriptBlock {
		param($exe, $filter, $index)
		$jsw = [Diagnostics.Stopwatch]::StartNew()
		$out = & $exe "--gtest_filter=$filter" 2>&1
		$jsw.Stop()
		[pscustomobject]@{ Index = $index; Seconds = $jsw.Elapsed.TotalSeconds; Output = @($out | ForEach-Object { "$_" }) }
	}
}
$results = @($running | Wait-Job | Receive-Job | Sort-Object Index)
$running | Remove-Job
$sw.Stop()

# --- merge ----------------------------------------------------------------
$passed = 0
$failed = [System.Collections.Generic.List[string]]::new()
$asserts = 0
$measured = @{}
foreach ($r in $results)
{
	foreach ($line in $r.Output)
	{
		if ($line -match '^\[       OK \] (.+) \((\d+) ms\)$') { ++$passed; $measured[$Matches[1]] = [double]$Matches[2]; continue }
		if ($line -match '^\[  FAILED  \] (\S+.*?)( \(\d+ ms\))$') { $failed.Add($Matches[1]); continue }
		if ($line -match 'afxwin1\.inl\(24\) : Assertion failed') { ++$asserts }
	}
	Write-Host ("  process {0}: {1,6:N1}s  {2} tests" -f $r.Index, $r.Seconds, $bins[$r.Index].Tests.Count)
}

# Merge rather than replace, so a filtered run does not forget everything else.
foreach ($k in $measured.Keys) { $timings[$k] = $measured[$k] }
$timings | ConvertTo-Json -Compress | Set-Content -Path $timingsPath -Encoding utf8

Write-Host ''
Write-Host ("wall {0:N1}s, {1} passed, {2} failed" -f $sw.Elapsed.TotalSeconds, $passed, $failed.Count)
# Not noise: it means a real (not mocked) code path invoked MFC resource magic
# with no CWinApp behind it. Fifteen are expected, from GetActionName.
if ($asserts) { Write-Host "$asserts afxwin1.inl(24) assertions (expected: 15)" }

if ($Slowest)
{
	Write-Host ''
	$measured.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 10 | ForEach-Object { Write-Host ("  {0,7:N0} ms  {1}" -f $_.Value, $_.Key) }
}

if ($failed.Count)
{
	Write-Host ''
	$failed | Sort-Object -Unique | ForEach-Object { Write-Host "FAILED $_" }
	exit 1
}
exit 0
