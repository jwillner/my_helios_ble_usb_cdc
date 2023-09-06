# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# Re-direct the directory where the 'boards' directory is found from
# $ZEPHYR_BASE to this directory.
list(PREPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# you can redefine the log levels for the serial modules like this (1=Error,2=Warning,3=Info,4=Debug):
add_compile_definitions(SERIAL_LOG_LEVEL=4 BLE_SERIAL_LOG_LEVEL=3)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(Helios)

target_sources(app PRIVATE src/main.c src/serial.c src/serial_internal.c src/uart_serial.c src/ble_serial.c src/cmd_parser.c)
