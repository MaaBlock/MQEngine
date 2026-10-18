function rotateLightZ() {
    if (entityInfo.hasComponent("DirectionalLightComponent")) {
        let lightComponent = entityInfo.getComponent("DirectionalLightComponent");
        
        let deltaTime = engine.logicDealt;
        
        let rotationSpeed = 1.0;
        
        let rotationAngle = rotationSpeed * deltaTime;
        
        let currentDirection = lightComponent.direction;
        
        let newX = currentDirection.x * Math.cos(rotationAngle) - currentDirection.y * Math.sin(rotationAngle);
        let newY = currentDirection.x * Math.sin(rotationAngle) + currentDirection.y * Math.cos(rotationAngle);
        
        lightComponent.direction.x = newX;
        lightComponent.direction.y = newY;
        
        entityInfo.setComponent("DirectionalLightComponent", lightComponent);
    } else {
        console.log("实体没有 DirectionalLightComponent 组件.");
    }
}