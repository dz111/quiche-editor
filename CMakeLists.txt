cmake_minimum_required(VERSION 2.8)

project(quiche-editor)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES src/quiche.cpp)
add_executable(qe ${SOURCE_FILES})

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})
target_link_libraries(qe ${CURSES_LIBRARIES})

install(TARGETS qe DESTINATION bin)

