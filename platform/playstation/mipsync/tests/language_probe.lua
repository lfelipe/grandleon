local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_LANGUAGE_MODE') or 'write'
local context = os.getenv('GRANDLEON_LANGUAGE_CONTEXT') or 'title'
local frame = 0
local actions = {}

if mode == 'write' then
  if context == 'title' then
    -- A new campaign title has New Game, Help, Settings.
    actions[30] = 0x0040
    actions[50] = 0x0040
    actions[70] = 0x4000
  else
    -- The standalone renderer probe begins directly on the board.
    actions[30] = 0x0008
    actions[50] = 0x0040
    actions[70] = 0x0040
    actions[90] = 0x4000
  end
  actions[110] = 0x0040
  actions[130] = 0x0040
  actions[150] = 0x0040
  actions[170] = 0x4000
end

grandleon_language_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon language probe', function()
  frame = frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if (mode == 'write' and frame == 230) or
     (mode == 'read' and frame == 70) then
    print(string.format('MIPSYNC_LANGUAGE_PROBE mode=%s frames=%d', mode, frame))
    PCSX.quit(0)
  end
  return true
end)

print(string.format('MIPSYNC_LANGUAGE_READY mode=%s update=%08x',
                    mode, update_address))
