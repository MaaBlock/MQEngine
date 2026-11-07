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
        name: 'mq_eval_js',
        description: '在 MQEngine 引擎内置 JS 运行时中直接执行 JavaScript 代码（可直接调用 engine.getProjectDir()、engine.getAssetRoot()、MQEngine.DataManager、MQEngine.StateMachine、MQEngine.ProjectManager、entityInfo 等真实 C++ 反射绑定对象）',
        inputSchema: {
            type: 'object',
            properties: {
                code: { type: 'string', description: '要在引擎内执行的 JS 表达式或代码片段' }
            },
            required: ['code']
        }
    },
    {
        name: 'mq_get_engine_status',
        description: '获取 MQEngine 引擎状态机状态、当前工程目录 (engine.getProjectDir()) 与活动场景概览',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_list_plugins',
        description: '列出引擎当前已注册或加载的所有插件与 UserCode 模块',
        inputSchema: {
            type: 'object',
            properties: {}
        }
    },
    {
        name: 'mq_set_play_mode',
        description: '控制引擎状态机进入或退出运行模式 (engine.setPlayMode)',
        inputSchema: {
            type: 'object',
            properties: {
                running: { type: 'boolean', description: 'true 为开始运行，false 为返回编辑态' }
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
        description: '根据场景 UUID 打开并切换到指定场景 (engine.openScene)',
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
        description: '保存当前打开的场景或指定 UUID 的场景 (engine.saveScene)',
        inputSchema: {
            type: 'object',
            properties: {
                uuid: { type: 'string', description: '可选，场景 UUID；留空则保存当前活动场景' }
            }
        }
    },
    {
        name: 'mq_new_scene',
        description: '创建新场景 (engine.newScene)',
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
    },
    {
        name: 'mq_capture_screenshot',
        description: '截取当前帧指定 RenderGraph 渲染目标（如 SceneColorTarget、mainWindowColor、RetTarget、PosTarget）保存为 PNG 文件，或查询最近一次截图统计信息',
        inputSchema: {
            type: 'object',
            properties: {
                outputPath: { type: 'string', description: 'PNG 输出路径（若为空则仅返回最近一次截图统计信息）' },
                targetName: { type: 'string', description: '可选，RenderGraph 目标名称，默认 SceneColorTarget（支持逗号分隔多目标）' }
            }
        }
    }
];

function executeMcpTool(toolName: string, args: Record<string, any>): Record<string, any> {
    const engine = (globalThis as any).engine || ((globalThis as any).MQEngine && (globalThis as any).MQEngine.Engine);
    const mq = (globalThis as any).MQEngine || {};
    const dm = mq.DataManager;
    const refl = mq.ComponentReflection;
    const stateMachine = mq.StateMachine;
    const entityInfo = (globalThis as any).entityInfo;

    if (!engine) {
        return { ok: false, error: 'engine instance is not bound in globalThis' };
    }

    try {
        switch (toolName) {
            case 'mq_eval_js': {
                const code = String(args.code || '');
                const result = (0, eval)(code);
                return { ok: true, result: result };
            }
            case 'mq_get_engine_status': {
                return {
                    ok: true,
                    engineState: engine.getEngineState(),
                    hostMode: stateMachine ? stateMachine.getHostModeName() : '',
                    isPlaying: engine.isPlaying(),
                    isProjectOpen: engine.isProjectOpen(),
                    projectName: engine.getProjectName(),
                    projectDir: engine.getProjectDir(),
                    projectFilePath: engine.getProjectFilePath(),
                    assetRoot: engine.getAssetRoot(),
                    currentSceneUuid: engine.getCurrentSceneUuid(),
                    currentSceneName: engine.getCurrentSceneName(),
                    loadedTrunks: dm ? dm.getLoadedTrunkNames() : []
                };
            }
            case 'mq_list_plugins': {
                const jsPlugins = Object.keys((globalThis as any).__mqPlugins || {});
                return {
                    ok: true,
                    plugins: engine.getPluginNames(),
                    userCodeModules: engine.getUserCodeNames(),
                    jsPlugins: jsPlugins
                };
            }
            case 'mq_set_play_mode': {
                engine.setPlayMode(!!args.running);
                return {
                    ok: true,
                    isPlaying: engine.isPlaying(),
                    engineState: engine.getEngineState()
                };
            }
            case 'mq_list_scenes': {
                return {
                    ok: true,
                    currentSceneUuid: engine.getCurrentSceneUuid(),
                    scenes: engine.getSceneUuidToNameMap()
                };
            }
            case 'mq_open_scene': {
                const ok = engine.openScene(String(args.uuid || ''));
                return {
                    ok: ok,
                    currentSceneUuid: engine.getCurrentSceneUuid(),
                    currentSceneName: engine.getCurrentSceneName()
                };
            }
            case 'mq_save_scene': {
                const ok = engine.saveScene(String(args.uuid || ''));
                return {
                    ok: ok,
                    currentSceneUuid: engine.getCurrentSceneUuid()
                };
            }
            case 'mq_new_scene': {
                const ok = engine.newScene(String(args.name || ''));
                return {
                    ok: ok,
                    scenes: engine.getSceneUuidToNameMap()
                };
            }
            case 'mq_list_entities': {
                if (!dm || !refl || !entityInfo) {
                    return { ok: false, error: 'DataManager or ComponentReflection is not bound' };
                }
                const filterTrunk = String(args.trunk || '');
                const trunkNames: Array<string> = filterTrunk ? [filterTrunk] : dm.getLoadedTrunkNames();
                const compNames: Array<string> = refl.getRegisteredComponentNames();
                const entities: Array<Record<string, any>> = [];

                for (const trunk of trunkNames) {
                    const regAddr = dm.getTrunkRegistryAddress(trunk);
                    if (!regAddr) continue;
                    const ids: Array<number> = dm.getTrunkEntityIds(trunk);
                    for (const id of ids) {
                        const attached: Array<string> = [];
                        for (const compName of compNames) {
                            if (entityInfo.hasComponentById(regAddr, id, compName)) {
                                attached.push(compName);
                            }
                        }
                        const nameTag = entityInfo.getComponentById(regAddr, id, 'NameTag');
                        entities.push({
                            entityId: id,
                            trunk: trunk,
                            name: nameTag && nameTag.name ? nameTag.name : '',
                            components: attached
                        });
                    }
                }
                return { ok: true, entities: entities };
            }
            case 'mq_get_entity': {
                if (!dm || !refl || !entityInfo) {
                    return { ok: false, error: 'DataManager or ComponentReflection is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const entityId = Number(args.entityId);
                const regAddr = dm.getTrunkRegistryAddress(trunk);
                if (!regAddr) {
                    return { ok: false, error: 'Trunk not loaded: ' + trunk };
                }
                const compNames: Array<string> = refl.getRegisteredComponentNames();
                const components: Record<string, any> = {};
                for (const compName of compNames) {
                    if (entityInfo.hasComponentById(regAddr, entityId, compName)) {
                        components[compName] = entityInfo.getComponentById(regAddr, entityId, compName);
                    }
                }
                return {
                    ok: true,
                    entityId: entityId,
                    trunk: trunk,
                    components: components
                };
            }
            case 'mq_create_entity': {
                if (!dm || !entityInfo) {
                    return { ok: false, error: 'DataManager is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const name = String(args.name || '');
                const entityId = dm.createEntity(trunk, name);
                const regAddr = dm.getTrunkRegistryAddress(trunk);
                if (regAddr && args.components && typeof args.components === 'object') {
                    for (const compName of Object.keys(args.components)) {
                        entityInfo.setComponentById(regAddr, entityId, compName, args.components[compName]);
                    }
                }
                return { ok: true, entityId: entityId, trunk: trunk, name: name };
            }
            case 'mq_destroy_entity': {
                if (!dm) {
                    return { ok: false, error: 'DataManager is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const entityId = Number(args.entityId);
                const ok = dm.destroyEntity(trunk, entityId);
                return { ok: ok, entityId: entityId, trunk: trunk };
            }
            case 'mq_list_component_types': {
                if (!refl) {
                    return { ok: false, error: 'ComponentReflection is not bound' };
                }
                const compNames: Array<string> = refl.getRegisteredComponentNames();
                const types: Array<Record<string, any>> = [];
                for (const compName of compNames) {
                    const fieldNames: Array<string> = refl.getComponentFieldNames(compName);
                    const fields: Array<Record<string, string>> = [];
                    for (const fName of fieldNames) {
                        fields.push({
                            name: fName,
                            type: refl.getComponentFieldType(compName, fName)
                        });
                    }
                    types.push({ name: compName, fields: fields });
                }
                return { ok: true, componentTypes: types };
            }
            case 'mq_get_component': {
                if (!dm || !entityInfo) {
                    return { ok: false, error: 'DataManager is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const entityId = Number(args.entityId);
                const compName = String(args.componentName || '');
                const regAddr = dm.getTrunkRegistryAddress(trunk);
                if (!regAddr) {
                    return { ok: false, error: 'Trunk not loaded: ' + trunk };
                }
                const data = entityInfo.getComponentById(regAddr, entityId, compName);
                return { ok: data !== undefined, entityId: entityId, componentName: compName, fields: data };
            }
            case 'mq_set_component': {
                if (!dm || !entityInfo) {
                    return { ok: false, error: 'DataManager is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const entityId = Number(args.entityId);
                const compName = String(args.componentName || '');
                const regAddr = dm.getTrunkRegistryAddress(trunk);
                if (!regAddr) {
                    return { ok: false, error: 'Trunk not loaded: ' + trunk };
                }
                const ok = entityInfo.setComponentById(regAddr, entityId, compName, args.fields || {});
                return { ok: ok, entityId: entityId, componentName: compName };
            }
            case 'mq_remove_component': {
                if (!dm || !entityInfo) {
                    return { ok: false, error: 'DataManager is not bound' };
                }
                const trunk = String(args.trunk || 'default');
                const entityId = Number(args.entityId);
                const compName = String(args.componentName || '');
                const regAddr = dm.getTrunkRegistryAddress(trunk);
                if (!regAddr) {
                    return { ok: false, error: 'Trunk not loaded: ' + trunk };
                }
                const ok = entityInfo.removeComponentById(regAddr, entityId, compName);
                return { ok: ok, entityId: entityId, componentName: compName };
            }
            case 'mq_capture_screenshot': {
                const outputPath = String(args.outputPath || '');
                const targetName = String(args.targetName || 'SceneColorTarget');
                let queued = false;
                if (outputPath.length > 0 && typeof engine.requestScreenshot === 'function') {
                    queued = !!engine.requestScreenshot(outputPath, targetName);
                }
                let lastStats: Record<string, any> = {};
                if (typeof engine.getLastScreenshotStatsJson === 'function') {
                    try {
                        lastStats = JSON.parse(engine.getLastScreenshotStatsJson() || '{}');
                    } catch (e) {}
                }
                return {
                    ok: true,
                    queued: queued,
                    outputPath: outputPath,
                    targetName: targetName,
                    lastCaptureStats: lastStats
                };
            }
            default:
                return { ok: false, error: 'Unknown MCP tool: ' + toolName };
        }
    } catch (err: any) {
        return {
            ok: false,
            error: String(err && err.message ? err.message : err)
        };
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
