#pragma once

#ifdef _WIN32
#ifdef NOMINMAX
#undef NOMINMAX
#endif
#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#if __has_include(<FCT/Context/ShaderCrossCompiler.h>)
#include <FCT/Context/ShaderCrossCompiler.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <functional>

#include <absl/strings/str_cat.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include "E2EAssert.h"
#include "TestProfiles.h"
#include "HeadlessGlesFixture.h"
#include "E2ETestBase.h"
#include "E2ETestRegistry.h"
