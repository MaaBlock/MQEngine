import { ScriptBehaviour, PositionComponent, Vec3 } from "mqengine";

export class UserBehaviorScript extends ScriptBehaviour {
    onTicker(): void {
        const posComp = this.getComponent(PositionComponent);
        if (posComp) {
            const pos: Vec3 = posComp.position;
            // 工程级用户脚本逻辑（属性代理自动同步回 C++）
        }
    }
}
