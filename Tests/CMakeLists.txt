# ============================================================================
# MQEngine Automated Verification & Acceptance Test Harness
# ============================================================================
cmake_minimum_required(VERSION 3.22.1)

enable_testing()
include("../Config/set_utf8_compiler_encoding.cmake")

# 宏：配置标准的 MQEngine 控制台测试目标
function(mq_add_test_executable target_name)
    cmake_parse_arguments(ARG "" "TIMEOUT" "SOURCES;EXTRA_LIBS" ${ARGN})

    add_executable(${target_name} ${ARG_SOURCES})

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/Core/src
        ${CMAKE_SOURCE_DIR}/Engine/Core/src/thirdparty
        ${CMAKE_SOURCE_DIR}/Engine/Core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_engine(${target_name})
    target_link_fct_libraries(${target_name})
    target_link_fct_node_libraries(${target_name})
    set_utf8_for_target(${target_name})

    if(ARG_EXTRA_LIBS)
        target_link_libraries(${target_name} ${ARG_EXTRA_LIBS})
    endif()

    if(MSVC)
        set_target_properties(${target_name} PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()

    # 注册到 CTest
    add_test(NAME ${target_name} COMMAND ${target_name})
    if(ARG_TIMEOUT)
        set_tests_properties(${target_name} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
    else()
        set_tests_properties(${target_name} PROPERTIES TIMEOUT 45)
    endif()
endfunction()

# ============================================================================
# Milestone 1: 核心并发时序与 ECS 延迟队列验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ThreadingAndECS.cpp")
    mq_add_test_executable(Test_ThreadingAndECS
        SOURCES Test_ThreadingAndECS.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM1.cpp")
    mq_add_test_executable(Test_ChallengerM1
        SOURCES Test_ChallengerM1.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_Challenger_Concurrency.cpp")
    mq_add_test_executable(Test_Challenger_Concurrency
        SOURCES Test_Challenger_Concurrency.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_M1_FrameSyncAndCapture.cpp")
    mq_add_test_executable(Test_M1_FrameSyncAndCapture
        SOURCES Test_M1_FrameSyncAndCapture.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 2: 输入系统、事件队列与动作映射验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_InputSystem.cpp")
    mq_add_test_executable(Test_InputSystem
        SOURCES Test_InputSystem.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM2_Stress.cpp")
    mq_add_test_executable(Test_ChallengerM2_Stress
        SOURCES Test_ChallengerM2_Stress.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM2_InputEdge.cpp")
    mq_add_test_executable(Test_ChallengerM2_InputEdge
        SOURCES Test_ChallengerM2_InputEdge.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 3: 物理子系统 (PhysX 5.5.0) 与双频定步长循环验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_PhysicsSubsystem.cpp")
    mq_add_test_executable(Test_PhysicsSubsystem
        SOURCES Test_PhysicsSubsystem.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM3_Collisions.cpp")
    mq_add_test_executable(Test_ChallengerM3_Collisions
        SOURCES Test_ChallengerM3_Collisions.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM3_Timing.cpp")
    mq_add_test_executable(Test_ChallengerM3_Timing
        SOURCES Test_ChallengerM3_Timing.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 4: 无头运行时与 Game 自动化验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_HeadlessRuntime.cpp")
    add_executable(Test_HeadlessRuntime Test_HeadlessRuntime.cpp)
    target_include_directories(Test_HeadlessRuntime PRIVATE
        ${CMAKE_SOURCE_DIR}/Engine/src
        ${CMAKE_SOURCE_DIR}/Engine/src/thirdparty
    )
    target_link_engine(Test_HeadlessRuntime)
    target_link_fct_libraries(Test_HeadlessRuntime)
    target_link_fct_node_libraries(Test_HeadlessRuntime)
    set_utf8_for_target(Test_HeadlessRuntime)
    if(MSVC)
        set_target_properties(Test_HeadlessRuntime PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
    endif()
    add_dependencies(Test_HeadlessRuntime Game)
    add_test(NAME Test_HeadlessRuntime COMMAND Test_HeadlessRuntime "$<TARGET_FILE:Game>")
    set_tests_properties(Test_HeadlessRuntime PROPERTIES TIMEOUT 45)
endif()

# 直接注册 Game.exe 无头运行到 CTest
if(TARGET Game)
    add_test(NAME Test_HeadlessGame COMMAND "$<TARGET_FILE:Game>" --headless --test-frames=60)
    set_tests_properties(Test_HeadlessGame PROPERTIES TIMEOUT 30)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM4_AutonomousPhysics.cpp")
    mq_add_test_executable(Test_ChallengerM4_AutonomousPhysics
        SOURCES Test_ChallengerM4_AutonomousPhysics.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 5: C++20 预处理器反射与元数据验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ReflectionPreprocessor.cpp")
    mq_add_test_executable(Test_ReflectionPreprocessor
        SOURCES Test_ReflectionPreprocessor.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM5_ReflectionStress.cpp")
    mq_add_test_executable(Test_ChallengerM5_ReflectionStress
        SOURCES Test_ChallengerM5_ReflectionStress.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 7: HomeLBM 流体仿真插件与压力测试
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_HomeLbmPlugin.cpp")
    mq_add_test_executable(Test_HomeLbmPlugin
        SOURCES Test_HomeLbmPlugin.cpp
        TIMEOUT 120
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM7_HomeLbmStress.cpp")
    mq_add_test_executable(Test_ChallengerM7_HomeLbmStress
        SOURCES Test_ChallengerM7_HomeLbmStress.cpp
        TIMEOUT 120
    )
endif()

# ============================================================================
# Milestone 8: 编辑器 Undo/Redo 与实体引用安全
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_EditorUndoRedo.cpp")
    mq_add_test_executable(Test_EditorUndoRedo
        SOURCES 
            Test_EditorUndoRedo.cpp
            ${CMAKE_SOURCE_DIR}/Applications/Editor/src/command/UndoManager.cpp
            ${CMAKE_SOURCE_DIR}/Applications/Editor/src/command/TranslateEntityCommand.cpp
        TIMEOUT 45
    )
    target_include_directories(Test_EditorUndoRedo PRIVATE ${CMAKE_SOURCE_DIR}/Applications/Editor/src)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM8_UndoRedoStress.cpp")
    mq_add_test_executable(Test_ChallengerM8_UndoRedoStress
        SOURCES Test_ChallengerM8_UndoRedoStress.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM8_EntitySafety.cpp")
    mq_add_test_executable(Test_ChallengerM8_EntitySafety
        SOURCES Test_ChallengerM8_EntitySafety.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 9: 多视口交互、数值与冲突压测
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_EditorViewportInteraction.cpp")
    mq_add_test_executable(Test_EditorViewportInteraction
        SOURCES Test_EditorViewportInteraction.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM9_NumericalStress.cpp")
    mq_add_test_executable(Test_ChallengerM9_NumericalStress
        SOURCES Test_ChallengerM9_NumericalStress.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM9_ConflictStress.cpp")
    mq_add_test_executable(Test_ChallengerM9_ConflictStress
        SOURCES Test_ChallengerM9_ConflictStress.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# 工具与专项测试: HotReload, EditorCli, GpuDriven, CesiumCache
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_HotReloadAndEditorCli.cpp")
    mq_add_test_executable(Test_HotReloadAndEditorCli
        SOURCES Test_HotReloadAndEditorCli.cpp
        TIMEOUT 45
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_GpuDrivenAndCesiumCache.cpp")
    mq_add_test_executable(Test_GpuDrivenAndCesiumCache
        SOURCES Test_GpuDrivenAndCesiumCache.cpp
        TIMEOUT 45
    )
endif()

# ============================================================================
# Milestone 11 & 12: 线程池并发基础设施、作业系统与 Computation<T> 验证
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ThreadPoolAndComputation.cpp")
    mq_add_test_executable(Test_ThreadPoolAndComputation
        SOURCES Test_ThreadPoolAndComputation.cpp
        TIMEOUT 60
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM11_Stress.cpp")
    mq_add_test_executable(Test_ChallengerM11_Stress
        SOURCES Test_ChallengerM11_Stress.cpp
        TIMEOUT 60
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM11_StarvationAndBoundaries.cpp")
    mq_add_test_executable(Test_ChallengerM11_StarvationAndBoundaries
        SOURCES Test_ChallengerM11_StarvationAndBoundaries.cpp
        TIMEOUT 60
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM12_Stress.cpp")
    mq_add_test_executable(Test_ChallengerM12_Stress
        SOURCES Test_ChallengerM12_Stress.cpp
        TIMEOUT 90
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM12_BoundariesAndInvariants.cpp")
    mq_add_test_executable(Test_ChallengerM12_BoundariesAndInvariants
        SOURCES Test_ChallengerM12_BoundariesAndInvariants.cpp
        TIMEOUT 90
    )
endif()

# ============================================================================
# Milestone 13: 渲染器跨线程 Computation 邮箱与高并发压测
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_RendererComputation.cpp")
    mq_add_test_executable(Test_RendererComputation
        SOURCES Test_RendererComputation.cpp
        TIMEOUT 60
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test_ChallengerM13_Concurrency.cpp")
    mq_add_test_executable(Test_ChallengerM13_Concurrency
        SOURCES Test_ChallengerM13_Concurrency.cpp
        TIMEOUT 90
    )
endif()

# ============================================================================
# Meta-Target: run_all_tests (全量自动化回归总入口)
# ============================================================================
# 动态收集所有已声明的测试目标
set(ALL_TEST_TARGETS
    Test_ThreadingAndECS
    Test_ChallengerM1
    Test_Challenger_Concurrency
    Test_M1_FrameSyncAndCapture
)

set(RUN_COMMANDS "")
foreach(t ${ALL_TEST_TARGETS})
    if(TARGET ${t})
        list(APPEND RUN_COMMANDS COMMAND $<TARGET_FILE:${t}>)
    endif()
endforeach()

add_custom_target(run_all_tests
    ${RUN_COMMANDS}
    DEPENDS ${ALL_TEST_TARGETS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Executing MQEngine automated verification test suites..."
)
