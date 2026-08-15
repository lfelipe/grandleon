// SPDX-License-Identifier: MIT
// SDL presenter: the same session, drawn into a window and driven by a mouse.
//
// This exists to prove the presenter seam is real. It shares no code with the
// terminal client beyond the interface, and the session does not know which one
// it is talking to.
//
// The board is drawn from the generated placeholder art, embedded at build
// time through tools/placeholder_art/assets/sprites.h the same way the
// Nintendo 64 title screen embeds logo.h: master-palette indices compiled into
// the binary, so the client needs no image decoder and no runtime asset path.
// Terrain autotiles through the generator's mask_to_variant table; units are
// the faction knight sprites. Everything renders through SDL_Texture blits,
// which the software renderer implements, so the offscreen probe still works
// on a machine with no display.
//
// What an author chose, the season the ground is drawn in and the colour each
// faction's characters wear, arrives in the package's presentation section and
// is applied here. A theme is a substitution over master-palette indices, so it
// costs one lookup while a terrain texture is expanded and no drawing code at
// all; character art is expanded without it, because a theme recolours ground
// and must leave a faction's colour alone.
//
// The same section says what the content itself looks like: which art-library
// terrain kind each cell's identity draws as. That matters more here than
// anywhere, because a cell carries a hash and this client has no source project
// to fall back on. Without the join it could only recognise a board whose
// terrain was named after the art library, so a map named `river` would draw as
// grass here and as water everywhere else.

#include <grandleon/desktop/presenters.hpp>

#include <grandleon/core/content_identity.hpp>

#include <SDL2/SDL.h>

#include "grandleon/view/board_view.hpp"

#include "sprites.h"
#include "themes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace grandleon::desktop {
namespace {

namespace sim = simulation;
namespace pr = package_runtime;
namespace view = grandleon::view;

// A theme substitutes one master-palette index for another, so its table has
// to cover every index the sprite sheets can hold. Nothing else keeps the two
// generated headers in step.
static_assert(
    sizeof(grandleon_theme_palette[0]) ==
        sizeof(grandleon_sprites_palette) / sizeof(grandleon_sprites_palette[0]),
    "the theme substitution table does not cover the master palette"
);

// Two native texels per window pixel: an integer scale keeps the nearest-
// neighbour blit exact, which is what lets the probe match palette colours
// without a tolerance.
constexpr int tile = grandleon_sprites_tile * 2;
constexpr int margin = 24;
constexpr int panel = 120;

struct Colour final {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

constexpr Colour background{16, 26, 27};
constexpr Colour highlight{242, 193, 78};
constexpr Colour health{121, 208, 110};

constexpr Colour palette_colour(unsigned char index) {
    return Colour{
        grandleon_sprites_palette[index][0],
        grandleon_sprites_palette[index][1],
        grandleon_sprites_palette[index][2],
    };
}

void fill(SDL_Renderer* renderer, const SDL_Rect& rect, Colour colour) {
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

// Expands palette indices into an RGBA texture. Index zero is the transparent
// slot, so terrain shows through a sprite's empty pixels.
//
// `substitution` is a theme: the master-palette index to paint in place of each
// one the art holds. A null table is the identity, which is what character art
// is always expanded with: a theme recolours ground and nothing else.
SDL_Texture* make_texture(
    SDL_Renderer* renderer,
    const unsigned char* indices,
    int width,
    int height,
    const unsigned char* substitution
) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_RGBA32
    );
    if (surface == nullptr) return nullptr;
    for (int y = 0; y < height; ++y) {
        auto* row = static_cast<std::uint8_t*>(surface->pixels) +
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(surface->pitch);
        for (int x = 0; x < width; ++x) {
            const unsigned char authored = indices[y * width + x];
            const unsigned char index =
                substitution == nullptr ? authored : substitution[authored];
            const Colour colour = palette_colour(index);
            row[x * 4 + 0] = colour.r;
            row[x * 4 + 1] = colour.g;
            row[x * 4 + 2] = colour.b;
            // Transparency is decided by what the art holds, not by what the
            // theme paints: a substitution may recolour a pixel and may never
            // add one or take one away.
            row[x * 4 + 3] =
                authored == grandleon_sprites_transparent ? 0 : 255;
        }
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture != nullptr) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    }
    return texture;
}

constexpr int blob_sheet_width =
    grandleon_sprites_blob_columns * grandleon_sprites_tile;
constexpr int blob_sheet_height =
    grandleon_sprites_blob_rows * grandleon_sprites_tile;

class SdlPresenter final : public Presenter {
public:
    SdlPresenter(bool probe, const pr::Presentation& presentation)
        : probe_(probe),
          presentation_(presentation),
          theme_(
              presentation.theme < grandleon_theme_count
                  ? presentation.theme
                  : static_cast<std::uint8_t>(pr::default_theme)
          ) {}

    ~SdlPresenter() override {
        for (SDL_Texture* texture : terrain_textures_) {
            if (texture != nullptr) SDL_DestroyTexture(texture);
        }
        for (SDL_Texture* texture : unit_textures_) {
            if (texture != nullptr) SDL_DestroyTexture(texture);
        }
        if (shadow_texture_ != nullptr) SDL_DestroyTexture(shadow_texture_);
        if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
        if (window_ != nullptr) SDL_DestroyWindow(window_);
        if (started_) SDL_Quit();
    }

    [[nodiscard]] bool start() {
        // Check for a display before initialising. SDL can block trying to
        // reach one that is not there, and a client that hangs on a headless
        // machine is worse than one that says it cannot draw.
        //
        // An explicit SDL_VIDEODRIVER wins: that is how the offscreen driver
        // renders without a display, which is what the probe test uses.
        if (SDL_getenv("SDL_VIDEODRIVER") == nullptr &&
            SDL_getenv("DISPLAY") == nullptr &&
            SDL_getenv("WAYLAND_DISPLAY") == nullptr) {
            std::cerr << "no display available; run without --sdl\n";
            return false;
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "SDL could not start: " << SDL_GetError() << '\n';
            return false;
        }
        started_ = true;
        return true;
    }

    void present_dialogue(const package_runtime::Dialogue& dialogue) override {
        // Text rendering is not worth a font dependency yet, so dialogue still
        // goes to the console. Stated plainly rather than silently dropped.
        for (const package_runtime::DialogueLine& line : dialogue.lines) {
            std::cout << "  " << line.speaker << ": " << line.text << '\n';
        }
    }

    void battle_begins(
        const sim::EncounterSnapshot& snapshot,
        const Roster&,
        sim::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) override {
        player_side_ = player_side;
        terrain_ = terrain;
        // A selection carried over from an earlier battle would name a unit
        // that no longer exists.
        selected_ = 0;
        board_width_ = snapshot.width;
        board_height_ = snapshot.height;
        load_terrain_table();
        // High ground is drawn above the row it stands in, so the window
        // reserves the tallest lift above the board. A board with no raised
        // terrain reserves nothing and is laid out exactly as before.
        const int lift = view::headroom(
            highest_elevation(), view::elevation_step_for(tile), tile
        );
        camera_ = view::Camera{
            0, 0, snapshot.width, snapshot.height,
            snapshot.width, snapshot.height
        };
        projection_ = view::Projection{
            margin, margin + lift, tile, view::elevation_step_for(tile)
        };
        const int width = margin * 2 + snapshot.width * tile;
        const int height =
            margin * 2 + lift + snapshot.height * tile + panel;
        if (window_ != nullptr) {
            // A later battle may sit on a different board, so the window is
            // resized to fit it rather than kept at the first battle's size.
            SDL_SetWindowSize(window_, width, height);
            return;
        }
        window_ = SDL_CreateWindow(
            "Grandleon",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width,
            height,
            probe_ ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN
        );
        if (window_ == nullptr) {
            std::cerr << "window could not open: " << SDL_GetError() << '\n';
            return;
        }
        renderer_ = SDL_CreateRenderer(
            window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        );
        if (renderer_ == nullptr) {
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        if (renderer_ != nullptr) load_sprites();
    }

    void draw(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) override {
        // The numbered roster is the terminal client's addressing scheme and
        // this window has no use for it: a token here is clicked, never named.
        static_cast<void>(roster);
        if (renderer_ == nullptr) return;
        SDL_SetRenderDrawColor(
            renderer_, background.r, background.g, background.b, 255
        );
        SDL_RenderClear(renderer_);

        // Everything the frame draws, in the order the shared presentation
        // model puts it in: the ground low-to-high, then a shadow under every
        // unit, then the units themselves.
        build_draw_list(snapshot);
        for (int i = 0; i < draw_list_.size(); ++i) {
            const view::DrawItem& item = draw_list_[i];
            if (item.layer != view::Layer::terrain) continue;
            const SDL_Rect source{
                view::variant_source_x(
                    item.variant, grandleon_sprites_blob_columns,
                    grandleon_sprites_tile
                ),
                view::variant_source_y(
                    item.variant, grandleon_sprites_blob_columns,
                    grandleon_sprites_tile
                ),
                grandleon_sprites_tile,
                grandleon_sprites_tile
            };
            const SDL_Rect cell{item.x, item.y, item.w, item.h};
            SDL_Texture* sheet = terrain_textures_[
                static_cast<std::size_t>(item.sheet)];
            if (sheet != nullptr) {
                SDL_RenderCopy(renderer_, sheet, &source, &cell);
            }
        }
        for (int i = 0; i < draw_list_.size(); ++i) {
            const view::DrawItem& item = draw_list_[i];
            if (item.layer != view::Layer::shadow) continue;
            const SDL_Rect cell{item.x, item.y, item.w, item.h};
            if (shadow_texture_ != nullptr) {
                SDL_RenderCopy(renderer_, shadow_texture_, nullptr, &cell);
            }
        }

        for (int i = 0; i < draw_list_.size(); ++i) {
            const view::DrawItem& item = draw_list_[i];
            if (item.layer != view::Layer::unit) continue;
            const sim::UnitSnapshot& unit = snapshot.units[item.subject];
            const int x = item.x;
            const int y = item.y;
            SDL_Texture* sprite = unit_textures_[colour_of(unit)];
            const SDL_Rect cell{x, y, tile, tile};
            if (sprite != nullptr) {
                SDL_RenderCopy(renderer_, sprite, nullptr, &cell);
            }
            if (unit.id == snapshot.active_unit_id) {
                const SDL_Rect ring{x + 2, y + 2, tile - 4, tile - 4};
                SDL_SetRenderDrawColor(
                    renderer_, highlight.r, highlight.g, highlight.b, 255
                );
                SDL_RenderDrawRect(renderer_, &ring);
            }
            // Health as a bar, and nothing written on the token.
            //
            // This window is steered with a mouse: a token is clicked, never
            // named, so nothing here has to address a character and no
            // identifier is drawn on one. The terminal presenter beside it
            // draws the numbered roster, because there a player types
            // `move 3 5 2` and needs to know which character is which.
            const int span = std::max(
                1,
                (tile - 16) * unit.health /
                    std::max<std::int16_t>(1, unit.maximum_health)
            );
            const SDL_Rect bar{x + 8, y + tile - 12, span, 4};
            fill(renderer_, bar, health);
        }

        SDL_RenderPresent(renderer_);
    }

    void report(const sim::CommandResult&, const Roster&) override {}

    void refused(sim::CommandError error) override {
        std::cout << "  refused: " << sim::error_name(error) << '\n';
    }

    void show_state(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<sim::ObjectiveDefinition>& objectives
    ) override {
        static_cast<void>(objectives);
        std::cout << "  round " << snapshot.round << "  canonical hash: "
                  << std::hex << canonical_hash << std::dec << '\n';
    }

    void battle_ended(
        const sim::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash
    ) override {
        std::cout << (snapshot.outcome == sim::Outcome::first_side_won
                          ? "Blue"
                          : "Red")
                  << " won.  canonical hash: " << std::hex << canonical_hash
                  << std::dec << '\n';
    }

    void campaign_ended() override {
        std::cout << "THE END - thanks for playing.\n"
                  << "github.com/lfelipe/grandleon\n";
    }

    Intent next_intent(
        const sim::EncounterSnapshot& snapshot,
        const Roster& roster
    ) override {
        if (renderer_ == nullptr) return {IntentKind::quit, 0, 0, {}};
        if (probe_) {
            report_framebuffer(snapshot);
            return {IntentKind::quit, 0, 0, {}};
        }
        SDL_Event event;
        while (SDL_WaitEvent(&event) != 0) {
            if (event.type == SDL_QUIT) return {IntentKind::quit, 0, 0, {}};
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    return {IntentKind::quit, 0, 0, {}};
                }
                if (event.key.keysym.sym == SDLK_SPACE && selected_ != 0) {
                    const sim::UnitId actor = selected_;
                    selected_ = 0;
                    return {IntentKind::wait, actor, 0, {}};
                }
                continue;
            }
            if (event.type != SDL_MOUSEBUTTONDOWN) continue;

            sim::Position at{0, 0};
            if (!cell_under(event.button.x, event.button.y, at)) continue;

            const sim::UnitSnapshot* occupant = nullptr;
            for (const sim::UnitSnapshot& unit : snapshot.units) {
                if (sim::on_board(unit) && unit.position == at) occupant = &unit;
            }

            // Same gesture rule as the browser's Play mode: a marked enemy is
            // an attack, an empty tile is a move, one of yours is a selection.
            if (selected_ != 0 && occupant != nullptr &&
                occupant->side != player_side_) {
                const sim::UnitId actor = selected_;
                selected_ = 0;
                return {IntentKind::attack, actor, occupant->id, {}};
            }
            if (occupant != nullptr && occupant->side == player_side_) {
                selected_ = occupant->id;
                draw(snapshot, roster);
                continue;
            }
            if (selected_ != 0 && occupant == nullptr) {
                const sim::UnitId actor = selected_;
                selected_ = 0;
                return {IntentKind::move_to, actor, 0, at};
            }
        }
        return {IntentKind::quit, 0, 0, {}};
    }

private:
    // Which sheet each terrain identity draws from, and how high it stands.
    // Both are pure art-library data with no renderer in them, and the layout
    // needs them before there is a window to lay out, because the board's
    // headroom is decided from the terrain it is about to draw, so they are
    // filled here rather than alongside the textures.
    void load_terrain_table() {
        sheet_of_kind_.fill(0);
        for (int index = 0; index < grandleon_sprites_terrain_count; ++index) {
            terrain_ids_[static_cast<std::size_t>(index)] =
                core::stable_content_id_v1(
                    grandleon_sprites_terrain_names[index]);
            // The sheets are in the generator's compositing order and the
            // registry is in its keyword order, so the two are joined by name
            // rather than by position. That join answers both questions this
            // renderer has: how high a sheet's terrain stands, and which sheet
            // a terrain kind draws from.
            terrain_elevation_[static_cast<std::size_t>(index)] = 0;
            for (int kind = 0; kind < grandleon_terrain_kind_count; ++kind) {
                if (std::string(grandleon_terrain_kind_names[kind]) ==
                    grandleon_sprites_terrain_names[index]) {
                    terrain_elevation_[static_cast<std::size_t>(index)] =
                        grandleon_terrain_elevation[kind];
                    sheet_of_kind_[static_cast<std::size_t>(kind)] =
                        static_cast<std::size_t>(index);
                }
            }
        }
    }

    void load_sprites() {
        for (int index = 0; index < grandleon_sprites_terrain_count; ++index) {
            terrain_textures_[static_cast<std::size_t>(index)] = make_texture(
                renderer_,
                grandleon_sprites_terrain_blobs[index],
                blob_sheet_width,
                blob_sheet_height,
                grandleon_theme_palette[theme_]
            );
        }
        // One shadow for every unit, on the character grid so it lines up
        // with their feet. A theme recolours ground, not the shadow cast on
        // it, so this is expanded through the identity palette like the
        // character art beside it.
        shadow_texture_ = make_texture(
            renderer_,
            grandleon_sprites_shadow,
            grandleon_sprites_tile,
            grandleon_sprites_tile,
            nullptr
        );
        // Every unit is a knight for now: nothing tells a presenter a unit's
        // class yet, only its identity and its side. Class-aware sprites need
        // the archetype to cross the presenter seam, which belongs to the
        // shared presentation model, not to one client.
        //
        // The colour, though, is the author's: one knight per colour on the
        // art library's menu, and a unit picks the one its faction chose.
        for (int colour = 0; colour < grandleon_sprites_faction_count;
             ++colour) {
            const std::string wanted =
                std::string("knight_") +
                grandleon_sprites_faction_names[colour];
            for (int index = 0;
                 index < grandleon_sprites_character_count; ++index) {
                if (std::string(grandleon_sprites_character_names[index]) !=
                    wanted) {
                    continue;
                }
                unit_textures_[static_cast<std::size_t>(colour)] = make_texture(
                    renderer_,
                    grandleon_sprites_characters[index],
                    grandleon_sprites_tile,
                    grandleon_sprites_tile,
                    nullptr
                );
            }
        }
    }

    // The colour a unit's characters wear: the one its faction chose, and only
    // when no faction claims it, the one its side's position gives it. That
    // fallback is what a package with no presentation section resolves to for
    // every unit, so such a package draws the blue-and-red board it always did.
    [[nodiscard]] std::size_t colour_of(const sim::UnitSnapshot& unit) const {
        const std::uint8_t chosen =
            presentation_.colour_of_unit_type(unit.unit_type_id);
        if (chosen < grandleon_sprites_faction_count) return chosen;
        return unit.side == sim::Side::first ? 0U : 1U;
    }

    // The cell a click landed on, answered by the same projection that drew
    // the board rather than by dividing the window into a flat grid.
    //
    // A raised cell overlaps the cell behind it, so a point can fall inside
    // two rectangles. The one the player sees is the one drawn last, so this
    // takes the largest depth key among the rectangles containing the point.
    // That is the renderer's own rule, and it keeps the click target on the art
    // rather than under it.
    [[nodiscard]] bool cell_under(
        int window_x, int window_y, sim::Position& found
    ) const {
        bool any = false;
        std::int64_t frontmost = -1;
        for (int y = 0; y < board_height_; ++y) {
            for (int x = 0; x < board_width_; ++x) {
                const int elevation = elevation_at(x, y);
                const int left = projection_.cell_left(camera_, x);
                const int top = projection_.cell_top(camera_, y, elevation);
                if (window_x < left || window_x >= left + tile) continue;
                if (window_y < top || window_y >= top + tile) continue;
                const std::int64_t depth = view::depth_key(
                    view::Layer::terrain, elevation, 0, y, x
                );
                if (any && depth <= frontmost) continue;
                frontmost = depth;
                found = sim::Position{
                    static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)
                };
                any = true;
            }
        }
        return any;
    }

    // The sheet a cell draws from.
    //
    // The package's terrain join answers this: the compiler resolved every
    // authored name to an art-library kind, and a kind names a sheet. That is
    // the whole point of carrying the join. A cell's identity is a hash, so
    // without it this client could only recognise a board whose terrain was
    // named after the art library itself, and a map named `river` would draw as
    // grass here while the editor and the console drew water.
    //
    // The identity match is still the fallback, for a package written before
    // the join existed: such a board draws exactly what it drew then. So do
    // boards with no terrain data and identities the package cannot place.
    [[nodiscard]] std::size_t sheet_index(int x, int y) const {
        const std::size_t cell = static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(board_width_) +
                                 static_cast<std::size_t>(x);
        if (static_cast<std::size_t>(board_width_) *
                static_cast<std::size_t>(board_height_) != terrain_.size()) {
            return 0;
        }
        const std::uint8_t kind =
            presentation_.kind_of_terrain(terrain_[cell]);
        if (kind < grandleon_terrain_kind_count) {
            return sheet_of_kind_[static_cast<std::size_t>(kind)];
        }
        for (std::size_t index = 0; index < terrain_ids_.size(); ++index) {
            if (terrain_ids_[index] == terrain_[cell]) return index;
        }
        return 0;
    }

    // The generator's eight-bit neighbour mask, through the shared model's
    // one implementation of it: a bit per neighbour that holds the same
    // terrain, off-board treated as different so the board edge reads as a
    // coastline. mask_to_variant collapses the diagonals.
    [[nodiscard]] std::uint8_t neighbour_mask(int x, int y) const {
        const bool no_data = static_cast<std::size_t>(board_width_) *
                                 static_cast<std::size_t>(board_height_) !=
                             terrain_.size();
        const auto at = [&](int cx, int cy) {
            return terrain_[static_cast<std::size_t>(cy) *
                                static_cast<std::size_t>(board_width_) +
                            static_cast<std::size_t>(cx)];
        };
        return view::neighbour_mask(x, y, [&](int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= board_width_ ||
                ny >= board_height_) {
                return false;
            }
            return no_data || at(nx, ny) == at(x, y);
        });
    }

    // How many levels above the valley floor a cell reads as, from the art
    // library's own table. Presentation only: no rule reads it and no hashed
    // value has ever seen it.
    [[nodiscard]] int elevation_at(int x, int y) const {
        return terrain_elevation_[sheet_index(x, y)];
    }

    [[nodiscard]] bool occupied(
        const sim::EncounterSnapshot& snapshot, int x, int y
    ) const {
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (sim::on_board(unit) && unit.position.x == x &&
                unit.position.y == y) {
                return true;
            }
        }
        return false;
    }

    // How many of a cell's own rows a *unit* in the neighbouring row is drawn
    // over: the difference in the two cells' lifts, through the projection
    // that drew both. Two cells at unequal heights share exactly that many
    // rows, and the unit standing in the other one is painted across them.
    //
    // Both directions, because a lift moves two rectangles apart in one
    // direction only and a scan can be wrong from either side. A unit in the
    // row in front, standing higher, is drawn over the bottom of this cell; a
    // unit in the row behind is drawn over the top of it when *this* cell is
    // the higher one and its rectangle reaches back over that row.
    //
    // Only a unit, and only the adjacent row. The layers are a fixed stack of
    // all terrain, then all shadows, then all units, so ground never hides
    // anything standing on another cell, and a lift is bounded well below a
    // whole tile, so nothing two rows away can reach here at all.
    [[nodiscard]] int rows_owned_by(
        const sim::EncounterSnapshot& snapshot, int x, int y, int row
    ) const {
        if (row < 0 || row >= board_height_) return 0;
        if (!occupied(snapshot, x, row)) return 0;
        const int here = projection_.lift(elevation_at(x, y));
        const int there = projection_.lift(elevation_at(x, row));
        const int taller = row < y ? here - there : there - here;
        return taller > 0 ? taller : 0;
    }

    [[nodiscard]] int highest_elevation() const {
        int highest = 0;
        for (int y = 0; y < board_height_; ++y) {
            for (int x = 0; x < board_width_; ++x) {
                const int level = elevation_at(x, y);
                if (level > highest) highest = level;
            }
        }
        return highest;
    }

    // The frame's draw list. The presenter walks it three times rather than
    // once because the ring, the health bar and the label are drawn over each
    // unit as it goes, but the order within each layer is the model's.
    void build_draw_list(const sim::EncounterSnapshot& snapshot) {
        const std::size_t needed =
            static_cast<std::size_t>(board_width_) *
                static_cast<std::size_t>(board_height_) +
            snapshot.units.size() * 2;
        if (draw_storage_.size() < needed) draw_storage_.resize(needed);
        draw_list_ = view::DrawList(
            draw_storage_.data(), static_cast<int>(draw_storage_.size())
        );
        for (int y = 0; y < board_height_; ++y) {
            for (int x = 0; x < board_width_; ++x) {
                const std::size_t sheet = sheet_index(x, y);
                const int variant = view::autotile_variant(
                    neighbour_mask(x, y), grandleon_sprites_mask_to_variant
                );
                // The sheet is also the batch: cells at one elevation cannot
                // overlap, so grouping them by texture costs the picture
                // nothing and saves the renderer a texture switch.
                draw_list_.add(
                    view::terrain_item(
                        camera_, projection_, x, y, elevation_at(x, y),
                        static_cast<int>(sheet), variant,
                        static_cast<std::uint32_t>(sheet)
                    ),
                    static_cast<int>(sheet)
                );
            }
        }
        for (std::size_t i = 0; i < snapshot.units.size(); ++i) {
            const sim::UnitSnapshot& unit = snapshot.units[i];
            // Only what is standing there is drawn. This loop and the probe's
            // census below have to agree on that word or the probe asserts
            // about pixels nobody painted.
            if (!sim::on_board(unit)) continue;
            const int elevation =
                elevation_at(unit.position.x, unit.position.y);
            draw_list_.add(view::billboard_item(
                camera_, projection_, view::Layer::shadow, unit.position.x,
                unit.position.y, elevation, 0,
                static_cast<std::uint32_t>(i)
            ));
            draw_list_.add(view::billboard_item(
                camera_, projection_, view::Layer::unit, unit.position.x,
                unit.position.y, elevation, 0,
                static_cast<std::uint32_t>(i)
            ));
        }
        draw_list_.sort();
    }

    // Reads the framebuffer back and reports what is actually on it.
    //
    // Terrain is textured, so a cell centre does not hold one flat colour;
    // what is provable instead is that every centre texel is an exact master-
    // palette colour, and that a unit's cell contains its faction ramp. The
    // team disc and tabard guarantee those pixels, and no terrain sheet in any
    // theme paints an index any faction ramp holds. Exact matching is safe
    // because the blit is an integer-scale nearest-neighbour copy.
    //
    // `blue` and `red` are the first and second sides, not the colours blue and
    // red. Once an author can choose colours, the probe has to ask which
    // colours each side actually drew rather than assume the first two on the
    // menu; a project that chooses none answers blue and red, which is why the
    // numbers are what they always were.
    void report_framebuffer(const sim::EncounterSnapshot& snapshot) {
        // The read-back covers the headroom the layout reserved above the
        // board as well as the board itself, and every cell is located
        // through the same projection that drew it, so a cell standing on
        // high ground is sampled where its art actually is, and a flat board
        // is sampled exactly where it always was.
        const int lift = projection_.origin_y - margin;
        const int board_width = snapshot.width * tile;
        const int board_height = lift + snapshot.height * tile;
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(board_width) *
            static_cast<std::size_t>(board_height) * 4U
        );
        const SDL_Rect area{margin, margin, board_width, board_height};
        if (SDL_RenderReadPixels(
                renderer_, &area, SDL_PIXELFORMAT_RGBA32, pixels.data(),
                board_width * 4
            ) != 0) {
            std::cout << "PROBE readback failed: " << SDL_GetError() << '\n';
            return;
        }

        // Which colours each side put on the board, asked of the units rather
        // than assumed from the menu. The same predicate the draw loop uses,
        // for the same reason: a census wider than the draw loop expects a
        // colour nobody painted.
        bool side_colours[2][grandleon_sprites_faction_count] = {};
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (!sim::on_board(unit)) continue;
            side_colours[unit.side == sim::Side::first ? 0 : 1]
                        [colour_of(unit)] = true;
        }

        int blue_cells = 0;
        int red_cells = 0;
        int terrain_cells = 0;
        int unknown_cells = 0;
        std::string first_blue = "none";
        std::string first_red = "none";
        std::uint64_t ground_digest = 0xcbf29ce484222325ULL;

        const auto pixel_at = [&](int px, int py) {
            const std::size_t offset =
                (static_cast<std::size_t>(py) *
                     static_cast<std::size_t>(board_width) +
                 static_cast<std::size_t>(px)) * 4U;
            return Colour{
                pixels[offset], pixels[offset + 1], pixels[offset + 2]
            };
        };

        for (std::uint16_t y = 0; y < snapshot.height; ++y) {
            for (std::uint16_t x = 0; x < snapshot.width; ++x) {
                const int cell_left =
                    projection_.cell_left(camera_, x) - margin;
                const int cell_top =
                    projection_.cell_top(camera_, y, elevation_at(x, y)) -
                    margin;
                bool found[2] = {false, false};
                // The scan covers the rows this cell owns. Where two rows
                // stand at different heights their rectangles overlap, and in
                // that band the unit from the other row is what was painted,
                // so scanning it would count a neighbour's unit as this cell's
                // occupant. The model's lift cap keeps each band under half a
                // tile, so what remains is always the middle of the cell,
                // centre included, whatever the board is made of.
                const int head = rows_owned_by(snapshot, x, y, y - 1);
                const int foot = rows_owned_by(snapshot, x, y, y + 1);
                for (int py = cell_top + head; py < cell_top + tile - foot;
                     ++py) {
                    for (int px = cell_left; px < cell_left + tile; ++px) {
                        const Colour colour = pixel_at(px, py);
                        for (int ramp = 0;
                             ramp < grandleon_sprites_faction_count; ++ramp) {
                            if (!in_ramp(colour, ramp)) continue;
                            for (int side = 0; side < 2; ++side) {
                                if (side_colours[side][ramp]) {
                                    found[side] = true;
                                }
                            }
                        }
                    }
                }
                const Colour centre =
                    pixel_at(cell_left + tile / 2, cell_top + tile / 2);
                const std::string where =
                    std::to_string(x) + "," + std::to_string(y);
                if (found[0] && !found[1]) {
                    ++blue_cells;
                    if (first_blue == "none") first_blue = where;
                } else if (found[1] && !found[0]) {
                    ++red_cells;
                    if (first_red == "none") first_red = where;
                } else if (!found[0] && !found[1] && in_palette(centre)) {
                    ++terrain_cells;
                    // The ground's own digest, so that "this board was drawn
                    // in winter" is provable by comparing two runs rather than
                    // by trusting the theme name the presenter printed. It is
                    // never compared against a constant: a test themes one
                    // project two ways and asserts the two differ.
                    for (const std::uint8_t channel :
                         {centre.r, centre.g, centre.b}) {
                        ground_digest ^= channel;
                        ground_digest *= 0x100000001b3ULL;
                    }
                } else {
                    ++unknown_cells;
                }
            }
        }

        std::cout << "PROBE board=" << snapshot.width << 'x' << snapshot.height
                  << " blue=" << blue_cells
                  << " red=" << red_cells
                  << " terrain=" << terrain_cells
                  << " unknown=" << unknown_cells
                  << " firstblue=" << first_blue
                  << " firstred=" << first_red
                  << " theme=" << grandleon_theme_names[theme_]
                  << " firstcolours=" << colour_names(side_colours[0])
                  << " secondcolours=" << colour_names(side_colours[1])
                  << " ground=" << std::hex << ground_digest << std::dec
                  << '\n';
        // The board's 2.5D geometry, reported separately because it is
        // asserted differently: everything above is read out of the
        // framebuffer, while these two are what the presenter decided before
        // drawing a pixel. A test authoring raised terrain can therefore
        // check the lift against the elevation the art library gives that
        // terrain, rather than against whatever the renderer happened to do
        // and then sampled back through its own projection.
        int raised_cells = 0;
        for (int y = 0; y < board_height_; ++y) {
            for (int x = 0; x < board_width_; ++x) {
                if (elevation_at(x, y) > 0) ++raised_cells;
            }
        }
        // `tile` is reported beside them so a test can restate the model's
        // bound of three eighths of a cell rather than accept whatever
        // headroom the presenter reserved.
        std::cout << "PROBE step=" << projection_.elevation_step
                  << " tile=" << tile
                  << " headroom=" << (projection_.origin_y - margin)
                  << " raised=" << raised_cells << '\n';
    }

    // The colours one side drew, named, so a themed board can be asserted by
    // what an author chose rather than by a count alone.
    static std::string colour_names(
        const bool (&used)[grandleon_sprites_faction_count]
    ) {
        std::string names;
        for (int colour = 0; colour < grandleon_sprites_faction_count;
             ++colour) {
            if (!used[colour]) continue;
            if (!names.empty()) names += '+';
            names += grandleon_sprites_faction_names[colour];
        }
        return names.empty() ? std::string("none") : names;
    }

    static bool in_ramp(Colour colour, int ramp) {
        for (int step = 0; step < grandleon_sprites_faction_ramp_length;
             ++step) {
            const unsigned char index =
                grandleon_sprites_faction_ramps[ramp][step];
            if (index == grandleon_sprites_transparent) continue;
            const Colour entry = palette_colour(index);
            if (colour.r == entry.r && colour.g == entry.g &&
                colour.b == entry.b) {
                return true;
            }
        }
        return false;
    }

    static bool in_palette(Colour colour) {
        constexpr std::size_t entries =
            sizeof(grandleon_sprites_palette) /
            sizeof(grandleon_sprites_palette[0]);
        for (std::size_t index = 1; index < entries; ++index) {
            const Colour entry =
                palette_colour(static_cast<unsigned char>(index));
            if (colour.r == entry.r && colour.g == entry.g &&
                colour.b == entry.b) {
                return true;
            }
        }
        return false;
    }

    bool probe_{false};
    // What the author chose, read out of the package. Empty for a package that
    // carries no presentation section.
    pr::Presentation presentation_;
    std::uint8_t theme_{pr::default_theme};
    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    bool started_{false};
    sim::Side player_side_{sim::Side::first};
    sim::UnitId selected_{0};
    std::vector<std::uint64_t> terrain_;
    int board_width_{0};
    int board_height_{0};
    std::array<std::uint64_t, grandleon_sprites_terrain_count> terrain_ids_{};
    // How many levels each sprite sheet's terrain stands above the valley,
    // in the sheets' own order rather than the registry's, so a cell resolves
    // its lift with the sheet lookup it already does.
    std::array<int, grandleon_sprites_terrain_count> terrain_elevation_{};
    // Which sheet each art-library terrain kind draws from, in the registry's
    // keyword order, which is the order the package's terrain join speaks in.
    std::array<std::size_t, grandleon_terrain_kind_count> sheet_of_kind_{};
    std::array<SDL_Texture*, grandleon_sprites_terrain_count>
        terrain_textures_{};
    std::array<SDL_Texture*, grandleon_sprites_faction_count> unit_textures_{};
    SDL_Texture* shadow_texture_{nullptr};
    // The board's pixel geometry, shared with the console and the editor.
    view::Camera camera_{};
    view::Projection projection_{margin, margin, tile, 0};
    std::vector<view::DrawItem> draw_storage_;
    view::DrawList draw_list_{nullptr, 0};
};

}  // namespace

std::unique_ptr<Presenter> make_sdl_presenter(
    bool probe,
    const package_runtime::Presentation& presentation
) {
    auto presenter = std::make_unique<SdlPresenter>(probe, presentation);
    if (!presenter->start()) return nullptr;
    return presenter;
}

}  // namespace grandleon::desktop
