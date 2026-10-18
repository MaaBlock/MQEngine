interface McpToolSchema {
    name: string;
    description: string;
    inputSchema: Record<string, any>;
}

interface JsonRpcRequest {
    jsonrpc: string;
    id?: string | number | null;
    method: string;
    params?: Record<string, any>;
}

const http = require('http');

const MCP_TOOLS: Array<McpToolSchema> = [
    {
        name: 'mq_get_engine_status',
        description: '获取 MQEngine 引擎运行状态、当前打开的工程信息与活动场景概览',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_list_plugins',
        description: '列出引擎当前已注册或加载的所有插件（包含 C++ 动态库插件与 JS/TS 脚本插件）',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_set_play_mode',
        description: '控制引擎进入或退出运行模式 (Play / Stop)',
        inputSchema: {
            type: 'object',
            properties: {
                running: { type: 'boolean', description: 'true 为开始运行，false 为停止运行' }
            },
            required: ['running']
        }
    },
    {
        name: 'mq_list_scenes',
        description: '列出当前工程中所有可用场景及其 UUID',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_open_scene',
        description: '根据场景 UUID 打开并切换到指定场景',
        inputSchema: {
            type: 'object',
            properties: {
                uuid: { type: 'string', description: '目标场景 UUID' }
            },
            required: ['uuid']
        }
    },
    {
        name: 'mq_save_scene',
        description: '保存当前打开的场景或指定 UUID 的场景',
        inputSchema: {
            type: 'object',
            properties: {
                uuid: { type: 'string', description: '可选，场景 UUID；留空则保存当前活动场景' }
            }
        }
    },
    {
        name: 'mq_new_scene',
        description: '创建新场景',
        inputSchema: {
            type: 'object',
            properties: {
                name: { type: 'string', description: '新场景名称' }
            },
            required: ['name']
        }
    },
    {
        name: 'mq_list_entities',
        description: '列出当前活动场景中的所有实体 ID、名称及挂载组件列表',
        inputSchema: {
            type: 'object',
            properties: {
                trunk: { type: 'string', description: '可选，过滤指定场景块名称（默认遍历全部已加载块）' }
            }
        }
    },
    {
        name: 'mq_get_entity',
        description: '获取指定实体的详细信息及所有组件字段值',
        inputSchema: {
            type: 'object',
            properties: {
                entityId: { type: 'number', description: '实体 ID' },
                trunk: { type: 'string', description: '可选，场景块名称，默认 default' }
            },
            required: ['entityId']
        }
    },
    {
        name: 'mq_create_entity',
        description: '在当前场景中创建新实体，支持同时指定名称与初始组件数据',
        inputSchema: {
            type: 'object',
            properties: {
                name: { type: 'string', description: '实体名称 (NameTag)' },
                trunk: { type: 'string', description: '可选，场景块名称，默认 default' },
                components: {
                    type: 'object',
                    description: '可选，初始组件映射表，例如 {"PositionComponent": {"position": {"x": 0, "y": 2, "z": 0}}}'
                }
            }
        }
    },
    {
        name: 'mq_destroy_entity',
        description: '销毁当前场景中的指定实体',
        inputSchema: {
            type: 'object',
            properties: {
                entityId: { type: 'number', description: '要销毁的实体 ID' },
                trunk: { type: 'string', description: '可选，场景块名称，默认 default' }
            },
            required: ['entityId']
        }
    },
    {
        name: 'mq_list_component_types',
        description: '列出引擎反射系统支持的所有组件类型及其字段名称、数据类型',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_get_component',
        description: '获取指定实体上特定组件的全部字段值',
        inputSchema: {
            type: 'object',
            properties: {
                entityId: { type: 'number', description: '实体 ID' },
                componentName: { type: 'string', description: '组件名称，例如 PositionComponent' },
                trunk: { type: 'string', description: '可选，场景块名称' }
            },
            required: ['entityId', 'componentName']
        }
    },
    {
        name: 'mq_set_component',
        description: '为指定实体添加或更新组件字段值',
        inputSchema: {
            type: 'object',
            properties: {
                entityId: { type: 'number', description: '实体 ID' },
                componentName: { type: 'string', description: '组件名称，例如 PositionComponent、RotationComponent、ScaleComponent' },
                fields: { type: 'object', description: '要更新的组件字段键值对' },
                trunk: { type: 'string', description: '可选，场景块名称' }
            },
            required: ['entityId', 'componentName']
        }
    },
    {
        name: 'mq_remove_component',
        description: '从指定实体移除特定组件',
        inputSchema: {
            type: 'object',
            properties: {
                entityId: { type: 'number', description: '实体 ID' },
                componentName: { type: 'string', description: '组件名称' },
                trunk: { type: 'string', description: '可选，场景块名称' }
            },
            required: ['entityId', 'componentName']
        }
    }
];

function executeMcpTool(toolName: string, args: Record<string, any>): Record<string, any> {
    const engine = (globalThis as any).MQEngine;
    if (!engine) {
        return { ok: false, error: 'MQEngine bridge is not initialized' };
    }

    switch (toolName) {
        case 'mq_get_engine_status':
            return engine.getEngineStatus();
        case 'mq_list_plugins':
            return engine.listPlugins();
        case 'mq_set_play_mode':
            return engine.setPlayMode(!!args.running);
        case 'mq_list_scenes':
            return engine.listScenes();
        case 'mq_open_scene':
            return engine.openScene(args.uuid);
        case 'mq_save_scene':
            return engine.saveScene(args.uuid || '');
        case 'mq_new_scene':
            return engine.newScene(args.name);
        case 'mq_list_entities':
            return engine.listEntities(args);
        case 'mq_get_entity':
            return engine.getEntity(args);
        case 'mq_create_entity':
            return engine.createEntity(args);
        case 'mq_destroy_entity':
            return engine.destroyEntity(args);
        case 'mq_list_component_types':
            return engine.listComponentTypes();
        case 'mq_get_component':
            return engine.getComponent(args);
        case 'mq_set_component':
            return engine.setComponent(args);
        case 'mq_remove_component':
            return engine.removeComponent(args);
        default:
            return { ok: false, error: 'Unknown MCP tool: ' + toolName };
    }
}

function handleJsonRpcRequest(req: JsonRpcRequest): Record<string, any> | null {
    const id = req.id !== undefined ? req.id : null;
    const method = req.method || '';
    const params = req.params || {};

    if (method === 'notifications/initialized' || method.startsWith('notifications/')) {
        return null;
    }

    if (method === 'initialize') {
        return {
            jsonrpc: '2.0',
            id: id,
            result: {
                protocolVersion: '2024-11-05',
                capabilities: {
                    tools: { listChanged: false }
                },
                serverInfo: {
                    name: 'MQEngine-MCP-Server',
                    version: '1.0.0'
                }
            }
        };
    }

    if (method === 'ping') {
        return {
            jsonrpc: '2.0',
            id: id,
            result: {}
        };
    }

    if (method === 'tools/list') {
        return {
            jsonrpc: '2.0',
            id: id,
            result: {
                tools: MCP_TOOLS
            }
        };
    }

    if (method === 'tools/call') {
        const toolName = String(params.name || '');
        const toolArgs = params.arguments || {};
        const resultObj = executeMcpTool(toolName, toolArgs);
        const isError = resultObj && resultObj.ok === false;

        return {
            jsonrpc: '2.0',
            id: id,
            result: {
                content: [
                    {
                        type: 'text',
                        text: JSON.stringify(resultObj, null, 2)
                    }
                ],
                isError: !!isError
            }
        };
    }

    return {
        jsonrpc: '2.0',
        id: id,
        error: {
            code: -32601,
            message: 'Method not found: ' + method
        }
    };
}

class McpPlugin {
    name: string;
    version: string;
    server: any;
    port: number;

    constructor() {
        this.name = 'McpPlugin';
        this.version = '1.0.0';
        this.server = null;
        this.port = Number(process.env.MQENGINE_MCP_PORT || 18080);
    }

    onLoad(): void {
        console.log('[McpPlugin] TypeScript 插件已加载');
    }

    onInit(): void {
        if (this.server) {
            return;
        }

        this.server = http.createServer((req: any, res: any) => {
            res.setHeader('Access-Control-Allow-Origin', '*');
            res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
            res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

            if (req.method === 'OPTIONS') {
                res.writeHead(204);
                res.end();
                return;
            }

            if (req.method === 'GET') {
                const status = executeMcpTool('mq_get_engine_status', {});
                res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' });
                res.end(JSON.stringify({
                    mcpServer: 'MQEngine-MCP-Server',
                    version: this.version,
                    port: this.port,
                    toolsCount: MCP_TOOLS.length,
                    engineStatus: status
                }, null, 2));
                return;
            }

            if (req.method === 'POST') {
                let body = '';
                req.on('data', (chunk: any) => {
                    body += chunk.toString();
                });
                req.on('end', () => {
                    try {
                        const parsed = JSON.parse(body);
                        const rpcResponse = handleJsonRpcRequest(parsed);
                        if (!rpcResponse) {
                            res.writeHead(202);
                            res.end();
                            return;
                        }
                        res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' });
                        res.end(JSON.stringify(rpcResponse));
                    } catch (err: any) {
                        res.writeHead(400, { 'Content-Type': 'application/json; charset=utf-8' });
                        res.end(JSON.stringify({
                            jsonrpc: '2.0',
                            id: null,
                            error: { code: -32700, message: 'Parse error: ' + String(err && err.message ? err.message : err) }
                        }));
                    }
                });
                return;
            }

            res.writeHead(405);
            res.end();
        });

        this.server.on('error', (err: any) => {
            if (err && err.code === 'EADDRINUSE' && this.port < 18090) {
                this.port += 1;
                this.server.listen(this.port, '127.0.0.1');
                return;
            }
            console.error('[McpPlugin] HTTP Server 错误:', err);
        });

        this.server.listen(this.port, '127.0.0.1', () => {
            console.log('[McpPlugin] MCP Server 已启动: http://127.0.0.1:' + this.port + '/mcp');
        });
    }

    onLogicTick(dt: number): void {
        void dt;
    }

    onRenderTick(): void {
    }

    onUnload(): void {
        if (this.server) {
            try {
                this.server.close();
            } catch (e) {}
            this.server = null;
            console.log('[McpPlugin] MCP Server 已关闭');
        }
    }
}

module.exports = new McpPlugin();
