#include "Geometry.h"

static constexpr float kPi = 3.14159265358979323846f;

std::pair<std::vector<float>, std::vector<float>> generateCircle(float centerX, float centerY, float radius) {
    std::vector<float> vx;
    std::vector<float> vy;

    // Approx 1 point per pixel along circumference for smoothness
    size_t points = static_cast<size_t>(2.0f * kPi * radius);
    float step = 2.0f * kPi / points;

    for (size_t i = 0; i < points; ++i) {
        float theta = i * step;
        vx.push_back(centerX + radius * std::cos(theta));
        vy.push_back(centerY + radius * std::sin(theta));
    }

    return {vx, vy};
}

std::pair<std::vector<float>, std::vector<float>> generateRect(float x, float y, float w, float h) {
    return {
        std::vector<float>{x, x + w, x + w, x},
        std::vector<float>{y, y, y + h, y + h}
    };
}
