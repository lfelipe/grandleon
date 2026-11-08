// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

namespace grandleon::client::console_localization {

// The compact consoles deliberately keep their own interface vocabulary
// separate from a game's authored translation catalog. These are the words
// Grandleon itself says around any game; authored titles, dialogue and content
// names continue to come from package_runtime::localized_*.
enum class Message : std::uint8_t {
    campaign_tagline,
    press_start,
    one_player,
    two_players,
    players,
    help,
    language,
    connect_controller_2,
    choose_confirm,
    how_to_play,
    move_cursor,
    select_confirm,
    back,
    action_menu,
    board_menu,
    hints_in_play,
    back_footer,
    begin,
    walk,
    attack,
    talk,
    end_character_turn,
    info,
    cancel,
    back_to_battle,
    end_player_1_turn,
    end_player_2_turn,
    leave_battle,
    show_fps_on,
    show_fps_off,
    another_stage,
    another_stage_testing,
    walk_where,
    talk_to_whom,
    pick_target,
    after_battle,
    joins_company,
    go_on,
    stage_warning,
    here,
    seen,
    go_back,
    game_saves_itself,
    choose_save,
    confirm,
    keep,
    continue_game,
    new_game,
    new_company,
    your_turn,
    enemy_turn,
    select,
    back_button,
    or_word,
    status,
    loadout,
    story,
    company,
    inventory,
    character_name,
    notice,
    new_company_member,
    stages,
    campaign_complete,
    choose_how_to_play,
    before_battle,
    action_heading,
    battle_heading,
    away,
    field,
    bench,
    lost,
    fielded,
    of_word,
    company_store,
    nothing_but_carry,
    levels,
    fell,
    joined,
    company_buries,
    carried_off_back,
    choose_name,
    change_name,
    sit_out,
    take_field,
    take_field_full,
    give,
    take,
    company_footer,
    company_footer_stages,
};

enum class Language : std::uint8_t { english, spanish, portuguese_brazil };

[[nodiscard]] inline Language interface_language(
    std::string_view language
) noexcept {
    if (language.size() >= 2U && language[0] == 'e' && language[1] == 's') {
        return Language::spanish;
    }
    if (language.size() >= 2U && language[0] == 'p' && language[1] == 't') {
        return Language::portuguese_brazil;
    }
    return Language::english;
}

[[nodiscard]] inline const char* language_code(
    std::string_view language
) noexcept {
    switch (interface_language(language)) {
        case Language::spanish: return "ES";
        case Language::portuguese_brazil: return "PT-BR";
        case Language::english: return "EN";
    }
    return "EN";
}

[[nodiscard]] inline const char* text(
    std::string_view language, Message message
) noexcept {
    const Language selected = interface_language(language);
    switch (message) {
        case Message::campaign_tagline:
            return selected == Language::spanish ? "UNA CAMPANA QUE GUARDA ESTE JUEGO"
                 : selected == Language::portuguese_brazil ? "UMA CAMPANHA SALVA NESTE JOGO"
                 : "A CAMPAIGN THIS CARTRIDGE KEEPS";
        case Message::press_start:
            return selected == Language::spanish ? "PULSA START"
                 : selected == Language::portuguese_brazil ? "APERTE START"
                 : "PRESS START";
        case Message::one_player:
            return selected == Language::spanish ? "1 JUGADOR"
                 : selected == Language::portuguese_brazil ? "1 JOGADOR"
                 : "1 PLAYER";
        case Message::two_players:
            return selected == Language::spanish ? "2 JUGADORES"
                 : selected == Language::portuguese_brazil ? "2 JOGADORES"
                 : "2 PLAYERS";
        case Message::players:
            return selected == Language::spanish ? "JUGADORES"
                 : selected == Language::portuguese_brazil ? "JOGADORES"
                 : "PLAYERS";
        case Message::help:
            return selected == Language::spanish ? "AYUDA"
                 : selected == Language::portuguese_brazil ? "AJUDA"
                 : "HELP";
        case Message::language:
            return selected == Language::spanish ? "IDIOMA"
                 : selected == Language::portuguese_brazil ? "IDIOMA"
                 : "LANGUAGE";
        case Message::connect_controller_2:
            return selected == Language::spanish ? "CONECTA EL CONTROL 2"
                 : selected == Language::portuguese_brazil ? "CONECTE O CONTROLE 2"
                 : "CONNECT CONTROLLER 2";
        case Message::choose_confirm:
            return selected == Language::spanish ? "CRUCETA ELIGE  A ACEPTA"
                 : selected == Language::portuguese_brazil ? "D-PAD ESCOLHE  A CONFIRMA"
                 : "D-PAD CHOOSE  A CONFIRM";
        case Message::how_to_play:
            return selected == Language::spanish ? "COMO JUGAR"
                 : selected == Language::portuguese_brazil ? "COMO JOGAR"
                 : "HOW TO PLAY";
        case Message::move_cursor:
            return selected == Language::spanish ? "CRUCETA MUEVE EL CURSOR"
                 : selected == Language::portuguese_brazil ? "D-PAD MOVE O CURSOR"
                 : "D-PAD   MOVE CURSOR";
        case Message::select_confirm:
            return selected == Language::spanish ? "A       ELIGE O ACEPTA"
                 : selected == Language::portuguese_brazil ? "A       ESCOLHE OU CONFIRMA"
                 : "A       SELECT OR CONFIRM";
        case Message::back:
            return selected == Language::spanish ? "B       VOLVER"
                 : selected == Language::portuguese_brazil ? "B       VOLTAR"
                 : "B       BACK";
        case Message::action_menu:
            return selected == Language::spanish ? "C       MENU DE ACCION"
                 : selected == Language::portuguese_brazil ? "C       MENU DE ACAO"
                 : "C       ACTION MENU";
        case Message::board_menu:
            return selected == Language::spanish ? "START   MENU DEL MAPA"
                 : selected == Language::portuguese_brazil ? "START   MENU DO MAPA"
                 : "START   BOARD MENU";
        case Message::hints_in_play:
            return selected == Language::spanish ? "LOS BOTONES APARECEN AL JUGAR"
                 : selected == Language::portuguese_brazil ? "OS BOTOES APARECEM AO JOGAR"
                 : "BUTTON HINTS APPEAR IN PLAY";
        case Message::back_footer:
            return selected == Language::spanish ? "A O B  VOLVER"
                 : selected == Language::portuguese_brazil ? "A OU B  VOLTAR"
                 : "A OR B  BACK";
        case Message::begin:
            return selected == Language::spanish ? "EMPEZAR"
                 : selected == Language::portuguese_brazil ? "INICIAR"
                 : "BEGIN";
        case Message::walk:
            return selected == Language::spanish ? "MOVER"
                 : selected == Language::portuguese_brazil ? "MOVER"
                 : "WALK";
        case Message::attack:
            return selected == Language::spanish ? "ATACAR"
                 : selected == Language::portuguese_brazil ? "ATACAR"
                 : "ATTACK";
        case Message::talk:
            return selected == Language::spanish ? "HABLAR"
                 : selected == Language::portuguese_brazil ? "FALAR"
                 : "TALK";
        case Message::end_character_turn:
            return selected == Language::spanish ? "TERMINAR TURNO DEL PERSONAJE"
                 : selected == Language::portuguese_brazil ? "TERMINAR TURNO DO PERSONAGEM"
                 : "END CHARACTER TURN";
        case Message::info:
            return selected == Language::spanish ? "DATOS"
                 : selected == Language::portuguese_brazil ? "INFO"
                 : "INFO";
        case Message::cancel:
            return selected == Language::spanish ? "CANCELAR"
                 : selected == Language::portuguese_brazil ? "CANCELAR"
                 : "CANCEL";
        case Message::back_to_battle:
            return selected == Language::spanish ? "VOLVER A LA BATALLA"
                 : selected == Language::portuguese_brazil ? "VOLTAR A BATALHA"
                 : "BACK TO BATTLE";
        case Message::end_player_1_turn:
            return selected == Language::spanish ? "TERMINAR TURNO JUGADOR 1"
                 : selected == Language::portuguese_brazil ? "TERMINAR TURNO JOGADOR 1"
                 : "END PLAYER 1 TURN";
        case Message::end_player_2_turn:
            return selected == Language::spanish ? "TERMINAR TURNO JUGADOR 2"
                 : selected == Language::portuguese_brazil ? "TERMINAR TURNO JOGADOR 2"
                 : "END PLAYER 2 TURN";
        case Message::leave_battle:
            return selected == Language::spanish ? "SALIR - NO SE GUARDA"
                 : selected == Language::portuguese_brazil ? "SAIR - NAO SERA SALVO"
                 : "LEAVE - THIS BATTLE IS NOT KEPT";
        case Message::show_fps_on:
            return selected == Language::spanish ? "MOSTRAR FPS: SI"
                 : selected == Language::portuguese_brazil ? "MOSTRAR FPS: SIM"
                 : "SHOW FPS: ON";
        case Message::show_fps_off:
            return selected == Language::spanish ? "MOSTRAR FPS: NO"
                 : selected == Language::portuguese_brazil ? "MOSTRAR FPS: NAO"
                 : "SHOW FPS: OFF";
        case Message::another_stage:
            return selected == Language::spanish ? "IR A OTRA FASE"
                 : selected == Language::portuguese_brazil ? "IR A OUTRA FASE"
                 : "GO TO ANOTHER STAGE";
        case Message::another_stage_testing:
            return selected == Language::spanish ? "IR A OTRA FASE - PRUEBA"
                 : selected == Language::portuguese_brazil ? "IR A OUTRA FASE - TESTE"
                 : "GO TO ANOTHER STAGE - TESTING";
        case Message::walk_where:
            return selected == Language::spanish ? "MOVER ADONDE"
                 : selected == Language::portuguese_brazil ? "MOVER PARA ONDE"
                 : "WALK WHERE";
        case Message::talk_to_whom:
            return selected == Language::spanish ? "HABLAR CON QUIEN"
                 : selected == Language::portuguese_brazil ? "FALAR COM QUEM"
                 : "TALK TO WHOM";
        case Message::pick_target:
            return selected == Language::spanish ? "ELIGE UN OBJETIVO"
                 : selected == Language::portuguese_brazil ? "ESCOLHA UM ALVO"
                 : "PICK A TARGET";
        case Message::after_battle:
            return selected == Language::spanish ? "DESPUES DE LA BATALLA"
                 : selected == Language::portuguese_brazil ? "DEPOIS DA BATALHA"
                 : "AFTER THE BATTLE";
        case Message::joins_company:
            return selected == Language::spanish ? "SE UNE A LA COMPANIA"
                 : selected == Language::portuguese_brazil ? "ENTRA PARA A COMPANHIA"
                 : "JOINS THE COMPANY";
        case Message::go_on:
            return selected == Language::spanish ? "A  SEGUIR"
                 : selected == Language::portuguese_brazil ? "A  CONTINUAR"
                 : "A  GO ON";
        case Message::stage_warning:
            return selected == Language::spanish ? "UNA FASE NO VISTA PUEDE NO ABRIR"
                 : selected == Language::portuguese_brazil ? "FASE NAO VISTA PODE NAO ABRIR"
                 : "A STAGE YOU HAVE NOT SEEN MAY NOT OPEN";
        case Message::here:
            return selected == Language::spanish ? "AQUI"
                 : selected == Language::portuguese_brazil ? "AQUI"
                 : "HERE";
        case Message::seen:
            return selected == Language::spanish ? "VISTA"
                 : selected == Language::portuguese_brazil ? "VISTA"
                 : "SEEN";
        case Message::go_back:
            return selected == Language::spanish ? "A IR  B VOLVER"
                 : selected == Language::portuguese_brazil ? "A IR  B VOLTAR"
                 : "A GO  B BACK";
        case Message::game_saves_itself:
            return selected == Language::spanish ? "EL JUEGO GUARDA SOLO"
                 : selected == Language::portuguese_brazil ? "O JOGO SALVA SOZINHO"
                 : "THE GAME SAVES ITSELF";
        case Message::choose_save:
            return selected == Language::spanish ? "ELIGE UNA PARTIDA"
                 : selected == Language::portuguese_brazil ? "ESCOLHA UM SAVE"
                 : "CHOOSE A SAVE";
        case Message::confirm:
            return selected == Language::spanish ? "CONFIRMAR"
                 : selected == Language::portuguese_brazil ? "CONFIRMAR"
                 : "CONFIRM";
        case Message::keep:
            return selected == Language::spanish ? "CONSERVAR"
                 : selected == Language::portuguese_brazil ? "MANTER"
                 : "KEEP";
        case Message::continue_game:
            return selected == Language::spanish ? "CONTINUAR"
                 : selected == Language::portuguese_brazil ? "CONTINUAR"
                 : "CONTINUE";
        case Message::new_game:
            return selected == Language::spanish ? "NUEVA"
                 : selected == Language::portuguese_brazil ? "NOVO"
                 : "NEW";
        case Message::new_company:
            return selected == Language::spanish ? "NUEVA COMPANIA"
                 : selected == Language::portuguese_brazil ? "NOVA COMPANHIA"
                 : "NEW COMPANY";
        case Message::your_turn:
            return selected == Language::spanish ? "TU TURNO"
                 : selected == Language::portuguese_brazil ? "SEU TURNO"
                 : "YOUR TURN";
        case Message::enemy_turn:
            return selected == Language::spanish ? "TURNO ENEMIGO"
                 : selected == Language::portuguese_brazil ? "TURNO INIMIGO"
                 : "ENEMY TURN";
        case Message::select:
            return selected == Language::spanish ? "ELEGIR"
                 : selected == Language::portuguese_brazil ? "SELECIONAR"
                 : "SELECT";
        case Message::back_button:
            return selected == Language::spanish ? "VOLVER"
                 : selected == Language::portuguese_brazil ? "VOLTAR"
                 : "BACK";
        case Message::or_word:
            return selected == Language::spanish ? "O"
                 : selected == Language::portuguese_brazil ? "OU"
                 : "OR";
        case Message::status:
            return selected == Language::spanish ? "ESTADO"
                 : selected == Language::portuguese_brazil ? "ESTADO"
                 : "STATUS";
        case Message::loadout:
            return selected == Language::spanish ? "EQUIPO"
                 : selected == Language::portuguese_brazil ? "EQUIPAMENTO"
                 : "LOADOUT";
        case Message::story:
            return selected == Language::spanish ? "HISTORIA"
                 : selected == Language::portuguese_brazil ? "HISTORIA"
                 : "STORY";
        case Message::company:
            return selected == Language::spanish ? "COMPANIA"
                 : selected == Language::portuguese_brazil ? "COMPANHIA"
                 : "COMPANY";
        case Message::inventory:
            return selected == Language::spanish ? "INVENTARIO"
                 : selected == Language::portuguese_brazil ? "INVENTARIO"
                 : "INVENTORY";
        case Message::character_name:
            return selected == Language::spanish ? "NOMBRE DEL PERSONAJE"
                 : selected == Language::portuguese_brazil ? "NOME DO PERSONAGEM"
                 : "CHARACTER NAME";
        case Message::notice:
            return selected == Language::spanish ? "AVISO"
                 : selected == Language::portuguese_brazil ? "AVISO"
                 : "NOTICE";
        case Message::new_company_member:
            return selected == Language::spanish ? "NUEVO MIEMBRO"
                 : selected == Language::portuguese_brazil ? "NOVO MEMBRO"
                 : "NEW COMPANY MEMBER";
        case Message::stages:
            return selected == Language::spanish ? "FASES"
                 : selected == Language::portuguese_brazil ? "FASES"
                 : "STAGES";
        case Message::campaign_complete:
            return selected == Language::spanish ? "CAMPANA COMPLETA"
                 : selected == Language::portuguese_brazil ? "CAMPANHA CONCLUIDA"
                 : "CAMPAIGN COMPLETE";
        case Message::choose_how_to_play:
            return selected == Language::spanish ? "ELIGE COMO JUGAR"
                 : selected == Language::portuguese_brazil ? "ESCOLHA COMO JOGAR"
                 : "CHOOSE HOW TO PLAY";
        case Message::before_battle:
            return selected == Language::spanish ? "ANTES DE LA BATALLA"
                 : selected == Language::portuguese_brazil ? "ANTES DA BATALHA"
                 : "BEFORE THE BATTLE";
        case Message::action_heading:
            return selected == Language::spanish ? "ACCION"
                 : selected == Language::portuguese_brazil ? "ACAO"
                 : "ACTION";
        case Message::battle_heading:
            return selected == Language::spanish ? "BATALLA"
                 : selected == Language::portuguese_brazil ? "BATALHA"
                 : "BATTLE";
        case Message::away:
            return selected == Language::spanish ? "FUERA"
                 : selected == Language::portuguese_brazil ? "FORA"
                 : "AWAY";
        case Message::field:
            return selected == Language::spanish ? "CAMPO"
                 : selected == Language::portuguese_brazil ? "CAMPO"
                 : "FIELD";
        case Message::bench:
            return selected == Language::spanish ? "RESERVA"
                 : selected == Language::portuguese_brazil ? "RESERVA"
                 : "BENCH";
        case Message::lost:
            return selected == Language::spanish ? "PERDIDO"
                 : selected == Language::portuguese_brazil ? "PERDIDO"
                 : "LOST";
        case Message::fielded:
            return selected == Language::spanish ? "EN CAMPO"
                 : selected == Language::portuguese_brazil ? "EM CAMPO"
                 : "FIELDED";
        case Message::of_word:
            return selected == Language::spanish ? "DE"
                 : selected == Language::portuguese_brazil ? "DE"
                 : "OF";
        case Message::company_store:
            return selected == Language::spanish ? "ALMACEN DE LA COMPANIA"
                 : selected == Language::portuguese_brazil ? "ARMAZEM DA COMPANHIA"
                 : "THE COMPANY'S STORE";
        case Message::nothing_but_carry:
            return selected == Language::spanish ? "SOLO LO QUE LLEVAN"
                 : selected == Language::portuguese_brazil ? "APENAS O QUE CARREGAM"
                 : "NOTHING BUT WHAT THEY CARRY";
        case Message::levels:
            return selected == Language::spanish ? "NIVELES"
                 : selected == Language::portuguese_brazil ? "NIVEIS"
                 : "LEVELS";
        case Message::fell:
            return selected == Language::spanish ? "CAIDOS"
                 : selected == Language::portuguese_brazil ? "CAIDOS"
                 : "FELL";
        case Message::joined:
            return selected == Language::spanish ? "UNIDOS"
                 : selected == Language::portuguese_brazil ? "RECRUTADOS"
                 : "JOINED";
        case Message::company_buries:
            return selected == Language::spanish ? "LA COMPANIA SEPULTA"
                 : selected == Language::portuguese_brazil ? "A COMPANHIA SEPULTA"
                 : "THE COMPANY BURIES";
        case Message::carried_off_back:
            return selected == Language::spanish ? "RETIRADOS, YA DE VUELTA"
                 : selected == Language::portuguese_brazil ? "RETIRADOS, JA DE VOLTA"
                 : "CARRIED OFF, AND BACK";
        case Message::choose_name:
            return selected == Language::spanish ? "ELEGIR NOMBRE"
                 : selected == Language::portuguese_brazil ? "ESCOLHER NOME"
                 : "CHOOSE NAME";
        case Message::change_name:
            return selected == Language::spanish ? "CAMBIAR NOMBRE"
                 : selected == Language::portuguese_brazil ? "MUDAR NOME"
                 : "CHANGE NAME";
        case Message::sit_out:
            return selected == Language::spanish ? "DEJAR EN RESERVA"
                 : selected == Language::portuguese_brazil ? "DEIXAR NA RESERVA"
                 : "SIT THIS ONE OUT";
        case Message::take_field:
            return selected == Language::spanish ? "LLEVAR AL CAMPO"
                 : selected == Language::portuguese_brazil ? "LEVAR AO CAMPO"
                 : "TAKE THE FIELD";
        case Message::take_field_full:
            return selected == Language::spanish ? "LLEVAR AL CAMPO (LLENO)"
                 : selected == Language::portuguese_brazil ? "LEVAR AO CAMPO (CHEIO)"
                 : "TAKE THE FIELD (FULL)";
        case Message::give:
            return selected == Language::spanish ? "DAR"
                 : selected == Language::portuguese_brazil ? "DAR"
                 : "GIVE";
        case Message::take:
            return selected == Language::spanish ? "TOMAR"
                 : selected == Language::portuguese_brazil ? "PEGAR"
                 : "TAKE";
        case Message::company_footer:
            return selected == Language::spanish
                     ? "A ELIGE  START BATALLA  B SALIR"
                 : selected == Language::portuguese_brazil
                     ? "A ESCOLHE START BATALHA B SAIR"
                 : "A CHOOSE  START BATTLE  B LEAVE";
        case Message::company_footer_stages:
            return selected == Language::spanish
                     ? "A ELIGE START BATALLA B SAL C FASES"
                 : selected == Language::portuguese_brazil
                     ? "A ESCOLHE START BATALHA B SAI C FASES"
                 : "A CHOOSE  START BATTLE  B LEAVE  C GO";
    }
    return "";
}

}  // namespace grandleon::client::console_localization
