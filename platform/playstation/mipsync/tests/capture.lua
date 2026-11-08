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
  -- The renderer emits its 600-frame budget result after roughly 1,000 GPU
  -- VSync callbacks. Leave enough room for the serial line to flush.
  if frame == 1200 then
    PCSX.quit(0)
  end
end)

print('MIPSYNC_CAPTURE_READY')

