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


set(MQENGINE_LAUNCHER_SOURCES
        ${CORE_MODULE_SOURCES}
)
set(MQENGINE_LAUNCHER_HEADERS
        ${CORE_MODULE_HEADERS}
)


add_executable(Editor
        ${MQENGINE_LAUNCHER_SOURCES}
        ${MQENGINE_LAUNCHER_HEADERS}
)