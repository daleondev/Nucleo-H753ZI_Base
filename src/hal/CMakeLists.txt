add_library(hal STATIC)

target_link_libraries(hal
    PUBLIC
        Platform
)

target_compile_features(hal
    PRIVATE
        cxx_std_23
)

set_target_properties(hal 
    PROPERTIES
        CXX_EXTENSIONS OFF
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_subdirectory(stm32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(linux)
else()
    message(FATAL_ERROR "Unsupported HAL platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        hal
)