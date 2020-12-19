cmake_minimum_required(VERSION 3.3.1)

set(CMAKE_CONFIGURATION_TYPES "Deploy;GTEST" CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -ffast-math -mavx -mrtm -mtune=corei7 -march=corei7 -Wall -Wextra -no-pie  -fomit-frame-pointer -mcx16 -g3 -ggdb3 -gdwarf")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -ffast-math -msse4.2 -mrtm -Wall -Wextra -no-pie  -fno-omit-frame-pointer -mcx16 -g3 -ggdb3 -gdwarf")

set(DEBUG_CMAKE_BUILD_TYPE "Debug")
set(RELEASE_CMAKE_BUILD_TYPE "Release")
set(DEPLOY_CMAKE_BUILD_TYPE "Deploy")

IF (DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE $ENV{CMAKE_BUILD_TYPE})

ELSEIF(CMAKE_BUILD_TYPE STREQUAL " " OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE ${RELEASE_CMAKE_BUILD_TYPE})

ENDIF ()

IF(CMAKE_BUILD_TYPE MATCHES ${DEBUG_CMAKE_BUILD_TYPE})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -D_LINUX -O0 -ffast-math -fsanitize=address" )

ELSEIF(CMAKE_BUILD_TYPE MATCHES ${DEPLOY_CMAKE_BUILD_TYPE} )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3  -D_LINUX  -DNDEBUG")

ELSEIF(CMAKE_BUILD_TYPE MATCHES ${RELEASE_CMAKE_BUILD_TYPE} )
    add_definitions(-D_INTEL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2  -D_LINUX  -DNDEBUG")

ELSE()
    message(FATAL_ERROR "new configuration type: ${CMAKE_BUILD_TYPE}")

ENDIF ()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE} configuration for ${PROJECT_NAME}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "./lib/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "./lib/")

set(CMAKE_DEFINITIONS_DEFINE "yes")

#----------------------------- test build configs -------------------------------------

if (COVERAGE)
    if (NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        message(FATAL_ERROR "Coverage requires -DCMAKE_BUILD_TYPE=Debug")
    endif()

    message(STATUS "Setting coverage compiler flags")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb3 -O0 --coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}     -g -ggdb3 -O0 --coverage")

    FIND_PROGRAM( GCOV_PATH gcov )
    FIND_PROGRAM( LCOV_PATH lcov )
    FIND_PROGRAM( GENHTML_PATH genhtml )
    FIND_PROGRAM( GCOVR_PATH gcovr PATHS ${CMAKE_SOURCE_DIR}/tests)

    IF(NOT GCOV_PATH)
        MESSAGE(FATAL_ERROR "gcov not found! Aborting...")
    ENDIF() # NOT GCOV_PATH

    IF ( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
        MESSAGE( WARNING "Code coverage results with an optimized (non-Debug) build may be misleading" )
    ENDIF() # NOT CMAKE_BUILD_TYPE STREQUAL "Debug"

#    enable_testing()
    include(CTest)
endif()

enable_testing()
