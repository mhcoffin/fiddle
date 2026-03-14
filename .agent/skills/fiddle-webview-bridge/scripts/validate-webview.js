const net = require('net');

function evalJS(script, port) {
    return new Promise((resolve, reject) => {
        const client = net.createConnection({ port }, () => {
            const buf = Buffer.from(script, 'utf8');
            const lengthBuf = Buffer.alloc(4);
            lengthBuf.writeUInt32LE(buf.length, 0);
            client.write(Buffer.concat([lengthBuf, buf]));
        });

        let resultData = Buffer.alloc(0);

        client.on('data', (data) => {
            resultData = Buffer.concat([resultData, data]);
            if (resultData.length >= 4) {
                const expectedLength = resultData.readUInt32LE(0);
                if (resultData.length >= 4 + expectedLength) {
                    client.end();
                }
            }
        });

        client.on('end', () => {
            if (resultData.length >= 4) {
                const payload = resultData.subarray(4).toString('utf8');
                resolve(payload);
            } else {
                reject(new Error('Incomplete response'));
            }
        });

        client.on('error', reject);
        client.setTimeout(6000);
        client.on('timeout', () => {
            client.destroy();
            reject(new Error('Timeout waiting for response'));
        });
    });
}

(async () => {
    try {
        let port = 9223;
        let scriptArgs = process.argv.slice(2);

        if (scriptArgs[0] === '--port' && scriptArgs.length >= 2) {
            port = parseInt(scriptArgs[1], 10);
            scriptArgs = scriptArgs.slice(2);
        }

        const inputArg = scriptArgs[0] || "document.documentElement.innerHTML";
        console.log(`[Port ${port}] Evaluating JavaScript: ${inputArg}`);

        const result = await evalJS(inputArg, port);
        console.log('--- RESULT ---');
        console.log(result);
        console.log('--------------');
        process.exit(0);
    } catch (error) {
        console.error('Connection failed. Is the app running and listening on the given port?');
        console.error(error.message);
        process.exit(1);
    }
})();