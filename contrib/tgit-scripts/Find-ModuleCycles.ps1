param([string]$Root = 'E:\3party\TortoiseGit')

# Module interface units -> imports, following quoted #includes transitively
# (an `import` inside an #included header is an import of the includer).
$src = Get-ChildItem -Path (Join-Path $Root 'src'), (Join-Path $Root 'test') -Recurse -File -Include *.ixx, *.cppm, *.h, *.hpp
$byName = @{}
foreach ($f in $src) { $k = $f.Name.ToLowerInvariant(); if (-not $byName[$k]) { $byName[$k] = @() }; $byName[$k] += $f.FullName }

$reImport  = [regex]'(?m)^\s*(?:export\s+)?import\s+([\w.]*(?::[\w.]+)?)\s*;'
$reInclude = [regex]'(?m)^\s*#\s*include\s+"([^"]+)"'
$reModule  = [regex]'(?m)^\s*export\s+module\s+([\w.]+(?::[\w.]+)?)\s*;'
$textCache = @{}
function Get-Text($p) { if (-not $textCache.ContainsKey($p)) { $textCache[$p] = [IO.File]::ReadAllText($p) }; $textCache[$p] }

function Resolve-Include($fromFile, $inc) {
    $cand = Join-Path (Split-Path $fromFile) $inc
    if (Test-Path -LiteralPath $cand) { return (Resolve-Path -LiteralPath $cand).Path }
    $hits = $byName[(Split-Path $inc -Leaf).ToLowerInvariant()]
    if ($hits -and $hits.Count -eq 1) { return $hits[0] }
    return $null
}

function Get-Imports($file, $primary, $seen) {
    if (-not $seen.Add($file)) { return }
    $t = Get-Text $file
    foreach ($m in $reImport.Matches($t)) {
        $n = $m.Groups[1].Value
        if ($n -eq '') { continue }
        if ($n.StartsWith(':')) { $n = $primary + $n }
        [pscustomobject]@{ Module = $n; Via = $file }
    }
    foreach ($m in $reInclude.Matches($t)) {
        $r = Resolve-Include $file $m.Groups[1].Value
        if ($r) { Get-Imports $r $primary $seen }
    }
}

$graph = @{}; $via = @{}; $fileOf = @{}
foreach ($f in $src | Where-Object { $_.Extension -in '.ixx', '.cppm' }) {
    $mm = $reModule.Match((Get-Text $f.FullName))
    if (-not $mm.Success) { continue }
    $name = $mm.Groups[1].Value
    $primary = $name.Split(':')[0]
    $fileOf[$name] = $f.FullName
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $edges = @(Get-Imports $f.FullName $primary $seen)
    $graph[$name] = @($edges.Module | Select-Object -Unique)
    foreach ($e in $edges) { $via["$name->$($e.Module)"] = $e.Via }
}
# A primary interface implicitly depends on nothing extra; partitions are their own nodes.

# Tarjan SCC
$index = 0; $stack = [Collections.Generic.Stack[string]]::new(); $onStack = @{}; $idx = @{}; $low = @{}; $sccs = @()
function StrongConnect($v) {
    $script:idx[$v] = $script:index; $script:low[$v] = $script:index; $script:index++
    $script:stack.Push($v); $script:onStack[$v] = $true
    foreach ($w in @($script:graph[$v])) {
        if (-not $script:graph.ContainsKey($w)) { continue }
        if (-not $script:idx.ContainsKey($w)) { StrongConnect $w; $script:low[$v] = [Math]::Min($script:low[$v], $script:low[$w]) }
        elseif ($script:onStack[$w]) { $script:low[$v] = [Math]::Min($script:low[$v], $script:idx[$w]) }
    }
    if ($script:low[$v] -eq $script:idx[$v]) {
        $c = @(); do { $w = $script:stack.Pop(); $script:onStack[$w] = $false; $c += $w } while ($w -ne $v)
        $selfLoop = $c.Count -eq 1 -and (@($script:graph[$v]) -contains $v)
        if ($c.Count -gt 1 -or $selfLoop) { $script:sccs += , $c }
    }
}
foreach ($v in @($graph.Keys)) { if (-not $idx.ContainsKey($v)) { StrongConnect $v } }

"modules: $($graph.Count)   cycles (SCCs): $($sccs.Count)"
foreach ($c in $sccs) {
    "--- cycle: $($c -join ', ')"
    foreach ($a in $c) { foreach ($b in @($graph[$a])) { if ($c -contains $b) {
        $v = $via["$a->$b"].Replace("$Root\", '')
        "    $a -> $b    (import in $v)"
    } } }
}
