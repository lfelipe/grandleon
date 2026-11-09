local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local capture_index = 0
local capture_due = false
local actions = {}

local function sequence(start, values)
  for index, value in ipairs(values) do
    actions[start + (index - 1) * 6] = value
  end
end

-- Dismiss the authored opening and send Rowan over a mixed horizontal and
-- vertical route.  Capturing both axes prevents a good side-view walk from
-- hiding a backwards or sliding north/south presentation.
actions[20] = 0x4000
actions[40] = 0x4000
actions[60] = 0x4000
actions[80] = 0x4000
sequence(100, {
  0x4000, 0x0020, 0x0020, 0x0040, 0x4000,
})

grandleon_review_movement_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon PS1 movement review', function()
  runtime_frame = runtime_frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[runtime_frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if runtime_frame >= 90 and runtime_frame <= 220 and
     runtime_frame % 2 == 0 then
    capture_due = true
  end
  if runtime_frame == 230 then
    print(string.format('MIPSYNC_REVIEW_MOVEMENT frames=%d', capture_index))
    PCSX.quit(0)
  end
  return true
end)

grandleon_review_movement_capture = PCSX.Events.createEventListener(
    'GPU::Vsync', function()
  if not capture_due then return end
  capture_due = false
  local shot = PCSX.GPU.takeScreenShot()
  local path = string.format('/out/frame-%04d.bin', capture_index)
  local output = Support.File.open(path, 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  capture_index = capture_index + 1
end)

print(string.format('MIPSYNC_REVIEW_MOVEMENT_READY update=%08x', update_address))
