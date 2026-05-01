import os, re

files = [
    "MixerPanel.svelte",
    "PluginsPanel.svelte",
    "VersionHistory.svelte"
]

get_native_regex = re.compile(
    r'\s+const getNative = \(name\) => \{.*?\n\s+return null;\n\s+\};\n',
    re.DOTALL
)

def process_file(path):
    with open(path, "r") as f:
        content = f.read()

    # Insert import
    if "import { dispatchCpp }" not in content:
        content = content.replace('from "svelte";', 'from "svelte";\n    import { dispatchCpp } from "./ipc.js";')

    # Remove getNative definition
    content = get_native_regex.sub('', content)

    # Replace usages:
    # const fn = getNative("foo");
    # if (fn) fn(args...);
    # => dispatchCpp("foo", args...);
    
    # We can handle the specific cases using regex, e.g.:
    # const f = getNative("requestSetupData");
    # if (f) f();
    
    def repl_block(m):
        var_name = m.group(1)
        func_name = m.group(2)
        return f'dispatchCpp("{func_name}");'
        
    # Replace multiline block like:
    # const f = getNative("foo");
    # if (f) f(bar, baz);
    # Wait, simple line replacements are safer
    
    # For now, let's just do a simpler search/replace or write custom regex hooks.
    with open(path, "w") as f:
        f.write(content)

for file in files:
    process_file(file)
