// SPDX-License-Identifier: MIT
#include "synth.h"

#include <libdragon.h>

#include <cstdint>

namespace grandleon::n64audio {
namespace {

constexpr int sample_rate = 22050;

// Fixed-point phase accumulators: 32-bit phase, top bit selects the square
// wave's half. increment = frequency * 2^32 / sample_rate.
std::uint32_t phase_increment(int frequency) {
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(frequency) << 32) / sample_rate
    );
}

// The ambient loop: a slow eight-step melody in A minor, one bar each of
// quiet square wave. Held deliberately far below the effects in volume.
constexpr int melody[] = {220, 262, 330, 262, 196, 247, 330, 247};
constexpr int melody_step_samples = sample_rate / 2;

struct Voice final {
    std::uint32_t phase{0};
    std::uint32_t increment{0};
    int remaining{0};
    int amplitude{0};
};

bool music_enabled = true;
int melody_index = 0;
int melody_elapsed = 0;
Voice music;

// One effect voice; a new effect replaces the last. `slide` bends the pitch
// per sample, `noise` replaces the square with a xorshift rattle for impacts.
Voice effect;
std::int32_t effect_slide = 0;
bool effect_noise = false;
std::uint32_t noise_state = 0x2545F491U;

// Queued follow-up notes let an arpeggio live in the same single voice.
struct Step final {
    int frequency;
    int milliseconds;
    int amplitude;
    bool noise;
};
Step queue[4];
int queue_length = 0;
int queue_next = 0;

void start_step(const Step& step) {
    effect.increment = phase_increment(step.frequency);
    effect.remaining = (sample_rate * step.milliseconds) / 1000;
    effect.amplitude = step.amplitude;
    effect_noise = step.noise;
    effect_slide = 0;
}

std::int16_t next_sample() {
    std::int32_t value = 0;

    if (music_enabled) {
        if (music.remaining <= 0) {
            music.increment = phase_increment(melody[melody_index]);
            music.remaining = melody_step_samples;
            melody_index = (melody_index + 1) % 8;
        }
        music.phase += music.increment;
        // A quarter-length envelope tail keeps the loop from droning.
        const int envelope =
            music.remaining < melody_step_samples / 4
                ? music.remaining * 4
                : melody_step_samples;
        const std::int32_t level =
            (900 * envelope) / melody_step_samples;
        value += (music.phase & 0x80000000U) != 0 ? level : -level;
        --music.remaining;
        ++melody_elapsed;
    }

    if (effect.remaining > 0) {
        effect.increment =
            static_cast<std::uint32_t>(
                static_cast<std::int64_t>(effect.increment) + effect_slide
            );
        effect.phase += effect.increment;
        std::int32_t wave;
        if (effect_noise) {
            noise_state ^= noise_state << 13;
            noise_state ^= noise_state >> 17;
            noise_state ^= noise_state << 5;
            wave = (noise_state & 1U) != 0 ? 1 : -1;
        } else {
            wave = (effect.phase & 0x80000000U) != 0 ? 1 : -1;
        }
        value += wave * effect.amplitude;
        --effect.remaining;
        if (effect.remaining == 0 && queue_next < queue_length) {
            start_step(queue[queue_next]);
            ++queue_next;
        }
    }

    if (value > 32000) value = 32000;
    if (value < -32000) value = -32000;
    return static_cast<std::int16_t>(value);
}

}  // namespace

void init() {
    audio_init(sample_rate, 4);
}

void pump() {
    while (audio_can_write() != 0) {
        short* buffer = audio_write_begin();
        const int samples = audio_get_buffer_length();
        for (int index = 0; index < samples; ++index) {
            const std::int16_t sample = next_sample();
            buffer[index * 2] = sample;
            buffer[index * 2 + 1] = sample;
        }
        audio_write_end();
    }
}

void play(Sfx sound) {
    queue_length = 0;
    queue_next = 0;
    switch (sound) {
        case Sfx::select:
            start_step({1320, 30, 3200, false});
            break;
        case Sfx::move:
            start_step({660, 70, 3600, false});
            effect_slide = static_cast<std::int32_t>(phase_increment(6));
            break;
        case Sfx::hit:
            start_step({220, 90, 5200, true});
            break;
        case Sfx::defeat:
            start_step({140, 220, 5600, true});
            break;
        case Sfx::victory:
            start_step({523, 90, 4200, false});
            queue[0] = {659, 90, 4200, false};
            queue[1] = {784, 90, 4200, false};
            queue[2] = {1047, 200, 4600, false};
            queue_length = 3;
            break;
        case Sfx::defeat_battle:
            start_step({392, 120, 4200, false});
            queue[0] = {330, 120, 4200, false};
            queue[1] = {262, 260, 4600, false};
            queue_length = 2;
            break;
        case Sfx::title:
            start_step({523, 110, 4200, false});
            queue[0] = {659, 110, 4200, false};
            queue[1] = {523, 110, 4200, false};
            queue[2] = {784, 260, 4600, false};
            queue_length = 3;
            break;
    }
}

void set_music(bool enabled) {
    music_enabled = enabled;
    if (!enabled) music.remaining = 0;
}

}  // namespace grandleon::n64audio
