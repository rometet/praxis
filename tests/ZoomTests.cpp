#include "core/ZoomPolicy.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace praxis::companion;
int main() {
    int checks = 0;
    auto require = [&](bool value, char const* message) {
        ++checks;
        if (!value) throw std::runtime_error(message);
    };
    try {
        ZoomOptions options;
        ZoomState zoom;
        require(zoom.update(false, true, true, options, 1.0f) == 1.0f, "disabled is vanilla");
        require(zoom.update(true, true, false, options, 1.0f) == 1.0f, "hold waits for key");
        auto partial = zoom.update(true, true, true, options, 0.02f);
        require(partial < 1.0f && partial > options.targetFov / 70.0f, "press animates toward target");
        require(zoom.update(true, true, false, options, 0.02f) == 1.0f, "release restores immediately");
        options.animation = false;
        require(std::abs(zoom.update(true, true, true, options, 0.02f) - 30.0f / 70.0f) < 0.0001f,
                "nonanimated target FOV");
        require(zoom.scroll(1, options) && zoom.effectiveFov(options) == 28.0f,
                "scroll adjusts target toward zoom");
        require(zoom.scroll(-1, options) && zoom.effectiveFov(options) == 30.0f,
                "reverse scroll restores target");
        short dx = 10, dy = -10;
        zoom.adjustMouse(dx, dy, options, 0.02f);
        require(dx == 4 && dy == -4, "camera sensitivity follows zoom factor");
        options.smoothCamera = true;
        options.cameraSmoothingMultiplier = 1.0f;
        dx = 10; dy = 0;
        zoom.adjustMouse(dx, dy, options, 0.02f);
        dx = 0; dy = 0;
        zoom.adjustMouse(dx, dy, options, 0.02f);
        require(dx > 0, "smooth camera retains decaying input");
        require(zoom.update(true, false, true, options, 0.02f) == 1.0f, "focus or GUI loss restores FOV");
        require(!zoom.scroll(1, options), "scroll does not capture outside zoom");
        options.hold = false;
        require(zoom.update(true, true, false, options, 1.0f) < 1.0f, "non-hold mode engages without key");
        zoom.reset();
        require(zoom.factor() == 1.0f && !zoom.engaged(), "world leave resets state");
        options.targetFov = 100.0f;
        require(!validZoomOptions(options), "invalid target FOV rejected");
        options.targetFov = 5.0f;
        options.animation = false;
        options.smoothCamera = false;
        zoom.update(true, true, false, options, 1.0f);
        int accumulated = 0;
        for (int i = 0; i < 20; ++i) {
            short smallX = 1, smallY = 0;
            zoom.adjustMouse(smallX, smallY, options, 0.02f);
            accumulated += smallX;
        }
        require(accumulated > 0, "fractional sensitivity does not freeze small mouse motion");
        std::cout << "Zoom checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
