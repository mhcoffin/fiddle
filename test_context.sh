#!/bin/bash
# Test script to verify ContextUpdate parsing

echo "Testing ContextUpdate parsing..."
echo ""

# Create a test HTML file that simulates the UI
cat > /tmp/test_context.html << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>Context Update Test</title>
</head>
<body>
    <h1>Context Update Test</h1>
    <div id="output"></div>
    <script>
        let channelInstruments = {};
        
        window.pushMidiEvent = (event) => {
            console.log("Received event:", event);
            
            if (event.description && event.description.startsWith("ContextUpdate:")) {
                console.log("Found ContextUpdate!");
                const indexMatch = event.description.match(/Index=(\d+)/);
                const nameMatch = event.description.match(/Name='([^']+)'/);
                
                if (indexMatch && nameMatch) {
                    const channelIndex = parseInt(indexMatch[1]);
                    const instrumentName = nameMatch[1];
                    const channel = channelIndex + 1;
                    
                    channelInstruments = { ...channelInstruments, [channel]: instrumentName };
                    console.log(`[Context] Ch ${channel}: ${instrumentName}`);
                    
                    document.getElementById('output').innerHTML += 
                        `<p>[Context] Ch ${channel}: ${instrumentName}</p>`;
                } else {
                    console.log("Failed to parse:", event.description);
                }
            }
        };
        
        // Test with sample data
        console.log("=== Testing ContextUpdate parsing ===");
        
        window.pushMidiEvent({
            description: "ContextUpdate: Index=0, Name='Violin 1', Namespace='VST3'"
        });
        
        window.pushMidiEvent({
            description: "ContextUpdate: Index=1, Name='Cello', Namespace='VST3'"
        });
        
        console.log("channelInstruments:", channelInstruments);
    </script>
</body>
</html>
EOF

echo "Created test file at /tmp/test_context.html"
echo "Open it in a browser and check the console"
echo ""
echo "Expected output:"
echo "  [Context] Ch 1: Violin 1"
echo "  [Context] Ch 2: Cello"
