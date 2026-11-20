cmake_minimum_required(VERSION 3.13...3.27)

#PICO_BOARD is set by command line -DPICO_BOARD=
##set(PICO_BOARD pico2_w)

#All builds share the same PICOTOOL
set(PICOTOOL_FETCH_FROM_GIT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/picotool)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(megaflash C CXX ASM)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

#add source file here
add_executable(${CMAKE_PROJECT_NAME}
    main.c
    a2bus.c
    busloop.c
    cmdhandler.c
    flash.c
    flashunitmapper.c
    mediaaccess.c
    romdisk.c  
    ramdisk.c
    usbserial.c
    filetransfer.c
    dmamemops.c
    formatter.c
    rtc.c
    misc.c
    cpanel.s
    userconfig.c
    encryption.c
    terminal.c
    slinky.c
    fpu.c
    network.cpp
    udptask.cpp
    ntptask.cpp
    testwifitask.cpp
    tftptask.cpp
    tftprxtask.cpp
    tftptxtask.cpp
    tftpstate.c
    imagewriter.cpp
)

#Use romdisk_debug.po for Debug build
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE romdisk_debug.s)
else()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE romdisk.s)
endif()

#standard C lib printf for FPU fout() function
pico_set_printf_implementation(${CMAKE_PROJECT_NAME} compiler)

#extra file dependencies
set_source_files_properties(romdisk.s OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/romdisk.po)
set_source_files_properties(romdisk_debug.s OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/romdisk_debug.po)
set_source_files_properties(cpanel.s  OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../cpanel/cpanel.bin)

#to generate .uf2 file
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

#to compile .pio file
pico_generate_pio_header(${CMAKE_PROJECT_NAME}
	${CMAKE_CURRENT_SOURCE_DIR}/a2bus_rp2350.pio
)

#include directories
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

#linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
  pico_stdlib
  pico_multicore    
  pico_mem_ops  
  pico_aon_timer
  pico_float
  pico_double
  hardware_pio
  hardware_spi
  hardware_dma
  hardware_divider
  hardware_adc
  hardware_watchdog
  pico_cyw43_arch_lwip_poll
)

#Set to 1 to overclock RP2350 CPU
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE OC_RP2350=1)

#Allow setting CYW43 PIO clock divisor
target_compile_definitions(cyw43_driver_picow INTERFACE CYW43_PIO_CLOCK_DIV_DYNAMIC=1)

#enable stdio 
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000)
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
