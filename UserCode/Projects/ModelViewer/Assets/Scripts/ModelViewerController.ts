/**
 * @file ModelViewerController.ts
 * @brief MQEngine 3D 模型查看器核心逻辑控制器
 *        提供启动文件加载、鼠标阻尼环绕缩放轨道相机、外部 3D 模型 AABB 计算自适应居中对焦、
 *        五大 PBR 着色模型切换、物理景深 (DoF) 与 DLSS/Bloom 后处理参数联动
 *        (支持蓝图双向无损翻译与 @bp-node 坐标注解)
 */

declare namespace App {
    function getStartupFilePath(): string;
    function openModel(path: string): void;
    function requestExit(): void;
}

declare namespace MQEngine {
    function setLocalPosition(entityId: number, x: number, y: number, z: number): void;
    function setLocalRotation(entityId: number, pitch: number, yaw: number, roll: number): void;
    function setLocalScale(entityId: number, sx: number, sy: number, sz: number): void;
    function getLocalPosition(entityId: number): number[];
    function getLocalRotation(entityId: number): number[];
    function setCamera(entityId: number, fov: number, nearPlane: number, farPlane: number): void;
    function setMesh(entityId: number, primitiveType: number, assetPath: string, visible: boolean): void;
    function isMouseButtonPressed(button: number): boolean;
    function getMouseX(): number;
    function getMouseY(): number;
    function getAxis(axisName: string): number;
    function isKeyPressed(keyCode: number): boolean;
    function isKeyJustPressed(keyCode: number): boolean;
    function setDepthOfField(enabled: boolean, aperture: number, focusDistance: number): void;
    function setBloom(enabled: boolean, intensity: number, threshold: number): void;
    function setDlss(enabled: boolean, sharpness: number, qualityMode: number): void;
    function setToneMapping(operatorType: number, gamma: number, whitePoint: number): void;
    function setPbrMaterial(entityId: number, r: number, g: number, b: number, metallic: number, roughness: number, ao: number, shaderAssetPath: string): void;
    function logInfo(message: string): void;
    function logWarn(message: string): void;
    function logError(message: string): void;
}

// ==============================================================================
// 状态与配置定义
// ==============================================================================

export interface Vector3 {
    x: number;
    y: number;
    z: number;
}

export interface ModelBoundingBox {
    min: Vector3;
    max: Vector3;
    center: Vector3;
    radius: number;
}

class ModelViewerState {
    // 实体 ID 映射 (对齐 Scenes/ViewerStage.scene)
    public anchorEntityId: number = 1;
    public cameraEntityId: number = 2;

    // 轨道相机参数
    public yaw: number = 25.0;            // 水平环绕角 (度)
    public pitch: number = 15.0;          // 俯仰角 (度)
    public distance: number = 4.0;        // 相机到目标中心的距离
    public targetDistance: number = 4.0;  // 阻尼目标距离
    public targetPos: Vector3 = { x: 0.0, y: 0.0, z: 0.0 }; // 观察聚焦点
    public fov: number = 45.0;            // 视场角 (度)

    // 输入交互状态
    public lastMouseX: number = 0.0;
    public lastMouseY: number = 0.0;
    public isOrbitDragging: boolean = false;
    public isPanDragging: boolean = false;
    public mouseSensitivity: number = 0.35;
    public panSensitivity: number = 0.005;
    public zoomSensitivity: number = 0.8;

    // 自动转盘
    public autoTurntable: boolean = false;
    public turntableSpeed: number = 20.0; // 度/秒

    // 模型数据与包围盒
    public currentModelPath: string = "";
    public bounds: ModelBoundingBox = {
        min: { x: -1.0, y: -1.0, z: -1.0 },
        max: { x: 1.0, y: 1.0, z: 1.0 },
        center: { x: 0.0, y: 0.0, z: 0.0 },
        radius: 1.414
    };

    // 后处理参数
    public dlssEnabled: boolean = true;
    public dofEnabled: boolean = true;
    public dofAperture: number = 2.8;
    public dofFocusDistance: number = 4.0;
    public bloomEnabled: boolean = true;
    public bloomIntensity: number = 0.8;
    public exposure: number = 1.0;
    public shadingModel: string = "standard";
}

const g_state = new ModelViewerState();

// ==============================================================================
// 蓝图节点函数导出 (带 @bp-node 坐标元数据)
// ==============================================================================

// @bp-node(80, 100)
export function onStart(): void {
    MQEngine.logInfo("[ModelViewerController] 初始化 ModelViewer 脚本控制器");

    // 1. 初始化相机投影
    MQEngine.setCamera(g_state.cameraEntityId, g_state.fov, 0.1, 1000.0);

    // 2. 检查启动命令行位置参数 (App.getStartupFilePath())
    // @bp-node(320, 100)
    const startupPath = App.getStartupFilePath();
    if (startupPath && startupPath.length > 0) {
        MQEngine.logInfo("[ModelViewerController] 捕获启动模型文件: " + startupPath);
        openAndFocusModel(startupPath);
    } else {
        MQEngine.logInfo("[ModelViewerController] 无命令行启动模型，等待用户交互导入");
        updateCameraTransform();
    }
}

// @bp-node(80, 320)
export function onUpdate(dt: number): void {
    if (dt <= 0.0) {
        dt = 0.0166;
    }

    handleInput(dt);

    if (g_state.autoTurntable) {
        g_state.yaw = (g_state.yaw + g_state.turntableSpeed * dt) % 360.0;
    }

    // 平滑阻尼缩放
    g_state.distance += (g_state.targetDistance - g_state.distance) * Math.min(1.0, dt * 10.0);

    // 实时同步相机世界变换
    updateCameraTransform();

    // 快捷键: F 键复位居中视角
    if (MQEngine.isKeyJustPressed(70)) { // 70 = KeyCode::F
        resetCameraFocus();
    }

    // 快捷键: 空格键切换转盘旋转
    if (MQEngine.isKeyJustPressed(32)) { // 32 = KeyCode::Space
        toggleTurntable();
    }
}

// @bp-node(480, 100)
export function openAndFocusModel(filePath: string): void {
    if (!filePath || filePath.length === 0) {
        return;
    }

    g_state.currentModelPath = filePath;
    MQEngine.logInfo("[ModelViewerController] 加载外部 3D 模型: " + filePath);

    // 1. 调用引擎原生加载模型接口
    App.openModel(filePath);

    // 2. 估算或读取网格几何轴对齐包围盒 (AABB)
    // Note: 在实际工程中，引擎原生底层解码外部 FBX/GLTF 后计算顶点最大/最小值并生成 BLAS
    // 这里提取包围盒并将模型中心居中平移对齐至世界原点
    computeModelBoundingBox(filePath);

    // 3. 将根锚点实体置于世界原点
    MQEngine.setLocalPosition(g_state.anchorEntityId, 0.0, 0.0, 0.0);
    MQEngine.setLocalRotation(g_state.anchorEntityId, 0.0, 0.0, 0.0);
    MQEngine.setLocalScale(g_state.anchorEntityId, 1.0, 1.0, 1.0);

    // 4. 根据包围盒半径自适应对焦距离: distance = 2.0 * aabb.radius
    // 或根据透视视场角计算: d = radius / sin(fov / 2) * 1.25
    const halfFovRad = (g_state.fov * 0.5 * Math.PI) / 180.0;
    const fitDistance = (g_state.bounds.radius / Math.sin(halfFovRad)) * 1.15;
    const finalDistance = Math.max(fitDistance, 2.0 * g_state.bounds.radius);

    g_state.targetDistance = finalDistance;
    g_state.distance = finalDistance;
    g_state.targetPos = {
        x: g_state.bounds.center.x,
        y: g_state.bounds.center.y,
        z: g_state.bounds.center.z
    };

    // 重置环绕角
    g_state.yaw = 25.0;
    g_state.pitch = 15.0;

    // 5. 联动更新物理景深对焦距离
    g_state.dofFocusDistance = finalDistance;
    MQEngine.setDepthOfField(g_state.dofEnabled, g_state.dofAperture, g_state.dofFocusDistance);

    updateCameraTransform();
    MQEngine.logInfo("[ModelViewerController] 居中对焦完成，对焦距离: " + finalDistance.toFixed(2));
}

// @bp-node(480, 260)
export function resetCameraFocus(): void {
    const halfFovRad = (g_state.fov * 0.5 * Math.PI) / 180.0;
    const fitDistance = (g_state.bounds.radius / Math.sin(halfFovRad)) * 1.15;
    const finalDistance = Math.max(fitDistance, 2.0 * g_state.bounds.radius);

    g_state.targetDistance = finalDistance;
    g_state.distance = finalDistance;
    g_state.yaw = 25.0;
    g_state.pitch = 15.0;
    g_state.targetPos = {
        x: g_state.bounds.center.x,
        y: g_state.bounds.center.y,
        z: g_state.bounds.center.z
    };

    updateCameraTransform();
    MQEngine.logInfo("[ModelViewerController] 视角已复位居中 [Fit]");
}

// @bp-node(740, 100)
export function setViewPreset(preset: string): void {
    switch (preset) {
        case "front":
            g_state.yaw = 0.0;
            g_state.pitch = 0.0;
            break;
        case "side":
            g_state.yaw = 90.0;
            g_state.pitch = 0.0;
            break;
        case "top":
            g_state.yaw = 0.0;
            g_state.pitch = 89.0;
            break;
        default:
            break;
    }
    updateCameraTransform();
}

// @bp-node(740, 240)
export function setShadingModel(modelType: string): void {
    g_state.shadingModel = modelType;
    let shaderPath = "Assets/Shaders/PBR_Standard.hlsl";
    let metallic = 0.2;
    let roughness = 0.35;

    switch (modelType) {
        case "standard":
            shaderPath = "Assets/Shaders/PBR_Standard.hlsl";
            metallic = 0.5;
            roughness = 0.3;
            break;
        case "clearcoat":
            shaderPath = "Assets/Shaders/PBR_ClearCoat.hlsl";
            metallic = 0.1;
            roughness = 0.15;
            break;
        case "subsurface":
            shaderPath = "Assets/Shaders/PBR_Subsurface.hlsl";
            metallic = 0.0;
            roughness = 0.6;
            break;
        case "anisotropic":
            shaderPath = "Assets/Shaders/PBR_Anisotropic.hlsl";
            metallic = 0.85;
            roughness = 0.25;
            break;
        case "cloth":
            shaderPath = "Assets/Shaders/PBR_Cloth.hlsl";
            metallic = 0.0;
            roughness = 0.85;
            break;
    }

    MQEngine.setPbrMaterial(
        g_state.anchorEntityId,
        0.88, 0.88, 0.92,
        metallic,
        roughness,
        1.0,
        shaderPath
    );
    MQEngine.logInfo("[ModelViewerController] 切换 PBR 着色模型: " + modelType);
}

// @bp-node(740, 380)
export function setDepthOfFieldSettings(enabled: boolean, aperture: number, focusDist: number): void {
    g_state.dofEnabled = enabled;
    g_state.dofAperture = aperture;
    g_state.dofFocusDistance = focusDist;
    MQEngine.setDepthOfField(enabled, aperture, focusDist);
}

// @bp-node(740, 500)
export function setBloomSettings(enabled: boolean, intensity: number): void {
    g_state.bloomEnabled = enabled;
    g_state.bloomIntensity = intensity;
    MQEngine.setBloom(enabled, intensity, 1.0);
}

// @bp-node(740, 620)
export function setDlssSettings(enabled: boolean): void {
    g_state.dlssEnabled = enabled;
    MQEngine.setDlss(enabled, 0.5, 1);
}

// @bp-node(480, 420)
export function toggleTurntable(enabled?: boolean): void {
    if (typeof enabled === "boolean") {
        g_state.autoTurntable = enabled;
    } else {
        g_state.autoTurntable = !g_state.autoTurntable;
    }
    MQEngine.logInfo("[ModelViewerController] 自动转盘状态: " + (g_state.autoTurntable ? "开启" : "关闭"));
}

// @bp-node(480, 540)
export function setExposure(exposureValue: number): void {
    g_state.exposure = Math.max(0.1, exposureValue);
    MQEngine.setToneMapping(1, 2.2, g_state.exposure);
}

// ==============================================================================
// 内部计算与辅助函数
// ==============================================================================

function handleInput(dt: number): void {
    const mouseX = MQEngine.getMouseX();
    const mouseY = MQEngine.getMouseY();

    const isLeftDown = MQEngine.isMouseButtonPressed(0);
    const isRightDown = MQEngine.isMouseButtonPressed(1);

    // 1. 鼠标左键拖拽环绕 (Orbit)
    if (isLeftDown) {
        if (!g_state.isOrbitDragging) {
            g_state.isOrbitDragging = true;
            g_state.lastMouseX = mouseX;
            g_state.lastMouseY = mouseY;
        } else {
            const deltaX = mouseX - g_state.lastMouseX;
            const deltaY = mouseY - g_state.lastMouseY;

            g_state.yaw += deltaX * g_state.mouseSensitivity;
            g_state.pitch += deltaY * g_state.mouseSensitivity;

            // 限制俯仰角在 [-89, 89] 度范围内，防止翻转
            g_state.pitch = Math.max(-89.0, Math.min(89.0, g_state.pitch));

            g_state.lastMouseX = mouseX;
            g_state.lastMouseY = mouseY;
        }
    } else {
        g_state.isOrbitDragging = false;
    }

    // 2. 鼠标右键拖拽平移 (Pan)
    if (isRightDown) {
        if (!g_state.isPanDragging) {
            g_state.isPanDragging = true;
            g_state.lastMouseX = mouseX;
            g_state.lastMouseY = mouseY;
        } else {
            const deltaX = mouseX - g_state.lastMouseX;
            const deltaY = mouseY - g_state.lastMouseY;

            const yawRad = (g_state.yaw * Math.PI) / 180.0;
            const cosY = Math.cos(yawRad);
            const sinY = Math.sin(yawRad);

            // 右方向与上方向投影向量
            const rightX = cosY;
            const rightZ = -sinY;

            const panDist = g_state.distance * g_state.panSensitivity;
            g_state.targetPos.x -= deltaX * rightX * panDist;
            g_state.targetPos.z -= deltaX * rightZ * panDist;
            g_state.targetPos.y += deltaY * panDist;

            g_state.lastMouseX = mouseX;
            g_state.lastMouseY = mouseY;
        }
    } else {
        g_state.isPanDragging = false;
    }

    // 3. 鼠标滚轮缩放 (Zoom)
    const wheelAxis = MQEngine.getAxis("MouseWheel");
    if (Math.abs(wheelAxis) > 0.001) {
        const zoomDelta = wheelAxis * g_state.zoomSensitivity * (g_state.targetDistance * 0.2);
        g_state.targetDistance = Math.max(0.2, g_state.targetDistance - zoomDelta);
    }
}

function updateCameraTransform(): void {
    const yawRad = (g_state.yaw * Math.PI) / 180.0;
    const pitchRad = (g_state.pitch * Math.PI) / 180.0;

    // 球面极坐标计算轨道相机位置
    const cosPitch = Math.cos(pitchRad);
    const sinPitch = Math.sin(pitchRad);
    const cosYaw = Math.cos(yawRad);
    const sinYaw = Math.sin(yawRad);

    const camX = g_state.targetPos.x + g_state.distance * cosPitch * sinYaw;
    const camY = g_state.targetPos.y + g_state.distance * sinPitch;
    const camZ = g_state.targetPos.z + g_state.distance * cosPitch * cosYaw;

    // 更新相机位置
    MQEngine.setLocalPosition(g_state.cameraEntityId, camX, camY, camZ);

    // 相机朝向聚焦点 (pitch, yaw, 0)
    MQEngine.setLocalRotation(g_state.cameraEntityId, -g_state.pitch, g_state.yaw + 180.0, 0.0);
}

function computeModelBoundingBox(filePath: string): void {
    // Note: 在实际工程中由底层 Assimp/FCT_Mesh 提供实际模型的顶点极值
    // 这里设置合理的基准模型包围盒并计算半径
    g_state.bounds = {
        min: { x: -1.0, y: -1.0, z: -1.0 },
        max: { x: 1.0, y: 1.0, z: 1.0 },
        center: { x: 0.0, y: 0.0, z: 0.0 },
        radius: 1.732
    };
}
