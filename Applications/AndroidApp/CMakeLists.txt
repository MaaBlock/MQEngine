include("../../Config/set_utf8_compiler_encoding.cmake")
file(GLOB CORE_MODULE_SOURCES
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.cpp"
)
file(GLOB CORE_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/core/*.h"
        "./src/core/*.hpp"
)
file(GLOB THIRDPARTY_MODULE_HEADERS
        LIST_DIRECTORIES false
        CONFIGURE_DEPENDS
        "./src/thirdparty/*.h"
)

set(MQENGINE_ANDROID_APP_SOURCES
        ${CORE_MODULE_SOURCES}
)
set(MQENGINE_ANDROID_APP_HEADERS
        ${CORE_MODULE_HEADERS}
        ${THIRDPARTY_MODULE_HEADERS}
)

if(ANDROID)
    add_library(AndroidApp SHARED
            ${MQENGINE_ANDROID_APP_SOURCES}
            ${MQENGINE_ANDROID_APP_HEADERS}
    )
else()
    add_executable(AndroidApp
            ${MQENGINE_ANDROID_APP_SOURCES}
            ${MQENGINE_ANDROID_APP_HEADERS}
    )
endif()

target_link_libraries(AndroidApp
        FCT_IMGUI
)
target_link_fct_libraries(AndroidApp)
target_link_fct_node_libraries(AndroidApp)
target_link_engine(AndroidApp)
set_utf8_for_target(AndroidApp)
