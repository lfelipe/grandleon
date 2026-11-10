-- Feed controller packets at the hardware input buffer, without replacing
-- the game's computed button edges or calling its settings functions.
local ffi = require('ffi')
local raw_address = assert(tonumber(os.getenv('GRANDLEON_PAD_BUFFER_ADDRESS')))
local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local packet = PCSX.getMemPtr() + bit.band(raw_address, 0x1fffff)
local frame = 0
local taps = {[30]=0x0040,[50]=0x0040,[70]=0x4000,
              [90]=0x0040,[110]=0x0040,[170]=0x0040}

grandleon_raw_audio_input = PCSX.addBreakpoint(raw_address, 'Read', 1, 'Virtual controller packet', function()
  frame = frame + 1
  local held = taps[frame] or 0
  if (frame >= 150 and frame <= 157) or (frame >= 190 and frame <= 197) then held = 0x0020 end
  local buttons = bit.band(bit.bnot(held), 0xffff)
  packet[0] = 0 -- connected
  packet[1] = 0x41 -- digital pad, two-byte button state
  packet[2] = bit.band(buttons, 255)
  packet[3] = bit.rshift(buttons, 8)
  return true
end)

grandleon_raw_audio_update = PCSX.addBreakpoint(update_address, 'Exec', 4, 'Raw controller audio check', function()
  if frame >= 240 then
    print('MIPSYNC_RAW_AUDIO_INPUT_DONE frames=' .. frame)
    PCSX.quit(0)
  end
  return true
end)
