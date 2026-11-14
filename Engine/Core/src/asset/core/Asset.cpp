/**
 * @file Asset.cpp
 * @brief Asset 模块顶层门面类实现
 */
#include "Asset.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/runtime/interface/Application.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine
{
    namespace Asset
    {
        Asset::Asset() = default;

        Asset::~Asset()
        {
            term();
        }

        void Asset::registerInfo()
        {
            compositeManager.registerInfo();
            asyncLoader.registerInfo();
            textureUploader.registerInfo();
        }

        Status Asset::init()
        {
            CHECK_STATUS(compositeManager.init());
            CHECK_STATUS(asyncLoader.init());
            CHECK_STATUS(textureUploader.init());
            m_initialized = true;
            return OkStatus();
        }

        void Asset::term() noexcept
        {
            if (m_initialized)
            {
                textureUploader.term();
                compositeManager.term();
                asyncLoader.term();
                m_initialized = false;
            }
        }

    } // namespace Asset
} // namespace MQEngine
