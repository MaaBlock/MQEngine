"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SpinBehaviour = void 0;
const mqengine_1 = require("mqengine");
class SpinBehaviour extends mqengine_1.ScriptBehaviour {
    constructor() {
        super(...arguments);
        this.speed = 45.0;
    }
    onStart() {
        console.log(`[SpinBehaviour] 实体 ${this.entity.id} 脚本已启动`);
    }
    onUpdate(dt) {
        const rot = this.getComponent("RotationComponent");
        if (rot) {
            rot.rotation.y += this.speed * dt;
            this.setComponent("RotationComponent", rot);
        }
    }
}
exports.SpinBehaviour = SpinBehaviour;
