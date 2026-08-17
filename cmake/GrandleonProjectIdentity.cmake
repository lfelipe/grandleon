# SPDX-License-Identifier: MIT
# Which game a console image is, derived from the project it is built for.
#
# A console image plays one campaign and keeps it in one place. Both of those
# are facts about the *project*, and both are needed before a single line of
# the image's code is compiled: the campaign because the executable asks the
# package for it by name, the slot because the save is named after the game.
#
# This file is the one implementation, so that two consoles built for one
# project cannot decide they are holding different games — which would show up
# as a cartridge and a disc of the same campaign refusing to read each other's
# name for the same save.
#
# Neither answer is read out of anything under `games/`. What a scripted run of
# a shipped campaign should observe is a different question from what game is
# running, and taking the second from the first is a category error that goes
# unnoticed only while there is one game. It is also exactly what would make an
# image of anybody else's project impossible.

# Sets `<campaign_var>` and `<slot_var>` in the caller's scope to the campaign
# `project_json` runs and the save slot base name it keeps it under. `what`
# names the build being configured, and appears in every refusal below.
function(grandleon_project_identity project_json what campaign_var slot_var)
    file(READ "${project_json}" _identity_document)

    # The campaign is the project's first. A console image plays one campaign
    # and the authored order is the only ordering a project offers, so "the
    # first one" is the only answer that does not require a second authored
    # field to select it.
    string(JSON _identity_campaign_count ERROR_VARIABLE _identity_campaign_error
        LENGTH "${_identity_document}" campaigns)
    if(_identity_campaign_error OR _identity_campaign_count EQUAL 0)
        message(FATAL_ERROR
            "the project ${project_json} carries no campaign; ${what} runs one")
    endif()
    string(JSON _identity_campaign GET "${_identity_document}" campaigns 0 id)

    # The id is written into a C++ string literal by every caller, so it is
    # code by the time the image compiles rather than data the image reads. A
    # project is content people share, and an id carrying a quote or a
    # `#include` would make opening somebody else's project and pressing Build
    # a way to put their C++ in your cartridge. The source contract's
    # identifier grammar is what a campaign id is allowed to be, so anything
    # else is refused here, before a single line is generated.
    if(NOT _identity_campaign MATCHES "^[a-z][a-z0-9]*([._-][a-z0-9]+)*$")
        message(FATAL_ERROR
            "the project ${project_json} names its first campaign "
            "'${_identity_campaign}', which is not a source identifier; "
            "${what} writes that id into the image's own code")
    endif()

    # The save slot's base name, out of the game's identity. The storage
    # contract allows lower-case letters, digits, `_` and `-`, and
    # `view::slot_name_at` spends three more characters on a row suffix, so
    # twenty-nine is the budget. `grandleon.tarnholt` is `tarnholt`, which is
    # the name every cartridge already carries, so a save keeps being readable.
    string(JSON _identity_game_id GET "${_identity_document}" gameId)
    string(REGEX REPLACE "^.*\\." "" _identity_slot "${_identity_game_id}")
    string(TOLOWER "${_identity_slot}" _identity_slot)
    string(REGEX REPLACE "[^a-z0-9_-]" "_" _identity_slot "${_identity_slot}")
    string(SUBSTRING "${_identity_slot}" 0 29 _identity_slot)
    if(_identity_slot STREQUAL "")
        message(FATAL_ERROR
            "the project ${project_json} has a gameId that spells no save slot "
            "name: '${_identity_game_id}'")
    endif()

    set(${campaign_var} "${_identity_campaign}" PARENT_SCOPE)
    set(${slot_var} "${_identity_slot}" PARENT_SCOPE)
endfunction()

# Writes `header` declaring the campaign and slot `grandleon_project_identity`
# derived, as the two symbols every console's sources read them under.
#
# The header rather than two `target_compile_definitions` because these are
# strings: a definition carrying one has to survive a shell, a CMake list and a
# compiler command line, and the escaping that takes is the sort of thing that
# works until a project is called something with a space in it.
function(grandleon_write_project_identity project_json what header)
    grandleon_project_identity(
        "${project_json}" "${what}" _identity_campaign _identity_slot)
    file(WRITE "${header}"
        "// Generated from ${project_json}. Do not edit.\n"
        "#pragma once\n"
        "// The campaign this image runs, and the slot it keeps it in.\n"
        "inline constexpr const char* project_campaign_key =\n"
        "    \"${_identity_campaign}\";\n"
        "inline constexpr const char* project_campaign_slot = "
        "\"${_identity_slot}\";\n"
    )
    message(STATUS
        "${what}: campaign '${_identity_campaign}' in slot "
        "'${_identity_slot}'")
endfunction()
