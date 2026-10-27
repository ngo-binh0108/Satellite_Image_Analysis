cmake_minimum_required(VERSION 3.16)
project(EarthView VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
#  Satellite image analysis desktop app (Qt Widgets).
#  The analysis itself runs in the Python engine (..\satimg), started by the app.
#  Build with build.bat, or open this folder / the generated solution in Visual Studio.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(PACKAGES_DIR "D:/KJS_02/work/Stable Diffusion/ALPR/packages" CACHE PATH "Folder with the downloaded C++ packages")
if(NOT Qt5_DIR AND EXISTS "${PACKAGES_DIR}/qt/5.15.2/msvc2019_64/lib/cmake/Qt5")
    set(Qt5_DIR "${PACKAGES_DIR}/qt/5.15.2/msvc2019_64/lib/cmake/Qt5")
endif()
find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

add_executable(EarthView WIN32
    src/main.cpp
    src/Theme.h
    src/Backend.h     src/Backend.cpp
    src/MapWidget.h   src/MapWidget.cpp
    src/Charts.h      src/Charts.cpp
    src/UiHelpers.h
    src/LoginDialog.h src/LoginDialog.cpp
    src/MainWindow.h  src/MainWindow.cpp  src/MainWindowPages.cpp
)
target_include_directories(EarthView PRIVATE src)
target_link_libraries(EarthView PRIVATE Qt5::Widgets Qt5::Network)
target_compile_definitions(EarthView PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(EarthView PRIVATE /utf-8 /MP /W3 /permissive- /Zc:__cplusplus)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT EarthView)
endif()

# ---- deploy the Qt DLLs and plugins next to the exe -------------------------
get_target_property(QMAKE_EXE Qt5::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QMAKE_EXE}" DIRECTORY)
add_custom_command(TARGET EarthView POST_BUILD
    # clean PATH: other Qt copies on PATH (e.g. Anaconda's) would confuse windeployqt
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${QT_BIN_DIR}$<SEMICOLON>$ENV{SystemRoot}/System32"
            "${QT_BIN_DIR}/windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw
            $<IF:$<CONFIG:Debug>,--debug,--release> "$<TARGET_FILE:EarthView>"
    COMMENT "Deploying Qt runtime")
