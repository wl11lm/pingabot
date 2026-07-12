cmake_minimum_required(VERSION 3.12)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pingabot_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

link_directories(libmicroros)
add_executable(pingabot_pico
    pingabot_pico.c
    pico_uart_transport.c
)
target_link_libraries(pingabot_pico
    pico_stdlib
    hardware_pwm
    hardware_adc
    microros
)

target_include_directories(pingabot_pico PUBLIC
    libmicroros/include
)

SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

# Configure Pico
pico_enable_stdio_usb(pingabot_pico 1)
pico_enable_stdio_uart(pingabot_pico 0)
add_compile_definitions(PICO_UART_ENABLE_CRLF_SUPPORT=0)
add_compile_definitions(PICO_STDIO_ENABLE_CRLF_SUPPORT=0)
add_compile_definitions(PICO_STDIO_DEFAULT_CRLF=0)

# Generate UF2
pico_add_extra_outputs(pingabot_pico)

#add_custom_command(TARGET pingabot_pico POST_BUILD
#    COMMAND sudo mount /dev/sda1 /mnt/pico || true
#    COMMAND sudo cp ${CMAKE_CURRENT_BINARY_DIR}/pingabot_pico.uf2 /mnt/pico
#    COMMAND sudo umount /mnt/pico || true
#    COMMENT "Enviando firmware para a Pico..."
#)