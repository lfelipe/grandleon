local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local capture_pending = false

local actions = {
  0x4000, 0x0020, 0x4000, 0x8000, -- select, move right, confirm, wait
  0x0080, 0x0040, 0x4000, 0x8000, -- second unit: left/down, select/wait
  0x0040, 0x4000, 0x8000,         -- third unit: down, select/wait
  0x0020, 0x0040, 0x4000, 0x8000, -- fourth unit: right/down, select/wait
}

grandleon_input_breakpoint = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon input-boundary probe', function()
  runtime_frame = runtime_frame + 1
  local held = 0
  if runtime_frame >= 90 then
    local action_frame = runtime_frame - 90
    local index = math.floor(action_frame / 8) + 1
    if index <= #actions and action_frame % 8 == 0 then
      held = actions[index]
      print(string.format('MIPSYNC_INPUT action=%d held=%04x frame=%d',
                          index, held, runtime_frame))
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  if runtime_frame == 900 then capture_pending = true end
  return true
end)

grandleon_input_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if capture_pending then
    capture_pending = false
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-input-final.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_INPUT_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height),
                        bytes))
    PCSX.quit(0)
  end
end)

print(string.format('MIPSYNC_INPUT_READY update=%08x', update_address))
