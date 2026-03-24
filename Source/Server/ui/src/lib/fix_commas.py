import re, glob

for file in glob.glob("*.svelte"):
    with open(file, 'r') as f:
        c = f.read()

    p = re.compile(r'dispatchCpp\("([^"]+)"([^);]*)\)')
    def repl(m):
        func = m.group(1)
        args_str = m.group(2)
        if args_str.strip() and not args_str.startswith(','):
            return f'dispatchCpp("{func}", {args_str})'
        return m.group(0)

    c = p.sub(repl, c)

    with open(file, 'w') as f:
        f.write(c)
