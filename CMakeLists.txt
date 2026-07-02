cmake_minimum_required(VERSION 3.25)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for Clangd")


project(MySDL2WebApp)



set(CMAKE_CXX_STANDARD 20) # Or 14
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Enable AddressSanitizer (native builds only)
if(NOT EMSCRIPTEN)
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    #set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
endif()



add_compile_definitions(_DEBUG)

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")

file(GLOB_RECURSE SRC_NODE CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/nodes/**/*.cpp")

if(UNIX AND NOT EMSCRIPTEN)
    add_compile_definitions(SMTG_OS_LINUX LINUX)
endif()

add_executable(DAW ${SOURCES} ${SRC_NODE})
target_compile_definitions(DAW PRIVATE _DEBUG)


# Check if we're using Emscripten
if(EMSCRIPTEN)
    message("Building for WASM with SDL3 & SDL3_ttf")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set(EMSCRIPTEN_FLAGS
        -sUSE_LIBPNG=1
        -sUSE_SDL=3
        -sUSE_FREETYPE=1
        -sUSE_HARFBUZZ=1
        -sFORCE_FILESYSTEM=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=512MB
        -sNO_DISABLE_EXCEPTION_CATCHING=1
    )
    target_compile_options(DAW PRIVATE ${EMSCRIPTEN_FLAGS})
    target_link_options(DAW PRIVATE ${EMSCRIPTEN_FLAGS} --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets)
else()
    message("Building natively")
endif()

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets
)


add_dependencies(DAW CopyAssets)  # Ensure assets are copied before running



if(NOT EMSCRIPTEN)
    include_directories(/usr/include/SDL3_ttf/)
endif()
include_directories(/usr/local/include)
include_directories(${CMAKE_SOURCE_DIR}/src/)

# Emscripten: compile SDL3_gfx + SDL3_ttf from source
if(EMSCRIPTEN)
    file(GLOB SDL_GFX_SRCS /mnt/2TB/home/brody/libraries/SDL3_gfx/*.c)
    add_library(sdl3_gfx STATIC ${SDL_GFX_SRCS})

    file(GLOB SDL_IMG_SRCS /mnt/2TB/home/brody/projects/lib/SDL_image/src/*.c)
    add_library(sdl3_image STATIC ${SDL_IMG_SRCS})
    target_compile_definitions(sdl3_image PRIVATE LOAD_PNG)
    target_include_directories(sdl3_image PUBLIC
        /mnt/2TB/home/brody/projects/lib/SDL_image/include
        /mnt/2TB/home/brody/projects/lib/SDL_image/include/SDL3_image
        /mnt/2TB/home/brody/projects/lib/SDL_image/src
    )

    file(GLOB SDL_TTF_SRCS /mnt/2TB/home/brody/projects/lib/SDL_ttf/src/*.c)
    add_library(sdl3_ttf STATIC ${SDL_TTF_SRCS})
    target_include_directories(sdl3_ttf PUBLIC
        /mnt/2TB/home/brody/projects/lib/SDL_ttf/include
        /mnt/2TB/home/brody/projects/lib/SDL_ttf/include/SDL3_ttf
        /mnt/2TB/home/brody/projects/lib/SDL_ttf/src
    )
    target_include_directories(sdl3_ttf PRIVATE
        /mnt/2TB/home/brody/projects/lib/emsdk/upstream/emscripten/cache/sysroot/include/freetype2
    )
    target_link_libraries(DAW PRIVATE sdl3_gfx sdl3_image sdl3_ttf)
endif()


if(NOT EMSCRIPTEN)
    find_package(SDL3 REQUIRED)
    find_package(SDL3_ttf REQUIRED)
    find_package(SDL3_image REQUIRED)
endif()

# Native-only dependencies
if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SNDFILE REQUIRED sndfile)

    # Build RtAudio statically from source
    set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "Build static library" FORCE)
    set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "Skip tests" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/../lib/rtaudio ${CMAKE_BINARY_DIR}/rtaudio)
endif()

target_include_directories(DAW PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
)

if(NOT EMSCRIPTEN)
    target_link_libraries(DAW PRIVATE
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        SDL3_image::SDL3_image
    )
endif()

if(NOT EMSCRIPTEN)
    target_link_libraries(DAW PRIVATE
        /usr/local/lib/SDL3_gfx/libSDL3_gfx.a
        rtaudio
        ${SNDFILE_LIBRARIES}
    )
    find_library(READLINE_LIBRARY readline)
    if(READLINE_LIBRARY)
        target_link_libraries(DAW PRIVATE ${READLINE_LIBRARY})
        target_compile_definitions(DAW PRIVATE DAW_HAS_READLINE_LIB=1)
    endif()
endif()

# VST3 SDK (native Linux only)
if(NOT EMSCRIPTEN)
    set(VST3_SDK_PATH "/home/brody/projects/lib/vst3sdk")

    target_include_directories(DAW PRIVATE
        ${VST3_SDK_PATH}
        ${VST3_SDK_PATH}/public.sdk
        ${VST3_SDK_PATH}/pluginterfaces
        ${VST3_SDK_PATH}/base/thread/source
    )

    set(VST3_SOURCES
        ${VST3_SDK_PATH}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/module_linux.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/connectionproxy.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp
        ${VST3_SDK_PATH}/public.sdk/source/common/commoniids.cpp
        ${VST3_SDK_PATH}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_PATH}/public.sdk/source/common/threadchecker_linux.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/utility/stringconvert.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/moduleinfo/moduleinfocreator.cpp
        ${VST3_SDK_PATH}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
        ${VST3_SDK_PATH}/base/source/fobject.cpp
        ${VST3_SDK_PATH}/base/source/fstring.cpp
        ${VST3_SDK_PATH}/base/source/fdebug.cpp
        ${VST3_SDK_PATH}/base/source/baseiids.cpp
        ${VST3_SDK_PATH}/base/source/timer.cpp
        ${VST3_SDK_PATH}/base/thread/source/flock.cpp
        ${VST3_SDK_PATH}/base/source/updatehandler.cpp
        ${VST3_SDK_PATH}/pluginterfaces/base/coreiids.cpp
        ${VST3_SDK_PATH}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_PATH}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_PATH}/pluginterfaces/base/conststringtable.cpp
    )

    target_sources(DAW PRIVATE ${VST3_SOURCES})

    target_link_libraries(DAW PRIVATE dl X11)
endif()

