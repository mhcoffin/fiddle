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

with open('scripts/out_init.cpp', 'r') as f:
    new_methods = f.read()

# Add a public method to replace the initial call `runInitStep(0)`
start_method = "void MainComponent::initializeApp() {\n  initExpressionMaps();\n}\n\n"

new_code = code[:start_idx] + start_method + new_methods + "\n" + code[end_idx:]

with open('Source/Server/MainComponent.cpp', 'w') as f:
    f.write(new_code)
