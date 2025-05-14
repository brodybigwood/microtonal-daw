# microtonal-midi

currently only runs on linux until i get around to adding further support

Install SDL3/SDL_TTF -> for gui and text rendering
Install rtaudio -> for audio device functionality
Install libdl -> to load vst libraries (.so)

download vst3sdk and link the dir in cmakelists.txt (at the top)

git clone https://github.com/brodybigwood/microtonal-midi.git

mkdir build 
cd build
cmake ..
make
./DAW
