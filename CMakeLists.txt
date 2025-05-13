cmake_minimum_required(VERSION 3.10)
project(MySDL2WebApp)

set(CMAKE_GENERATOR_PLATFORM x64)  # Forces x64 architecture for MSVC

# Enable AddressSanitizer
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")

# Set the sanitizer runtime
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")



file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Add an executable target


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




set(VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/lib/VST_SDK/vst3sdk")

include_directories(${VST3_SDK_PATH})




include_directories(/usr/include/SDL3_ttf/)

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RTAUDIO REQUIRED rtaudio)

target_include_directories(DAW PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${RTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(DAW PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    ${SDL2_LIBRARIES}
    ${RTAUDIO_LIBRARIES}
    ${VST3_SDK_PATH}/build/lib/Debug/libsdk_hosting.a
)


