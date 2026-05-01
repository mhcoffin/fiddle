with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

start_idx = code.find('void MainComponent::handleJsMessage(const juce::String &type,\n                                    const juce::var &payload) {')

if start_idx == -1:
    print("Could not find function")
    exit(1)

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

if end_idx == -1:
    print("End not found")
    exit(1)

with open('scripts/out_refactored.cpp', 'r') as f:
    replacement = f.read()

new_code = code[:start_idx] + replacement + code[end_idx:]

with open('Source/Server/MainComponent.cpp', 'w') as f:
    f.write(new_code)

print("Replaced!")
