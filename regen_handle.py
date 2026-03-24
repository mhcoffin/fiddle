import re
import subprocess

def get_matching_brace(text, start_idx, open_char='{', close_char='}'):
    depth = 0
    in_string = False
    escape = False
    for i in range(start_idx, len(text)):
        c = text[i]
        if escape:
            escape = False
            continue
        if in_string:
            if c == '\\': escape = True
            elif c == '"': in_string = False
            continue
        if c == '"': in_string = True
        elif c == open_char: depth += 1
        elif c == close_char:
            depth -= 1
            if depth == 0: return i
    return -1

# Get original file
orig_text = subprocess.check_output(["git", "show", "HEAD:Source/Server/MainComponent.cpp"]).decode('utf-8')

native_funcs = []
search_idx = 0
while True:
    idx = orig_text.find('withNativeFunction', search_idx)
    if idx == -1: break
    start_paren = orig_text.find('(', idx)
    if start_paren == -1 or orig_text[idx-1] != '.':
        search_idx = idx + 1
        continue
    end_paren = get_matching_brace(orig_text, start_paren, '(', ')')
    if end_paren == -1:
        search_idx = idx + 1
        continue
    closure_text = orig_text[start_paren:end_paren+1]
    name_match = re.search(r'"([^"]+)"', closure_text)
    if not name_match:
        search_idx = end_paren + 1
        continue
    name = name_match.group(1)
    lambda_start = closure_text.find('{')
    if lambda_start != -1:
        lambda_end = closure_text.rfind('}')
        body = closure_text[lambda_start+1:lambda_end]
        native_funcs.append((name, body))
    search_idx = end_paren + 1

handle_body = "\nvoid MainComponent::handleJsMessage(const juce::String& type, const juce::var& payload) {\n"
for name, body in native_funcs:
    handle_body += f'  if (type == "{name}") {{\n'
    handle_body += f'    juce::Array<juce::var> args;\n'
    handle_body += f'    if (payload.isArray()) args = *payload.getArray();\n'
    handle_body += f'    {body}\n'
    handle_body += f'    return;\n'
    handle_body += f'  }}\n'
handle_body += "  std::cerr << \"[IPC] Unknown message type: \" << type << std::endl;\n}\n"

# Remove completion(*)
handle_body = re.sub(r'^\s*completion\([^)]*\);\s*\n', '', handle_body, flags=re.MULTILINE)

# Inject into current MainComponent.cpp right before } // namespace fiddle
with open('Source/Server/MainComponent.cpp', 'r') as f:
    curr_text = f.read()

inject_idx = curr_text.rfind('} // namespace fiddle')
if inject_idx != -1:
    new_text = curr_text[:inject_idx] + handle_body + "\n" + curr_text[inject_idx:]
    with open('Source/Server/MainComponent.cpp', 'w') as f:
        f.write(new_text)
else:
    print("FAILED to find namespace end")
