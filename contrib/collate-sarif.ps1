# collate-sarif.ps1 - merge the per-object SARIF logs MSVC writes into one file.
#
# The root CMakeLists appends -experimental:log<OBJECT>.sarif to every MSVC
# compile, so each translation unit leaves a SARIF log next to its object file.
# CLion's SARIF Viewer imports files, not directories, and does not watch
# anything, so this script folds every log under the build directory into a
# single diagnostics.sarif that the viewer can be pointed at once and
# re-imported after each build.
#
# Usage:
#   collate-sarif.ps1 <build dir> <output file>
#
# Requires jq (1.7.1 or later) on PATH. The work is split by what each side is
# good at: this script walks the file system - discovery, build.ninja, mtimes -
# and collate-sarif.jq, beside it, does everything that is JSON.
#
# What it does, and why:
#   - Appends each input's runs verbatim rather than merging them into one run.
#     SARIF 2.1.0 allows any number of runs per log, and a verbatim copy is
#     always schema-valid; merging runs means rebasing every ruleIndex and
#     artifact index, which is where hand-rolled mergers produce files the
#     viewer rejects.
#   - Drops runs with no results. MSVC writes a log on every compile, success
#     included, so most inputs are an empty run. Carrying them along would make
#     the merged file mostly noise.
#   - Drops orphans: an X.obj.sarif whose X.obj is no longer an output in
#     build.ninja describes a source that left the build. A missing X.obj is
#     not the test - a failed compile has no object either, and that is the
#     log worth keeping.
#   - Points every result into the source tree (not build/**): a diagnostic
#     MSVC reports inside gtest.h or <utility> moves to the line of ours that
#     instantiated it, or to the translation unit, or to no location at all.
#   - Dedupes identical results across translation units, so a header error is
#     one finding rather than one per includer.
#   - Drops stale results: anything whose file or reporting unit was edited
#     after the log was written. Current, not comprehensive.
#   - Re-lays MSVC's relatedLocations - overload candidates, the template
#     instantiation chain, notes - as one codeFlow of indented lists, which is
#     the shape the viewer can actually display.
#   - Folds MSVC's multi-line "with [ T=... ]" template bindings into one line.
#   - Drops nodes whose message matches drop_message (in collate-sarif.jq).
#   - Repairs URIs MSVC writes unescaped (see repair_uri in collate-sarif.jq).
#   - Always writes the output, even when there is nothing to report, so the
#     path CLion remembers keeps resolving to a valid, empty log.
#
# Pipeline (see the WORKFLOW section at the bottom):
#   1. Build graph  - read build.ninja's outputs, for orphan detection.
#   2. Discover     - find every *.sarif under the build directory.
#   3. Bundle       - drop orphans and no-op logs; pack the rest for jq.
#   4. Stat         - jq lists every file the logs mention; stat each once.
#   5. Collate      - jq reshapes, dedupes and merges.
#   6. Emit         - write the merged log and a one-line summary.
#
# Diagnostics are data, not failures: this script exits 0 unless it cannot read
# the build directory, run jq, or write the output.

#Requires -Version 7

param(
	[Parameter(Mandatory, Position = 0)] [string] $BuildDir,
	[Parameter(Mandatory, Position = 1)] [string] $OutFile
)

Set-StrictMode -Off
$ErrorActionPreference = 'Break'


# =============================================================================
# FUNCTIONS
# =============================================================================

# -----------------------------------------------------------------------------
# Build graph
# -----------------------------------------------------------------------------

# Orphan detection asks the build graph, not the file system. "The object is
# missing" is ambiguous: a removed source leaves no object, but so does a failed
# compile, and the failed compile is the log worth keeping. Ninja's graph is
# unambiguous - an object that is still an output of some build edge belongs to
# a translation unit that is still part of the build. CMake writes one flat
# build.ninja (no subninja), with outputs relative to the build directory and
# only the drive colon of absolute paths escaped as "$:".
#
# Returns the set of outputs, or $null when there is no build.ninja (which
# disables orphan detection rather than treating every log as an orphan).
function Get-NinjaOutputs([string] $buildDir)
{
	$ninjaFile = Join-Path $buildDir 'build.ninja'
	if (-not (Test-Path -LiteralPath $ninjaFile -PathType Leaf)) { return $null }

	$outputs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($line in [System.IO.File]::ReadLines($ninjaFile))
	{
		if (-not $line.StartsWith('build ')) { continue }
		# outputs end at the first colon that is not a "$:" escape
		$m = [regex]::Match($line, '^build\s+(.*?)(?<!\$):')
		if (-not $m.Success) { continue }
		foreach ($token in ($m.Groups[1].Value -split '\s+'))
		{
			if ($token -and $token -ne '|') { [void]$outputs.Add($token.Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$')) }
		}
	}
	# The comma stops PowerShell from unrolling the set into the pipeline.
	return , $outputs
}

# -----------------------------------------------------------------------------
# jq
# -----------------------------------------------------------------------------

# Run collate-sarif.jq over the bundle and return its stdout lines. Arguments
# go in as plain --arg strings only: PowerShell's native-argument quoting
# mangles embedded double quotes, so JSON never travels on the command line -
# anything structured goes through a file (--slurpfile).
function Invoke-CollateJq([string] $mode, [string[]] $extra)
{
	$jqArgs = @(
		'-n', '-r'
		'-f', (Join-Path $PSScriptRoot 'collate-sarif.jq')
		'--arg', 'mode', $mode
		'--arg', 'root', $script:rootUri
		'--arg', 'exclude', ($script:excludeUris -join '|')
		'--arg', 'schema', $script:schema
	) + $extra + @($script:bundleFile)
	$out = & jq @jqArgs
	if ($LASTEXITCODE -ne 0) { throw "collate-sarif: jq ($mode) exited with $LASTEXITCODE" }
	# The comma keeps a one-line result an array.
	return , @($out)
}

# A directory as a file: URI with a trailing slash, the form the jq side
# compares prefixes against.
function ConvertTo-DirUri([string] $path)
{
	return [System.Uri]::new([System.IO.Path]::TrimEndingDirectorySeparator($path) + '\').AbsoluteUri
}

function ConvertTo-UnixMs([datetime] $utc)
{
	return [System.DateTimeOffset]::new($utc).ToUnixTimeMilliseconds()
}


# =============================================================================
# WORKFLOW
# =============================================================================

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$OutFile  = [System.IO.Path]::GetFullPath($OutFile)

if (-not (Test-Path -LiteralPath $BuildDir -PathType Container))
{
	Write-Host "collate-sarif: build directory not found: $BuildDir"
	exit 1
}
if (-not (Get-Command jq -CommandType Application -ErrorAction SilentlyContinue))
{
	Write-Host 'collate-sarif: jq not found on PATH'
	exit 1
}

# This script lives in <tree>\contrib. "The tree" excludes <tree>\build and the
# build directory itself, wherever it is.
$treeDir     = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rootUri     = ConvertTo-DirUri $treeDir
$excludeUris = @((ConvertTo-DirUri (Join-Path $treeDir 'build')), (ConvertTo-DirUri $BuildDir)) | Select-Object -Unique
$schema      = 'https://json.schemastore.org/sarif-2.1.0-rtm.5.json'

$seen = 0; $orphans = 0; $noop = 0

# jq reads and writes UTF-8; make PowerShell decode its stdout the same way.
$savedEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) "collate-sarif-$([guid]::NewGuid().ToString('N'))"
[void][System.IO.Directory]::CreateDirectory($workDir)
$bundleFile = Join-Path $workDir 'bundle.jsonl'
$mtimesFile = Join-Path $workDir 'mtimes.json'

try
{
	# -------------------------------------------------------------------------
	# Stage 1: Build graph - which objects does the build still produce?
	# -------------------------------------------------------------------------

	$liveOutputs = Get-NinjaOutputs $BuildDir

	# -------------------------------------------------------------------------
	# Stage 2: Discover - every SARIF log under the build directory.
	# -------------------------------------------------------------------------

	# -Filter runs in the provider, so the walk is cheap even on a large tree.
	# The output file may itself live under the build directory; never read it back.
	$inputs = Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter '*.sarif' |
		Where-Object { $_.FullName -ne $OutFile }

	# -------------------------------------------------------------------------
	# Stage 3: Bundle - drop orphans and no-op logs; pack the rest for jq.
	# -------------------------------------------------------------------------

	# One JSON object per line, the log carried as a string so that jq can
	# reject one malformed log without losing the stream. Files, not arguments:
	# a broken build can leave more logs than a command line holds.
	$writer = [System.IO.StreamWriter]::new($bundleFile, $false, [System.Text.UTF8Encoding]::new($false))
	try
	{
		foreach ($file in $inputs)
		{
			$seen++

			# X.obj.sarif / X.bmi.sarif belong to an object; if the graph no longer
			# produces that object, the log describes a translation unit that is gone.
			if ($null -ne $liveOutputs -and $file.Name -match '\.(obj|bmi)\.sarif$')
			{
				$object = [System.IO.Path]::GetRelativePath($BuildDir, $file.FullName.Substring(0, $file.FullName.Length - '.sarif'.Length))
				if (-not $liveOutputs.Contains($object)) { $orphans++; continue }
			}

			$text = [System.IO.File]::ReadAllText($file.FullName)

			# The common case is the no-op log MSVC writes for a clean compile. A text
			# test is far cheaper than handing it to jq, and the only runs it can
			# miss are ones jq sees anyway.
			if ($text -match '"results"\s*:\s*\[\s*\]' -and $text -notmatch '"results"\s*:\s*\[\s*\{') { $noop++; continue }

			$writer.Write('{"file":')
			$writer.Write((ConvertTo-Json -InputObject $file.FullName -Compress))
			$writer.Write(',"mtime":')
			$writer.Write((ConvertTo-UnixMs $file.LastWriteTimeUtc))
			$writer.Write(',"text":')
			$writer.Write((ConvertTo-Json -InputObject $text -Compress))
			$writer.WriteLine('}')
		}
	}
	finally
	{
		$writer.Dispose()
	}

	# -------------------------------------------------------------------------
	# Stage 4: Stat - every file the logs mention, once.
	# -------------------------------------------------------------------------

	# Staleness compares each result's files against its log's write time; jq
	# cannot stat, so it names the files and this stage answers.
	$mtimes = [ordered]@{}
	foreach ($uri in (Invoke-CollateJq 'uris' @()))
	{
		try
		{
			$local = ([System.Uri] $uri).LocalPath
			if (Test-Path -LiteralPath $local -PathType Leaf) { $mtimes[$uri] = ConvertTo-UnixMs (Get-Item -LiteralPath $local).LastWriteTimeUtc }
		}
		catch { }
	}
	[System.IO.File]::WriteAllText($mtimesFile, (ConvertTo-Json -InputObject $mtimes -Compress), [System.Text.UTF8Encoding]::new($false))

	# -------------------------------------------------------------------------
	# Stage 5: Collate - reshape, dedupe and merge (see collate-sarif.jq).
	# -------------------------------------------------------------------------

	# The first line is the stats object; the rest is the merged log.
	$out   = Invoke-CollateJq 'collate' @('--slurpfile', 'mtimes', $mtimesFile)
	$stats = $out[0] | ConvertFrom-Json
	$json  = ($out | Select-Object -Skip 1) -join "`n"

	# -------------------------------------------------------------------------
	# Stage 6: Emit - always write, even an empty log, so CLion's path stays valid.
	# -------------------------------------------------------------------------

	[void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($OutFile))
	[System.IO.File]::WriteAllText($OutFile, $json + "`n", [System.Text.UTF8Encoding]::new($false))
}
finally
{
	[Console]::OutputEncoding = $savedEncoding
	Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($bad in $stats.unreadable) { Write-Host "collate-sarif: skipped unreadable $bad" }
Write-Host ("collate-sarif: $seen logs, $($stats.runs) with findings ($($stats.results) results, " +
	"$($stats.duplicates) duplicates dropped, $($stats.stale) stale dropped, $($stats.retargeted) retargeted, " +
	"$($stats.flows) code flows), $($stats.filtered) filtered, $orphans orphaned, $($stats.repaired) URIs repaired -> $OutFile")
exit 0
