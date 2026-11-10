local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_SETTINGS_MODE') or 'write'
local frame = 0
local capture = false
local actions = {}

if mode == 'write' then
  -- A fresh title has New Game, Help, Settings. Open Settings, move to
  -- Show FPS, turn it on, then leave through the normal Circle path.
  actions[30] = 0x0040
  actions[50] = 0x0040
  actions[70] = 0x4000
  actions[90] = 0x0040
  actions[130] = 0x4000
  actions[180] = 0x2000
  -- Return to the title, start the one-player campaign, enter Bell Orchard,
  -- and reach the same Settings rows through the in-battle Start menu.
  actions[200] = 0x4000
  actions[220] = 0x4000
  actions[250] = 0x4000
  actions[280] = 0x4000
  actions[320] = 0x0008
  actions[340] = 0x0040
  actions[360] = 0x0040
  actions[380] = 0x4000
end

grandleon_settings_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon settings probe', function()
  frame = frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if mode == 'write' and frame == 405 then capture = true end
  if (mode == 'write' and frame == 440) or
     (mode == 'read' and frame == 70) then
    print(string.format('MIPSYNC_SETTINGS_PROBE mode=%s frames=%d', mode, frame))
    PCSX.quit(0)
  end
  return true
end)

grandleon_settings_capture = PCSX.Events.createEventListener('GPU::Vsync', function()
  if not capture then return end
  capture = false
  local shot = PCSX.GPU.takeScreenShot()
  local bytes = shot.data.size
  local output = Support.File.open('/out/settings.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  print(string.format('MIPSYNC_SETTINGS_CAPTURE %dx%d bytes=%d',
                      tonumber(shot.width), tonumber(shot.height), bytes))
end)

print(string.format('MIPSYNC_SETTINGS_READY mode=%s update=%08x',
                    mode, update_address))
