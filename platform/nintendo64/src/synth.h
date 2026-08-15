// SPDX-License-Identifier: MIT
// A small square-wave synthesiser for the Nintendo 64 client.
//
// Everything is synthesised on the console at boot-time cost of nothing: there
// are no audio assets, no conversion pipeline, and no file system in the ROM.
// That is a deliberate first step, the audio twin of the CPU renderer. The
// sampled-music upgrade can come later without changing any call site.
//
// The synthesiser owns the audio hardware through libdragon's plain audio API
// rather than the mixer, because it generates every sample itself. Callers
// must pump it from every wait loop; a starved audio DMA loops its last
// buffer, which is exactly the kind of artefact this hardware is famous for.

#pragma once

namespace grandleon::n64audio {

enum class Sfx {
    // A short tick when a unit is selected.
    select,
    // A rising blip when a unit moves.
    move,
    // A dull thud when a unit is hit.
    hit,
    // A deeper, longer thud when a unit is defeated.
    defeat,
    // An ascending arpeggio for a won battle.
    victory,
    // A descending one for a lost battle.
    defeat_battle,
    // The four-note title jingle.
    title,
};

// Initialises the audio hardware and starts the ambient loop.
void init();

// Fills any writable audio buffers. Call from every loop that waits.
void pump();

// Starts a sound effect, replacing whichever one is still playing.
void play(Sfx effect);

// Turns the background melody on or off; effects keep working either way.
void set_music(bool enabled);

}  // namespace grandleon::n64audio
