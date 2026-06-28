add_library(osal STATIC)

add_subdirectory(libc)

target_sources(osal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Mutex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Semaphore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Thread.cpp
)

target_link_libraries(osal
    PUBLIC
        osal::libc
        project_compiler_settings
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        osal
)
