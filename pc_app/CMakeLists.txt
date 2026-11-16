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
    src/plotwidget.h
    src/plotwidget.cpp
)

target_include_directories(OfetMeter PRIVATE src)

target_link_libraries(OfetMeter PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

# ------------------------------------------------------------------
# Portable-версия для флешки (только Windows, только сборка Release).
# После сборки в папке <репозиторий>/OfetMeter_portable лежат OfetMeter.exe
# и все нужные ему библиотеки. Папку можно скопировать на любой ПК с Windows.
if(WIN32)
    get_target_property(_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin}")
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(PORTABLE_DIR "${CMAKE_SOURCE_DIR}/../OfetMeter_portable")

    add_custom_command(TARGET OfetMeter POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DCONFIG=$<CONFIG>
            -DEXE=$<TARGET_FILE:OfetMeter>
            -DOUT=${PORTABLE_DIR}
            -DWINDEPLOYQT=${WINDEPLOYQT_EXE}
            -DCOMPILER_DIR=${_compiler_dir}
            -P "${CMAKE_SOURCE_DIR}/cmake/deploy.cmake"
        COMMENT "Portable-папка OfetMeter_portable"
        VERBATIM)
endif()
