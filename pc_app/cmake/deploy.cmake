# Сборка portable-папки (вызывается после сборки, см. CMakeLists.txt).
# Копирует OfetMeter.exe в папку OUT и добавляет к нему библиотеки Qt (windeployqt)
# и библиотеки компилятора MinGW. Эту папку можно переносить на флешке.

if(NOT CONFIG MATCHES "^(Release|MinSizeRel|RelWithDebInfo)$")
    message(STATUS "Portable-папка создаётся только в сборке Release (сейчас: ${CONFIG}).")
    return()
endif()

get_filename_component(exe_name "${EXE}" NAME)
file(MAKE_DIRECTORY "${OUT}")
file(COPY "${EXE}" DESTINATION "${OUT}")

if(NOT WINDEPLOYQT)
    message(WARNING "windeployqt не найден, библиотеки Qt не скопированы.")
    return()
endif()

execute_process(
    COMMAND "${WINDEPLOYQT}" --release --no-translations --no-system-d3d-compiler
            --no-opengl-sw --no-compiler-runtime "${OUT}/${exe_name}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT res EQUAL 0)
    message(WARNING "windeployqt завершился с ошибкой ${res}:\n${out}\n${err}")
endif()

# Библиотеки MinGW (без них exe не запустится на ПК без Qt)
foreach(dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libstdc++-6.dll libwinpthread-1.dll)
    if(EXISTS "${COMPILER_DIR}/${dll}")
        file(COPY "${COMPILER_DIR}/${dll}" DESTINATION "${OUT}")
    endif()
endforeach()

message(STATUS "Portable-папка готова: ${OUT}")
