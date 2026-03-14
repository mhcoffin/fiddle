import re

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
            if c == '\\':
                escape = True
            elif c == '"':
                in_string = False
            continue
        if c == '"':
            in_string = True
        elif c == open_char:
            depth += 1
        elif c == close_char:
            depth -= 1
            if depth == 0:
                return i
    return -1

with open('/Users/mhc/fiddle/Source/Server/MainComponent.cpp', 'r') as f:
    text = f.read()

native_funcs = []
search_idx = 0
out_text = []

while True:
    idx = text.find('withNativeFunction', search_idx)
    if idx == -1:
        out_text.append(text[search_idx:])
        break
        
    # verify it's .withNativeFunction(
    start_paren = text.find('(', idx)
    if start_paren == -1 or text[idx-1] != '.':
        out_text.append(text[search_idx:idx+1])
        search_idx = idx + 1
        continue
    
    end_paren = get_matching_brace(text, start_paren, '(', ')')
    if end_paren == -1:
        print("Failed to find end paren")
        out_text.append(text[search_idx:idx+1])
        search_idx = idx + 1
        continue
        
    closure_text = text[start_paren:end_paren+1]
    
    # Extract name
    name_match = re.search(r'"([^"]+)"', closure_text)
    if not name_match:
        out_text.append(text[search_idx:end_paren+1])
        search_idx = end_paren + 1
        continue
        
    name = name_match.group(1)
    
    lambda_start = closure_text.find('{')
    if lambda_start != -1:
        lambda_end = closure_text.rfind('}')
        body = closure_text[lambda_start+1:lambda_end]
        native_funcs.append((name, body))
        
    out_text.append(text[search_idx:idx-1]) # don't include the .
    search_idx = end_paren + 1

dispatcher_closure = """
      .withNativeFunction("dispatchMessage",
                          [this](const juce::Array<juce::var> &args,
                                 juce::WebBrowserComponent::NativeFunctionCompletion completion) {
                            if (args.isEmpty() || !args[0].isObject()) {
                              completion(true);
                              return;
                            }
                            auto *obj = args[0].getDynamicObject();
                            juce::String type = obj->getProperty("type").toString();
                            juce::var payload = obj->getProperty("payload");
                            
                            safeCallAsync([this, type, payload]() {
                              handleJsMessage(type, payload);
                            });
                            completion(true);
                          })"""

new_text = "".join(out_text)

# We need to insert the dispatcher closure where the first .withNativeFunction was
# Since we stripped all of them out, we need to append it to the options builder.
# Actually, the options chain is: return juce::WebBrowserComponent::Options{}.withNativeIntegrationEnabled(true)...withUserScript("...")
# Let's just find withUserScript and insert dispatcher_closure right after it.
opt_idx = new_text.find('.withResourceProvider')
if opt_idx != -1:
    # Find the end of the Options building
    # Wait, the best place is just where we removed them. Let's just insert it at the very end of the builder.
    semicolon_idx = new_text.find(';', opt_idx)
    new_text = new_text[:semicolon_idx] + dispatcher_closure + new_text[semicolon_idx:]

with open('/Users/mhc/fiddle/Source/Server/MainComponent.cpp.new', 'w') as f:
    f.write(new_text)

# Now, write out the handleJsMessage body
handle_body = "\nvoid MainComponent::handleJsMessage(const juce::String& type, const juce::var& payload) {\n"
for name, body in native_funcs:
    handle_body += f'  if (type == "{name}") {{\n'
    # we need to provide 'args' for backwards compatibility inside the body, since body uses args
    # payload is actually the args array passed from JS!
    handle_body += f'    juce::Array<juce::var> args;\n'
    handle_body += f'    if (payload.isArray()) args = *payload.getArray();\n'
    handle_body += f'    {body}\n'
    handle_body += f'    return;\n'
    handle_body += f'  }}\n'

handle_body += "  std::cerr << \"[IPC] Unknown message type: \" << type << std::endl;\n"
handle_body += "}\n"

with open('/Users/mhc/fiddle/Source/Server/MainComponent.handle', 'w') as f:
    f.write(handle_body)

print(f"Found {len(native_funcs)} native functions.")
