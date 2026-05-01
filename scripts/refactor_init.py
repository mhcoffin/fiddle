import re

with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

start_idx = code.find('void MainComponent::runInitStep(int step) {')
brace_count = 0
in_function = False
end_idx = -1
for i in range(start_idx, len(code)):
    if code[i] == '{':
        brace_count += 1
        in_function = True
    elif code[i] == '}':
        brace_count -= 1
        if in_function and brace_count == 0:
            end_idx = i + 1
            break

func_body = code[start_idx:end_idx]

import sys
sys.path.append('.')

cases = re.split(r'case \d+:\s*(?:\{)?', func_body)[1:]

methods = [
    "initExpressionMaps",
    "initInstrumentBrowser",
    "initPlaceholder",
    "initLuaPlugins",
    "initExpressionMapLibrary",
    "initMidiServer",
    "initAudioDevice",
    "initDatabase",
    "initPluginsAndStrips"
]

new_methods = []
for i, case_body in enumerate(cases):
    if i >= len(methods): break
    
    # Clean up the case body
    # Remove 'return;' and closing brace if it was a block
    # case body usually ends with `safeCallAsync([this]() { runInitStep(X); });\n    return;\n  }`
    
    # We replace `runInitStep(X)` with `nextMethod()`
    if i < len(methods) - 1:
        next_method = methods[i + 1]
        case_body = re.sub(rf'runInitStep\({i+1}\)', f'{next_method}()', case_body)
    
    # Find the last return;
    last_return = case_body.rfind('return;')
    if last_return != -1:
        case_body = case_body[:last_return] + case_body[last_return+7:]
        
    # Trim trailing closing brace if it was inside a block
    case_body = case_body.strip()
    if case_body.endswith('}'):
        case_body = case_body[:-1].strip()
        
    new_method = f"void MainComponent::{methods[i]}() {{\n  {case_body}\n}}"
    new_methods.append(new_method)

new_code = "\n\n".join(new_methods)

# write to a file to inspect
with open('scripts/out_init.cpp', 'w') as f:
    f.write(new_code)
