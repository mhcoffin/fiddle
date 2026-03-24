import sys

with open("Source/Server/MainComponent.cpp", "r") as f:
    lines = f.readlines()

new_lines = []
in_signal_ready = False
bracket_depth = 0

for line in lines:
    if 'withNativeFunction(' in line and '"signalReady"' in line:
        in_signal_ready = True
        bracket_depth = 0

    if in_signal_ready:
        bracket_depth += line.count('{') - line.count('}')
        if "webComponent.evaluateJavascript(" in line:
            new_lines.append(line.replace("webComponent.evaluateJavascript(", "targetWebComponent->evaluateJavascript("))
            # If we hit the end of the lambda
        else:
            new_lines.append(line)
            
        if bracket_depth <= 0 and "})" in line:
            in_signal_ready = False
    else:
        if "webComponent.evaluateJavascript(" in line:
            new_lines.append(line.replace("webComponent.evaluateJavascript(", "broadcastJavascript("))
        else:
            new_lines.append(line)

with open("Source/Server/MainComponent.cpp", "w") as f:
    f.writelines(new_lines)
