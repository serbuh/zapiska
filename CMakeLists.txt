cmake_minimum_required(VERSION 3.16)

project(zapiska VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Core Widgets)
if(Qt6_FOUND)
    set(ZAPISKA_QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
    set(ZAPISKA_QT_PACKAGE Qt5)
endif()

add_subdirectory(src/marine)

