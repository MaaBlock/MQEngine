[英文](README.md) | [文档](https://maablock.github.io/MQEngine/)

# MQEngine
MQEngine
## 构建教程
1. **克隆项目（包含子模块）**
   ```bash
   git clone --recursive https://github.com/your-username/MQEngine.git
   ```
2. **设置环境变量**
 
   **使用visual studio 2022可跳过此步** 

   - **Windows:**
   系统环境变量中添加 `VCPKG_ROOT` 变量
   - 如果使用独立的vcpkg：
      `C:/vcpkg`
     - 如果使用Visual Studio集成的vcpkg：
           `VS根目录/版本号/(Community/Professional/Enterprise)/VC/vcpkg`

   **注意：** 为避免CMake路径转义问题，建议使用正斜杠 `/` 而不是反斜杠 `\`

   - **Linux/macOS:**
   
3. **使用IDE打开**
   - **CLion:** 直接打开项目根目录，等待CMake加载及vcpkg下载库完毕后，切换到Editor/Game运行配置
   - **Visual Studio 2022:** 使用"打开文件夹"功能打开项目根目录，等待CMake配置完成后选择Editor/Game作为启动项
### CLion配置
**设置忽略文件**
1. 打开 `文件` → `设置` → `编辑器` → `文件类型`
2. 在 `忽略的文件和文件夹` 部分点击 `+` 按钮
3. 添加以下目录到忽略列表：
    - `include`