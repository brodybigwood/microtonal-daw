#pragma once

#include <vector>
#include <cstdint>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class idManager {
    public:
        uint16_t newID();
        void releaseID(uint16_t);
        bool reserveID(uint16_t);        

        void fromJSON(json j);
        json toJSON();

    private:
        std::vector<uint16_t> free_ids;
        uint16_t next_id = 0;
};
