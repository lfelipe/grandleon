-- Direct current Twin Gate co-op fixture with its authored pt-BR catalog first.
-- The 103-glyph opening line must retain its speaker until its second page.
-- This observes state and supplies ordinary Confirm input; it never edits RAM.
local ffi = require('ffi')
local update = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local address = assert(tonumber(os.getenv('GRANDLEON_MOMENT_ADDRESS')))
local state = PCSX.getMemPtr() + bit.band(address, 0x1fffff)
local words = ffi.cast('uint32_t*', state)
local frame = 0
local first_offset
local capture
PCSX.addBreakpoint(update, 'Exec', 4, 'Authored translated dialogue pagination', function()
  frame = frame + 1
  local held = (frame == 30 or frame == 50) and 0x4000 or 0
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  if frame == 20 then
    assert(state[24] == 1 and words[4] == 0 and words[5] == 0, 'expected first authored speaker/page')
    capture = 'dialogue-page-one'
  end
  if frame == 31 then
    assert(words[4] == 0 and words[5] > 0 and state[24] == 1,
           'Confirm must show the remaining text before changing speaker')
    first_offset = tonumber(words[5])
  end
  if frame == 40 then capture = 'dialogue-page-two' end
  if frame == 51 then
    assert(words[4] == 1 and words[5] == 0 and state[24] == 1,
           'only Confirm on the final text page may advance the speaker')
    print('MIPSYNC_DIALOGUE_PAGING_PASS same_speaker_offset=' .. first_offset .. ' next_line=1')
  end
  if frame == 70 then PCSX.quit(0) end
  return true
end)
PCSX.Events.createEventListener('GPU::Vsync', function()
  if not capture then return end
  local shot = PCSX.GPU.takeScreenShot()
  local output = Support.File.open('/out/' .. capture .. '.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  capture = nil
end)
