function newRotateLightZ() {
    if (this.components.DirectionalLightComponent) {
        let lightComponent = this.components.DirectionalLightComponent;
        
        let deltaTime = engine.logicDealt;
        let rotationSpeed = 1.0;
        let rotationAngle = rotationSpeed * deltaTime;
        
        let currentDirection = lightComponent.direction;
        
        let newX = currentDirection.x * Math.cos(rotationAngle) - currentDirection.y * Math.sin(rotationAngle);
        let newY = currentDirection.x * Math.sin(rotationAngle) + currentDirection.y * Math.cos(rotationAngle);
        
        lightComponent.direction.x = newX;
        lightComponent.direction.y = newY;
        
        this.components.DirectionalLightComponent = lightComponent;
    }
}