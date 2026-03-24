import re, glob

# This will find lines like:
# const fn = getNative("someName");
# if (fn) fn(args...);
# and replace them with:
# dispatchCpp("someName", args...);

for file in glob.glob("*.svelte"):
    with open(file, 'r') as f:
        content = f.read()

    # Pattern: const <var> = getNative("<name>");\n <whitespace> if (<var>) <var>(<args>);
    patt = re.compile(r'const\s+([a-zA-Z0-9_]+)\s*=\s*getNative\("([^"]+)"\);\s*if\s*\(\1\)\s*\1\((.*?)\);')
    content = patt.sub(r'dispatchCpp("\2"\3);', content)
    
    # Second pattern: sometimes there's no if check, or just calling it directly:
    # const fn = getNative("someName"); fn(...);
    # Actually wait. Sometimes it is inside an `if (fn)` block:
    # const rb = getNative("requestBranches");
    # if (rb) rb();
    patt2 = re.compile(r'const\s+([a-zA-Z0-9_]+)\s*=\s*getNative\("([^"]+)"\);\s*if\s*\(\1\)\s*\{\s*\1\((.*?)\);\s*\}')
    content = patt2.sub(r'dispatchCpp("\2"\3);', content)

    # Let's just fix it manually if it gets too complex, but I'll try catching the most common forms:
    # Pattern 3: let's replace dispatchCpp("name"); with no leading comma.
    content = content.replace('dispatchCpp("requestBranches");', 'dispatchCpp("requestBranches");')

    with open(file, 'w') as f:
        f.write(content)
