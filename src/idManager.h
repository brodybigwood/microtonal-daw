#pragma once

#include <vector>
#include <cstdint>

class idManager {
    public:
        uint16_t newID();
        void releaseID(uint16_t);

    private:
        std::vector<uint16_t> free_ids;
        uint16_t next_id = 0;
};
