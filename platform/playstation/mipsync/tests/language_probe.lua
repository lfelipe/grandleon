local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_LANGUAGE_MODE') or 'write'
local context = os.getenv('GRANDLEON_LANGUAGE_CONTEXT') or 'title'
local frame = 0
local actions = {}
local first_setting = 110

if mode == 'write' then
  if context == 'title' then
    -- A new campaign title has New Game, Help, Settings.
    actions[30] = 0x0040
    actions[50] = 0x0040
    actions[70] = 0x4000
  else
    -- The Deepwood co-op fixture opens with four authored dialogue lines.
    actions[20] = 0x4000
    actions[35] = 0x4000
    actions[50] = 0x4000
    actions[65] = 0x4000
    actions[90] = 0x0008
    actions[110] = 0x0040
    actions[130] = 0x0040
    actions[150] = 0x4000
    first_setting = 170
  end
  -- Music and Effects precede Language in the current Settings menu.
  for row = 0, 3 do actions[first_setting + row * 20] = 0x0040 end
  actions[first_setting + 100] = 0x4000
end

grandleon_language_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon language probe', function()
  frame = frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if (mode == 'write' and frame == 330) or
     (mode == 'read' and frame == 70) then
    print(string.format('MIPSYNC_LANGUAGE_PROBE mode=%s frames=%d', mode, frame))
    PCSX.quit(0)
  end
  return true
end)

print(string.format('MIPSYNC_LANGUAGE_READY mode=%s update=%08x',
                    mode, update_address))
