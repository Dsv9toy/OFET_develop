# Сборка portable-папки (вызывается после сборки, см. CMakeLists.txt).
# Копирует OfetMeter.exe в папку OUT и кладёт рядом всё, что нужно для запуска
# на ПК без Qt: библиотеки Qt, плагин платформы Windows, библиотеки MinGW.
# Библиотеки копируются напрямую из папки Qt; windeployqt (если найден) запускается
# дополнительно. Подробности о каждом запуске пишутся в OUT/deploy_log.txt.

if(NOT CONFIG MATCHES "^(Release|MinSizeRel|RelWithDebInfo)$")
    message(STATUS "Portable-папка создаётся только в сборке Release (сейчас: ${CONFIG}).")
    return()
endif()

get_filename_component(exe_name "${EXE}" NAME)
file(MAKE_DIRECTORY "${OUT}")
file(COPY "${EXE}" DESTINATION "${OUT}")

set(LOG "${OUT}/deploy_log.txt")
file(WRITE "${LOG}" "Сборка portable-папки\nexe: ${EXE}\nQt bin: ${QT_BIN}\nкомпилятор: ${COMPILER_DIR}\nwindeployqt: ${WINDEPLOYQT}\n\n")

set(missing "")
macro(copy_one src dst_dir)
    if(EXISTS "${src}")
        file(MAKE_DIRECTORY "${dst_dir}")
        file(COPY "${src}" DESTINATION "${dst_dir}")
        file(APPEND "${LOG}" "скопирован: ${src}\n")
    else()
        file(APPEND "${LOG}" "НЕ НАЙДЕН: ${src}\n")
        list(APPEND missing "${src}")
    endif()
endmacro()

# 1. Библиотеки Qt, с которыми собрана программа
foreach(m Core Gui Widgets Network)
    copy_one("${QT_BIN}/Qt${QT_MAJOR}${m}.dll" "${OUT}")
endforeach()

# 2. Плагины: без platforms/qwindows.dll окно не откроется
get_filename_component(qt_root "${QT_BIN}" DIRECTORY)
copy_one("${qt_root}/plugins/platforms/qwindows.dll" "${OUT}/platforms")
if(EXISTS "${qt_root}/plugins/styles/qwindowsvistastyle.dll")
    copy_one("${qt_root}/plugins/styles/qwindowsvistastyle.dll" "${OUT}/styles")
endif()

# 3. Библиотеки компилятора MinGW: ищем рядом с компилятором, затем в папке Qt
foreach(dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
    if(EXISTS "${COMPILER_DIR}/${dll}")
        copy_one("${COMPILER_DIR}/${dll}" "${OUT}")
    elseif(EXISTS "${QT_BIN}/${dll}")
        copy_one("${QT_BIN}/${dll}" "${OUT}")
    else()
        file(APPEND "${LOG}" "НЕ НАЙДЕН: ${dll}\n")
        list(APPEND missing "${dll}")
    endif()
endforeach()

# 4. windeployqt дополнительно (может доложить что-то ещё); его ошибки не критичны
if(WINDEPLOYQT AND EXISTS "${WINDEPLOYQT}")
    execute_process(
        COMMAND "${WINDEPLOYQT}" --release --no-translations --no-system-d3d-compiler
                --no-opengl-sw --no-compiler-runtime "${OUT}/${exe_name}"
        WORKING_DIRECTORY "${QT_BIN}"
        RESULT_VARIABLE res OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(APPEND "${LOG}" "\nwindeployqt, код ${res}:\n${out}\n${err}\n")
else()
    file(APPEND "${LOG}" "\nwindeployqt не найден, пропущен\n")
endif()

if(missing)
    message(WARNING "Portable-папка: не найдены файлы ${missing}. Подробно: ${LOG}")
else()
    message(STATUS "Portable-папка готова: ${OUT}")
endif()
