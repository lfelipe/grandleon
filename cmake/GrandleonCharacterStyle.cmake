# SPDX-License-Identifier: MIT
# Resolving the character art a console build embeds.
#
# The choice is bound at configure time, from the project itself, which is what
# makes a style added to the library cost a console nothing.
# This file is the one implementation, so no two console targets can answer
# the question differently.
#
# A style is a property of a *character*, not of a game: the project's choice is
# a default that a character may override with any style the library holds. What
# a build must embed is therefore **the combinations the content actually
# references**, which for a project that overrides nothing is one style and one
# figure. The rule this exists to keep out is embedding styles a project can
# never draw: a style a project does not draw must cost the ROM zero.
#
# This also *is* the invariant that a project naming art the build did not embed
# is impossible. The build reads the project, so the embedded set is the
# content's set by construction; there is no third value and no run-time
# fallback anywhere downstream. The two ways they could still disagree are both
# fatal errors here, by name: a name the art library does not hold, and a
# project drawing more combinations than a given console can carry.

# Fails the configure unless `style` is a name the art library's menu in
# `manifest_json` holds.
#
# Split out of the resolver below rather than written twice, because the two
# callers ask the same question for different reasons: a project names a style,
# and the GTE measurement executable is *told* one. A second copy of this
# loop is a second place a style could be accepted that the library does not
# hold. The message is deliberately the same either way: what went wrong is the
# same thing.
function(grandleon_require_character_style style manifest_json)
    file(READ "${manifest_json}" _style_manifest)
    string(JSON _style_menu_count LENGTH
        "${_style_manifest}" character_styles menu)
    math(EXPR _style_last "${_style_menu_count} - 1")
    foreach(_style_index RANGE ${_style_last})
        string(JSON _style_name GET
            "${_style_manifest}" character_styles menu ${_style_index} name)
        if(_style_name STREQUAL style)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "character style '${style}' is not one the art library holds")
endfunction()

# Sets `<out_var>` in the caller's scope to the character style `project_json`
# names, validated against the art library's menu in `manifest_json`.
function(grandleon_resolve_character_style project_json manifest_json out_var)
    file(READ "${project_json}" _style_project)
    string(JSON _style ERROR_VARIABLE _style_error
        GET "${_style_project}" "characterStyleId")
    if(_style_error)
        # A project that names no style is drawn in the default one. The default
        # comes from the art library so the two cannot drift.
        set(_style "")
    endif()

    file(READ "${manifest_json}" _style_manifest)
    if(_style STREQUAL "")
        string(JSON _style GET "${_style_manifest}" character_styles default)
    endif()

    grandleon_require_character_style("${_style}" "${manifest_json}")
    set(${out_var} "${_style}" PARENT_SCOPE)
endfunction()

# Fails the configure unless `figure` is a body the art library draws.
#
# The figure axis, asked exactly as the style axis is one function above. A
# figure is the build a role is drawn at rather than the role: every figure
# draws every archetype in every style, so the two choices combine freely and
# are validated independently.
function(grandleon_require_character_figure figure manifest_json)
    file(READ "${manifest_json}" _figure_manifest)
    string(JSON _figure_menu_count LENGTH
        "${_figure_manifest}" character_styles figures menu)
    math(EXPR _figure_last "${_figure_menu_count} - 1")
    foreach(_figure_index RANGE ${_figure_last})
        string(JSON _figure_name GET
            "${_figure_manifest}" character_styles figures menu
            ${_figure_index} name)
        if(_figure_name STREQUAL figure)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "character figure '${figure}' is not one the art library draws")
endfunction()

# Sets `<out_var>` in the caller's scope to the figure `project_json` names,
# validated against the art library's menu in `manifest_json`.
function(grandleon_resolve_character_figure project_json manifest_json out_var)
    file(READ "${project_json}" _figure_project)
    string(JSON _figure ERROR_VARIABLE _figure_error
        GET "${_figure_project}" "characterFigureId")
    if(_figure_error)
        # A project that names no figure is drawn with the default one. The
        # default comes from the art library rather than from a literal here,
        # so the two cannot drift.
        set(_figure "")
    endif()

    file(READ "${manifest_json}" _figure_manifest)
    if(_figure STREQUAL "")
        string(JSON _figure GET
            "${_figure_manifest}" character_styles figures default)
    endif()

    grandleon_require_character_figure("${_figure}" "${manifest_json}")
    set(${out_var} "${_figure}" PARENT_SCOPE)
endfunction()

# Fails the configure unless `geometry` is a way the art library draws.
#
# The third axis, asked exactly as the two above are. Unlike them it is a
# statement about the **whole game** rather than about one character: "this
# unit is a model and that one is a sprite" is not a board anything here can
# draw coherently, so there is no per-character override to resolve and no set
# to accumulate.
function(grandleon_require_character_geometry geometry manifest_json)
    file(READ "${manifest_json}" _geometry_manifest)
    string(JSON _geometry_menu_count LENGTH
        "${_geometry_manifest}" character_styles geometries menu)
    math(EXPR _geometry_last "${_geometry_menu_count} - 1")
    foreach(_geometry_index RANGE ${_geometry_last})
        string(JSON _geometry_name GET
            "${_geometry_manifest}" character_styles geometries menu
            ${_geometry_index} name)
        if(_geometry_name STREQUAL geometry)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "character geometry '${geometry}' is not one the art library draws")
endfunction()

# Sets `<out_var>` in the caller's scope to the geometry `project_json` names,
# validated against the art library's menu in `manifest_json`.
function(grandleon_resolve_character_geometry project_json manifest_json out_var)
    file(READ "${project_json}" _geometry_project)
    string(JSON _geometry ERROR_VARIABLE _geometry_error
        GET "${_geometry_project}" "characterGeometry")
    if(_geometry_error)
        # A project that names nothing is drawn as sprites, which is what every
        # board showed before the models were drawn. The default comes from the
        # art library rather than from a literal here, so the two cannot drift.
        set(_geometry "")
    endif()

    file(READ "${manifest_json}" _geometry_manifest)
    if(_geometry STREQUAL "")
        string(JSON _geometry GET
            "${_geometry_manifest}" character_styles geometries default)
    endif()

    grandleon_require_character_geometry("${_geometry}" "${manifest_json}")
    set(${out_var} "${_geometry}" PARENT_SCOPE)
endfunction()

# Sets `<out_var>` to the mesh header a build of this style, figure and
# geometry must include.
#
# The one place the naming convention in
# `tools/placeholder_art/placeholder_art/playstation_header.py` is mirrored, so
# a rename there breaks one line here rather than several. `sprites` resolves
# to the empty string: a build drawing sprites includes no mesh header at all,
# and the caller is expected to treat that as "draw no solids" rather than as a
# missing file.
function(grandleon_mesh_header style figure geometry out_var)
    if(NOT geometry STREQUAL "models")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    if(figure STREQUAL "first")
        set(${out_var} "playstation_meshes_${style}_models.h" PARENT_SCOPE)
    else()
        set(${out_var} "playstation_meshes_${style}_models_${figure}.h"
            PARENT_SCOPE)
    endif()
endfunction()

# Sets `<styles_var>` and `<figures_var>` to the sorted, de-duplicated sets of
# character styles and figures `project_json`'s content actually draws.
#
# This is the resolution a console build needs, given that a style is a
# property of a character rather than of a game. The project's own choice is a
# default and never a gate: a character may name any style the library holds,
# so what a build has to embed is not "the style the project names" but **the
# combinations the content actually references**. For a project that overrides
# nothing that is exactly one style and one figure; for a project that
# overrides something it is that plus what it asked for, and never a style it
# cannot draw.
#
# Every name is validated on the way through, so a character naming a style the
# library does not hold fails the configure here rather than reaching a filter
# that silently drops its art.
function(grandleon_resolve_character_art
         project_json manifest_json styles_var figures_var)
    grandleon_resolve_character_style(
        "${project_json}" "${manifest_json}" _art_default_style)
    grandleon_resolve_character_figure(
        "${project_json}" "${manifest_json}" _art_default_figure)

    set(_art_styles "${_art_default_style}")
    set(_art_figures "${_art_default_figure}")

    file(READ "${project_json}" _art_project)
    string(JSON _art_unit_count ERROR_VARIABLE _art_unit_error
        LENGTH "${_art_project}" unitTypes)
    if(_art_unit_error OR _art_unit_count EQUAL 0)
        set(_art_unit_count 0)
    endif()
    if(_art_unit_count GREATER 0)
        math(EXPR _art_last_unit "${_art_unit_count} - 1")
        foreach(_art_unit RANGE ${_art_last_unit})
            string(JSON _art_unit_style ERROR_VARIABLE _art_style_absent
                GET "${_art_project}" unitTypes ${_art_unit} characterStyleId)
            if(NOT _art_style_absent)
                grandleon_require_character_style(
                    "${_art_unit_style}" "${manifest_json}")
                list(APPEND _art_styles "${_art_unit_style}")
            endif()
            string(JSON _art_unit_figure ERROR_VARIABLE _art_figure_absent
                GET "${_art_project}" unitTypes ${_art_unit} characterFigureId)
            if(NOT _art_figure_absent)
                grandleon_require_character_figure(
                    "${_art_unit_figure}" "${manifest_json}")
                list(APPEND _art_figures "${_art_unit_figure}")
            endif()
        endforeach()
    endif()

    list(REMOVE_DUPLICATES _art_styles)
    list(SORT _art_styles)
    list(REMOVE_DUPLICATES _art_figures)
    list(SORT _art_figures)
    set(${styles_var} "${_art_styles}" PARENT_SCOPE)
    set(${figures_var} "${_art_figures}" PARENT_SCOPE)
endfunction()

# Sets `<out_var>` to the art library's faction colour menu, in its own order.
#
# Read from the library's own published names rather than restated here. The
# order is what a colour *index* means: in the package the compiler writes, in
# the column order a console's roster table spells, and in the "a faction that
# chooses no colour takes the column at its own position" rule below. A second
# copy of the list would be a second place it could be wrong.
#
# `sprites.h` is the desktop client's art header and this is the only thing any
# console build wants out of it; `file(STRINGS ... REGEX)` takes the one line
# and costs about forty milliseconds on the whole 1.8 MB.
function(grandleon_character_faction_colours menu_header out_var)
    file(STRINGS "${menu_header}" _colour_line
        REGEX "grandleon_sprites_faction_names")
    if(_colour_line STREQUAL "")
        message(FATAL_ERROR
            "the art library header ${menu_header} publishes no faction "
            "colour menu; a console build cannot say which column a faction "
            "takes without it")
    endif()
    string(REGEX MATCHALL "\"[a-z_]+\"" _colour_quoted "${_colour_line}")
    set(_colours "")
    foreach(_colour_entry IN LISTS _colour_quoted)
        string(REPLACE "\"" "" _colour_entry "${_colour_entry}")
        list(APPEND _colours "${_colour_entry}")
    endforeach()
    if(_colours STREQUAL "")
        message(FATAL_ERROR
            "could not read the faction colour menu out of ${menu_header}")
    endif()
    set(${out_var} "${_colours}" PARENT_SCOPE)
endfunction()

# Sets `<out_var>` to the drawings `project_json`'s content references that are
# **not** the project's own combination, each spelled
# `<style>/<figure>/<archetype>/<colour>` and sorted.
#
# This is the finest grain a build can embed at, and the measurement is why it
# is the right one: on the shipped play ROM a second whole roster costs 11.3%,
# its archetype rows 7.1%, and the drawings themselves 1.2%. A cartridge should
# carry the drawings its content draws and no more, which is the build-time
# style filter taken two axes further in.
#
# The project's own combination is deliberately absent from the answer: a build
# embeds it whole whatever the content does with it, because it is what a
# character the package says nothing about is drawn in and what the dialogue
# portraits are drawn from.
#
# Three resolutions happen here, and each is the convention `game_content`
# already owns rather than a new one:
#
#   * the style and the figure are the character's own, else the game's
#     (`resolved_character_style` and `resolved_character_figure`);
#   * the archetype is the one its class name spells, else the one its own name
#     spells, else the roster's first (`resolved_archetype`, over the roster
#     the manifest publishes);
#   * the colour is the one the faction it names wears
#     (`resolved_faction_colour`), including the rule that a faction choosing
#     none takes the column at its own position.
#
# A character naming **no** faction is the one case a build cannot narrow: the
# package leaves its colour unresolved on purpose, and the client then draws it
# in the column of whichever side it turns up on. So both of those columns are
# embedded, because guessing one would be a character drawn in the wrong
# colours on the board it actually appears on.
function(grandleon_resolve_character_drawings
         project_json manifest_json colour_menu_header out_var)
    grandleon_resolve_character_style(
        "${project_json}" "${manifest_json}" _draw_project_style)
    grandleon_resolve_character_figure(
        "${project_json}" "${manifest_json}" _draw_project_figure)

    file(READ "${manifest_json}" _draw_manifest)
    string(JSON _draw_roster_count LENGTH
        "${_draw_manifest}" character_styles archetypes)
    math(EXPR _draw_roster_last "${_draw_roster_count} - 1")
    set(_draw_roster "")
    foreach(_draw_index RANGE ${_draw_roster_last})
        string(JSON _draw_role GET
            "${_draw_manifest}" character_styles archetypes ${_draw_index})
        list(APPEND _draw_roster "${_draw_role}")
    endforeach()

    grandleon_character_faction_colours(
        "${colour_menu_header}" _draw_colour_menu)
    list(LENGTH _draw_colour_menu _draw_colour_count)

    file(READ "${project_json}" _draw_project)

    # The project's factions, in the order it lists them, each with the colour
    # it wears. A faction that names one wears it; a faction that names none
    # takes the column at its own position, wrapping.
    set(_draw_faction_ids "")
    set(_draw_faction_colours "")
    string(JSON _draw_faction_count ERROR_VARIABLE _draw_faction_error
        LENGTH "${_draw_project}" factions)
    if(_draw_faction_error OR _draw_faction_count EQUAL 0)
        set(_draw_faction_count 0)
    endif()
    if(_draw_faction_count GREATER 0)
        math(EXPR _draw_faction_last "${_draw_faction_count} - 1")
        foreach(_draw_index RANGE ${_draw_faction_last})
            string(JSON _draw_faction_id GET
                "${_draw_project}" factions ${_draw_index} id)
            string(JSON _draw_faction_colour ERROR_VARIABLE _draw_colour_absent
                GET "${_draw_project}" factions ${_draw_index} color)
            if(_draw_colour_absent)
                math(EXPR _draw_column "${_draw_index} % ${_draw_colour_count}")
                list(GET _draw_colour_menu ${_draw_column} _draw_faction_colour)
            elseif(NOT _draw_faction_colour IN_LIST _draw_colour_menu)
                message(FATAL_ERROR
                    "faction '${_draw_faction_id}' wears colour "
                    "'${_draw_faction_colour}', which is not one the art "
                    "library's menu holds")
            endif()
            list(APPEND _draw_faction_ids "${_draw_faction_id}")
            list(APPEND _draw_faction_colours "${_draw_faction_colour}")
        endforeach()
    endif()

    set(_draw_drawings "")
    string(JSON _draw_unit_count ERROR_VARIABLE _draw_unit_error
        LENGTH "${_draw_project}" unitTypes)
    if(_draw_unit_error OR _draw_unit_count EQUAL 0)
        set(_draw_unit_count 0)
    endif()
    if(_draw_unit_count GREATER 0)
        math(EXPR _draw_unit_last "${_draw_unit_count} - 1")
        foreach(_draw_unit RANGE ${_draw_unit_last})
            string(JSON _draw_style ERROR_VARIABLE _draw_style_absent
                GET "${_draw_project}" unitTypes ${_draw_unit}
                characterStyleId)
            if(_draw_style_absent)
                set(_draw_style "${_draw_project_style}")
            else()
                grandleon_require_character_style(
                    "${_draw_style}" "${manifest_json}")
            endif()
            string(JSON _draw_figure ERROR_VARIABLE _draw_figure_absent
                GET "${_draw_project}" unitTypes ${_draw_unit}
                characterFigureId)
            if(_draw_figure_absent)
                set(_draw_figure "${_draw_project_figure}")
            else()
                grandleon_require_character_figure(
                    "${_draw_figure}" "${manifest_json}")
            endif()
            if(_draw_style STREQUAL _draw_project_style AND
               _draw_figure STREQUAL _draw_project_figure)
                continue()
            endif()

            _grandleon_character_archetype_of_unit(
                "${_draw_project}" ${_draw_unit} "${_draw_roster}"
                _draw_archetype)

            string(JSON _draw_unit_faction ERROR_VARIABLE _draw_faction_absent
                GET "${_draw_project}" unitTypes ${_draw_unit} factionId)
            set(_draw_unit_colours "")
            if(NOT _draw_faction_absent)
                list(FIND _draw_faction_ids "${_draw_unit_faction}"
                    _draw_faction_at)
                if(NOT _draw_faction_at EQUAL -1)
                    list(GET _draw_faction_colours ${_draw_faction_at}
                        _draw_unit_colours)
                endif()
            endif()
            if(_draw_unit_colours STREQUAL "")
                # The package leaves this character's colour unresolved, so the
                # client draws it in its side's column and the build cannot
                # know which side that is. Both, then.
                list(GET _draw_colour_menu 0 _draw_first_side)
                list(GET _draw_colour_menu 1 _draw_second_side)
                set(_draw_unit_colours
                    "${_draw_first_side}" "${_draw_second_side}")
            endif()

            foreach(_draw_colour IN LISTS _draw_unit_colours)
                list(APPEND _draw_drawings
                    "${_draw_style}/${_draw_figure}/${_draw_archetype}/${_draw_colour}")
            endforeach()
        endforeach()
    endif()

    list(REMOVE_DUPLICATES _draw_drawings)
    list(SORT _draw_drawings)
    set(${out_var} "${_draw_drawings}" PARENT_SCOPE)
endfunction()

# Sets `<out_var>` to the archetype the unit type at `unit_index` draws as.
#
# `game_content`'s `resolved_archetype` and `archetype_index`, restated over the
# roster the art manifest publishes: the class's name if it spells a role, else
# the character's own name, else the roster's first. It is a substring match on
# the lower-cased name, and the roster order settles a name that spells two.
#
# This is the one rule on this path that a build has to restate rather than
# read, because the compiler that owns it runs on the console rather than in
# the configure. It is not left to drift: the ROM asserts at boot that every
# drawing the compiled package asks for is one the build embedded, so a
# disagreement between this function and `resolved_archetype` is a checked
# failure naming the drawing rather than a character drawn wrong.
function(_grandleon_character_archetype_of_unit
         project_document unit_index roster out_var)
    string(JSON _arch_class ERROR_VARIABLE _arch_class_absent
        GET "${project_document}" unitTypes ${unit_index} classId)
    if(NOT _arch_class_absent)
        string(JSON _arch_class_count ERROR_VARIABLE _arch_classes_absent
            LENGTH "${project_document}" classes)
        if(NOT _arch_classes_absent AND _arch_class_count GREATER 0)
            math(EXPR _arch_class_last "${_arch_class_count} - 1")
            foreach(_arch_index RANGE ${_arch_class_last})
                string(JSON _arch_id GET
                    "${project_document}" classes ${_arch_index} id)
                if(NOT _arch_id STREQUAL _arch_class)
                    continue()
                endif()
                string(JSON _arch_class_name GET
                    "${project_document}" classes ${_arch_index} name)
                _grandleon_character_archetype_named(
                    "${_arch_class_name}" "${roster}" _arch_named)
                if(NOT _arch_named STREQUAL "")
                    set(${out_var} "${_arch_named}" PARENT_SCOPE)
                    return()
                endif()
                break()
            endforeach()
        endif()
    endif()
    string(JSON _arch_unit_name GET
        "${project_document}" unitTypes ${unit_index} name)
    _grandleon_character_archetype_named(
        "${_arch_unit_name}" "${roster}" _arch_named)
    if(_arch_named STREQUAL "")
        list(GET roster 0 _arch_named)
    endif()
    set(${out_var} "${_arch_named}" PARENT_SCOPE)
endfunction()

# Sets `<out_var>` to the first archetype in `roster` whose name appears in
# `name`, or to the empty string if none does.
function(_grandleon_character_archetype_named name roster out_var)
    string(TOLOWER "${name}" _named_lower)
    foreach(_named_role IN LISTS roster)
        string(FIND "${_named_lower}" "${_named_role}" _named_at)
        if(NOT _named_at EQUAL -1)
            set(${out_var} "${_named_role}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Fails the configure, by name and with the count, unless `project_json` draws
# at most one style and at most one figure.
#
# The PlayStation consumes generated art as a single header per style, holding
# the first figure alone, and a build includes exactly one of them. Those
# headers all declare the same symbols, which is what makes the choice a
# build-time include rather than a run-time index, so a second one cannot be
# included beside the first. Until the art library emits per-combination
# tables, such a build cannot draw a project that asks for two, and the honest
# answer is to say so at configure time.
#
# The Nintendo 64 does not ask this: it consumes per-asset files and names a
# drawing by all four of its keys, so it embeds the combinations its content
# draws (`grandleon_resolve_character_drawings` above). Lifting the limit on
# the PlayStation is an art-library change: `playstation_header.py` would have
# to key its symbols and its lookup tables by style and figure as the Nintendo
# 64's asset walk does.
#
# Refusing is deliberately not the same as dropping. A build that quietly drew
# the project's default style for a character that named another would ship a
# ROM that disagrees with the editor about what the game looks like. Refuse it
# rather than hide it.
function(grandleon_require_single_character_combination
         project_json manifest_json console)
    grandleon_resolve_character_art(
        "${project_json}" "${manifest_json}" _one_styles _one_figures)
    list(LENGTH _one_styles _one_style_count)
    list(LENGTH _one_figures _one_figure_count)
    if(_one_style_count GREATER 1)
        message(FATAL_ERROR
            "${console} embeds one character style's art, and this project "
            "draws ${_one_style_count}: ${_one_styles}. A character may name "
            "any style the library holds, but this console's art arrives as "
            "one generated header per style and two cannot be included at "
            "once.")
    endif()
    if(_one_figure_count GREATER 1)
        message(FATAL_ERROR
            "${console} embeds one character figure's art, and this project "
            "draws ${_one_figure_count}: ${_one_figures}. The generated "
            "console headers carry the first figure alone.")
    endif()
endfunction()
