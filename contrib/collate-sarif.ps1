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
#   - Dedupes identical results across translation units, so a header error is
#     one finding rather than one per includer.
#   - Drops stale results: anything whose location file or reporting unit was
#     edited after the log was written. Current, not comprehensive.
#   - Lifts MSVC's relatedLocations (the template instantiation chain) into a
#     codeFlow, which is the shape the viewer can actually display.
#   - Repairs URIs MSVC writes unescaped (see Repair-Uri).
#   - Always writes the output, even when there is nothing to report, so the
#     path CLion remembers keeps resolving to a valid, empty log.
#
# Diagnostics are data, not failures: this script exits 0 unless it cannot read
# the build directory or write the output.

#Requires -Version 7

param(
	[Parameter(Mandatory, Position = 0)] [string] $BuildDir,
	[Parameter(Mandatory, Position = 1)] [string] $OutFile
)

$ErrorActionPreference = 'Stop'

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$OutFile  = [System.IO.Path]::GetFullPath($OutFile)

if (-not (Test-Path -LiteralPath $BuildDir -PathType Container))
{
	Write-Host "collate-sarif: build directory not found: $BuildDir"
	exit 1
}

$schema = 'https://schemastore.azurewebsites.net/schemas/json/sarif-2.1.0-rtm.5.json'
$runs   = [System.Collections.Generic.List[object]]::new()
# When each kept run's log was written, parallel to $runs. Kept outside the run
# object: an extra top-level property on a run is off-schema and the viewer
# validates before importing.
$runTimes = [System.Collections.Generic.List[datetime]]::new()

$seen = 0; $orphans = 0; $repaired = 0

# MSVC writes artifact URIs with the path unescaped - a header under
# "C:\Windows Kits\10" comes out as file:///C:/Windows Kits/10/..., with the
# space intact. The SARIF schema types every uri as an RFC 3986 uri-reference,
# and CLion validates against the schema before importing, so one such location
# anywhere in the log rejects the whole file. [System.Uri] re-emits an absolute
# URI with every reserved character percent-encoded; relative references get
# the one character MSVC is known to leak.
function Repair-Uri([string] $value)
{
	if ($value -match '^[A-Za-z][A-Za-z0-9+.-]*:')
	{
		try { return ([System.Uri] $value).AbsoluteUri } catch { }
	}
	return ($value -replace '\\', '/') -replace ' ', '%20'
}

# Walk the parsed run and fix every property named "uri" in place. SARIF puts
# them in artifactLocation objects at several depths (results, relatedLocations,
# codeFlows, artifacts), so a generic walk is simpler than naming each path.
function Repair-Uris($node)
{
	if ($null -eq $node -or $node -is [string]) { return }
	if ($node -is [System.Collections.IList])
	{
		foreach ($item in $node) { Repair-Uris $item }
		return
	}
	if ($node -is [System.Management.Automation.PSCustomObject])
	{
		foreach ($prop in $node.PSObject.Properties)
		{
			if ($prop.Name -eq 'uri' -and $prop.Value -is [string])
			{
				$fixed = Repair-Uri $prop.Value
				if ($fixed -cne $prop.Value) { $prop.Value = $fixed; $script:repaired++ }
			}
			else
			{
				Repair-Uris $prop.Value
			}
		}
	}
}

# Orphan detection asks the build graph, not the file system. "The object is
# missing" is ambiguous: a removed source leaves no object, but so does a failed
# compile, and the failed compile is the log worth keeping. Ninja's graph is
# unambiguous - an object that is still an output of some build edge belongs to
# a translation unit that is still part of the build. CMake writes one flat
# build.ninja (no subninja), with outputs relative to the build directory and
# only the drive colon of absolute paths escaped as "$:".
$liveOutputs = $null
$ninjaFile = Join-Path $BuildDir 'build.ninja'
if (Test-Path -LiteralPath $ninjaFile -PathType Leaf)
{
	$liveOutputs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($line in [System.IO.File]::ReadLines($ninjaFile))
	{
		if (-not $line.StartsWith('build ')) { continue }
		# outputs end at the first colon that is not a "$:" escape
		$m = [regex]::Match($line, '^build\s+(.*?)(?<!\$):')
		if (-not $m.Success) { continue }
		foreach ($token in ($m.Groups[1].Value -split '\s+'))
		{
			if ($token -and $token -ne '|') { [void]$liveOutputs.Add($token.Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$')) }
		}
	}
}

# -Filter runs in the provider, so the walk is cheap even on a large tree.
# The output file may itself live under the build directory; never read it back.
$inputs = Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter '*.sarif' |
	Where-Object { $_.FullName -ne $OutFile }

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
	# test is far cheaper than parsing it to find out, and the only runs it can
	# miss are ones we parse anyway.
	if ($text -match '"results"\s*:\s*\[\s*\]' -and $text -notmatch '"results"\s*:\s*\[\s*\{') { continue }

	try
	{
		$log = $text | ConvertFrom-Json -Depth 100
	}
	catch
	{
		Write-Host "collate-sarif: skipping unreadable $($file.FullName): $($_.Exception.Message)"
		continue
	}

	if ($log.'$schema') { $schema = $log.'$schema' }

	foreach ($run in @($log.runs))
	{
		#if (-not $run.results -or @($run.results).Count -eq 0) { continue }
		Repair-Uris $run
		$runs.Add($run)
		$runTimes.Add($file.LastWriteTimeUtc)
	}
}

# Two reshapes for a viewer that renders only locations[0] and the message:
#
# - Dedupe across translation units. An error in a header is reported once per
#   includer, each as its own result with a different analysisTarget. One
#   finding per (rule, location, message) is what a reader wants; the first
#   TU to report it is as good as any.
# - Lift relatedLocations into a codeFlow. MSVC records the template
#   instantiation chain ("see reference to ... being compiled", with
#   nestingLevel) as relatedLocations, which the viewer ignores; it does render
#   codeFlows. The thread flow is the chain in MSVC's order, which is oldest
#   first, ending at the result's own location.
$seenKeys = [System.Collections.Generic.HashSet[string]]::new()
$duplicates = 0; $flows = 0; $stale = 0
$kept = 0; $results = 0
$deduped = [System.Collections.Generic.List[object]]::new()

# The collated log is current, not comprehensive. A log describes the last
# compile of its translation unit, and a build that stops at the first failure
# can leave that compile far behind the sources. A result is stale when the
# file it points at, or the unit that reported it, was modified after the log
# was written: the line it names may no longer be the line it meant, so it is
# dropped. The next compile of that unit says what is true now.
$mtimeCache = [System.Collections.Generic.Dictionary[string, datetime]]::new([System.StringComparer]::OrdinalIgnoreCase)
function Get-UriMTime([string] $uri)
{
	if (-not $uri -or -not $uri.StartsWith('file:', [System.StringComparison]::OrdinalIgnoreCase)) { return [datetime]::MinValue }
	if ($mtimeCache.ContainsKey($uri)) { return $mtimeCache[$uri] }
	$t = [datetime]::MinValue
	try
	{
		$local = ([System.Uri] $uri).LocalPath
		if (Test-Path -LiteralPath $local -PathType Leaf) { $t = (Get-Item -LiteralPath $local).LastWriteTimeUtc }
	}
	catch { }
	$mtimeCache[$uri] = $t
	return $t
}

for ($r = 0; $r -lt $runs.Count; $r++)
{
	$run = $runs[$r]
	$logTime = $runTimes[$r]
	$unique = [System.Collections.Generic.List[object]]::new()
	foreach ($res in @($run.results))
	{
		$loc = $res.locations[0].physicalLocation
		$key = "$($res.ruleId)|$($loc.artifactLocation.uri)|$($loc.region.startLine)|$($loc.region.startColumn)|$($res.message.text)"
		if (-not $seenKeys.Add($key)) { $duplicates++; continue }

		if ((Get-UriMTime $loc.artifactLocation.uri) -gt $logTime -or (Get-UriMTime $res.analysisTarget.uri) -gt $logTime)
		{
			$stale++
			continue
		}

		if ($res.relatedLocations -and -not $res.codeFlows)
		{
			$steps = foreach ($rel in @($res.relatedLocations))
			{
				$step = [ordered]@{ location = [ordered]@{ physicalLocation = $rel.physicalLocation } }
				if ($rel.message) { $step.location.message = $rel.message }
				if ($null -ne $rel.properties.nestingLevel) { $step.nestingLevel = [int]$rel.properties.nestingLevel }
				[pscustomobject]$step
			}
			$steps = @($steps) + [pscustomobject]@{ location = [ordered]@{ physicalLocation = $loc; message = $res.message } }
			$res | Add-Member -NotePropertyName codeFlows -NotePropertyValue @(
				[pscustomobject]@{ threadFlows = @([pscustomobject]@{ locations = $steps }) }
			)
			$flows++
		}

		$unique.Add($res)
	}
	if ($unique.Count -eq 0) { continue }
	$run.results = $unique.ToArray()
	$deduped.Add($run)
	$kept++
	$results += $unique.Count
}

$merged = [ordered]@{
	'$schema' = $schema
	version   = '2.1.0'
	runs      = $deduped.ToArray()
}

[void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($OutFile))

# ConvertTo-Json's default depth of 2 would silently flatten everything below
# runs[].results[] into strings; SARIF nests a dozen levels deep.
$json = $merged | ConvertTo-Json -Depth 100
[System.IO.File]::WriteAllText($OutFile, $json, [System.Text.UTF8Encoding]::new($false))

Write-Host "collate-sarif: $seen logs, $kept with findings ($results results, $duplicates duplicates dropped, $stale stale dropped, $flows code flows), $orphans orphaned, $repaired URIs repaired -> $OutFile"
exit 0
