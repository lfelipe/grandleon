local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local capture_pending = false
local actions = {}
for _ = 1, 10 do actions[#actions + 1] = 0x0020 end -- right, x=3 to x=13
for _ = 1, 6 do actions[#actions + 1] = 0x0040 end  -- down, y=2 to y=8
local opening = {
  [20] = 0x4000,
  [35] = 0x4000,
  [50] = 0x4000,
  [65] = 0x4000,
}

grandleon_scroll_breakpoint = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon scrolling probe', function()
  runtime_frame = runtime_frame + 1
  local held = opening[runtime_frame] or 0
  if runtime_frame >= 90 then
    local action_frame = runtime_frame - 90
    local index = math.floor(action_frame / 8) + 1
    if index <= #actions and action_frame % 8 == 0 then
      held = actions[index]
      print(string.format('MIPSYNC_SCROLL action=%d held=%04x frame=%d',
                          index, held, runtime_frame))
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if runtime_frame == 900 then capture_pending = true end
  return true
end)

grandleon_scroll_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if capture_pending then
    capture_pending = false
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-scroll-final.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_SCROLL_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height),
                        bytes))
    PCSX.quit(0)
  end
end)

print(string.format('MIPSYNC_SCROLL_READY update=%08x', update_address))
