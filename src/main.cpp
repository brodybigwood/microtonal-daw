
#include "fract.h" 

#include "PianoRoll.h"

#include "Region.h"
#include <thread>
#include <iostream>
#include <SDL3/SDL.h>
#include <vector>
#include <algorithm>



int main() {


    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    
    Region midiRegion1;
    Region midiRegion2;
    SDL_Event e;
    
    double fps = 60;
    
    std::vector<PianoRoll*>* windows = new std::vector<PianoRoll*>();

    PianoRoll* window = new PianoRoll(800, 600, midiRegion1);
    windows->push_back(window);



    
    double frameTime = 1000/fps;   

    SDL_Window* focusedWindow;
    PianoRoll* activeWindow = nullptr;

    Uint32 lastTime = SDL_GetTicks();


    bool running = true;

    bool usingApp;

    while(running) {
                
        double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
        if(timeSinceLastFrame >= frameTime) {   
            lastTime = double(SDL_GetTicks())-frameTime;


            for(int i = windows->size() - 1; i >= 0; --i) {

                    (*windows)[i]->tick();
                
            }
            

            while (SDL_PollEvent(&e)) {
                switch (e.type) {


                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                        focusedWindow = SDL_GetKeyboardFocus();
                        if (focusedWindow != nullptr) {
                            // Iterate through the windows to find the corresponding PianoRoll object
                            for (PianoRoll* window : *windows) {
                                // Assuming that the PianoRoll class has a method to get the SDL_Window* for that window
                                if (window->window == focusedWindow) {
                                    activeWindow = window;
                                    usingApp = true;
                                    break;
                                }
                            }
                        } else {
                            usingApp = false;
                        }
                        break;

                    case SDL_EVENT_KEY_DOWN:

                            if (e.key.scancode == SDL_SCANCODE_N) {
                                PianoRoll* window1 = new PianoRoll(800, 600, midiRegion2);
                                windows->push_back(window1);
                                break;
                            } else {
                                if(usingApp) {
                                    activeWindow->handleInput(e);
                                }
                                break;
                            }


                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                        {  
                            std::cout<<"u pressed close"<<std::endl;
                            auto it = std::find(windows->begin(), windows->end(), activeWindow);
                            delete *it;
                            windows->erase(it);

                            activeWindow = nullptr;
                            usingApp = false;
                            break;
                        }




                    case SDL_EVENT_QUIT:
                        running = false;
                        break;

                    default:
                        if(usingApp) {
                            activeWindow->handleInput(e);
                        }

                        break;
                }
            }







        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));


    }
    std::cout<<"quitting"<<std::endl;
    SDL_Quit();
    TTF_Quit();

}

