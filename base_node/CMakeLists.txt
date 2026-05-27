# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(project_base_node)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE src/main.c src/nus_central.c src/json_serial.c src/watchdog.c src/buzzer.c src/air_quality.c src/kalman.c src/node_buffer.c)
