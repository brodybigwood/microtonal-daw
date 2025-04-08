cmake_minimum_required(VERSION 3.10)
project(MySDL2WebApp)

set(CMAKE_GENERATOR_PLATFORM x64)  # Forces x64 architecture for MSVC


set(SOURCES
    src/main.cpp
    src/PianoRoll.cpp  
    src/Region.cpp
    src/Note.cpp  
    src/Home.cpp 
    src/ControlArea.cpp 
    src/Button.cpp  
    src/WindowHandler.cpp
    src/Instrument.cpp
    src/InstrumentList.cpp
    src/SongRoll.cpp
    #src/Plugin.cpp
    
)

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


# Existing code for SDL3 and SDL3_ttf
include_directories(/usr/include/SDL3_ttf/)

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)  # Add SDL3_ttf

target_link_libraries(DAW PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
target_include_directories(DAW PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(DAW PRIVATE ${SDL2_LIBRARIES})


if(FALSE)
# Correct the path to include the public.sdk directory
target_include_directories(DAW PRIVATE 
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk
)

# If you're linking to static libraries, link the appropriate ones
link_directories(${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib)

# Link libraries for VST3 SDK
#target_link_libraries(DAW PRIVATE sdk vst3 vstgui vstgui_support)

target_link_libraries(DAW PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libsdk_hosting.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libsdk.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libsdk_common.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libpluginterfaces.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libvstgui_standalone.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libvstgui_support.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libvstgui_uidescription.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libvstgui.a
    ${CMAKE_SOURCE_DIR}/lib/vst3sdk/build/lib/libbase.a
)

endif()