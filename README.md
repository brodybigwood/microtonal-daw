# microtonal-daw

currently only runs on linux until i get around to adding further support

Install SDL3/SDL_TTF -> for gui and text rendering
Install rtaudio -> for audio device functionality

get tinyfiledialogs source and header, link it in cmakelists

git clone https://github.com/brodybigwood/microtonal-daw.git

mkdir build 
cd build
cmake ..
make
./DAW
