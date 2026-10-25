# collate-sarif.jq - the JSON half of collate-sarif.ps1.
#
# collate-sarif.ps1 does what JSON cannot - walk the build tree, read
# build.ninja, stat files - and hands this program one bundle entry per input
# log:
#
#     {"file": "<log path>", "mtime": <log mtime, unix ms>, "text": "<raw SARIF>"}
#
# The log travels as a string so that one malformed log is reported and
# skipped (try fromjson) instead of aborting the whole stream.
#
# Named arguments, read through $ARGS.named so that a mode may omit the ones it
# does not use without a compile error:
#   mode     "uris"    - print every file: URI the logs mention (after repair),
#                        one per line, so the script can stat each one once.
#            "collate" - print a one-line stats object, then the merged log.
#   root     URI of the source tree, with a trailing slash.
#   exclude  "|"-separated URI prefixes under root that are not "the tree"
#            (the build directories).
#   schema   $schema of the merged log.
#   mtimes   (--slurpfile) [{"<uri>": <unix ms>}] from the "uris" pass.


# =============================================================================
# CONFIGURATION
# =============================================================================

# Drop every result or related location whose message satisfies this, together
# with everything nested under it. Input: the message text, after tidy.
def drop_message: test("https://aka\\.ms");


# =============================================================================
# FUNCTIONS
# =============================================================================

def arg($k): $ARGS.named[$k];
def lc: ascii_downcase;

# -----------------------------------------------------------------------------
# Text
# -----------------------------------------------------------------------------

# MSVC writes template bindings as an indented block under the line they bind:
#
#     ...of type 'const T2' (or there is no acceptable conversion)
#             with
#             [
#                 T2=std::basic_string<wchar_t,...>
#             ]
#             and
#             [ ... ]
#
# Fold each block into a trailing "[with T2=..., T1=...]" so every message is
# one line. A binding block ends at a line holding only "]"; the bindings
# themselves never span lines.
def bindings: [splits("\n") | trim | rtrimstr(",") | select(length > 0)] | join(", ");
def collapse_bindings:
	gsub("\\n[ \\t]*(?<kw>with|and)[ \\t]*\\n[ \\t]*\\[[ \\t]*\\n(?<body>(?:[^\\n]*\\n)*?)[ \\t]*\\]";
		" [\(.kw) \(.body | bindings)]")
	| gsub("\\] \\[and "; ", ");

# The standard string typedefs, spelled out in full. Exact aliases, so nothing
# is lost. CStringT is deliberately left alone: its StrTraitMFC_DLL / StrTraitATL
# argument is the ABI wall, and hiding it would hide the diagnosis.
def alias_types:
	gsub("std::basic_string<wchar_t,std::char_traits<wchar_t>,std::allocator<wchar_t>\\s*>"; "std::wstring")
	| gsub("std::basic_string<char,std::char_traits<char>,std::allocator<char>\\s*>"; "std::string")
	| gsub("std::basic_string_view<wchar_t,std::char_traits<wchar_t>\\s*>"; "std::wstring_view")
	| gsub("std::basic_string_view<char,std::char_traits<char>\\s*>"; "std::string_view");

# One line, typedefs shortened, and MSVC's column alignment ("or       '...'"
# lines up under "could be '...'") dropped.
def tidy:
	collapse_bindings
	| alias_types
	| gsub("\\s*\\n\\s*"; " ")
	| sub("^or\\s+"; "or ")
	| trim;

# -----------------------------------------------------------------------------
# URIs
# -----------------------------------------------------------------------------

# MSVC writes artifact URIs with the path unescaped - a header under
# "C:\Windows Kits\10" comes out as file:///C:/Windows Kits/10/..., with the
# space intact. The SARIF schema types every uri as an RFC 3986 uri-reference,
# and CLion validates against the schema before importing, so one such location
# anywhere in the log rejects the whole file. Percent-encode everything outside
# RFC 3986's unreserved and reserved sets (as UTF-8, which is what @uri does),
# and any "%" that does not already start an escape.
def repair_uri:
	gsub("\\\\"; "/")
	| gsub("%(?![0-9A-Fa-f]{2})"; "%25")
	| gsub("(?<c>[^A-Za-z0-9\\-._~:/?#\\[\\]@!$&'()*+,;=%])"; .c | @uri);

# SARIF puts uris in artifactLocation objects at several depths (results,
# relatedLocations, analysisTarget, artifacts); a generic walk covers them all.
def repair_uris: walk(if type == "object" and (.uri | type) == "string" then .uri |= repair_uri else . end);
def repair_count: [.. | objects | .uri | strings | select(repair_uri != .)] | length;

# "The tree" is the source tree minus the build directories: a header in
# build/**/vcpkg_installed is as foreign as one in the MSVC install.
def root: arg("root") | lc;
def excludes: arg("exclude") | lc | split("|") | map(select(length > 0));
def intree: lc as $u | ($u | startswith(root)) and all(excludes[]; . as $x | $u | startswith($x) | not);

# A physicalLocation as a short "path:line" for messages: tree-relative inside
# the tree, include-relative (<gtest/gtest.h>, <utility>) outside it.
def where:
	(.artifactLocation.uri // "?") as $u
	| (if $u | intree then $u[(root | length):]
	   elif $u | test("/include/") then $u | sub("^.*/include/"; "")
	   else $u | sub("^file:///"; "") end) as $p
	| "\($p | gsub("%20"; " "))\(if .region.startLine then ":\(.region.startLine)" else "" end)";

# Unix ms of the file a uri names, from the "uris" pass; 0 (never stale) when
# the file is missing or the uri is not a file.
def mtime: . as $u | arg("mtimes")[0][$u] // 0;

# -----------------------------------------------------------------------------
# relatedLocations tree
# -----------------------------------------------------------------------------

# MSVC flattens a tree into relatedLocations in pre-order, with the depth as
# properties.nestingLevel (absent at depth 0). Rebuild it as [{n, kids}]: each
# node owns the run of following entries that are deeper than it.
def node: {depth: (.properties.nestingLevel // 0), text: (.message.text // "" | tidy), loc: .physicalLocation};
def forest:
	if length == 0 then []
	else
		.[0].depth as $d
		| ((.[1:] | map(.depth <= $d) | index(true)) // (length - 1)) as $n
		| [{n: .[0], kids: (.[1:$n + 1] | forest)}] + (.[$n + 1:] | forest)
	end;
def preorder: .[] | ., (.kids | preorder);
def size: map(1 + (.kids | size)) | add // 0;
def prune: map(select(.n.text | drop_message | not) | .kids |= prune);

# What MSVC's notes are, by their wording:
#   candidate - one overload in a set: "could be 'sig'" / "or 'sig'"
#   heads_set - introduces a nested overload set ("'X': no overloaded function
#               could convert all the argument types"); its candidates are kids
#   arglist   - "while trying to match the argument list '(...)'", the
#               footer MSVC gives every set
#   context   - "the template instantiation context (the oldest one first) is",
#               whose kids are the "see reference to ... being compiled" frames
def is_candidate: .n.text | test("^(could be|or) '");
def heads_set: any(.kids[]; is_candidate);
def is_arglist: .n.text | startswith("while trying to match the argument list");
def is_context: .n.text | startswith("the template instantiation context");

# -----------------------------------------------------------------------------
# codeFlow
# -----------------------------------------------------------------------------

# The viewer ignores relatedLocations but renders codeFlows, so the tree is
# re-laid as one thread flow made of sections: a header step at nestingLevel 0,
# its entries at 1. One flow rather than one per section, because a viewer that
# shows only codeFlows[0].threadFlows[0] still shows everything.
def step($loc; $text; $level):
	{location: ({message: {text: $text}} + (if $loc then {physicalLocation: $loc} else {} end)), nestingLevel: $level};

# Why a candidate lost: its subtree as text, minus nested overload sets, which
# get their own section - only the line that names the nested set is kept.
def reasons:
	if is_candidate then empty
	elif heads_set then .n.text
	else (.n.text | select(length > 0)), (.kids[] | reasons) end;

# "3/40: <signature> — <why it lost>", at the candidate's declaration. MSVC
# restates the signature at the front of each reason; that echo is dropped.
def candidate_step($i; $n):
	(.n.text | capture("^(?:could be|or) '(?<sig>.*?)'(?<tail>.*)$")
		// {sig: (.n.text | sub("^(could be|or) '"; "")), tail: ""}) as $c
	| [.kids[] | reasons | ltrimstr("'\($c.sig)': ")] as $why
	| step(.n.loc; "\($i + 1)/\($n): \($c.sig)\($c.tail)\(if $why == [] then "" else " — " + ($why | join("; ")) end)"; 1);

# Anything else MSVC says under a header - "see declaration of 'X'", "Reason:
# ...", a concept that evaluated to false - with its own subtree indented below.
def note_steps($level):
	step(.n.loc; .n.text; $level),
	(if heads_set then empty else (.kids[] | note_steps($level + 1)) end);

# A header and what hangs under it: the argument list first, then the
# candidates as a numbered list, then the remaining notes. A kid that heads a
# nested set is left out: unique_sets gives it a section of its own, and
# listing it here too would print its line twice in a row.
def section($title; $loc; $kids):
	[$kids[] | select(is_candidate)] as $cands
	| step($loc; $title; 0),
	  ($kids[] | select(is_arglist) | step(.n.loc; .n.text | sub("^while trying to match the "; ""); 1)),
	  ($cands | to_entries[] | .key as $i | .value | candidate_step($i; $cands | length)),
	  ($kids[] | select((is_candidate or is_arglist or is_context or heads_set) | not) | note_steps(1));

# Every nested overload set, in pre-order, once each. MSVC repeats a set under
# every candidate that needed the same conversion - ArgvData's constructors
# appear once per Run overload - so identical sets are folded together.
def sets: if is_context then empty else (if heads_set then . else empty end), (.kids[] | sets) end;
def unique_sets:
	reduce (.[] | sets) as $s ({seen: {}, out: []};
		([$s.n.text, ($s.kids[] | select(is_candidate) | .n.text)] | tojson) as $k
		| if .seen[$k] then . else .seen[$k] = true | .out += [$s] end)
	| .out[];

# -----------------------------------------------------------------------------
# Result
# -----------------------------------------------------------------------------

# Reshape one result for a viewer that renders locations[0], the message, and
# codeFlows. Emits a record for the bookkeeping in main:
#   {status: "kept" | "stale" | "filtered", result, key, moved, pruned}
#
# - Retarget. A result should land in the tree. When MSVC reports it in a
#   foreign header (gtest.h, <utility>), point it at the newest instantiation
#   frame that is in the tree - the line that dragged the header in; failing
#   that, at the translation unit as a whole; failing that, nowhere, leaving a
#   plain description. Where MSVC really reported it goes into the message and
#   stays reachable from the flow.
# - Stale. The collated log is current, not comprehensive: a log describes the
#   last compile of its translation unit, and a build that stops at the first
#   failure can leave that far behind the sources. A result is dropped when the
#   file it was reported in, the file it now points at, or its translation unit
#   was modified after the log was written - the line it names may no longer
#   be the line it meant.
# - Flow. relatedLocations become one codeFlow (see section) and are removed.
def reshape($logtime):
	. as $res
	| ($res.message.text // "" | tidy) as $msg
	| if $msg | drop_message then {status: "filtered", pruned: 0}
	  else
		($res.locations[0].physicalLocation // null) as $at
		| ([($res.relatedLocations // [])[] | node] | forest) as $all
		| ($all | prune) as $forest
		| [$forest | preorder | select(is_context) | .kids | preorder] as $frames
		| (if $at and ($at.artifactLocation.uri // "" | intree) then {loc: $at, moved: false}
		   else
			([$frames[] | .n.loc | select(.artifactLocation.uri // "" | intree)] | last) as $frame
			| if $frame then {loc: $frame, moved: true}
			  elif $res.analysisTarget.uri // "" | intree then {loc: {artifactLocation: {uri: $res.analysisTarget.uri}}, moved: true}
			  else {loc: null, moved: ($at != null)} end
		   end) as $target
		| ([$at.artifactLocation.uri, $target.loc.artifactLocation.uri, $res.analysisTarget.uri]
			| map(strings | mtime) | max // 0) as $newest
		| if $newest > $logtime then {status: "stale", pruned: 0}
		  else
			[ (if $frames == [] then empty else
				step($frames[0].n.loc; "instantiated from (oldest first)"; 0),
				($frames[] | step(.n.loc; .n.text | sub("^see reference to "; "") | sub(" being compiled"; ""); 1))
			   end),
			  ([$forest[] | select(is_context | not)] as $top
				| if $top == [] and $frames == [] then empty else section($msg; $at; $top) end),
			  ($forest | unique_sets | section(.n.text; .n.loc; .kids))
			] as $steps
			| {
				status: "kept",
				moved: $target.moved,
				pruned: (($all | size) - ($forest | size)),
				key: "\($res.ruleId)|\($target.loc.artifactLocation.uri)|\($target.loc.region.startLine)|\($target.loc.region.startColumn)|\($msg)",
				result: ($res
					| .message.text = (if $target.moved and $at then "\($msg) (reported at \($at | where))" else $msg end)
					| if $target.loc then .locations = [{physicalLocation: $target.loc}] else del(.locations) end
					| if .codeFlows or $steps == [] then . else .codeFlows = [{threadFlows: [{locations: $steps}]}] end
					| del(.relatedLocations))
			  }
		  end
	  end;

# MSVC stamps every run with the same driver description and link; they are
# noise when the viewer lists one run per translation unit.
def prepare_run: del(.tool.driver.shortDescription, .tool.driver.informationUri);

def load: . as $e | try {file, mtime, log: (.text | fromjson)} catch {file: $e.file, unreadable: .};


# =============================================================================
# MAIN
# =============================================================================

if arg("mode") == "uris" then
	[inputs | load | select(.log) | .log | repair_uris | .. | objects | .uri | strings | select(lc | startswith("file:"))]
	| unique[]
else
	[inputs | load] as $entries

	# Load: parse, repair URIs, reshape every result of every run.
	| [$entries[] | select(.log) | .mtime as $t
		| .log | repair_uris | (.runs // [])[]
		| {run: (prepare_run | del(.results)), items: [(.results // [])[] | reshape($t)]}
	  ] as $runs

	# Dedupe across translation units: a header error is reported once per
	# includer. Sequential, so the first unit to report a finding keeps it; a
	# run left with nothing is dropped.
	| (reduce $runs[] as $r ({seen: {}, runs: [], duplicates: 0, moved: 0};
		(reduce ($r.items[] | select(.status == "kept")) as $i ({seen: .seen, kept: [], dup: 0};
			if .seen[$i.key] then .dup += 1 else .seen[$i.key] = true | .kept += [$i] end)) as $x
		| .seen = $x.seen
		| .duplicates += $x.dup
		| .moved += ([$x.kept[] | select(.moved)] | length)
		| if $x.kept == [] then . else .runs += [$r.run + {results: [$x.kept[].result]}] end)) as $d

	| [$runs[].items[]] as $all
	| ({
		runs: ($d.runs | length),
		results: ([$d.runs[].results[]] | length),
		duplicates: $d.duplicates,
		stale: ([$all[] | select(.status == "stale")] | length),
		filtered: (([$all[] | select(.status == "filtered")] | length) + ([$all[].pruned] | add // 0)),
		flows: ([$d.runs[].results[] | select(.codeFlows)] | length),
		retargeted: $d.moved,
		repaired: ([$entries[] | select(.log) | .log | repair_count] | add // 0),
		unreadable: [$entries[] | select(.unreadable) | "\(.file): \(.unreadable)"]
	  } | tojson),
	  {"$schema": arg("schema"), version: "2.1.0", runs: $d.runs}
end
