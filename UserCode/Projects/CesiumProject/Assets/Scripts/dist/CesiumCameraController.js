"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.CesiumCameraController = void 0;
const mqengine_1 = require("mqengine");

const KEY_W = 87;
const KEY_A = 65;
const KEY_S = 83;
const KEY_D = 68;
const KEY_Q = 81;
const KEY_E = 69;
const KEY_O = 79;
const KEY_1 = 49;
const KEY_2 = 50;
const KEY_3 = 51;
const KEY_SPACE = 32;
const KEY_LEFT = 37;
const KEY_UP = 38;
const KEY_RIGHT = 39;
const KEY_DOWN = 40;

class CesiumCameraController extends mqengine_1.ScriptBehaviour {
    constructor() {
        super(...arguments);
        this.moveSpeed = 120.0;
        this.lookSpeedDeg = 65.0;
        this.mouseSensitivity = 0.22;
        this.orbitMode = false;
        this.orbitSpeedDeg = 12.0;
        this.orbitRadius = 420.0;
        this.orbitHeight = 260.0;
        this.orbitAngleDeg = 90.0;
        this.prevOrbitKeyDown = false;
    }

    onStart() {
        mqengine_1.DataManager.setActiveCamera("default", this.entity.id);

        const cam = this.getComponent(mqengine_1.CameraComponent);
        if (cam) {
            cam.active = true;
            cam.fov = 60.0;
            cam.nearPlane = 0.5;
            cam.farPlane = 20000000.0;
            cam.reverseZ = true;
        }

        const parentId = this.getParent("default");
        const coords = this.getCoordinatesSummary();
        console.log(
            `[CesiumCameraController] 摄像机已启动 | entity=${this.entity.id}, parent=${parentId}, reverseZ=true, far=20000000 | ` +
            `Engine=(${coords.engine.x.toFixed(2)}, ${coords.engine.y.toFixed(2)}, ${coords.engine.z.toFixed(2)}) | ` +
            `EUS=(${coords.eus.x.toFixed(2)}, ${coords.eus.y.toFixed(2)}, ${coords.eus.z.toFixed(2)}) | ` +
            `ECEF=(${coords.ecef.x.toFixed(2)}, ${coords.ecef.y.toFixed(2)}, ${coords.ecef.z.toFixed(2)}) | ` +
            `LonLatH=(${coords.cartographic.x.toFixed(6)}, ${coords.cartographic.y.toFixed(6)}, ${coords.cartographic.z.toFixed(2)})`
        );

        globalThis.cesiumCamera = this;
    }

    onUpdate(dt) {
        const clampedDt = Math.min(Math.max(dt, 0.0001), 0.1);

        this.handlePresetHotkeys();
        this.handleSpeedWheel();
        this.handleOrbitToggle();

        if (this.orbitMode) {
            this.updateOrbitMotion(clampedDt);
            return;
        }

        this.handleLookRotation(clampedDt);
        this.handleFlightMovement(clampedDt);
    }

    handleOrbitToggle() {
        const isODown = mqengine_1.Input.isKeyPressed(KEY_O);
        if (isODown && !this.prevOrbitKeyDown) {
            this.orbitMode = !this.orbitMode;
            console.log(`[CesiumCameraController] 环绕模式切换: ${this.orbitMode ? "开启" : "关闭"}`);
        }
        this.prevOrbitKeyDown = isODown;
    }

    handlePresetHotkeys() {
        if (mqengine_1.Input.isKeyPressed(KEY_1)) {
            this.orbitMode = false;
            this.flyTo(0.0, 95.0, 180.0, -18.0, -90.0);
        } else if (mqengine_1.Input.isKeyPressed(KEY_2)) {
            this.orbitMode = false;
            this.flyTo(0.0, 260.0, 420.0, -28.0, -90.0);
        } else if (mqengine_1.Input.isKeyPressed(KEY_3)) {
            this.orbitMode = false;
            this.flyTo(0.0, 680.0, 900.0, -38.0, -90.0);
        }
    }

    handleSpeedWheel() {
        const wheel = mqengine_1.Input.getMouseWheelDelta();
        if (wheel !== 0) {
            const factor = wheel > 0 ? 1.25 : 0.8;
            this.moveSpeed = Math.min(Math.max(this.moveSpeed * factor, 1.0), 500000.0);
        }
    }

    handleLookRotation(dt) {
        const rotComp = this.getComponent(mqengine_1.RotationComponent);
        if (!rotComp) {
            return;
        }

        let pitch = rotComp.rotation.x;
        let yaw = rotComp.rotation.y;
        let changed = false;

        if (mqengine_1.Input.isKeyPressed(KEY_LEFT)) {
            yaw -= this.lookSpeedDeg * dt;
            changed = true;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_RIGHT)) {
            yaw += this.lookSpeedDeg * dt;
            changed = true;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_UP)) {
            pitch += this.lookSpeedDeg * dt;
            changed = true;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_DOWN)) {
            pitch -= this.lookSpeedDeg * dt;
            changed = true;
        }

        if (mqengine_1.Input.isRightMousePressed()) {
            const delta = mqengine_1.Input.getMouseDelta();
            if (delta && (Math.abs(delta.x) > 0.001 || Math.abs(delta.y) > 0.001)) {
                yaw += delta.x * this.mouseSensitivity;
                pitch -= delta.y * this.mouseSensitivity;
                changed = true;
            }
        }

        if (changed) {
            pitch = Math.min(Math.max(pitch, -89.0), 89.0);
            this.rotation = { x: pitch, y: yaw, z: 0.0 };
        }
    }

    handleFlightMovement(dt) {
        const posComp = this.getComponent(mqengine_1.PositionComponent);
        const rotComp = this.getComponent(mqengine_1.RotationComponent);
        if (!posComp || !rotComp) {
            return;
        }

        const deg2Rad = Math.PI / 180.0;
        const pitchRad = rotComp.rotation.x * deg2Rad;
        const yawRad = rotComp.rotation.y * deg2Rad;

        const fwdX = Math.cos(yawRad) * Math.cos(pitchRad);
        const fwdY = Math.sin(pitchRad);
        const fwdZ = Math.sin(yawRad) * Math.cos(pitchRad);

        const rightX = -Math.sin(yawRad);
        const rightZ = Math.cos(yawRad);

        let moveX = 0.0;
        let moveY = 0.0;
        let moveZ = 0.0;

        if (mqengine_1.Input.isKeyPressed(KEY_W)) {
            moveX += fwdX;
            moveY += fwdY;
            moveZ += fwdZ;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_S)) {
            moveX -= fwdX;
            moveY -= fwdY;
            moveZ -= fwdZ;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_D)) {
            moveX += rightX;
            moveZ += rightZ;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_A)) {
            moveX -= rightX;
            moveZ -= rightZ;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_E) || mqengine_1.Input.isKeyPressed(KEY_SPACE)) {
            moveY += 1.0;
        }
        if (mqengine_1.Input.isKeyPressed(KEY_Q)) {
            moveY -= 1.0;
        }

        const len = Math.hypot(moveX, moveY, moveZ);
        if (len > 1e-5) {
            const step = (this.moveSpeed * dt) / len;
            this.position = {
                x: posComp.position.x + moveX * step,
                y: posComp.position.y + moveY * step,
                z: posComp.position.z + moveZ * step
            };
        }
    }

    updateOrbitMotion(dt) {
        this.orbitAngleDeg += this.orbitSpeedDeg * dt;
        const rad = (this.orbitAngleDeg * Math.PI) / 180.0;
        const x = Math.cos(rad) * this.orbitRadius;
        const z = Math.sin(rad) * this.orbitRadius;
        this.position = { x, y: this.orbitHeight, z };
        this.lookAt({ x: 0.0, y: 40.0, z: 0.0 }, "default");
    }

    flyTo(x, y, z, pitch = -28.0, yaw = -90.0) {
        this.setPositionEngine({ x, y, z }, "default");
        this.rotation = { x: pitch, y: yaw, z: 0.0 };
    }

    flyToEngine(x, y, z, pitch = -28.0, yaw = -90.0) {
        this.setPositionEngine({ x, y, z }, "default");
        this.rotation = { x: pitch, y: yaw, z: 0.0 };
    }

    flyToEus(east, up, south, pitch = -28.0, yaw = -90.0) {
        this.setPositionEus({ x: east, y: up, z: south }, "default");
        this.rotation = { x: pitch, y: yaw, z: 0.0 };
    }

    flyToEcef(x, y, z, pitch = -28.0, yaw = -90.0) {
        this.setPositionEcef({ x, y, z }, "default");
        this.rotation = { x: pitch, y: yaw, z: 0.0 };
    }

    flyToCartographic(longitude, latitude, height = 150.0, pitch = -28.0, yaw = -90.0) {
        this.setCartographic(longitude, latitude, height, "default");
        this.rotation = { x: pitch, y: yaw, z: 0.0 };
    }

    getCoordinatesSummary() {
        return {
            engine: this.getPositionEngine("default"),
            eus: this.getPositionEus("default"),
            ecef: this.getPositionEcef("default"),
            cartographic: this.getCartographic("default"),
            georeferenceOrigin: mqengine_1.CesiumGeoreferenceService.getGeoreferenceOrigin("default")
        };
    }

    setReverseZ(enabled) {
        const cam = this.getComponent(mqengine_1.CameraComponent);
        if (cam) {
            cam.reverseZ = enabled;
        }
    }

    setClipPlanes(nearPlane, farPlane) {
        const cam = this.getComponent(mqengine_1.CameraComponent);
        if (cam) {
            cam.nearPlane = Math.max(0.01, nearPlane);
            cam.farPlane = Math.max(cam.nearPlane + 1.0, farPlane);
        }
    }

    lookAtTarget(x, y, z) {
        this.lookAt({ x, y, z }, "default");
    }

    setOrbitMode(enabled, speedDegPerSec = 12.0) {
        this.orbitMode = enabled;
        this.orbitSpeedDeg = speedDegPerSec;
    }

    setFov(fovDeg) {
        const cam = this.getComponent(mqengine_1.CameraComponent);
        if (cam) {
            cam.fov = Math.min(Math.max(fovDeg, 15.0), 120.0);
        }
    }

    setGeoreference(longitude, latitude, height = 50.0) {
        return mqengine_1.CesiumGeoreferenceService.setGeoreferenceOrigin("default", longitude, latitude, height);
    }
}
exports.CesiumCameraController = CesiumCameraController;
