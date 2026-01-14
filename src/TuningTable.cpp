#include "TuningTable.h"
#include <iostream>
#include "tinyfiledialogs.h"
#include <fstream>
#include "WindowHandler.h"

std::vector<float> TuningTable::getNoteNums() {
    std::vector<float> nums;
    for(auto note: notes) {
        nums.push_back(note.midiNum);
    }
    return nums;
}

int TuningTable::byID(std::string id) {
    auto it = std::find_if(notes.begin(), notes.end(),
                       [&id](const ScaleNote& note) {
                           return note.identifier == id;
                       });

    if (it != notes.end()) {
        size_t index = std::distance(notes.begin(), it);
        return index;
    } else {
        return 0;
    }   
}

TuningTable::TuningTable(bool dialog) {

    id = -1;

    if(!dialog) {

        for(float note = 0; note <= 127.0f; note++) {
            ScaleNote s = {
                .midiNum = note,
                .identifier = std::to_string(note)
            };
            notes.push_back(s);
        };

        filepath = "";
        name = "12 ed2";

        return;
    }
    std::string home = std::getenv("HOME"); // e.g., "/home/user"

    const char *file = tinyfd_openFileDialog(
        "Select Tuning Table",
        home.c_str(),
        0, nullptr, nullptr, 0
    );

    filepath = file;
    std::filesystem::path p(file);
    name = p.filename().string();

    std::ifstream fs(file);
    if(!fs.is_open()) {
        std::cerr << "couldnt open the file" << std::endl;
        return;
    }

    json j;
    fs >>j;

    try {
        for(json n : j["notes"]) {
            ScaleNote note = {
                .midiNum = n["midiNum"].get<float>(),
                .identifier = n["identifier"].get<std::string>()
            };
            notes.push_back(note);
        }
    } catch (const std::exception& e) {
        std::cerr << "json parse error: " << e.what() << std::endl;
    }

    auto win = WindowHandler::instance()->home->pianoRoll->window;
    SDL_RaiseWindow(win);        // Brings window to front
    SDL_SetWindowKeyboardGrab(win, true); // Grab keyboard input
}

json TuningTable::serialize() {
    json j;

    j["filepath"] = filepath;
    j["name"] = name;

    j["notes"] = json::array();
    for( auto n : notes) {
        json note;
        note["midiNum"] = n.midiNum;
        note["identifier"] = n.identifier;

        j["notes"].push_back(note);
    }

    j["id"] = id;

    return j;
}

void TuningTable::deSerialize(json j) {
    filepath = j["filepath"];
    name = j["name"];

    notes.clear();

    for( auto note : j["notes"] ) {
        ScaleNote n = {
            .midiNum = note["midiNum"],
            .identifier = note["identifier"]
        };
        notes.push_back(n);
    }

    id = j["id"];
}
