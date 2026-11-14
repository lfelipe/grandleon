// SPDX-License-Identifier: MIT
#include <grandleon/view/stage_overview.hpp>
#include <iostream>

int main() {
    using grandleon::view::StageOverview;
    for (const auto ticks : {1U, 2U, 7U}) {
        StageOverview sweep;
        sweep.begin(20, 14);
        if (sweep.blend() != 0) return 1;
        int previous = 0;
        while (sweep.active()) {
            sweep.advance(ticks);
            const int blend = sweep.blend();
            if (blend < previous || blend > 65536) return 2;
            previous = blend;
        }
        if (sweep.elapsed != 240 || sweep.blend() != 65536) return 3;
        sweep.advance(1000);
        if (sweep.elapsed != 240) return 4;
        sweep.begin(8, 6);
        sweep.advance(20);
        sweep.finish();
        if (sweep.active() || sweep.blend() != 65536) return 5;
    }
    for (int size = 1; size <= 64; ++size) {
        for (int home = 0; home < size; ++home) {
            const int edge = StageOverview::far_edge(size, home);
            const int distance = edge > home ? edge - home : home - edge;
            if (distance < home || distance < size - 1 - home) return 6;
        }
    }
    std::cout << "Stage sweep timing, easing, skip and far edge pass\n";
}
