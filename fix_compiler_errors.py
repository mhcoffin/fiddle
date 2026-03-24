import sys

with open("Source/Server/MainComponent.cpp", "r") as f:
    lines = f.readlines()

new_lines = []
in_signal_ready = False
for line in lines:
    if 'withNativeFunction("signalReady"' in line:
        in_signal_ready = True
    elif 'withNativeFunction(' in line and '"signalReady"' not in line:
        in_signal_ready = False

    if not in_signal_ready and "targetWebComponent->evaluateJavascript" in line:
        line = line.replace("targetWebComponent->evaluateJavascript", "broadcastJavascript")
        
    if in_signal_ready and "auto dbBranches = db_.listBranches();" in line:
        # Remove the block that pushes branches explicitly since pushBranches() does it
        # Actually I can just call pushBranches() instead of the manual loop
        line = line.replace("auto dbBranches = db_.listBranches();", "pushBranches();")
        
    new_lines.append(line)

with open("Source/Server/MainComponent.cpp", "w") as f:
    f.writelines(new_lines)
