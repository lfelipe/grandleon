local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_PROJECT_MODE') or 'one'
local frame = 0

local function update()
  frame = frame + 1
  local held = 0
  if mode == 'one' then
    if frame == 30 then
      held = 0x4000 -- New Game.
    elseif frame == 70 then
      -- A fixed one-player encounter must already be at its objective screen.
      -- If a player-count chooser was inserted, this press is consumed there
      -- and the simulation never starts, which makes the probe fail.
      held = 0x4000
    end
  else
    if frame == 30 then
      held = 0x4000 -- New Game opens the authored player-count chooser.
    elseif frame == 50 then
      held = 0x0040 -- Select 2 Players.
    elseif frame == 70 or frame == 100 or frame == 130 then
      held = 0x4000 -- Confirm players, prologue, and objective.
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if held ~= 0 then
    print(string.format('MIPSYNC_PROJECT_INPUT frame=%d held=%04x', frame, held))
  end
  if frame >= 180 then PCSX.quit(0) end
  return true
end

grandleon_project_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon project launch', update)

print(string.format('MIPSYNC_PROJECT_PROBE_READY mode=%s update=%08x',
                    mode, update_address))
