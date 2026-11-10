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
    settings,
    resume,
    close_view,
    reduced_motion,
    show_fps,
    audio,
    music_volume,
    effects_volume,
    volume_controls,
    on,
    off,
    next,
    victory,
    defeat,
    deployment,
    campaign_ended,
    battles_played,
    progress_saved,
    start_campaign,
    no_save,
    menu,
    opponent_turn,
    character_actions,
    close_view_help,
    ability,
    wait,
    attack_target,
    ability_target,
    hit,
    counter,
    no_counter,
    clear_ground,
    target_safe,
    mend,
    cast,
    result,
    turn_complete,
    miss,
    mended,
    damage,
    restored,
    encounter_clear,
    required,
    type_name,
    erase,
    save_action,
    toggle,
    deploy,
    clear,
    failed,
    pending,
    return_to_title,
    company_regroup,
    memory_card,
    save_unavailable,
    cross_confirm,
    circle_back,
    start_menu,
    cross_circle_return,
    ready,
    out_of_range,
    choose_enemy,
    choose_target,
    target_down,
    ability_spent,
    finish_turn,
    action_refused,
    presentation_options,
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
            return selected == Language::spanish ? "UNA NUEVA AVENTURA"
                 : selected == Language::portuguese_brazil ? "UMA NOVA AVENTURA"
                 : "YOUR NEXT ADVENTURE";
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
        case Message::settings:
            return selected == Language::spanish ? "AJUSTES"
                 : selected == Language::portuguese_brazil ? "CONFIGURACOES"
                 : "SETTINGS";
        case Message::resume:
            return selected == Language::spanish ? "CONTINUAR"
                 : selected == Language::portuguese_brazil ? "CONTINUAR"
                 : "RESUME";
        case Message::close_view:
            return selected == Language::spanish ? "VISTA CERCANA"
                 : selected == Language::portuguese_brazil ? "VISTA DE PERTO"
                 : "CLOSE VIEW";
        case Message::reduced_motion:
            return selected == Language::spanish ? "MOVIMIENTO REDUCIDO"
                 : selected == Language::portuguese_brazil ? "MOVIMENTO REDUZIDO"
                 : "REDUCED MOTION";
        case Message::show_fps:
            return selected == Language::spanish ? "MOSTRAR FPS"
                 : selected == Language::portuguese_brazil ? "MOSTRAR FPS"
                 : "SHOW FPS";
        case Message::audio:
            return selected == Language::spanish ? "SONIDO"
                 : selected == Language::portuguese_brazil ? "AUDIO"
                 : "AUDIO";
        case Message::music_volume:
            return selected == Language::english ? "MUSIC" : "MUSICA";
        case Message::effects_volume:
            return selected == Language::spanish ? "EFECTOS"
                 : selected == Language::portuguese_brazil ? "EFEITOS" : "EFFECTS";
        case Message::volume_controls:
            return selected == Language::spanish ? "IZQ / DER  VOLUMEN"
                 : selected == Language::portuguese_brazil ? "ESQ / DIR  VOLUME"
                 : "LEFT / RIGHT  VOLUME";
        case Message::on:
            return selected == Language::spanish ? "SI"
                 : selected == Language::portuguese_brazil ? "SIM"
                 : "ON";
        case Message::off:
            return selected == Language::spanish ? "NO"
                 : selected == Language::portuguese_brazil ? "NAO"
                 : "OFF";
        case Message::next:
            return selected == Language::spanish ? "SIGUIENTE"
                 : selected == Language::portuguese_brazil ? "CONTINUAR"
                 : "NEXT";
        case Message::victory:
            return selected == Language::spanish ? "VICTORIA"
                 : selected == Language::portuguese_brazil ? "VITORIA"
                 : "VICTORY";
        case Message::defeat:
            return selected == Language::spanish ? "DERROTA"
                 : selected == Language::portuguese_brazil ? "DERROTA"
                 : "DEFEAT";
        case Message::deployment:
            return selected == Language::spanish ? "DESPLIEGUE"
                 : selected == Language::portuguese_brazil ? "POSICIONAMENTO"
                 : "DEPLOYMENT";
        case Message::campaign_ended:
            return selected == Language::spanish ? "CAMPANA TERMINADA"
                 : selected == Language::portuguese_brazil ? "CAMPANHA ENCERRADA"
                 : "CAMPAIGN ENDED";
        case Message::battles_played:
            return selected == Language::spanish ? "BATALLAS JUGADAS"
                 : selected == Language::portuguese_brazil ? "BATALHAS JOGADAS"
                 : "BATTLES PLAYED";
        case Message::progress_saved:
            return selected == Language::spanish ? "PROGRESO GUARDADO"
                 : selected == Language::portuguese_brazil ? "PROGRESSO SALVO"
                 : "PROGRESS SAVED";
        case Message::start_campaign:
            return selected == Language::spanish ? "INICIAR CAMPANA"
                 : selected == Language::portuguese_brazil ? "INICIAR CAMPANHA"
                 : "START CAMPAIGN";
        case Message::no_save:
            return selected == Language::spanish ? "NO HAY PARTIDA GUARDADA"
                 : selected == Language::portuguese_brazil ? "NENHUM SAVE NO CARTUCHO"
                 : "NO SAVE ON CARTRIDGE";
        case Message::menu:
            return selected == Language::spanish ? "MENU"
                 : selected == Language::portuguese_brazil ? "MENU"
                 : "MENU";
        case Message::opponent_turn:
            return selected == Language::spanish ? "TURNO DEL OPONENTE"
                 : selected == Language::portuguese_brazil ? "TURNO DO OPONENTE"
                 : "OPPONENT TURN";
        case Message::character_actions:
            return selected == Language::spanish ? "ACCIONES DEL PERSONAJE"
                 : selected == Language::portuguese_brazil ? "ACOES DO PERSONAGEM"
                 : "CHARACTER ACTIONS";
        case Message::close_view_help:
            return selected == Language::spanish ? "L REDUCIDO  R VISTA RAPIDA"
                 : selected == Language::portuguese_brazil ? "L REDUZIDO  R VISTA RAPIDA"
                 : "L REDUCED   R FAST CLOSE";
        case Message::ability:
            return selected == Language::spanish ? "HABILIDAD"
                 : selected == Language::portuguese_brazil ? "HABILIDADE"
                 : "ABILITY";
        case Message::wait:
            return selected == Language::spanish ? "ESPERAR"
                 : selected == Language::portuguese_brazil ? "ESPERAR"
                 : "WAIT";
        case Message::attack_target:
            return selected == Language::spanish ? "OBJETIVO DEL ATAQUE"
                 : selected == Language::portuguese_brazil ? "ALVO DO ATAQUE"
                 : "ATTACK TARGET";
        case Message::ability_target:
            return selected == Language::spanish ? "OBJETIVO DE HABILIDAD"
                 : selected == Language::portuguese_brazil ? "ALVO DA HABILIDADE"
                 : "ABILITY TARGET";
        case Message::hit:
            return selected == Language::spanish ? "IMPACTO"
                 : selected == Language::portuguese_brazil ? "ACERTO"
                 : "HIT";
        case Message::counter:
            return selected == Language::spanish ? "CONTRAATAQUE"
                 : selected == Language::portuguese_brazil ? "CONTRA-ATAQUE"
                 : "COUNTER";
        case Message::no_counter:
            return selected == Language::spanish ? "SIN CONTRAATAQUE"
                 : selected == Language::portuguese_brazil ? "SEM CONTRA-ATAQUE"
                 : "NO COUNTER";
        case Message::clear_ground:
            return selected == Language::spanish ? "TERRENO LIBRE"
                 : selected == Language::portuguese_brazil ? "AREA LIVRE"
                 : "CLEAR GROUND";
        case Message::target_safe:
            return selected == Language::spanish ? "OBJETIVO SEGURO"
                 : selected == Language::portuguese_brazil ? "ALVO SEGURO"
                 : "TARGET SAFE";
        case Message::mend:
            return selected == Language::spanish ? "CURAR"
                 : selected == Language::portuguese_brazil ? "CURAR"
                 : "MEND";
        case Message::cast:
            return selected == Language::spanish ? "LANZAR"
                 : selected == Language::portuguese_brazil ? "LANCAR"
                 : "CAST";
        case Message::result:
            return selected == Language::spanish ? "RESULTADO"
                 : selected == Language::portuguese_brazil ? "RESULTADO"
                 : "RESULT";
        case Message::turn_complete:
            return selected == Language::spanish ? "TURNO COMPLETO"
                 : selected == Language::portuguese_brazil ? "TURNO CONCLUIDO"
                 : "TURN COMPLETE";
        case Message::miss:
            return selected == Language::spanish ? "FALLO"
                 : selected == Language::portuguese_brazil ? "ERROU"
                 : "MISS";
        case Message::mended:
            return selected == Language::spanish ? "CURADO"
                 : selected == Language::portuguese_brazil ? "CURADO"
                 : "MENDED";
        case Message::damage:
            return selected == Language::spanish ? "DANO"
                 : selected == Language::portuguese_brazil ? "DANO"
                 : "DAMAGE";
        case Message::restored:
            return selected == Language::spanish ? "RESTAURADO"
                 : selected == Language::portuguese_brazil ? "RECUPERADO"
                 : "RESTORED";
        case Message::encounter_clear:
            return selected == Language::spanish ? "COMBATE SUPERADO"
                 : selected == Language::portuguese_brazil ? "BATALHA CONCLUIDA"
                 : "ENCOUNTER CLEAR";
        case Message::required:
            return selected == Language::spanish ? "OBLIGATORIO"
                 : selected == Language::portuguese_brazil ? "OBRIGATORIO"
                 : "REQUIRED";
        case Message::type_name:
            return selected == Language::spanish ? "ESCRIBIR"
                 : selected == Language::portuguese_brazil ? "DIGITAR"
                 : "TYPE";
        case Message::erase:
            return selected == Language::spanish ? "BORRAR"
                 : selected == Language::portuguese_brazil ? "APAGAR"
                 : "ERASE";
        case Message::save_action:
            return selected == Language::spanish ? "GUARDAR"
                 : selected == Language::portuguese_brazil ? "SALVAR"
                 : "SAVE";
        case Message::toggle:
            return selected == Language::spanish ? "CAMBIAR"
                 : selected == Language::portuguese_brazil ? "ALTERAR"
                 : "TOGGLE";
        case Message::deploy:
            return selected == Language::spanish ? "DESPLEGAR"
                 : selected == Language::portuguese_brazil ? "POSICIONAR"
                 : "DEPLOY";
        case Message::clear:
            return selected == Language::spanish ? "SUPERADO"
                 : selected == Language::portuguese_brazil ? "CONCLUIDO"
                 : "CLEAR";
        case Message::failed:
            return selected == Language::spanish ? "FALLIDO"
                 : selected == Language::portuguese_brazil ? "FALHOU"
                 : "FAILED";
        case Message::pending:
            return selected == Language::spanish ? "PENDIENTE"
                 : selected == Language::portuguese_brazil ? "PENDENTE"
                 : "PENDING";
        case Message::return_to_title:
            return selected == Language::spanish ? "VOLVER AL TITULO"
                 : selected == Language::portuguese_brazil ? "VOLTAR AO TITULO"
                 : "RETURN TO TITLE";
        case Message::company_regroup:
            return selected == Language::spanish ? "LA COMPANIA DEBE REAGRUPARSE"
                 : selected == Language::portuguese_brazil ? "A COMPANHIA DEVE SE REORGANIZAR"
                 : "THE COMPANY MUST REGROUP";
        case Message::memory_card:
            return selected == Language::spanish ? "TARJETA DE MEMORIA"
                 : selected == Language::portuguese_brazil ? "CARTAO DE MEMORIA"
                 : "MEMORY CARD";
        case Message::save_unavailable:
            return selected == Language::spanish ? "PARTIDA NO DISPONIBLE"
                 : selected == Language::portuguese_brazil ? "SAVE INDISPONIVEL"
                 : "SAVE DATA IS NOT AVAILABLE";
        case Message::cross_confirm:
            return selected == Language::spanish ? "CRUZ  CONFIRMAR"
                 : selected == Language::portuguese_brazil ? "X  CONFIRMAR"
                 : "CROSS  CONFIRM";
        case Message::circle_back:
            return selected == Language::spanish ? "CIRCULO  VOLVER"
                 : selected == Language::portuguese_brazil ? "CIRCULO  VOLTAR"
                 : "CIRCLE  BACK";
        case Message::start_menu:
            return selected == Language::spanish ? "START  MENU"
                 : selected == Language::portuguese_brazil ? "START  MENU"
                 : "START  MENU";
        case Message::cross_circle_return:
            return selected == Language::spanish ? "CRUZ O CIRCULO PARA VOLVER"
                 : selected == Language::portuguese_brazil ? "X OU CIRCULO PARA VOLTAR"
                 : "CROSS OR CIRCLE TO RETURN";
        case Message::ready:
            return selected == Language::spanish ? "LISTO"
                 : selected == Language::portuguese_brazil ? "PRONTO"
                 : "READY";
        case Message::out_of_range:
            return selected == Language::spanish ? "FUERA DE ALCANCE"
                 : selected == Language::portuguese_brazil ? "FORA DE ALCANCE"
                 : "OUT OF RANGE";
        case Message::choose_enemy:
            return selected == Language::spanish ? "ELIGE UN ENEMIGO"
                 : selected == Language::portuguese_brazil ? "ESCOLHA UM INIMIGO"
                 : "CHOOSE AN ENEMY";
        case Message::choose_target:
            return selected == Language::spanish ? "ELIGE UN OBJETIVO"
                 : selected == Language::portuguese_brazil ? "ESCOLHA UM ALVO"
                 : "CHOOSE A TARGET";
        case Message::target_down:
            return selected == Language::spanish ? "OBJETIVO CAIDO"
                 : selected == Language::portuguese_brazil ? "ALVO DERROTADO"
                 : "TARGET IS DOWN";
        case Message::ability_spent:
            return selected == Language::spanish ? "HABILIDAD AGOTADA"
                 : selected == Language::portuguese_brazil ? "HABILIDADE ESGOTADA"
                 : "ABILITY SPENT";
        case Message::finish_turn:
            return selected == Language::spanish ? "TERMINA ESTE TURNO"
                 : selected == Language::portuguese_brazil ? "TERMINE ESTE TURNO"
                 : "FINISH THIS TURN";
        case Message::action_refused:
            return selected == Language::spanish ? "ACCION RECHAZADA"
                 : selected == Language::portuguese_brazil ? "ACAO RECUSADA"
                 : "ACTION REFUSED";
        case Message::presentation_options:
            return selected == Language::spanish ? "OPCIONES DE PRESENTACION"
                 : selected == Language::portuguese_brazil ? "OPCOES DE APRESENTACAO"
                 : "PRESENTATION OPTIONS";
    }
    return "";
}

}  // namespace grandleon::client::console_localization
