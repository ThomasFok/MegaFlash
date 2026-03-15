cmake_minimum_required(VERSION 3.13...3.27)

#PICO_BOARD is set by command line -DPICO_BOARD=
##set(PICO_BOARD pico_w)

#All builds share the same PICOTOOL
set(PICOTOOL_FETCH_FROM_GIT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/picotool)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(megaflash C CXX ASM)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)
pico_sdk_init()

#add source file here
add_executable(${CMAKE_PROJECT_NAME}
    main.c
    a2bus.c
    busloop.c
    busloop_wa.c
    cmdhandler.c
    flash.c
    flashunitmapper.c
    mediaaccess.c
    romdisk.c
    romdisk.s       
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

#standard C lib printf for FPU fout() function
pico_set_printf_implementation(${CMAKE_PROJECT_NAME} compiler)

#extra file dependencies
set_source_files_properties(romdisk.s OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/romdisk.po)
set_source_files_properties(cpanel.s  OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../cpanel/cpanel.bin)

#to generate .uf2 file
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

#to compile .pio file
pico_generate_pio_header(${CMAKE_PROJECT_NAME}
	${CMAKE_CURRENT_SOURCE_DIR}/a2bus_rp2040.pio
)

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

#RP2040: Set system speed to 150MHz
#SPI does not work at 100MHz. So, set the speed to 150MHz
#and run SPI at 75MHz
if (PICO_BOARD STREQUAL pico_w)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SYS_CLK_MHZ=150)
endif()

#RP2350: Set to 1 to overclock CPU
if (PICO_BOARD STREQUAL pico2_w)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE OC_RP2350=1)
endif()

#Allow setting CYW43 PIO clock divisor
target_compile_definitions(cyw43_driver_picow INTERFACE CYW43_PIO_CLOCK_DIV_DYNAMIC=1)

#enable stdio 
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000)
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
