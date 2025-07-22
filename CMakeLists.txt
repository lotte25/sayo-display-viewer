cmake_minimum_required(VERSION 3.16)
project(SayoDeviceDisplay VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hidapi STATIC
    lib/hidapi/windows/hid.c
)

target_include_directories(hidapi PUBLIC
    lib/hidapi/hidapi
    lib/hidapi/windows
)
target_link_libraries(hidapi PRIVATE
    setupapi
    cfgmgr32
)
target_compile_definitions(hidapi PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

add_executable(${PROJECT_NAME} WIN32 src/main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE hidapi)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/SUBSYSTEM:WINDOWS"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()