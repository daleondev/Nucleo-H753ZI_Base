add_library(osal STATIC)

target_sources(osal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Mutex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Semaphore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Thread.cpp
)

target_link_libraries(osal
    PRIVATE
        threadx
)

target_compile_features(osal
    PRIVATE
        cxx_std_23
)

set_target_properties(osal 
    PROPERTIES
        CXX_EXTENSIONS OFF
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        osal
)