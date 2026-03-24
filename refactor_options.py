import sys

with open("Source/Server/MainComponent.cpp", "r") as f:
    lines = f.readlines()

start_idx = -1
end_idx = -1

for i, line in enumerate(lines):
    if "webComponent(" in line and "juce::WebBrowserComponent::Options{}" in lines[i+1]:
        start_idx = i
        break

for i in range(start_idx, len(lines)):
    if ".withNativeFunction(\"saveSetup\"," in lines[i]:
        for j in range(i, len(lines)):
            if "})) {" in lines[j]:
                end_idx = j
                break
        break

if start_idx != -1 and end_idx != -1:
    options_block = lines[start_idx+1:end_idx+1]
    options_block[-1] = options_block[-1].replace("})) {", "});\n")
    
    new_method = ["juce::WebBrowserComponent::Options MainComponent::createWebOptions() {\n", "  return "] + options_block + ["}\n\n"]
    
    # modify the constructor
    lines[start_idx] = "    : webComponent(createWebOptions()) {\n"
    del lines[start_idx+1:end_idx+1]
    
    # insert the new method right before the constructor
    final_lines = lines[:start_idx-1] + new_method + lines[start_idx-1:]
    
    with open("Source/Server/MainComponent.cpp", "w") as f:
        f.writelines(final_lines)
    print("Refactoring successful.")
else:
    print(f"Extraction failed: start={start_idx}, end={end_idx}")
