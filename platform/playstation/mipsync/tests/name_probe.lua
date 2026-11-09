local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_NAME_MODE') or 'write'
local frame = 0
local actions = {}

if mode == 'write' then
  actions[30] = 0x4000  -- New Game
  actions[50] = 0x4000  -- One Player
  for index = 0, 13 do
    actions[70 + index] = 0x2000  -- erase SER ROWAN VALE
  end
  actions[100] = 0x4000 -- type A
  actions[120] = 0x0008 -- save
else
  actions[30] = 0x0040  -- Continue
  actions[50] = 0x4000
end

grandleon_name_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon name probe', function()
  frame = frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if (mode == 'write' and frame == 180) or
     (mode == 'read' and frame == 100) then
    print(string.format('MIPSYNC_NAME_PROBE mode=%s frames=%d', mode, frame))
    PCSX.quit(0)
  end
  return true
end)

print(string.format('MIPSYNC_NAME_READY mode=%s update=%08x',
                    mode, update_address))
