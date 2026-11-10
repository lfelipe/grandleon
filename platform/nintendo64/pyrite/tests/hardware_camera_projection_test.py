#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Project the shipping tactical camera to catch diagonal controller movement."""
from pathlib import Path
import math
import json
import os
import re
import subprocess
import tempfile
import textwrap
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "project/src/user/ProjectBoard.cpp"


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalized(vector):
    length = math.sqrt(dot(vector, vector))
    return tuple(value / length for value in vector)


class HardwareCameraProjection(unittest.TestCase):
    def test_shipping_camera_projects_controller_axes_without_diagonal_drift(self):
        source = SOURCE.read_text()
        match = re.search(r"constexpr CameraPose kTacticalCamera\s*\{([^}]+)\}", source)
        self.assertIsNotNone(match)
        values = [float(value) for value in re.findall(r"(-?\d+\.\d+)F", match[1])]
        self.assertEqual(len(values), 7)
        eye, target = values[:3], values[3:6]
        forward = normalized(tuple(t - e for t, e in zip(target, eye)))
        right = normalized(cross(forward, (0, 1, 0)))
        up = cross(right, forward)
        # GameplaySlice Right increments x; Up decrements y, which is world z.
        for movement, expected in [((1, 0, 0), (1, 0)), ((0, 0, -1), (0, -1))]:
            projected = (dot(movement, right), -dot(movement, up))
            if expected[0]:
                self.assertGreater(projected[0], 0)
                self.assertAlmostEqual(projected[1], 0, places=6)
            else:
                self.assertLess(projected[1], 0)
                self.assertAlmostEqual(projected[0], 0, places=6)
        # Full camera dolly/follow translations preserve the same orientation.
        # The cylindrical board billboard must span the camera's horizontal axis.
        billboard = normalized((eye[2] - target[2], 0, target[0] - eye[0]))
        self.assertAlmostEqual(dot(billboard, right), 1, places=6)


    def test_shipping_stick_input_for_both_controller_ports(self):
        source = SOURCE.read_text()
        start = source.index("joypad_buttons_t directional_input(")
        end = source.index("\nvoid update_shell_input()", start)
        # Compile the shipping input adapter against a deterministic pad source.
        # This checks thresholds, repeat, seat independence, and D-pad coexistence.
        harness = r"""
            #include <array>
            #include <cstdint>
            #include <cstdlib>
            #include <cassert>
            using joypad_port_t = unsigned;
            struct joypad_buttons_t { bool d_left{}, d_right{}, d_up{}, d_down{}, a{}; };
            struct joypad_inputs_t { int stick_x{}, stick_y{}; };
            std::array<joypad_buttons_t, 2> buttons{};
            std::array<joypad_inputs_t, 2> sticks{};
            std::uint32_t frame_count{};
            joypad_buttons_t joypad_get_buttons_pressed(unsigned port) { return buttons[port]; }
            joypad_inputs_t joypad_get_inputs(unsigned port) { return sticks[port]; }
        """ + source[start:end] + r"""
            int main() {
                for (unsigned seat = 0; seat != 2; ++seat) {
                    sticks[seat] = {60, 0};
                    assert(directional_input(seat, seat).d_right);
                    assert(!directional_input(seat, seat).d_right);
                    frame_count += 16;
                    assert(directional_input(seat, seat).d_right);
                    sticks[seat] = {0, 60};
                    assert(directional_input(seat, seat).d_up);
                    sticks[seat] = {-60, 0};
                    assert(directional_input(seat, seat).d_left);
                    sticks[seat] = {0, -60};
                    assert(directional_input(seat, seat).d_down);
                    sticks[seat] = {31, -31};
                    auto deadzone = directional_input(seat, seat);
                    assert(!deadzone.d_left && !deadzone.d_right && !deadzone.d_up && !deadzone.d_down);
                    buttons[seat].d_right = true;
                    buttons[seat].a = true;
                    auto digital = directional_input(seat, seat);
                    assert(digital.d_right && digital.a);
                    buttons[seat] = {};
                    sticks[seat] = {};
                }
                sticks[0] = {60, 0};
                sticks[1] = {0, 60};
                assert(directional_input(0, 0).d_right);
                assert(directional_input(1, 1).d_up);
            }
        """
        with tempfile.TemporaryDirectory(prefix="grandleon-n64-input-") as temporary:
            cpp = Path(temporary) / "input.cpp"
            binary = Path(temporary) / "input-test"
            cpp.write_text(textwrap.dedent(harness))
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Werror", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


    def test_translated_dialogue_pages_preserve_complete_text(self):
        source = SOURCE.read_text()
        ascii_start = source.index("std::string console_ascii(")
        ascii_end = source.index("\nvoid display_printf(", ascii_start)
        story_start = source.index("void draw_wrapped_story(")
        story_end = source.index("\nstd::vector<client::RosterEntry>", story_start)
        moment_start = source.index("        constexpr std::size_t width = 31U;")
        moment_end = source.index('        display_printf(238, 215,', moment_start)
        harness = r"""
            #include <grandleon/view/console_text.h>
            #include <algorithm>
            #include <string>
            #include <string_view>
            #include <vector>
            #include <cstddef>
            #include <cstdarg>
            #include <cstdio>
            #include <cassert>
            struct Shell { std::size_t story_text_offset{}, story_text_next{}; bool story_text_more{}; } shell;
            struct Moment { std::size_t text_offset{}, text_next{}; bool text_more{}; } battle_moment;
            std::vector<std::string> rows;
            void display_printf(int, int, const char* format, ...) {
                char text[512]{};
                va_list arguments; va_start(arguments, format);
                std::vsnprintf(text, sizeof text, format, arguments);
                va_end(arguments); rows.emplace_back(text);
            }
        """ + source[ascii_start:ascii_end] + source[story_start:story_end] + r"""
            void draw_moment(const std::string& dialogue_text) {
        """ + source[moment_start:moment_end] + r"""
            }
            std::string joined_rows() {
                std::string result;
                for (const auto& row : rows) {
                    if (!result.empty()) result += ' ';
                    result += row;
                }
                return result;
            }
            int main() {
                const std::string translated = "Não tenham pressa de nos encontrar. Vençam sua rota primeiro; nos reuniremos na estrada da vigia. Então chegaremos à campainha antes que uma patrulha assustada se transforme numa guerra. Mantenham a companhia próxima e deixem testemunhas vivas para contar toda a história.";
                const std::string expected = console_ascii(translated);
                unsigned pages = 0;
                do {
                    draw_wrapped_story(translated);
                    shell.story_text_offset = shell.story_text_next;
                    assert(++pages < 10);
                } while (shell.story_text_more);
                assert(pages > 1);
                assert(joined_rows() == expected);
                rows.clear(); pages = 0;
                do {
                    draw_moment(expected);
                    battle_moment.text_offset = battle_moment.text_next;
                    assert(++pages < 10);
                } while (battle_moment.text_more);
                assert(pages > 1);
                assert(joined_rows() == expected);
                assert(expected.find("Nao tenham") == 0);
            }
        """
        with tempfile.TemporaryDirectory(prefix="grandleon-n64-dialogue-") as temporary:
            cpp = Path(temporary) / "dialogue.cpp"
            binary = Path(temporary) / "dialogue-test"
            cpp.write_text(harness)
            repo = Path(os.environ.get("GRANDLEON_REPO", Path(__file__).resolve().parents[4]))
            include = repo / "platform/view/include"
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Werror", "-I", str(include), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


    def test_company_and_enemy_speakers_use_shared_portrait_draw_path(self):
        source = SOURCE.read_text()
        start = source.index("bool speaker_name_matches(")
        end = source.index("\nvoid draw_battle_ui()", start)
        repo = Path(os.environ.get("GRANDLEON_REPO", Path(__file__).resolve().parents[4]))
        project = json.loads((repo / "games/steel_and_cedar/source/project.json").read_text())
        names = [unit["name"] for unit in project["unitTypes"]]
        name_literals = ",".join(json.dumps(name) for name in names)
        checks = []
        for speaker in ["Akari", "Masatora", "Ren", "Rowan", "Tomas", "Vere"]:
            matches = [index + 1 for index, name in enumerate(names) if speaker in name.split()]
            self.assertEqual(len(matches), 1)
            checks.append(f'assert(draw_speaker_portrait("{speaker}", "{speaker}", roster, 23, 164, .64F)); assert(last_type == {matches[0]}); assert(blits == ++expected_blits);')
        harness = r"""
            #include <string>
            #include <string_view>
            #include <vector>
            #include <cstdint>
            #include <cassert>
            namespace sim { using ContentId = unsigned; struct UnitSnapshot { unsigned unit_type_id{}; }; }
            namespace client { struct RosterEntry { std::string name, authored_name; struct { unsigned stable_id; } unit_type; }; }
            namespace package_format { enum class SectionType { unit_types }; }
            const char* names[] = { NAMES };
            namespace pr {
                struct UnitTypeColour { unsigned unit_type_id; };
                std::string_view content_name(int, package_format::SectionType, unsigned id) { return names[id - 1]; }
            }
            struct { struct { std::vector<pr::UnitTypeColour> unit_types; } presentation;
                struct { int package() const { return 0; } } runtime; } board;
            struct sprite_t {} sprite;
            enum class VisualPose { close_idle };
            unsigned last_type{}, blits{};
            sprite_t* sprite_for_unit(const sim::UnitSnapshot& unit, VisualPose) { last_type = unit.unit_type_id; return &sprite; }
            std::uint32_t frame_count{};
            struct rdpq_blitparms_t { bool filtering{}; float scale_x{}, scale_y{}; };
            enum { RDPQ_COMBINER_TEX, RDPQ_BLENDER_MULTIPLY };
            void rdpq_mode_push() {} void rdpq_mode_pop() {} void rdpq_mode_begin() {} void rdpq_mode_end() {}
            void rdpq_mode_combiner(int) {} void rdpq_mode_alphacompare(int) {} void rdpq_mode_blender(int) {}
            void rdpq_sprite_blit(sprite_t*, float, float, rdpq_blitparms_t*) { ++blits; }
            namespace P64::Debug { void printStart() {} }
        """.replace("NAMES", name_literals) + source[start:end] + r"""
            int main() {
                for (unsigned index = 1; index <= sizeof names / sizeof names[0]; ++index) board.presentation.unit_types.push_back({index});
                const std::vector<client::RosterEntry> roster;
                unsigned expected_blits = 0;
                CHECKS
                assert(!draw_speaker_portrait("Narrator", "Narrator", roster, 23, 164, .64F));
                assert(!speaker_name_matches("Joanne", "Ann"));
            }
        """.replace("CHECKS", "\n".join(checks))
        with tempfile.TemporaryDirectory(prefix="grandleon-n64-portrait-") as temporary:
            cpp = Path(temporary) / "portrait.cpp"
            binary = Path(temporary) / "portrait-test"
            cpp.write_text(harness)
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Werror", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        battle = source[source.index("void draw_battle_ui()"):source.index("void draw_wrapped_story(")]
        self.assertIn("draw_speaker_portrait(speaker, line.speaker, roster, 23.0F, 164.0F, 0.64F)", battle)


if __name__ == "__main__":
    unittest.main()
