import { ScriptBehaviour } from "mqengine";

export class SpinBehaviour extends ScriptBehaviour {
    speed: number = 45.0;

    onStart(): void {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }

    onUpdate(dt: number): void {
        const rot = this.getComponent("RotationComponent");
        if (rot) {
            rot.rotation.y += this.speed * dt;
            this.setComponent("RotationComponent", rot);
        }
    }
}
