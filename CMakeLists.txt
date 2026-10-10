cmake_minimum_required(VERSION 3.21)
project(ArcadeMulti VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Multimedia ShaderTools OpenGL)
find_package(SDL2 REQUIRED)

qt_add_executable(Arcade WIN32
    src/main.cpp
    src/libretrocore.h   src/libretrocore.cpp
    src/emulatorview.h   src/emulatorview.cpp
    src/gamepad.h        src/gamepad.cpp
    src/gamelistmodel.h  src/gamelistmodel.cpp
    src/appcontroller.h  src/appcontroller.cpp
    src/theme.h          src/theme.cpp
    src/libretro/libretro.h
)

qt_add_qml_module(Arcade
    URI ArcadeUI
    VERSION 1.0
    QML_FILES qml/Main.qml
    RESOURCES resources/names.txt resources/fbneo.txt
    NO_RESOURCE_TARGET_PATH
)

# Shaders del efecto CRT (se compilan a .qsb y quedan en :/shaders/)
qt_add_shaders(Arcade "arcade_shaders"
    PREFIX "/"
    FILES shaders/crt.vert shaders/crt.frag
)

target_include_directories(Arcade PRIVATE src)

# SDL2: Funciona tanto con el paquete de vcpkg/MSYS2 (SDL2::SDL2) como con el clásico (${SDL2_LIBRARIES})
if(TARGET SDL2::SDL2)
    target_link_libraries(Arcade PRIVATE SDL2::SDL2)
else()
    target_include_directories(Arcade PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(Arcade PRIVATE ${SDL2_LIBRARIES})
endif()

target_link_libraries(Arcade PRIVATE Qt6::Quick Qt6::Multimedia Qt6::OpenGL)

# Núcleo libretro de prueba que dibuja con OpenGL (no se compila por defecto):
#   cmake --build build --config Release --target testgl_libretro
add_library(testgl_libretro MODULE EXCLUDE_FROM_ALL tests/testgl_core.cpp)
set_target_properties(testgl_libretro PROPERTIES PREFIX "" AUTOMOC OFF)

if(WIN32)
    target_link_libraries(Arcade PRIVATE winmm)
endif()
