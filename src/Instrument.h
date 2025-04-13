#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>


#ifndef INSTRUMENT_H
#define INSTRUMENT_H

class Instrument {

    public:
    Instrument();

        ~Instrument();


        std::string name = "Instrument Rack";

        std::vector<int> outputs;
        std::string outputType = "Output to Mixer Tracks:";


};

#endif