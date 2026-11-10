-- Exercise actual digital and analog packets, leaving the game's edge detection,
-- physical-to-board mapping and runtime update arguments intact. Direct Deepwood
-- fixture only: dismiss the four authored opening lines through Confirm.
local ffi = require('ffi')
local raw = assert(tonumber(os.getenv('GRANDLEON_PAD_BUFFER_ADDRESS')))
local update = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local cursor_address = assert(tonumber(os.getenv('GRANDLEON_CURSOR_ADDRESS')))
local packet = PCSX.getMemPtr() + bit.band(raw, 0x1fffff)
local cursor = ffi.cast('int16_t*', PCSX.getMemPtr() + bit.band(cursor_address, 0x1fffff))
local frame, initial_x, initial_y = 0, nil, nil
local taps = {[20]=0x4000,[35]=0x4000,[50]=0x4000,[65]=0x4000,
              [100]=0x0020,[120]=0x0010}
local checks = {
  [105]={1,0,'digital right'}, [125]={1,1,'digital up'},
  [145]={1,0,'analog down'}, [165]={0,0,'analog left'},
  [185]={0,1,'analog up'}, [205]={1,1,'analog right'},
}
PCSX.addBreakpoint(raw, 'Read', 1, 'Physical controller packets', function()
  frame = frame + 1
  local held = taps[frame] or 0
  local buttons = bit.band(bit.bnot(held), 0xffff)
  packet[0] = 0
  packet[1] = frame >= 130 and 0x73 or 0x41
  packet[2] = bit.band(buttons, 255)
  packet[3] = bit.rshift(buttons, 8)
  packet[4], packet[5], packet[6], packet[7] = 128, 128, 128, 128
  if frame >= 140 and frame <= 143 then packet[7] = 255 end
  if frame >= 160 and frame <= 163 then packet[6] = 0 end
  if frame >= 180 and frame <= 183 then packet[7] = 0 end
  if frame >= 200 and frame <= 203 then packet[6] = 255 end
  return true
end)
PCSX.addBreakpoint(update, 'Exec', 4, 'Physical controller assertions', function()
  if frame == 90 then initial_x, initial_y = tonumber(cursor[0]), tonumber(cursor[1]) end
  local check = checks[frame]
  if check then
    assert(initial_x and initial_y, 'missing initial board cursor')
    assert(cursor[0] == initial_x + check[1] and cursor[1] == initial_y + check[2],
           check[3] .. ' must move one projected-board cell, including held-stick edge handling')
    print('MIPSYNC_PHYSICAL_CONTROL_PASS ' .. check[3])
  end
  if frame >= 220 then
    print('MIPSYNC_PHYSICAL_CONTROLLER_PASS digital=2 analog=4')
    PCSX.quit(0)
  end
  return true
end)
