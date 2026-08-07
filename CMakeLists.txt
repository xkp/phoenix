cmake_minimum_required(VERSION 3.20)

project(phoenix_cgal_app VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CGAL REQUIRED)

add_executable(phoenix_cgal_app
    src/main.cpp
)

target_link_libraries(phoenix_cgal_app
    PRIVATE
        CGAL::CGAL
)

if(MSVC)
    target_compile_options(phoenix_cgal_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(phoenix_cgal_app PRIVATE -Wall -Wextra -Wpedantic)
endif()
