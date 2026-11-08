local frame = 0
local listener

listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  frame = frame + 1
  if frame == 900 then
    local shot = PCSX.GPU.takeScreenShot()
    print(string.format('MIPSYNC_CAPTURE %dx%d bytes=%d frame=%d',
      tonumber(shot.width), tonumber(shot.height), shot.data.size, frame))
    local output = Support.File.open('/out/mipsync-bell-0900.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
  end
  -- BIOS/disc boot consumes a variable number of retraces before the runtime's
  -- own 600-frame budget window begins. Leave enough room for the serial line
  -- to flush on the pinned interpreter build.
  if frame == 1800 then
    PCSX.quit(0)
  end
end)

print('MIPSYNC_CAPTURE_READY')

