// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/client/campaign_session.hpp>
#include <grandleon/client/presenter.hpp>
#include <grandleon/package_runtime/presentation.hpp>

#include <memory>

namespace grandleon::desktop {

using client::Presenter;
using client::Roster;
using client::Intent;
using client::IntentKind;

// Draws with ANSI text and reads a line at a time. Always available.
// `package` is what the client opened, or null. It is where every name this
// presenter prints is looked up first, so a terminal and a cartridge running
// the same project cannot call one character two different things.
[[nodiscard]] std::unique_ptr<Presenter> make_terminal_presenter(
    bool colour,
    const package_format::LoadedPackage* package = nullptr
);

// The same front end, as the thing that also narrates a campaign.
//
// Two factories rather than one return type because the two surfaces are
// genuinely different: `--sdl` produces a presenter that draws a battle and
// nothing more, and campaign mode needs a front end that can also say who died
// for good. A caller that wants both asks for both, rather than casting one
// into the other and hoping.
[[nodiscard]] std::unique_ptr<client::CampaignFrontEnd> make_terminal_front_end(
    bool colour,
    const package_format::LoadedPackage* package = nullptr
);

#ifdef GRANDLEON_DESKTOP_SDL
// Draws into a window and reads mouse and keyboard. Built only when SDL2 is
// found at configure time, so the client still builds on a machine without it.
//
// `probe` renders the opening frame, reads the framebuffer back, prints a
// machine-checkable summary of what was actually drawn, and exits. That is what
// lets the renderer be verified without a display, using SDL's offscreen video
// driver, rather than waiting for somebody to look at a window.
//
// `presentation` is what the author chose about how the game looks, read out of
// the package. A package that carries none resolves to the default theme and to
// no faction colours, and the presenter then draws exactly what it drew before
// the package carried any of this.
[[nodiscard]] std::unique_ptr<Presenter> make_sdl_presenter(
    bool probe,
    const package_runtime::Presentation& presentation
);
#endif

}  // namespace grandleon::desktop
