#!/usr/bin/env python3
# Finds the "include race" in src/libgit2: a textual standard-library include
# reached *after* `import std;` in the same translation unit. MSVC supports
# include-then-import but not import-then-include, and the failure shows up as
# redefinition errors far from the cause. Follows project headers transitively;
# needs no compiler, so it runs on any platform.
import re,glob,os,sys
ROOT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../src/libgit2'))
DIRS=[ROOT+'/include',ROOT+'/../gitdll',ROOT,ROOT+'/..']
STL=set("""algorithm any array atomic bit bitset cassert cctype cerrno cfloat charconv chrono climits cmath compare concepts condition_variable cstdarg cstddef cstdint cstdio cstdlib cstring ctime cwchar cwctype deque exception execution expected filesystem format forward_list fstream functional future initializer_list iomanip ios iosfwd iostream istream iterator limits list locale map memory memory_resource mutex new numbers numeric optional ostream print queue random ranges ratio regex scoped_allocator set shared_mutex source_location span sstream stack stdexcept stop_token streambuf string string_view system_error thread tuple type_traits typeindex typeinfo unordered_map unordered_set utility valarray variant vector version""".split())
EXT_STL=('gsl/','wil/','winrt/')
def resolve(name,cur,quoted):
    cands=([os.path.dirname(cur)] if quoted else [])+DIRS
    for d in cands:
        p=os.path.normpath(os.path.join(d,name))
        if os.path.isfile(p): return p
def walk(f,state,trail,seen):
    try: lines=open(f,encoding='utf-8-sig',errors='replace').read().split('\n')
    except: return
    if f in seen: return
    seen.add(f)
    for i,l in enumerate(lines):
        if re.match(r'\s*(export\s+)?import\s+std\s*;',l) and not state.get('std'):
            state['std']=f"{os.path.relpath(f,ROOT)}:{i+1}"
        m=re.match(r'\s*#\s*include\s*([<"])([^>"]+)[>"]',l)
        if not m: continue
        name=m.group(2); loc=f"{os.path.relpath(f,ROOT)}:{i+1}"
        p=resolve(name,f,m.group(1)=='"')
        if p: walk(p,state,trail+[loc],seen); continue
        if state.get('std') and (name in STL or name.startswith(EXT_STL)):
            state['hits'].append((loc,name,state['std']))
bad=0
for f in sorted(glob.glob(ROOT+'/**/*.*',recursive=True)):
    if '/orphans/' in f or not re.search(r'\.(ixx|cpp)$',f): continue
    st={'hits':[]}; walk(f,st,[],set())
    for loc,name,s in st['hits']:
        bad=1; print(f"{os.path.relpath(f,ROOT)}: <{name}> at {loc} after import std at {s}")
sys.exit(bad)
