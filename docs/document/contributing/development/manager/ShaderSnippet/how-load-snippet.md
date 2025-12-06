# 加载着色器片段流程
## 核心数据结构
```
多index map (by uuid,by name,by sourceHash) -> {
    uuid,
    name.
    sourceHash,
    source,
    input,
    output
}
```
## 文件存储
```
name.meta{
    uuid,
    sourceHash
}
name.hlsl {
    source
}
```
## 辅助函数
```
加载name.hlsl函数 {
    如果存在对应的name.hlsl
        加载对应的name.hlsl，
        并看有没有对应的meta数据，
        有就 
            存进行，
            然后,解析着色器片段(to input/output),
            检测sourceHash，
            如果不一致,
                触发update事件，更新sourceHash，保存对应的meta文件
        没有对应的.meta文件，
            看有没有对应的sourceHash的.meta数据，
            如果存在且该条目的Source字段为空,触发rename事件，加载对应的soruceHash
        不存在对应的.meta文件，
            此时触发create事件，并生成对应的meta数据，保存对应的meta文件
    如果不存在对应的name.hlsl
        return false
}
```
## 初始化流程
```
registerEmbedSnippet，embed的sourceHash = embed
加载所有.meta文件
遍历meta加载所有对应的name.hlsl {
    for each meta {
        source.size ? continue
        call 加载name.hlsl函数
        加载失败不处理
    }
}
for（name.hlsl）{
    对应的meta存在且source.size那么continue
    call 加载name.hlsl
}
```
## 注意点
存在.meta文件 但是没有对应的.hlsl文件，那么source就为空，相当于被标记为不存在了
## 运行时加载流程
```
检测到文件更新 -> call 加载name.hlsl
监测到文件删除 -> call source = 空
监测到文件rename -> call source = 空,call 加载newName.hlsl
```