local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local forecast_capture = false
local cinematic_capture = false
local final_capture = false
local actions = {}
local player_two_actions = {}
local capture_motion = os.getenv('GRANDLEON_CAPTURE_MOTION') == '1'
local motion_due = false
local motion_index = 0

local function sequence(target, start, values)
  for index, value in ipairs(values) do
    target[start + (index - 1) * 6] = value
  end
end

-- Dismiss both two-line authored opening conversations before driving the
-- board, so the following inputs prove combat rather than merely closing
-- dialogue.
actions[20] = 0x4000
actions[40] = 0x4000
actions[60] = 0x4000
actions[80] = 0x4000

-- Rowan walks beside the raider, then attacks after the visible movement has
-- settled. Keeping these phases separate proves that input cannot skip the
-- route animation or accidentally act from the old square.
sequence(actions, 100, {
  0x4000, 0x0020, 0x0020, 0x0020, 0x4000, -- select, destination 4,1, move
})
sequence(actions, 230, {
  0x4000, 0x0000, 0x4000, -- Attack auto-focuses legal target 5,1; commit
})

-- After the close view returns, Elowen casts her authored ability on Rowan.
-- She is already within its two-tile reach, so the probe also covers choosing
-- an action without forcing an unnecessary move.
sequence(actions, 500, {
  0x0080, 0x0080, 0x0080, -- cursor from 5,1 to Elowen at 2,1
  0x4000, 0x4000,         -- select, keep the current square
  0x4000,                 -- Ability 1 (Attack absent without legal targets)
  0x0020, 0x0020, 0x4000, -- Rowan at 4,1, cast
})

-- Player 2 spends Akari's two units through the second controller. This hands
-- the encounter to the package-authored opponent policy without violating
-- the co-op ownership rules the probe is meant to protect.
sequence(player_two_actions, 770, {
  0x4000, 0x4000, 0x0010, 0x4000,
  0x0020, 0x4000, 0x4000, 0x0010, 0x4000,
})

grandleon_combat_breakpoint = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon combat/action probe', function()
  runtime_frame = runtime_frame + 1
  local held = actions[runtime_frame] or 0
  if held ~= 0 then
    print(string.format('MIPSYNC_COMBAT_INPUT held=%04x frame=%d',
                        held, runtime_frame))
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = player_two_actions[runtime_frame] or 0
  if runtime_frame == 240 then forecast_capture = true end
  if runtime_frame == 300 then cinematic_capture = true end
  if runtime_frame == 1800 then final_capture = true end
  if capture_motion and runtime_frame >= 90 and runtime_frame <= 900 and
     runtime_frame % 2 == 0 then motion_due = true end
  return true
end)

grandleon_combat_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if motion_due then
    motion_due = false
    local shot = PCSX.GPU.takeScreenShot()
    local output = Support.File.open(string.format('/out/frame-%04d.bin', motion_index), 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    motion_index = motion_index + 1
  end
  if forecast_capture then
    forecast_capture = false
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-combat-forecast.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_COMBAT_FORECAST_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height), bytes))
  end
  if cinematic_capture then
    cinematic_capture = false
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-combat-close.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_COMBAT_CLOSE_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height), bytes))
  end
  if final_capture then
    final_capture = false
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-combat-final.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_COMBAT_FINAL_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height), bytes))
    PCSX.quit(0)
  end
end)

print(string.format('MIPSYNC_COMBAT_READY update=%08x', update_address))
