import re

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

# Split func_body by top-level "if (type == " or "} else if (type =="
# Actually, the simplest is to parse it manually keeping track of brace counts
inside_start = func_body.find('{') + 1
inside_end = func_body.rfind('}')
inside = func_body[inside_start:inside_end]

lines = inside.split('\n')
out_lines = []
out_lines.append("void MainComponent::setupJsHandlers() {")

in_handler = False
brace_depth = 0
handler_content = []
handler_type = ""

for line in lines:
    stripped = line.strip()
    
    # Check if we're at level 0/1 and find an if (type == "...")
    if brace_depth == 0 and (stripped.startswith('if (type == "') or stripped.startswith('} else if (type == "') or stripped.startswith('else if (type == "')):
        # Extract the type
        match = re.search(r'type == "([^"]+)"', stripped)
        if match:
            if in_handler:
                # Need to close the previous handler
                # Wait, the previous handler might already be closed by a '}' if it was an isolated block.
                pass
            handler_type = match.group(1)
            out_lines.append(f'  jsRouter_.registerHandler("{handler_type}", [this](const juce::var& payload) {{')
            in_handler = True
            brace_depth += line.count('{') - line.count('}')
            
            # Since the { is on the same line, brace_depth is usually 1
            continue

    if in_handler:
        brace_depth += line.count('{') - line.count('}')
        if brace_depth == 0 and stripped == '}':
            # End of handler
            out_lines.append('  });')
            in_handler = False
        else:
            out_lines.append(line)
    else:
        if stripped != "" and stripped != "}":
            # Some other code outside? Let's print it to check
            pass

out_lines.append("}")

out_lines.append("")
out_lines.append("void MainComponent::handleJsMessage(const juce::String &type, const juce::var &payload) {")
out_lines.append("  if (!jsRouter_.handleMessage(type, payload)) {")
out_lines.append('    std::cerr << "Unknown JS message: " << type << std::endl;')
out_lines.append("  }")
out_lines.append("}")

with open('scripts/out_refactored.cpp', 'w') as f:
    f.write('\n'.join(out_lines))

print("Done. Wrote out_refactored.cpp")
