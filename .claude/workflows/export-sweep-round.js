export const meta = {
  name: 'export-sweep-round',
  description: 'One round of the module-export / compile-fix sweep: triage build failures, rank blockers by friction, fix blockers above threshold, rebuild',
  phases: [
    { title: 'Structural', detail: 'orchestrator-defined multi-file jobs (cycle breaks, moves) with exclusive file ownership' },
    { title: 'Triage', detail: 'classify each failed object to a canonical blocker + file to edit' },
    { title: 'Rank', detail: 'friction = distinct failed objects per blocker' },
    { title: 'Fix modules', detail: 'one agent per .ixx above threshold' },
    { title: 'Fix consumers', detail: 'one agent per non-module file above threshold' },
    { title: 'Build', detail: 'single full ninja build, commit the round' },
  ],
}

const REPO = 'E:\\3party\\TortoiseGit'
const { failuresFile, failedCount, batches, threshold, round, logIn, logOut, roundNotes } = args
const structuralJobs = args.structuralJobs || []
const structuralFiles = new Set(structuralJobs.flatMap(j => j.files.map(f => f.replace(/\\/g, '/'))))
const NOTES = roundNotes ? `\nORCHESTRATOR NOTES FOR THIS ROUND (override triage hints where they conflict):\n${roundNotes}\n` : ''

const COMMON = `
Repo: ${REPO} (Windows, PowerShell tool; one command per PowerShell call, never Set-Location/cd).
Build: CMake + Ninja, MSVC, C++ modules (.ixx). Build dir: ${REPO}\\build\\debug. compile_commands.json is in that dir.
To get the VS toolchain in a PowerShell call, dot-source first: \`. C:\\Users\\refac\\.msbuild\\Enter-VsDevShellCached.ps1 | Out-Null; <your command>\` (same call).
HARD RULES:
- Do NOT run a ninja build (no \`ninja -C ... <targets>\`), only one build may run at a time and the orchestrator owns it. \`ninja -C build\\debug -t commands -s <OBJ>\` (read-only) is allowed.
- Do NOT run any git command that writes (no add/commit/stash/reset/checkout/restore). Read-only git (log/show/diff/grep) is fine.
- The working tree is shared with other agents editing OTHER files concurrently. Edit ONLY the file(s) you are assigned.
`

const TRIAGE_SCHEMA = {
  type: 'object',
  properties: {
    items: { type: 'array', items: {
      type: 'object',
      properties: {
        object: { type: 'string' },
        causes: { type: 'array', items: {
          type: 'object',
          properties: {
            blockerKey: { type: 'string', description: 'canonical key, see prompt' },
            kind: { type: 'string', enum: ['module-not-exported', 'missing-import', 'unqualified-name', 'moved-header', 'textual-after-import', 'api-drift', 'build-config', 'other'] },
            fileToEdit: { type: 'string', description: 'repo-relative path with forward slashes, e.g. src/SharedMFC/ProgressDlg.ixx; or CMakeLists path for build-config' },
            fix: { type: 'string', description: 'concrete, minimal edit instruction for fileToEdit' },
            evidence: { type: 'string', description: 'the error line(s) and what you checked' },
          },
          required: ['blockerKey', 'kind', 'fileToEdit', 'fix', 'evidence'],
        } },
      },
      required: ['object', 'causes'],
    } },
  },
  required: ['items'],
}

const triagePrompt = (batch) => `${COMMON}
You are triaging C++ build failures from a full \`ninja -k 0\` build (round ${round}). For EACH failed object in your batch, find the ROOT cause(s) by reading the source and the errors, and express each as a canonical blocker. You only read and report; you do not edit anything.${NOTES}
Objects ending in .obj.ddi are dependency-SCAN failures (the preprocessor could not even open an include); .obj are compile failures; .res are resource compiles; .exe/.dll are links.

Canonical blockerKey formats (be consistent: other agents use the same scheme and keys are counted across agents):
- "module:<ModuleName>" — kinds module-not-exported / missing-import / unqualified-name. <ModuleName> is the name in the \`export module X;\` line of the module that DEFINES the missing name (find it with Grep over src/**/*.ixx). Use this even when the fix is in the consumer.
- "header:<basename.h>" — kind moved-header (an #include that no longer resolves because the header moved, became a module, or was deleted). Find where it lives now (Glob src/**/<name>), or which module replaced it.
- "order:<repo-relative file with the bad ordering>" — kind textual-after-import (C2995/C2374/C2572/C2011 redefinitions in MSVC std/ATL headers: a textual #include of std/ATL/wil comes after an \`import\` that brings \`import std;\`). Name the file whose include/import order is wrong (often a GMF header or stdafx/pch header).
- "api:<Symbol>" — kind api-drift (a function/member was renamed/removed/changed signature; e.g. GetVal removed). Say what replaced it.
- "cmake:<target>:<short issue>" — kind build-config (PCH, missing link lib, missing source, linker entry point).
- If an error is only a CASCADE of another failure (e.g. "could not find module 'X'" because X's own .ixx failed to compile, or errors after a fatal include failure), attribute it to the upstream blocker's key, do not invent a new one.

fileToEdit: the single file where the fix belongs (the .ixx for module-not-exported; the consumer for missing-import/unqualified-name; the includer for moved-header). Repo-relative, forward slashes.
fix: a concrete minimal instruction, e.g. "change \`namespace ProgressDlg {\` to \`export namespace ProgressDlg {\`", or "replace #include \\"AddDlg.h\\" with #include \\"ProtoModules/AddDlg.h\\" (SharedMFC is on the include path as src/SharedMFC)", or "add \`import Foo;\` after the existing imports". Check include directories for that object in compile_commands.json before proposing an include path.
Project conventions: STL strings preferred; do not introduce CString in new signatures; no \`#ifdef NO_TGIT_MFC\` outside the shim; in .h files qualify names instead of adding using-directives; in .cpp/.ixx a \`using namespace X;\` after the imports is acceptable (existing pattern).

Your batch: read the JSON array in ${failuresFile} (Read tool) and take the entries at these 0-based indices: ${JSON.stringify(batch)}. Each entry is {object, target, errors[]} (up to 12 distinct error lines). The full build log is ${logIn} if you need more context (search it, it is large).

Return one item per object in your batch (object = the entry's "object" string, verbatim).`

// ---------- Structural (multi-file, orchestrator-defined) ----------
const STRUCT_SCHEMA = {
  type: 'object',
  properties: {
    label: { type: 'string' },
    changedFiles: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string', description: 'what moved where and why, incl. any design choice you had to make' },
    verification: { type: 'string', enum: ['compiled-ok', 'compile-failed', 'not-compiled'] },
    verificationNotes: { type: 'string' },
    blocked: { type: 'string', description: 'if the job could not be completed as specified, why and what the user must decide; empty otherwise' },
  },
  required: ['label', 'changedFiles', 'summary', 'verification', 'verificationNotes', 'blocked'],
}
phase('Structural')
const structuralResults = (await parallel(structuralJobs.map(j => () =>
  agent(`${COMMON}${NOTES}
STRUCTURAL JOB "${j.label}". You exclusively own these files (create/edit/move only these; a file move may use Move-Item, the orchestrator's commit records renames): ${j.files.join(', ')}
${j.prompt}`, { label: `structural: ${j.label}`, phase: 'Structural', schema: STRUCT_SCHEMA })
))).filter(Boolean)

// ---------- Triage ----------
phase('Triage')
log(`round ${round}: ${failedCount} failed objects in ${batches.length} triage batches; threshold friction > ${threshold}`)

const triagedRaw = await parallel(batches.map(b => () =>
  agent(triagePrompt(b.indices), { label: `triage ${b.label}`, phase: 'Triage', schema: TRIAGE_SCHEMA })
))
const lost = batches.filter((b, i) => !triagedRaw[i]).map(b => b.label)
if (lost.length) log(`WARNING: triage batches returned nothing: ${lost.join(', ')} — their objects are not ranked this round`)
const triaged = triagedRaw.filter(Boolean).flatMap(r => r.items)

// ---------- Rank ----------
phase('Rank')
const norm = s => s.trim().replace(/\\/g, '/')
const friction = {}   // blockerKey -> {objects:Set, kind, edits: [{file, fix, evidence, object}]}
for (const it of triaged) {
  for (const c of it.causes) {
    const key = norm(c.blockerKey)
    const e = (friction[key] ||= { key, kinds: new Set(), objects: new Set(), edits: [] })
    e.kinds.add(c.kind)
    e.objects.add(it.object)
    e.edits.push({ file: norm(c.fileToEdit), fix: c.fix, evidence: c.evidence, object: it.object, kind: c.kind })
  }
}
const ranked = Object.values(friction)
  .map(e => ({ key: e.key, kinds: [...e.kinds], friction: e.objects.size, edits: e.edits }))
  .sort((a, b) => b.friction - a.friction)
const selected = ranked.filter(r => r.friction > threshold)
const deferred = ranked.filter(r => r.friction <= threshold)
log(`ranked ${ranked.length} blockers; selected ${selected.length} with friction > ${threshold}: ${selected.map(s => `${s.key}(${s.friction})`).join(', ')}`)
if (deferred.length) log(`deferred to later rounds (friction <= ${threshold}): ${deferred.map(s => `${s.key}(${s.friction})`).join(', ')}`)

// group selected edits by file (disjoint ownership => safe parallel edits in one tree)
const byFile = {}
for (const s of selected) for (const e of s.edits) {
  (byFile[e.file] ||= { file: e.file, blockers: new Set(), items: [] })
  byFile[e.file].blockers.add(s.key)
  byFile[e.file].items.push(e)
}
const fileJobs = Object.values(byFile).map(j => {
  // dedupe identical fix text
  const seen = new Set(); const items = []
  for (const i of j.items) { const k = i.fix; if (!seen.has(k)) { seen.add(k); items.push(i) } }
  return { file: j.file, blockers: [...j.blockers], items }
})
const skippedForStructural = fileJobs.filter(j => structuralFiles.has(j.file)).map(j => j.file)
if (skippedForStructural.length) log(`skipped fix jobs on files owned by structural jobs: ${skippedForStructural.join(', ')}`)
const freeJobs = fileJobs.filter(j => !structuralFiles.has(j.file))
const moduleJobs = freeJobs.filter(j => j.file.endsWith('.ixx'))
const consumerJobs = freeJobs.filter(j => !j.file.endsWith('.ixx'))
log(`fix jobs: ${moduleJobs.length} module files, ${consumerJobs.length} other files`)

const FIX_SCHEMA = {
  type: 'object',
  properties: {
    file: { type: 'string' },
    changed: { type: 'boolean' },
    summary: { type: 'string', description: 'what you changed and why, 1-4 sentences' },
    verification: { type: 'string', enum: ['compiled-ok', 'compile-failed', 'not-compiled'] },
    verificationNotes: { type: 'string' },
    rejectedInstructions: { type: 'string', description: 'any triage instruction you judged wrong and did not apply, with reason; empty if none' },
  },
  required: ['file', 'changed', 'summary', 'verification', 'verificationNotes', 'rejectedInstructions'],
}

const fixPrompt = (job) => `${COMMON}${NOTES}
You own exactly ONE file this round: ${job.file}
Blockers being resolved (by friction, highest first): ${job.blockers.join(', ')}
Triage findings for this file (the triage agent read the errors; treat its fix as a strong hint, but verify against the source — if an instruction is wrong, do the right minimal fix or skip it and say why):
${JSON.stringify(job.items, null, 1)}

Make the minimal correct edit(s) to ${job.file} only. Preserve behaviour. Do not delete features to make things compile. Do not touch other files — if the real fix belongs elsewhere, say so in rejectedInstructions.
For a module that must export more: prefer exporting the existing namespace/declarations (\`export namespace X {\`, \`export class\`, \`export using\`) over moving code. Exporting a name that is defined in the global module fragment is not possible — say so if that's the case.
Optional self-check: compile ONLY your own object(s). Find the object in compile_commands.json or via \`ninja -C build\\debug -t targets all\`, then \`$c = ninja -C build\\debug -t commands -s <OBJ>\` and run it with \`cmd /c "cd /d ${REPO}\\build\\debug && $c"\` in the same dev-shell call. Caveat: a newly ADDED import is not in the object's .modmap until the orchestrator's next build rescans, so "could not find module" after adding an import is expected, not a failure of your fix; and modules other agents are editing right now may be stale. Report what you saw honestly.`

phase('Fix modules')
const moduleResults = (await parallel(moduleJobs.map(j => () =>
  agent(fixPrompt(j), { label: `fix ${j.file.split('/').pop()}`, phase: 'Fix modules', schema: FIX_SCHEMA })
))).filter(Boolean)

phase('Fix consumers')
const consumerResults = (await parallel(consumerJobs.map(j => () =>
  agent(fixPrompt(j), { label: `fix ${j.file.split('/').pop()}`, phase: 'Fix consumers', schema: FIX_SCHEMA })
))).filter(Boolean)

// ---------- Build ----------
phase('Build')
const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    exitCode: { type: 'integer' },
    failedCount: { type: 'integer' },
    failedByTarget: { type: 'string', description: 'e.g. "TortoiseProc 12, TGitCache 8"' },
    committed: { type: 'string', description: 'commit hash, or "none" with reason' },
    notes: { type: 'string' },
  },
  required: ['exitCode', 'failedCount', 'failedByTarget', 'committed', 'notes'],
}
const changedFiles = [...moduleResults, ...consumerResults].filter(r => r.changed).map(r => r.file).concat(structuralResults.flatMap(r => r.changedFiles))
const build = await agent(`${COMMON}
You are the orchestrator's build step for round ${round}. You are the ONLY agent allowed to run ninja now (all fix agents have finished). For step 3 only, the no-git-write rule is lifted for exactly one add + one commit.
1. Run exactly this in ONE PowerShell call (it takes minutes; use a 600000 ms timeout):
   . C:\\Users\\refac\\.msbuild\\Enter-VsDevShellCached.ps1 | Out-Null; $p = Start-Process -FilePath ninja -ArgumentList '-C','${REPO}\\build\\debug','-j','8','-k','0' -RedirectStandardOutput '${logOut}' -RedirectStandardError '${logOut}.err' -NoNewWindow -PassThru; $p.PriorityClass = 'BelowNormal'; $p.WaitForExit(); "exit: $($p.ExitCode)"
2. Count lines matching ^FAILED: in ${logOut} and group them by the CMake target (the path segment after the first '/'). If CMake reconfigure failed (CMake Error), report that in notes with the first error.
3. Commit this round's source changes: \`git -C ${REPO} add -A src doc test\` then \`git -C ${REPO} commit -m "[suggested fix] export sweep round ${round}: <one-line summary>" -m "<body: blockers addressed, files changed>"\`. Blockers addressed this round: ${selected.map(s => `${s.key} (friction ${s.friction})`).join('; ')}. Files the fix agents changed: ${changedFiles.join(', ') || '(none)'}. If nothing is staged, do not commit; say so. Never push.
Return the structured result.`, { label: 'build + commit', phase: 'Build', schema: BUILD_SCHEMA })

return {
  round, threshold,
  failedBefore: failedCount,
  triagedObjects: triaged.length,
  lostTriageBatches: lost,
  build,
  ranked: ranked.map(r => ({ key: r.key, friction: r.friction, kinds: r.kinds })),
  selected: selected.map(s => s.key),
  structural: structuralResults,
  skippedForStructural,
  fixes: [...moduleResults, ...consumerResults],
  logOut,
}
