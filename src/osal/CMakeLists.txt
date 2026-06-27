add_library(osal STATIC)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_subdirectory(stm32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(linux)
else()
    message(FATAL_ERROR "Unsupported OSAL platform: ${CMAKE_SYSTEM_NAME}")
endif()

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