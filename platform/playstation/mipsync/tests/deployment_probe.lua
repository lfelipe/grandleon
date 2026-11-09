local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local frame = 0
local capture = false
local actions = {
  [30] = 0x4000,  -- Select the Dawn Guard at the opening cursor.
  [50] = 0x0040,  -- Move onto an empty authored deployment tile.
  [70] = 0x4000,  -- Place the selected unit through the simulation command.
  [110] = 0x0008, -- Begin the battle.
}

grandleon_deployment_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon deployment probe', function()
  frame = frame + 1
  local held = actions[frame] or 0
  if held ~= 0 then
    print(string.format('MIPSYNC_DEPLOYMENT_INPUT frame=%d held=%04x',
                        frame, held))
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if frame == 85 then capture = true end
  if frame == 170 then
    print(string.format('MIPSYNC_DEPLOYMENT_PROBE frames=%d', frame))
    PCSX.quit(0)
  end
  return true
end)

grandleon_deployment_capture = PCSX.Events.createEventListener(
    'GPU::Vsync', function()
  if not capture then return end
  capture = false
  local shot = PCSX.GPU.takeScreenShot()
  local bytes = shot.data.size
  local output = Support.File.open('/out/deployment-result.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  print(string.format('MIPSYNC_DEPLOYMENT_CAPTURE %dx%d bytes=%d',
                      tonumber(shot.width), tonumber(shot.height), bytes))
end)

print(string.format('MIPSYNC_DEPLOYMENT_READY update=%08x', update_address))
