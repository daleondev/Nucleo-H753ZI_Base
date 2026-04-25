# STM32 lwIP port: ethernetif glue + LAN8742 PHY driver.

add_library(LwipPortStm32 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/ethernetif.c
    ${CMAKE_CURRENT_LIST_DIR}/src/lan8742.c
)

target_include_directories(LwipPortStm32 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(LwipPortStm32 PUBLIC
    lwip
    Platform
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(LwipPortStm32 PRIVATE -Wno-unused-parameter)
endif()
