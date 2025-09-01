function testRotation() {
    console.log("Testing rotation component modification...");
    
    if (entityInfo.addComponent("RotationComponent")) {
        console.log("RotationComponent added successfully");
        
        let rotationComponent = entityInfo.getComponent("RotationComponent");
        console.log("Current rotation component:", rotationComponent);
        
        let newRotation = {
            rotation: {
                x: 45.0,
                y: 90.0,
                z: 180.0
            }
        };
        
        if (entityInfo.setComponent("RotationComponent", newRotation)) {
            console.log("Set rotation to (45, 90, 180) degrees");
            
            let updatedComponent = entityInfo.getComponent("RotationComponent");
            console.log("Updated rotation component:", updatedComponent);
            
            console.log("Rotation test completed successfully!");
        } else {
            console.log("Failed to set rotation component");
        }
    } else {
        console.log("Failed to add RotationComponent");
    }
}