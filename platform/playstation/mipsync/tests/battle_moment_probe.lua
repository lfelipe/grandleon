local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local frame = 0
local capture = false
local actions = {
  [30] = 0x4000,  -- Close the authored opening line
  [50] = 0x4000,  -- Select the captain
  [70] = 0x4000,  -- Keep the authored starting tile
  [90] = 0x0040,  -- Talk row
  [110] = 0x4000, -- Choose Talk
  [130] = 0x0020, -- Aim at the adjacent envoy
  [150] = 0x4000, -- Talk through the ordinary command path
  [190] = 0x4000, -- Close the triggered line
}

grandleon_battle_moment_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon battle-moment probe', function()
  frame = frame + 1
  local held = actions[frame] or 0
  if held ~= 0 then
    print(string.format('MIPSYNC_BATTLE_MOMENT_INPUT frame=%d held=%04x',
                        frame, held))
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if frame == 170 then capture = true end
  if frame == 230 then
    print(string.format('MIPSYNC_BATTLE_MOMENT_PROBE frames=%d', frame))
    PCSX.quit(0)
  end
  return true
end)

grandleon_battle_moment_capture = PCSX.Events.createEventListener(
    'GPU::Vsync', function()
  if not capture then return end
  capture = false
  local shot = PCSX.GPU.takeScreenShot()
  local bytes = shot.data.size
  local output = Support.File.open('/out/battle-moment.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  print(string.format('MIPSYNC_BATTLE_MOMENT_CAPTURE %dx%d bytes=%d',
                      tonumber(shot.width), tonumber(shot.height), bytes))
end)

print(string.format('MIPSYNC_BATTLE_MOMENT_READY update=%08x', update_address))
