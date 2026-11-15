cmake_minimum_required(VERSION 3.16)

project(OfetMeter VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# Основная версия: Qt 5.15 (C:/Qt/5.15.2/mingw81_64). Qt 6 тоже подойдёт.
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_executable(OfetMeter WIN32
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mainwindow.ui
    src/deviceclient.h
    src/deviceclient.cpp
    src/measurementfile.h
    src/measurementfile.cpp
    src/sweepparams.h
    src/sweepparams.cpp
)

target_link_libraries(OfetMeter PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)
