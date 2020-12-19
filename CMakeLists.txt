cmake_minimum_required(VERSION 3.3.1)
project(replica_sync)

# add_subdirectory(3dparty/pod)
# add_subdirectory(3dparty/trace)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -mrtm -mtune=native -march=native -Wall -Wextra -g3 -ggdb3 -gdwarf")

IF(CMAKE_BUILD_TYPE MATCHES "Debug")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fsanitize=address" )
ELSE()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -DNDEBUG")
ENDIF ()

set(CMAKE_CXX_STANDARD 17)
add_definitions(-D_LINUX)

include_directories(
    include
)

add_executable(replica_sync
    source/replica_sync.cpp
    include/thread.h
    include/mpmc_bounded_queue.h
)

target_link_libraries(${PROJECT_NAME}
   -pthread -ldl
)
