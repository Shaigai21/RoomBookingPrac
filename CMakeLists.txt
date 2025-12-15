cmake_minimum_required(VERSION 3.16)
project(BookingSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_SOURCE_DIR}/inc
)

file(GLOB_RECURSE SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

set(MAIN_FILE ${CMAKE_SOURCE_DIR}/src/main.cpp)
list(REMOVE_ITEM SOURCES ${MAIN_FILE})

add_library(booking_core ${SOURCES})
set_target_properties(booking_core PROPERTIES
    OUTPUT_NAME booking_core
)

add_executable(booking_app ${MAIN_FILE})
target_link_libraries(booking_app booking_core)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(booking_core nlohmann_json::nlohmann_json)
target_link_libraries(booking_app  nlohmann_json::nlohmann_json)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

enable_testing()

file(GLOB_RECURSE TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/*.cpp)

add_executable(booking_tests ${TEST_SOURCES})
target_link_libraries(booking_tests booking_core nlohmann_json::nlohmann_json GTest::GTest GTest::Main pthread)

add_test(NAME booking_tests COMMAND booking_tests)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE CLANG_FORMAT_SOURCES
        ${CMAKE_SOURCE_DIR}/inc/*.hpp
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/tests/*.cpp
    )

    set(FORMAT_FILES)
    foreach(file ${CLANG_FORMAT_SOURCES})
        if(EXISTS ${file} AND NOT IS_DIRECTORY ${file})
            list(APPEND FORMAT_FILES ${file})
        endif()
    endforeach()
    
    if(FORMAT_FILES)
        # cmake --build . --target clang_format_check
        add_custom_target(
            clang_format_check
            COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror --style=file
            ${FORMAT_FILES}
            COMMENT "Running clang-format Yandex style check"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )

        add_test(
            NAME clang_format
            COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror --style=file
            ${FORMAT_FILES}
        )
        
        message(STATUS "clang-format: найдено ${FORMAT_FILES} файлов для проверки")
    else()
        message(WARNING "clang-format: не найдено файлов для проверки")
    endif()
endif()