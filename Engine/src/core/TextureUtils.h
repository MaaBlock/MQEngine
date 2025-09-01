#ifndef ENGINE_TEXTURE_UTILS_H
#define ENGINE_TEXTURE_UTILS_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    /**
     * @brief 将纹理类型枚举转换为字符串
     * @param type 纹理类型
     * @return 对应的字符串描述
     */
    ENGINE_API const char* textureTypeToString(FCT::ModelInfo::TextureType type);
    
    /**
     * @brief 将纹理类型枚举转换为本地化字符串（中文）
     * @param type 纹理类型
     * @return 对应的中文字符串描述
     */
    ENGINE_API const char* textureTypeToChineseString(FCT::ModelInfo::TextureType type);
    

}

#endif // ENGINE_TEXTURE_UTILS_H