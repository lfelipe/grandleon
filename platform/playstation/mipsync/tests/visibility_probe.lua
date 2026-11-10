local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local runtime_frame = 0
local before_capture = false
local after_capture = false
local actions = {}
local capture_motion = os.getenv('GRANDLEON_CAPTURE_MOTION') == '1'
local motion_due = false
local motion_index = 0
-- Both authored opening lines wrap to two readable pages. Consume every
-- page before normal board input; Confirm never skips remaining dialogue.
actions[20] = 0x4000
actions[35] = 0x4000
actions[50] = 0x4000
actions[65] = 0x4000

local function sequence(start, spacing, values)
  for index, value in ipairs(values) do
    actions[start + (index - 1) * spacing] = value
  end
end

-- Use the real one-player Twin Gate encounter. Rowan first advances to the
-- castle approach, and both friendly units finish their turns so the authored
-- opponent gets a normal turn. On the next round Rowan crosses the doorway at
-- 13,6. This is deliberately player input rather than a test-only reveal hook.
sequence(80, 10, {
  0x4000,                         -- select Rowan at 6,6
  0x0020, 0x0020, 0x0020,
  0x4000,                         -- move to 9,6
})
sequence(200, 10, {
  0x0010, 0x4000,                 -- choose Wait
})
sequence(260, 10, {
  0x0080, 0x0080, 0x0080, 0x0080,
  0x4000, 0x4000,                 -- select Elowen; keep 5,6
  0x0010, 0x4000,                 -- choose Wait
})
-- After the enemy turn returns the camera to the player's cursor, inspect
-- the covered doorway with that cursor. Looking inside must not reveal it.
sequence(1350, 10, {
  0x0020, 0x0020, 0x0020, 0x0020,
  0x0020, 0x0020, 0x0020, 0x0020,
})
sequence(1480, 10, {
  0x0080, 0x0080, 0x0080, 0x0080,
  0x4000,                         -- select Rowan at 9,6
  0x0020, 0x0020, 0x0020, 0x0020, 0x4000,
                                    -- cross into the castle at 13,6
})

grandleon_visibility_breakpoint = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon visibility probe', function()
  runtime_frame = runtime_frame + 1
  local held = actions[runtime_frame] or 0
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if held ~= 0 then
    print(string.format('MIPSYNC_VISIBILITY_INPUT held=%04x frame=%d',
                        held, runtime_frame))
  end
  if runtime_frame == 1450 then before_capture = true end
  if runtime_frame == 1800 then after_capture = true end
  if capture_motion and runtime_frame >= 1300 and runtime_frame <= 1800 and
     runtime_frame % 2 == 0 then motion_due = true end
  return true
end)

grandleon_visibility_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if motion_due then
    motion_due = false
    local shot = PCSX.GPU.takeScreenShot()
    local output = Support.File.open(string.format('/out/frame-%04d.bin', motion_index), 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    motion_index = motion_index + 1
  end
  local name = nil
  if before_capture then
    before_capture = false
    name = 'before'
  elseif after_capture then
    after_capture = false
    name = 'after'
  end
  if name then
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    local output = Support.File.open('/out/mipsync-visibility-' .. name .. '.bin',
                                     'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_VISIBILITY_CAPTURE name=%s %dx%d bytes=%d',
                        name, tonumber(shot.width), tonumber(shot.height),
                        bytes))
    if name == 'after' then PCSX.quit(0) end
  end
end)

print(string.format('MIPSYNC_VISIBILITY_READY update=%08x', update_address))
