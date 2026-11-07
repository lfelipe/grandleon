// SPDX-License-Identifier: MIT

#include "sevenfold_hybrid_experiment.h"

#include <libdragon.h>

#include <cstdlib>
#include <cstring>

#include "sevenfold_hybrid_assets.h"

namespace grandleon::nintendo64::sevenfold_hybrid_experiment {
namespace {

struct Pair final {
    sprite_t* attacker_idle;
    sprite_t* attacker_attack;
    sprite_t* defender_idle;
    sprite_t* defender_hurt;
};

Pair medieval{};
Pair mythical{};
sprite_t* shadow = nullptr;
sprite_t* impact = nullptr;
bool ready = false;

[[nodiscard]] sprite_t* load(
    const unsigned char* data, std::size_t size
) noexcept {
    void* copy = std::malloc(size);
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, data, size);
    sprite_t* result = sprite_load_buf(copy, static_cast<int>(size));
    if (result == nullptr) std::free(copy);
    return result;
}

void blit(
    sprite_t* sprite, float x, float y, float scale_x, float scale_y,
    bool flip = false
) {
    if (sprite == nullptr) return;
    rdpq_blitparms_t parameters{};
    parameters.scale_x = scale_x;
    parameters.scale_y = scale_y;
    parameters.flip_x = flip;
    rdpq_sprite_blit(sprite, x, y, &parameters);
}

}  // namespace

bool initialize() {
    if (ready) return true;
    medieval = {
        load(hybrid_medieval_knight_male_blue_idle,
             hybrid_medieval_knight_male_blue_idle_size),
        load(hybrid_medieval_knight_male_blue_attack,
             hybrid_medieval_knight_male_blue_attack_size),
        load(hybrid_medieval_knight_female_red_idle,
             hybrid_medieval_knight_female_red_idle_size),
        load(hybrid_medieval_knight_female_red_hurt,
             hybrid_medieval_knight_female_red_hurt_size),
    };
    mythical = {
        load(hybrid_mythical_mage_male_blue_idle,
             hybrid_mythical_mage_male_blue_idle_size),
        load(hybrid_mythical_mage_male_blue_attack,
             hybrid_mythical_mage_male_blue_attack_size),
        load(hybrid_mythical_mage_female_red_idle,
             hybrid_mythical_mage_female_red_idle_size),
        load(hybrid_mythical_mage_female_red_hurt,
             hybrid_mythical_mage_female_red_hurt_size),
    };
    shadow = load(hybrid_shadow, hybrid_shadow_size);
    impact = load(hybrid_impact, hybrid_impact_size);
    ready = medieval.attacker_idle != nullptr &&
        medieval.attacker_attack != nullptr &&
        medieval.defender_idle != nullptr &&
        medieval.defender_hurt != nullptr &&
        mythical.attacker_idle != nullptr &&
        mythical.attacker_attack != nullptr &&
        mythical.defender_idle != nullptr &&
        mythical.defender_hurt != nullptr && shadow != nullptr &&
        impact != nullptr;
    return ready;
}

void draw(Style style, const Frame& frame) {
    if (!ready && !initialize()) return;
    const Pair& pair = style == Style::medieval ? medieval : mythical;
    constexpr float character_scale = 1.38F;
    const float attacker_x =
        13.0F + static_cast<float>(frame.attacker_push);
    const float defender_x =
        197.0F + static_cast<float>(frame.defender_recoil);
    const float attacker_y = 82.0F;
    const float defender_y = frame.defender_hurt ? 86.0F : 82.0F;

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);

    blit(shadow, attacker_x + 27.0F, 171.0F, 1.8F, 1.0F);
    blit(shadow, defender_x + 25.0F, 171.0F, 1.8F, 1.0F);
    blit(
        frame.attacker_pose ? pair.attacker_attack : pair.attacker_idle,
        attacker_x, attacker_y, character_scale, character_scale
    );
    blit(
        frame.defender_hurt ? pair.defender_hurt : pair.defender_idle,
        defender_x, defender_y, character_scale, character_scale, true
    );
    if (frame.impact) {
        blit(impact, 139.0F, 103.0F, 1.0F, 1.0F);
    }
}

std::size_t embedded_bytes() noexcept {
    return hybrid_medieval_knight_male_blue_idle_size +
        hybrid_medieval_knight_male_blue_attack_size +
        hybrid_medieval_knight_female_red_idle_size +
        hybrid_medieval_knight_female_red_hurt_size +
        hybrid_mythical_mage_male_blue_idle_size +
        hybrid_mythical_mage_male_blue_attack_size +
        hybrid_mythical_mage_female_red_idle_size +
        hybrid_mythical_mage_female_red_hurt_size +
        hybrid_shadow_size + hybrid_impact_size;
}

}  // namespace grandleon::nintendo64::sevenfold_hybrid_experiment
