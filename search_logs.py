import os

brain_dir = "/Users/mhc/.gemini/antigravity/brain"
found = False
for root, dirs, files in os.walk(brain_dir):
    for f in files:
        if f.endswith(".txt"):
            try:
                with open(os.path.join(root, f), 'r', encoding='utf-8', errors='ignore') as file:
                    content = file.read()
                    if "checkoutBranch" in content and "versionStore_" in content:
                        print(f"FOUND IN: {os.path.join(root, f)}")
                        lines = content.split('\n')
                        for i, line in enumerate(lines):
                            if "checkoutBranch" in line:
                                print('\n'.join(lines[max(0, i-5):min(len(lines), i+30)]))
                                break
                        found = True
                        break
            except Exception as e:
                pass
    if found: break
