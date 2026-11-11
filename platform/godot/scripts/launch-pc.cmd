@echo off
setlocal DisableDelayedExpansion
set "bundle=%~dp0"
set "campaign=steel_and_cedar"
if exist "%bundle%DEFAULT-CAMPAIGN" set /p campaign=<"%bundle%DEFAULT-CAMPAIGN"
if defined GRANDLEON_CAMPAIGN set "campaign=%GRANDLEON_CAMPAIGN%"
if not defined GRANDLEON_PLAYERS set "GRANDLEON_PLAYERS=2"
set "mode="
if exist "%bundle%PLAYER-ONLY" set "mode=--player-only"
"%bundle%Godot_v4.7.2-stable_win64_console.exe" --path "%bundle%project" -- --package=res://game.gpk --campaign=%campaign% --players=%GRANDLEON_PLAYERS% %mode% %*
exit /b %errorlevel%
