local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local forecast_capture = false
local cinematic_capture = false
local final_capture = false
local actions = {}

local function sequence(start, values)
  for index, value in ipairs(values) do
    actions[start + (index - 1) * 6] = value
  end
end

-- The opening archer walks into reach, opens the action menu and attacks. The fixed pause
-- leaves the optional close-combat camera enough time to fly in and answer.
sequence(80, {
  0x4000, 0x0020, 0x0020, 0x4000, -- select, destination 3,1, move
  0x4000, 0x0020, 0x0020, 0x4000, -- Attack, target 5,1, commit
})

-- Elowen then moves into range and uses every ability row she owns (one in
-- Bell Orchard) to restore Rowan through the same action/forecast boundary.
sequence(280, {
  0x0080, 0x0080, 0x0080, 0x0080, 0x0040, 0x0040, -- cursor to 1,3
  0x4000, 0x0020, 0x0020, 0x0010, 0x4000,         -- select, move to 3,2
  0x0040, 0x4000, 0x0010, 0x4000,                 -- Ability 1, archer, cast
})

-- Spend the two remaining player units through the visible Wait row.  This
-- hands the encounter to the real package-authored opponent policy.
sequence(530, {
  0x0080, 0x0080, 0x0040, 0x4000, 0x4000, 0x0010, 0x4000,
  0x0020, 0x0040, 0x0040, 0x4000, 0x4000, 0x0010, 0x4000,
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
  if runtime_frame == 120 then forecast_capture = true end
  if runtime_frame == 170 then cinematic_capture = true end
  if runtime_frame == 1800 then final_capture = true end
  return true
end)

grandleon_combat_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
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
