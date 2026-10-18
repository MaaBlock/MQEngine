function testRotation() {
    if (entityInfo.hasComponent("RotationComponent")) {
        let rotationComponent = entityInfo.getComponent("RotationComponent");
        rotationComponent.rotation.y += engine.logicDealt * 10;
        entityInfo.setComponent("RotationComponent", rotationComponent);
    } else {
        console.log("实体没有 RotationComponent 组件.");
    }
}