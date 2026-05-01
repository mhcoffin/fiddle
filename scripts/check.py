with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

start_idx = code.find('void MainComponent::handleJsMessage(const juce::String &type,\n                                    const juce::var &payload) {')
func_start = start_idx
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

func_body = code[func_start:end_idx]

inside_start = func_body.find('{') + 1
inside_end = func_body.rfind('}')
inside = func_body[inside_start:inside_end]

lines = inside.split('\n')
for line in lines[:10]:
    print(line)
