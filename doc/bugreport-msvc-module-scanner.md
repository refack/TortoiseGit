# MSVC: two silent module-name defects the dependency scanner does not report

Draft, not filed; recipient undecided. It is written against MSVC's
`/scanDependencies` because that is where both defects were reproduced. The
originating complaint was CLion/ReSharper (and possibly clang-tidy)
"saying weird things" in this tree; **that IDE symptom was not reproduced or
observed** while writing this. Both repros are self-contained: no MFC, no CMake.

Toolchain observed: MSVC 14.51.36231 (VS 2026 Community), `/std:c++latest`.
Compared against clang 23.1.5 (`-std=c++26`).

---

## 1. A module interface that imports itself (through its global module fragment) is not diagnosed by the scanner, and the compile error points away from the cause

### Repro

`h.h`
```cpp
#pragma once
import M;
inline int from_header() { return answer(); }
```

`M.ixx`
```cpp
module;
#include "h.h"
export module M;
export int answer() { return 42; }
```

### Observed

`cl /std:c++latest /TP /scanDependencies M.json M.ixx` succeeds, exit 0, and
emits a P1689 rule that provides **and** requires the same module:

```json
"provides": [ { "logical-name": "M", "is-interface": true } ],
"requires": [ { "logical-name": "M" } ]
```

`cl /std:c++latest /c /interface M.ixx` then fails with:

```
h.h(2): error C2230: could not find module 'M'
h.h(3): error C3861: 'answer': identifier not found
```

### Expected

A rule whose `requires` contains its own `provides` cannot be built in any
order. The scanner already knows both names when it writes the rule, so it
should diagnose the cycle, naming the `#include` chain that led to the
`import`. The compile-time error should say "module 'M' imports itself
(via h.h)" instead of "could not find module 'M'". 'Not found' sends the user
to their include paths and build order, when the actual fault is a header in
the global module fragment.

### Why it matters

In a real tree the `import` sits several headers deep in the global module
fragment, and the header is usually meant for non-module translation units. The
P1689 output is the only compiler artefact that shows the cycle, and nothing
reads it with that question in mind. (Not verified here: what CMake/Ninja's
dyndep step does with such a rule. The developer reports that while these
cycles existed, CLion's engine "said weird things"; the mechanism is not
established.)

In this project the build tree still holds two stale `.ddi` files that record
such a cycle: `PreserveChdir.ixx` (11-03) and `TortoiseProc.ixx` (11-05). The
`PreserveChdir` one was found and fixed by hand in `320827253`; the header
behind the `TortoiseProc` one was not identified. Both are gone from current
sources.

clang reports the same `module 'M' not found`, so the misleading wording is
not MSVC-only. The scanner's silence is the MSVC-specific part.

---

## 2. Module declaration names are macro-expanded (non-conforming since P3034R1)

### Repro

`mb.h`
```cpp
#pragma once
#define MessageBox MessageBoxW      // exactly what <windows.h> does under UNICODE
```

`MB.ixx`
```cpp
module;
#include "mb.h"
export module MessageBox;
export int mb() { return 1; }
```

`use.cpp`
```cpp
#include "mb.h"
import MessageBox;
int main() { return mb(); }
```

### Observed

| | `export module MessageBox;` | `import MessageBox;` |
|---|---|---|
| MSVC `/scanDependencies` | provides **`MessageBoxW`**, no diagnostic | requires `MessageBoxW` |
| clang 23 | `error: module name component 'MessageBox' cannot be a object-like macro` | requires `MessageBoxW` |

MSVC builds it only because the macro is applied consistently on both sides.
The module's real name is whatever `<windows.h>` happens to define, so it
depends on `UNICODE`/`_MBCS` and on whether the importing TU saw `<windows.h>`.
An importer that never saw `<windows.h>` asks for `MessageBox` (scanned: a
`use.cpp` without the `#include` requires `MessageBox`), which nobody provides.

### Expected

P3034R1 ("Module Declarations Shouldn't be Macros", adopted for C++26) makes
a module-name component that names an object-like macro ill-formed in a module
declaration. MSVC should diagnose it as clang does, or at minimum warn under
`/std:c++latest`. A Win32 project hits this with any module named after a
`<windows.h>` A/W macro (`MessageBox`, `LoadString`, `GetObject`, ...).

### Impact here

`src/SharedMFC/MessageBox.ixx` exported `module MessageBox;` and 73 files
imported it. MSVC built it as `MessageBoxW`. Per the repro above, clang rejects
that pattern outright, so clang-based tooling should lose the module and every
importer with it. That was inferred from the repro, not observed on this tree.
The workaround is to rename the module to a name with no macro in any part
(`TGitMessageBox`). A dotted `tgit.MessageBox` is **not** enough, because clang
checks each part of the name separately.
