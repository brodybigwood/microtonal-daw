cmake_minimum_required(VERSION 3.25)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for Clangd")


project(MySDL2WebApp)



set(CMAKE_CXX_STANDARD 17) # Or 14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Enable AddressSanitizer
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")

# Set the sanitizer runtime
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")




add_compile_definitions(_DEBUG)


file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")

set(VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/lib/VST_SDK/vst3sdk")

set(VST3_HOSTING_SRC
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/utility/testing.cpp
   # ${VST3_SDK_PATH}/public.sdk/samples/vst-hosting/validator/source/validator.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/test/hostclassestest.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/module.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/moduleinfo/moduleinfocreator.cpp
    ${VST3_SDK_PATH}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp

    ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/connectionproxy.cpp

    ${VST3_SDK_PATH}/public.sdk/source/common/commoniids.cpp
    ${VST3_SDK_PATH}/public.sdk/source/common/commonstringconvert.cpp
)

set(VST3_BASE_SRC
    ${VST3_SDK_PATH}/base/source/fobject.cpp
    ${VST3_SDK_PATH}/base/source/fdebug.cpp
    ${VST3_SDK_PATH}/base/thread/source/flock.cpp
    ${VST3_SDK_PATH}/base/source/fstring.cpp
    ${VST3_SDK_PATH}/base/source/updatehandler.cpp

)

set(VST3_INTERFACE_SRC
    ${VST3_SDK_PATH}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK_PATH}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK_PATH}/pluginterfaces/base/ustring.cpp
    ${VST3_SDK_PATH}/pluginterfaces/base/conststringtable.cpp
)


if(UNIX)
    add_compile_definitions(SMTG_OS_LINUX LINUX)
    list(APPEND VST3_HOSTING_SRC
        "${VST3_SDK_PATH}/public.sdk/source/vst/hosting/module_linux.cpp"
        "${VST3_SDK_PATH}/public.sdk/source/common/threadchecker_linux.cpp"
    )
endif()


add_library(vst3 STATIC
    ${VST3_INTERFACE_SRC}
    ${VST3_BASE_SRC}
    ${VST3_HOSTING_SRC}
)



add_executable(DAW ${SOURCES})
target_compile_definitions(DAW PRIVATE _DEBUG)


# Check if we're using Emscripten
if(EMSCRIPTEN)

# Set Emscripten as the compiler
    set(CMAKE_SYSTEM_NAME Emscripten)
    set(CMAKE_C_COMPILER emcc)
    set(CMAKE_CXX_COMPILER em++)

    # Specify that you are building for WebAssembly (WASM)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    message("Building for WASM with SDL3 & SDL3_ttf")


else()
    # Native platform build (GCC/Clang)
    # You can add any native-specific settings here, such as debugging options or extra libraries.
    
    message("Building natively")




endif()

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
)


add_dependencies(DAW CopyAssets)  # Ensure assets are copied before running



include_directories(${VST3_SDK_PATH})
include_directories(${VST3_SDK_PATH}/pluginterfaces)
include_directories(${VST3_SDK_PATH}/base)

include_directories(/usr/include/SDL3_ttf/)

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RTAUDIO REQUIRED rtaudio)

find_package(X11 REQUIRED)


target_include_directories(DAW PRIVATE

    ${SDL2_INCLUDE_DIRS}
    ${RTAUDIO_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)

target_link_libraries(DAW PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    ${SDL2_LIBRARIES}
    ${RTAUDIO_LIBRARIES}
    vst3
    ${X11_LIBRARIES}
)


