local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local frame = 0
local capture_index = 0
local actions = {}

local function sequence(start, spacing, values)
  for index, value in ipairs(values) do
    actions[start + (index - 1) * spacing] = value
  end
end

-- Enter the two-player campaign and reach Bell Orchard without a deployment
-- detour. The following inputs leave enough screen time to see the routed
-- walk before opening and committing an attack.
actions[30] = 0x4000
actions[50] = 0x0040
actions[70] = 0x4000
actions[100] = 0x4000
actions[130] = 0x4000
sequence(170, 12, {0x4000, 0x0020, 0x0020, 0x4000})
sequence(250, 12, {0x4000, 0x0020, 0x0020, 0x4000})

grandleon_review_motion_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon review motion', function()
  frame = frame + 1
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  if frame >= 150 and frame <= 450 and frame % 2 == 0 then
    local shot = PCSX.GPU.takeScreenShot()
    local path = string.format('/out/frame-%04d.bin', capture_index)
    local output = Support.File.open(path, 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    capture_index = capture_index + 1
  end
  if frame == 470 then
    print(string.format('MIPSYNC_REVIEW_MOTION frames=%d', capture_index))
    PCSX.quit(0)
  end
  return true
end)

print(string.format('MIPSYNC_REVIEW_MOTION_READY update=%08x', update_address))
