#include "TuningTable.h"
#include <iostream>
#include "tinyfiledialogs.h"
#include <fstream>

TuningTable::TuningTable(bool dialog) {

    if(!dialog) {

        for(float note = 0; note <= 127.0f; note++) {
            notes.push_back(note);
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
        std::vector<double> notesD = j["notes"].get<std::vector<double>>();
        notes.assign(notesD.begin(), notesD.end());

    } catch (const std::exception& e) {
        std::cerr << "json parse error: " << e.what() << std::endl;
    }

    
}
