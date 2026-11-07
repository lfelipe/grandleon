// SPDX-License-Identifier: MIT

#include "sevenfold_hybrid_experiment.h"

#include <libdragon.h>

#include <cstdlib>
#include <cstring>

#include "sevenfold_hybrid_assets.h"

namespace grandleon::nintendo64::sevenfold_hybrid_experiment {
namespace {

struct Fighter final {
    sprite_t* idle_a;
    sprite_t* idle_b;
    sprite_t* windup;
    sprite_t* follow_through;
    sprite_t* hurt;
};

struct Pair final {
    Fighter left;
    Fighter right;
};

Pair medieval{};
Pair mythical{};
constexpr int tactical_units = 8;
sprite_t* tactical_idle[tactical_units]{};
sprite_t* tactical_idle_b[tactical_units]{};
sprite_t* tactical_rogue_move[2]{};
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

[[nodiscard]] sprite_t* drawing_for(
    const Fighter& fighter, Pose pose
) noexcept {
    switch (pose) {
        case Pose::idle_b: return fighter.idle_b;
        case Pose::windup: return fighter.windup;
        case Pose::follow_through: return fighter.follow_through;
        case Pose::hurt: return fighter.hurt;
        case Pose::idle_a: return fighter.idle_a;
    }
    return fighter.idle_a;
}

[[nodiscard]] float smoothstep(float amount) noexcept {
    if (amount <= 0.0F) return 0.0F;
    if (amount >= 1.0F) return 1.0F;
    return amount * amount * (3.0F - 2.0F * amount);
}

struct TacticalPlacement final {
    float left;
    float foot;
    float scale;
};

[[nodiscard]] TacticalPlacement tactical_placement(
    int unit, const TacticalFrame& frame
) noexcept {
    constexpr float left[tactical_units] = {
        54.0F, 103.0F, 152.0F, 201.0F,
        32.0F, 96.0F, 160.0F, 224.0F,
    };
    constexpr float foot[tactical_units] = {
        112.0F, 112.0F, 112.0F, 112.0F,
        181.0F, 181.0F, 181.0F, 181.0F,
    };
    constexpr float scale[tactical_units] = {
        0.68F, 0.68F, 0.68F, 0.68F,
        0.90F, 0.90F, 0.90F, 0.90F,
    };
    TacticalPlacement result{left[unit], foot[unit], scale[unit]};
    if (unit != 6) return result;
    const int progress = frame.move_progress < 0 ? 0 :
        frame.move_progress > 100 ? 100 : frame.move_progress;
    const float amount = smoothstep(static_cast<float>(progress) / 100.0F);
    result.left -= 20.0F * amount;
    result.foot -= 46.0F * amount;
    result.scale -= 0.18F * amount;
    return result;
}

void draw_tactical_marks(const TacticalFrame& frame) {
    constexpr int team[tactical_units] = {0, 1, 0, 1, 0, 1, 0, 1};
    constexpr int health[tactical_units] = {10, 8, 7, 9, 6, 10, 5, 8};
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    for (int unit = 0; unit < tactical_units; ++unit) {
        const TacticalPlacement placed = tactical_placement(unit, frame);
        const int width = placed.foot < 150.0F ? 14 : 18;
        const int left = static_cast<int>(
            placed.left + (56.0F * placed.scale - static_cast<float>(width)) /
                2.0F
        );
        const int top = static_cast<int>(placed.foot - 55.0F * placed.scale);
        rdpq_set_prim_color(RGBA32(10, 14, 17, 255));
        rdpq_fill_rectangle(left, top, left + width, top + 4);
        rdpq_set_prim_color(RGBA32(74, 196, 92, 255));
        rdpq_fill_rectangle(
            left + 1, top + 1,
            left + 1 + (width - 2) * health[unit] / 10, top + 3
        );
        rdpq_set_prim_color(
            team[unit] == 0 ? RGBA32(35, 117, 169, 255) :
                              RGBA32(179, 72, 63, 255)
        );
        rdpq_fill_rectangle(left - 5, top, left - 1, top + 4);
    }
}

}  // namespace

bool initialize() {
    if (ready) return true;
    medieval.left = {
        load(hybrid_medieval_knight_male_blue_idle,
             hybrid_medieval_knight_male_blue_idle_size),
        load(hybrid_medieval_knight_male_blue_idle_b,
             hybrid_medieval_knight_male_blue_idle_b_size),
        load(hybrid_medieval_knight_male_blue_windup,
             hybrid_medieval_knight_male_blue_windup_size),
        load(hybrid_medieval_knight_male_blue_attack,
             hybrid_medieval_knight_male_blue_attack_size),
        load(hybrid_medieval_knight_male_blue_hurt,
             hybrid_medieval_knight_male_blue_hurt_size),
    };
    medieval.right = {
        load(hybrid_medieval_knight_female_red_idle,
             hybrid_medieval_knight_female_red_idle_size),
        load(hybrid_medieval_knight_female_red_idle_b,
             hybrid_medieval_knight_female_red_idle_b_size),
        load(hybrid_medieval_knight_female_red_windup,
             hybrid_medieval_knight_female_red_windup_size),
        load(hybrid_medieval_knight_female_red_attack,
             hybrid_medieval_knight_female_red_attack_size),
        load(hybrid_medieval_knight_female_red_hurt,
             hybrid_medieval_knight_female_red_hurt_size),
    };
    mythical.left = {
        load(hybrid_mythical_mage_male_blue_idle,
             hybrid_mythical_mage_male_blue_idle_size),
        load(hybrid_mythical_mage_male_blue_idle_b,
             hybrid_mythical_mage_male_blue_idle_b_size),
        load(hybrid_mythical_mage_male_blue_windup,
             hybrid_mythical_mage_male_blue_windup_size),
        load(hybrid_mythical_mage_male_blue_attack,
             hybrid_mythical_mage_male_blue_attack_size),
        load(hybrid_mythical_mage_male_blue_hurt,
             hybrid_mythical_mage_male_blue_hurt_size),
    };
    mythical.right = {
        load(hybrid_mythical_mage_female_red_idle,
             hybrid_mythical_mage_female_red_idle_size),
        load(hybrid_mythical_mage_female_red_idle_b,
             hybrid_mythical_mage_female_red_idle_b_size),
        load(hybrid_mythical_mage_female_red_windup,
             hybrid_mythical_mage_female_red_windup_size),
        load(hybrid_mythical_mage_female_red_attack,
             hybrid_mythical_mage_female_red_attack_size),
        load(hybrid_mythical_mage_female_red_hurt,
             hybrid_mythical_mage_female_red_hurt_size),
    };
    tactical_idle[0] = load(
        hybrid_tactical_medieval_knight_male_blue_idle,
        hybrid_tactical_medieval_knight_male_blue_idle_size
    );
    tactical_idle[1] = load(
        hybrid_tactical_medieval_archer_female_red_idle,
        hybrid_tactical_medieval_archer_female_red_idle_size
    );
    tactical_idle[2] = load(
        hybrid_tactical_medieval_mage_male_blue_idle,
        hybrid_tactical_medieval_mage_male_blue_idle_size
    );
    tactical_idle[3] = load(
        hybrid_tactical_medieval_stormcaller_female_red_idle,
        hybrid_tactical_medieval_stormcaller_female_red_idle_size
    );
    tactical_idle[4] = load(
        hybrid_tactical_medieval_healer_male_blue_idle,
        hybrid_tactical_medieval_healer_male_blue_idle_size
    );
    tactical_idle[5] = load(
        hybrid_tactical_medieval_commander_female_red_idle,
        hybrid_tactical_medieval_commander_female_red_idle_size
    );
    tactical_idle[6] = load(
        hybrid_tactical_medieval_rogue_male_blue_idle,
        hybrid_tactical_medieval_rogue_male_blue_idle_size
    );
    tactical_idle[7] = load(
        hybrid_tactical_medieval_beast_female_red_idle,
        hybrid_tactical_medieval_beast_female_red_idle_size
    );
    tactical_idle_b[0] = load(
        hybrid_tactical_medieval_knight_male_blue_idle_b,
        hybrid_tactical_medieval_knight_male_blue_idle_b_size
    );
    tactical_idle_b[1] = load(
        hybrid_tactical_medieval_archer_female_red_idle_b,
        hybrid_tactical_medieval_archer_female_red_idle_b_size
    );
    tactical_idle_b[2] = load(
        hybrid_tactical_medieval_mage_male_blue_idle_b,
        hybrid_tactical_medieval_mage_male_blue_idle_b_size
    );
    tactical_idle_b[3] = load(
        hybrid_tactical_medieval_stormcaller_female_red_idle_b,
        hybrid_tactical_medieval_stormcaller_female_red_idle_b_size
    );
    tactical_idle_b[4] = load(
        hybrid_tactical_medieval_healer_male_blue_idle_b,
        hybrid_tactical_medieval_healer_male_blue_idle_b_size
    );
    tactical_idle_b[5] = load(
        hybrid_tactical_medieval_commander_female_red_idle_b,
        hybrid_tactical_medieval_commander_female_red_idle_b_size
    );
    tactical_idle_b[6] = load(
        hybrid_tactical_medieval_rogue_male_blue_idle_b,
        hybrid_tactical_medieval_rogue_male_blue_idle_b_size
    );
    tactical_idle_b[7] = load(
        hybrid_tactical_medieval_beast_female_red_idle_b,
        hybrid_tactical_medieval_beast_female_red_idle_b_size
    );
    tactical_rogue_move[0] = load(
        hybrid_tactical_medieval_rogue_male_blue_move_a,
        hybrid_tactical_medieval_rogue_male_blue_move_a_size
    );
    tactical_rogue_move[1] = load(
        hybrid_tactical_medieval_rogue_male_blue_move_b,
        hybrid_tactical_medieval_rogue_male_blue_move_b_size
    );
    shadow = load(hybrid_shadow, hybrid_shadow_size);
    impact = load(hybrid_impact, hybrid_impact_size);
    ready = shadow != nullptr && impact != nullptr;
    const Fighter* fighters[] = {
        &medieval.left, &medieval.right, &mythical.left, &mythical.right,
    };
    for (const Fighter* fighter : fighters) {
        ready = ready && fighter->idle_a != nullptr &&
            fighter->idle_b != nullptr && fighter->windup != nullptr &&
            fighter->follow_through != nullptr && fighter->hurt != nullptr;
    }
    for (sprite_t* drawing : tactical_idle) {
        ready = ready && drawing != nullptr;
    }
    for (sprite_t* drawing : tactical_idle_b) {
        ready = ready && drawing != nullptr;
    }
    ready = ready && tactical_rogue_move[0] != nullptr &&
        tactical_rogue_move[1] != nullptr;
    return ready;
}

void draw(Style style, const Frame& frame) {
    if (!ready && !initialize()) return;
    const Pair& pair = style == Style::medieval ? medieval : mythical;
    constexpr float character_scale = 1.31F;
    const float left_x = 13.0F + static_cast<float>(frame.left_push) -
        static_cast<float>(frame.left_recoil);
    const float right_x = 197.0F - static_cast<float>(frame.right_push) +
        static_cast<float>(frame.right_recoil);
    const float left_y = frame.left_pose == Pose::hurt ? 86.0F : 82.0F;
    const float right_y = frame.right_pose == Pose::hurt ? 86.0F : 82.0F;

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);

    blit(shadow, left_x + 27.0F, 171.0F, 1.8F, 1.0F);
    blit(shadow, right_x + 25.0F, 171.0F, 1.8F, 1.0F);
    blit(
        drawing_for(pair.left, frame.left_pose), left_x, left_y,
        character_scale, character_scale
    );
    blit(
        drawing_for(pair.right, frame.right_pose), right_x, right_y,
        character_scale, character_scale, true
    );
    if (frame.impact_target >= 0) {
        blit(
            impact, frame.impact_target == 0 ? 104.0F : 139.0F,
            103.0F, 1.0F, 1.0F
        );
    }
}

void draw_matched_board(int left_health, int right_health, bool focus) {
    if (!ready && !initialize()) return;
    constexpr float left_x = 68.0F;
    constexpr float left_foot = 177.0F;
    constexpr float left_scale = 0.62F;
    constexpr float right_x = 184.0F;
    constexpr float right_foot = 123.0F;
    constexpr float right_scale = 0.54F;
    const Pose idle = focus ? Pose::idle_b : Pose::idle_a;

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    blit(shadow, right_x + 7.0F, right_foot - 6.0F, 0.95F, 0.48F);
    blit(
        drawing_for(medieval.right, idle), right_x,
        right_foot - 84.0F * right_scale, right_scale, right_scale, true
    );
    blit(shadow, left_x + 10.0F, left_foot - 7.0F, 1.08F, 0.55F);
    blit(
        drawing_for(medieval.left, idle), left_x,
        left_foot - 84.0F * left_scale, left_scale, left_scale
    );

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    const int left_width = 18;
    const int right_width = 16;
    rdpq_set_prim_color(RGBA32(10, 14, 17, 255));
    rdpq_fill_rectangle(84, 124, 84 + left_width, 128);
    rdpq_fill_rectangle(199, 77, 199 + right_width, 81);
    rdpq_set_prim_color(RGBA32(74, 196, 92, 255));
    rdpq_fill_rectangle(
        85, 125, 85 + 16 * left_health / 12, 127
    );
    rdpq_fill_rectangle(
        200, 78, 200 + 14 * right_health / 11, 80
    );
    rdpq_set_prim_color(RGBA32(35, 117, 169, 255));
    rdpq_fill_rectangle(79, 124, 83, 128);
    rdpq_set_prim_color(RGBA32(179, 72, 63, 255));
    rdpq_fill_rectangle(194, 77, 198, 81);
    if (focus) {
        rdpq_set_prim_color(RGBA32(241, 183, 55, 255));
        rdpq_fill_rectangle(61, 181, 123, 183);
        rdpq_fill_rectangle(176, 127, 231, 129);
    }
}

void draw_tactical(const TacticalFrame& frame) {
    if (!ready && !initialize()) return;
    constexpr int team[tactical_units] = {0, 1, 0, 1, 0, 1, 0, 1};
    int order[tactical_units]{};
    for (int index = 0; index < tactical_units; ++index) order[index] = index;
    for (int index = 1; index < tactical_units; ++index) {
        const int inserted = order[index];
        int position = index;
        while (position > 0 &&
               tactical_placement(order[position - 1], frame).foot >
                   tactical_placement(inserted, frame).foot) {
            order[position] = order[position - 1];
            --position;
        }
        order[position] = inserted;
    }

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_BILINEAR);
    for (int position = 0; position < tactical_units; ++position) {
        const int unit = order[position];
        const TacticalPlacement placed = tactical_placement(unit, frame);
        const float shadow_scale = placed.scale * 1.10F;
        blit(
            shadow,
            placed.left + (56.0F * placed.scale - 32.0F * shadow_scale) /
                2.0F,
            placed.foot - 7.0F, shadow_scale, placed.scale * 0.65F
        );
        const int idle_pose = unit == 6
            ? frame.selected_idle_pose : frame.ambient_idle_pose;
        sprite_t* drawing = idle_pose != 0
            ? tactical_idle_b[unit] : tactical_idle[unit];
        if (unit == 6 && frame.move_pose > 0) {
            drawing = tactical_rogue_move[(frame.move_pose - 1) & 1];
        }
        blit(
            drawing, placed.left, placed.foot - 56.0F * placed.scale,
            placed.scale, placed.scale, team[unit] != 0
        );
    }
    draw_tactical_marks(frame);
}

std::size_t embedded_bytes() noexcept {
    return hybrid_medieval_knight_male_blue_idle_size +
        hybrid_medieval_knight_male_blue_idle_b_size +
        hybrid_medieval_knight_male_blue_windup_size +
        hybrid_medieval_knight_male_blue_attack_size +
        hybrid_medieval_knight_male_blue_hurt_size +
        hybrid_medieval_knight_female_red_idle_size +
        hybrid_medieval_knight_female_red_idle_b_size +
        hybrid_medieval_knight_female_red_windup_size +
        hybrid_medieval_knight_female_red_attack_size +
        hybrid_medieval_knight_female_red_hurt_size +
        hybrid_mythical_mage_male_blue_idle_size +
        hybrid_mythical_mage_male_blue_idle_b_size +
        hybrid_mythical_mage_male_blue_windup_size +
        hybrid_mythical_mage_male_blue_attack_size +
        hybrid_mythical_mage_male_blue_hurt_size +
        hybrid_mythical_mage_female_red_idle_size +
        hybrid_mythical_mage_female_red_idle_b_size +
        hybrid_mythical_mage_female_red_windup_size +
        hybrid_mythical_mage_female_red_attack_size +
        hybrid_mythical_mage_female_red_hurt_size +
        hybrid_shadow_size + hybrid_impact_size;
}

std::size_t tactical_embedded_bytes() noexcept {
    return hybrid_tactical_medieval_knight_male_blue_idle_size +
        hybrid_tactical_medieval_archer_female_red_idle_size +
        hybrid_tactical_medieval_mage_male_blue_idle_size +
        hybrid_tactical_medieval_stormcaller_female_red_idle_size +
        hybrid_tactical_medieval_healer_male_blue_idle_size +
        hybrid_tactical_medieval_commander_female_red_idle_size +
        hybrid_tactical_medieval_rogue_male_blue_idle_size +
        hybrid_tactical_medieval_beast_female_red_idle_size +
        hybrid_tactical_medieval_knight_male_blue_idle_b_size +
        hybrid_tactical_medieval_archer_female_red_idle_b_size +
        hybrid_tactical_medieval_mage_male_blue_idle_b_size +
        hybrid_tactical_medieval_stormcaller_female_red_idle_b_size +
        hybrid_tactical_medieval_healer_male_blue_idle_b_size +
        hybrid_tactical_medieval_commander_female_red_idle_b_size +
        hybrid_tactical_medieval_rogue_male_blue_idle_b_size +
        hybrid_tactical_medieval_beast_female_red_idle_b_size +
        hybrid_tactical_medieval_rogue_male_blue_move_a_size +
        hybrid_tactical_medieval_rogue_male_blue_move_b_size;
}

}  // namespace grandleon::nintendo64::sevenfold_hybrid_experiment
