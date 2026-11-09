-- Observe real cue calls and the presentation clock while the normal combat
-- probe supplies controller input. No game state or cue arguments are changed.
local ffi = require('ffi')
local cue_address = assert(tonumber(os.getenv('GRANDLEON_AUDIO_CUE_ADDRESS')))
local duration_address = assert(tonumber(os.getenv('GRANDLEON_RESULT_DURATION_ADDRESS')))
local remaining_address = assert(tonumber(os.getenv('GRANDLEON_RESULT_FRAMES_ADDRESS')))
local launches, impacts = {}, {}
local function read_word(address)
  return tonumber(ffi.cast('uint32_t*', PCSX.getMemPtr() + bit.band(address, 0x1fffff))[0])
end
grandleon_combat_audio = PCSX.addBreakpoint(cue_address, 'Exec', 4, 'Combat sound timing', function()
  local cue = tonumber(PCSX.getRegisters().GPR.n.a0)
  local elapsed = read_word(duration_address) - read_word(remaining_address)
  if cue >= 16 and cue <= 20 then
    launches[#launches + 1] = elapsed
  elseif cue == 7 or cue == 9 or cue == 10 then
    impacts[#impacts + 1] = elapsed
  end
  return true
end)
local quit = PCSX.quit
PCSX.quit = function(code)
  assert(#launches >= 2 and #impacts >= 2, 'attack and counterattack must both play')
  assert(launches[1] == 28 and impacts[1] == 37, 'first attack audio is out of phase')
  assert(launches[2] == 68 and impacts[2] == 77, 'counterattack audio is out of phase')
  print('MIPSYNC_COMBAT_AUDIO_PASS launch=28 impact=37 counter_launch=68 counter_impact=77')
  quit(code)
end
dofile('/probe/combat_probe.lua')
