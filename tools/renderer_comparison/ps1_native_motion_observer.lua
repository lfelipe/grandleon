-- SPDX-License-Identifier: MIT
assert(PCSX.getCPUCycles, 'native cycle API is required')
-- Capture actual displayed frames at every native VSync. The original legal
-- gameplay/audio/camera assertions remain intact; this observer changes no input.
dofile('/probe/combat_audio_original.lua')
local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local ffi, bit = require('ffi'), require('bit')
local frame, index, finished = 0, 0, false
local first_update = grandleon_capture_first_update or 90
local last_update = grandleon_capture_last_update or 480
local display_origin, unknown_writes = nil, 0
local trace = assert(io.open('/out/presentation.csv', 'wb'))
trace:write('index,cycle,update,display_origin\n')
grandleon_presentation_tick = function(value) frame = value end
-- Observe actual GP1 Display Area Start commands, not screenshot differences.
local function word(address)
  return tonumber(ffi.cast('const uint32_t*', PCSX.getMemPtr() + bit.band(address, 0x1fffff))[0])
end
local function observe_display_write(address, width)
  if frame == 0 or finished then return true end
  local registers = PCSX.getRegisters()
  local pc = tonumber(registers.pc)
  if bit.band(pc, 0x1fffffff) >= 0x200000 or width ~= 4 then
    unknown_writes = unknown_writes + 1
    return true
  end
  local value
  for _, candidate in ipairs({pc, pc - 4}) do
    local instruction = word(candidate)
    if bit.rshift(instruction, 26) == 0x2b then
      local base = tonumber(registers.GPR.r[bit.band(bit.rshift(instruction, 21), 31)])
      local offset = bit.band(instruction, 0xffff)
      if offset >= 0x8000 then offset = offset - 0x10000 end
      local effective = (base + offset) % 4294967296
      if bit.band(effective, 0x1fffffff) == bit.band(address, 0x1fffffff) then
        value = tonumber(registers.GPR.r[bit.band(bit.rshift(instruction, 16), 31)])
        break
      end
    end
  end
  if value == nil then
    unknown_writes = unknown_writes + 1
    if unknown_writes <= 3 then print(string.format('PRESENTATION_GP1_UNKNOWN pc=%08x address=%08x', pc, address)) end
    return true
  end
  if bit.band(bit.rshift(value, 24), 255) == 5 then
    display_origin = bit.band(value, 0xffffff)
  end
  return true
end
grandleon_display_write_bps = {
  PCSX.addBreakpoint(0x1f801814, 'Write', 4, 'Read-only physical display origin', observe_display_write),
  PCSX.addBreakpoint(0xbf801814, 'Write', 4, 'Read-only uncached display origin', observe_display_write),
}
grandleon_presentation_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if frame < first_update or finished then return end
  -- Pinned emulator's documented monotonic 64-bit native CPU cycle counter.
  local cycle = assert(tonumber(PCSX.getCPUCycles()))
  assert(display_origin ~= nil and unknown_writes == 0, 'display origin observation incomplete')
  local shot = PCSX.GPU.takeScreenShot()
  assert(tonumber(shot.width) == 320 and tonumber(shot.height) == 240)
  local output = Support.File.open(string.format('/out/present-%05d.bin', index), 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  trace:write(string.format('%d,%.0f,%d,%d\n', index, cycle, frame, display_origin))
  index = index + 1
  if frame > last_update then
    finished = true
    trace:flush()
    print(string.format('MIPSYNC_PRESENTATION_CAPTURE frames=%d first_update=%d last_update=%d', index, first_update, frame))
  end
end)
local previous_quit = PCSX.quit
PCSX.quit = function(code)
  trace:close()
  if code == 0 then assert(finished and index > 1, 'native presentation trace incomplete') end
  previous_quit(code)
end
