local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local frame = 0
local capture = false
local actions = {
  [30] = 0x4000,  -- Select the Dawn Guard
  [50] = 0x0020,  -- Step beside the River Watch
  [70] = 0x4000,  -- Commit the move
  [100] = 0x4000, -- Choose Attack
  [120] = 0x0020, -- Target the River Watch
  [140] = 0x4000, -- Attack and take its counter
  [500] = 0x0080, -- Return the cursor to the wounded Dawn Guard
  [520] = 0x4000, -- Select the Dawn Guard
  [540] = 0x4000, -- Keep the current tile
  [560] = 0x0040, -- Choose the now-usable field tonic
  [580] = 0x4000, -- Use the item through the ordinary action path
}

grandleon_item_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon item probe', function()
  frame = frame + 1
  local held = actions[frame] or 0
  if held ~= 0 then
    print(string.format('MIPSYNC_ITEM_INPUT frame=%d held=%04x', frame, held))
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if frame == 600 then capture = true end
  if frame == 850 then
    print(string.format('MIPSYNC_ITEM_PROBE frames=%d', frame))
    PCSX.quit(0)
  end
  return true
end)

grandleon_item_capture = PCSX.Events.createEventListener(
    'GPU::Vsync', function()
  if not capture then return end
  capture = false
  local shot = PCSX.GPU.takeScreenShot()
  local bytes = shot.data.size
  local output = Support.File.open('/out/item-result.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  print(string.format('MIPSYNC_ITEM_CAPTURE %dx%d bytes=%d',
                      tonumber(shot.width), tonumber(shot.height), bytes))
end)

print(string.format('MIPSYNC_ITEM_READY update=%08x', update_address))
