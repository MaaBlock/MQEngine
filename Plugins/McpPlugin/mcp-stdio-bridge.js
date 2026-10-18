#!/usr/bin/env node
const http = require('http');
const readline = require('readline');

const TARGET_HOST = process.env.MQENGINE_MCP_HOST || '127.0.0.1';
const TARGET_PORT = Number(process.env.MQENGINE_MCP_PORT || 18080);

function forwardJsonRpcToEngine(rawLine) {
    const req = http.request(
        {
            hostname: TARGET_HOST,
            port: TARGET_PORT,
            path: '/mcp',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(rawLine, 'utf8')
            }
        },
        (res) => {
            let body = '';
            res.on('data', (chunk) => {
                body += chunk.toString('utf8');
            });
            res.on('end', () => {
                if (body && body.trim().length > 0) {
                    process.stdout.write(body.trim() + '\n');
                }
            });
        }
    );

    req.on('error', (err) => {
        try {
            const parsed = JSON.parse(rawLine);
            if (parsed && parsed.id !== undefined) {
                const errResp = {
                    jsonrpc: '2.0',
                    id: parsed.id,
                    error: {
                        code: -32000,
                        message: '无法连接到运行中的 MQEngine MCP 服务 (' + TARGET_HOST + ':' + TARGET_PORT + '): ' + err.message
                    }
                };
                process.stdout.write(JSON.stringify(errResp) + '\n');
            }
        } catch (_) {}
    });

    req.write(rawLine);
    req.end();
}

const rl = readline.createInterface({
    input: process.stdin,
    terminal: false
});

rl.on('line', (line) => {
    const trimmed = line.trim();
    if (trimmed.length > 0) {
        forwardJsonRpcToEngine(trimmed);
    }
});
