cmake_minimum_required(VERSION 3.10)
project(MySDL2WebApp)

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
    
)

# Add an executable target


add_executable(DAW ${SOURCES})


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


include_directories(/usr/include/SDL3_ttf/)


find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)  # Add SDL3_ttf

target_link_libraries(DAW PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)



# Link SDL2 (this works for both native and Emscripten)
target_include_directories(DAW PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(DAW PRIVATE ${SDL2_LIBRARIES})
