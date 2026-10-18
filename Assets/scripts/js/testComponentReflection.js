function testEntityInfoFunctions() {
    console.log("开始测试entityInfo成员函数...");

    console.log("\n=== 测试hasComponent ===");
    if (entityInfo.hasComponent("NameTag")) {
        console.log("实体拥有NameTag组件");
    } else {
        console.log("实体没有NameTag组件");
    }

    const testComponents = ["PositionComponent", "MeshComponent"];
    for (const componentName of testComponents) {
        if (entityInfo.hasComponent(componentName)) {
            console.log(`实体拥有${componentName}组件`);
        } else {
            console.log(`实体没有${componentName}组件`);
        }
    }

    
    console.log("\n=== 测试getComponentField ===");
    if (entityInfo.hasComponent("NameTag")) {
        console.log("获取NameTag组件的name字段...");
        let nameValue = entityInfo.getComponentField("NameTag", "name");
        console.log("NameTag.name = ", nameValue);
    }

    console.log("\n=== 测试getComponent ===");
    if (entityInfo.hasComponent("NameTag")) {
        console.log("获取完整的NameTag组件...");
        let nameTagComponent = entityInfo.getComponent("NameTag");
        console.log("NameTag组件内容:", nameTagComponent);
    }

    console.log("\n=== 测试addComponent ===");
    if (!entityInfo.hasComponent("NameTag")) {
        console.log("尝试添加NameTag组件...");
        if (entityInfo.addComponent("NameTag")) {
            console.log("成功添加NameTag组件");
        } else {
            console.log("添加NameTag组件失败");
        }
    }


    console.log("\n=== 测试setComponentField ===");
    if (entityInfo.hasComponent("NameTag")) {
        console.log("设置NameTag组件的name字段为'TestEntity'...");
        if (entityInfo.setComponentField("NameTag", "name", "TestEntity")) {
            console.log("成功设置NameTag.name字段");
        } else {
            console.log("设置NameTag.name字段失败");
        }
    }

    console.log("\n=== 测试setComponent ===");
    if (entityInfo.hasComponent("NameTag")) {
        console.log("设置完整的NameTag组件...");
        newNameTag = entityInfo.getComponent("NameTag");
        newNameTag.name = "testName"
        if (entityInfo.setComponent("NameTag", newNameTag)) {
            console.log("成功设置完整的NameTag组件");
            
            let updatedComponent = entityInfo.getComponent("NameTag");
            console.log("更新后的NameTag组件:", updatedComponent);
        } else {
            console.log("设置完整的NameTag组件失败");
        }
    }

    console.log("\n=== 测试其他组件 ===");
    for (const componentName of testComponents) {
        if (entityInfo.hasComponent(componentName)) {
            console.log(`获取${componentName}组件...`);
            let component = entityInfo.getComponent(componentName);
            console.log(`${componentName}组件内容:`, component);
            
            // 如果是PositionComponent，尝试修改position字段
            if (componentName === "PositionComponent" && component) {
                console.log("设置PositionComponent.x字段...");
                if (entityInfo.setComponentField("PositionComponent", "x", 100.0)) {
                    console.log("成功设置PositionComponent.x");
                    
                    // 验证设置
                    let xValue = entityInfo.getComponentField("PositionComponent", "x");
                    console.log("新的x值:", xValue);
                } else {
                    console.log("设置PositionComponent.x失败");
                }
            }
        }
    }

    console.log("\n=== 测试removeComponent（可选） ===");
    /* if (entityInfo.hasComponent("NameTag")) {
         console.log("移除NameTag组件...");
         if (entityInfo.removeComponent("NameTag")) {
             console.log("成功移除NameTag组件");
         } else {
             console.log("移除NameTag组件失败");
         }
     }*/

    console.log("\n=== entityInfo成员函数测试完成 ===");
}