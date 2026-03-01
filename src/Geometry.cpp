#include "Geometry.h"

std::pair<std::vector<float>, std::vector<float>> generateCircle(float centerX, float centerY, float radius) {
    std::vector<float> vx;
    std::vector<float> vy;

    // Approx 1 point per pixel along circumference for smoothness
    size_t points = static_cast<size_t>(2.0f * std::numbers::pi_v<float> * radius);
    float step = 2.0f * std::numbers::pi_v<float> / points;

    for (size_t i = 0; i < points; ++i) {
        float theta = i * step;
        vx.push_back(centerX + radius * std::cos(theta));
        vy.push_back(centerY + radius * std::sin(theta));
    }

    return {vx, vy};
}
