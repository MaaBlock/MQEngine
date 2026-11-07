import { ScriptBehaviour, PositionComponent, UserRotateComponent, Vec3 } from "mqengine";

/**
 * 工程级 TypeScript 用户脚本示例（挂载到 Entity 的 ScriptFunctionTableComponent 上执行）
 */
export class UserBehaviorScript extends ScriptBehaviour {
    onTicker(): void {
        const rotateComp = this.getComponent(UserRotateComponent);
        if (rotateComp && rotateComp.enabled) {
            return;
        }
        const posComp = this.getComponent(PositionComponent);
        if (posComp) {
            const pos: Vec3 = posComp.position;
            // 可在此直接读写实体组件与 UserCode.dll 注册的组件（属性代理自动同步回 C++）
        }
    }
}

export function onUserEntityStart(): void {
    const status = MQEngine.getEngineStatus();
    console.log("[UserScript.ts] Engine running:", status.isRunning);
}
