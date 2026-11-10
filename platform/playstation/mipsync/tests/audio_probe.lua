-- Exercise Music and Effects through the actual title and battle menus.
local address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local mode = os.getenv('GRANDLEON_AUDIO_MODE') or 'write'
local frame, shot_name = 0, nil
local actions = {}
local ffi = require('ffi')
local function symbol(name)
  local address = tonumber(os.getenv(name))
  assert(address, name .. ' is required (resolve against the tested ELF)')
  return tonumber(ffi.cast('uint32_t*', PCSX.getMemPtr() + bit.band(address, 0x1fffff))[0])
end
if mode == 'write' then
  actions = {
    [30]=0x0040, [50]=0x0040, [70]=0x4000,
    [90]=0x0040, [110]=0x0040,
    [150]=0x0080, [170]=0x0080, [190]=0x0040, [210]=0x0020,
    [250]=0x2000, [270]=0x4000, [290]=0x4000,
    [320]=0x4000, [350]=0x4000, [380]=0x4000,
    [410]=0x4000, [440]=0x4000,
    -- Finish the opening conversation, then return to board navigation.
    [460]=0x4000, [480]=0x2000,
    [510]=0x0008, [530]=0x0040, [550]=0x0040, [570]=0x4000,
    [590]=0x0040, [610]=0x0040,
    [650]=0x0020, [670]=0x0040, [690]=0x0080,
  }
end
grandleon_audio_probe = PCSX.addBreakpoint(address, 'Exec', 4, 'Audio menus', function()
  frame = frame + 1
  if frame == 10 then
    assert(symbol('GRANDLEON_CD_READY_ADDRESS') == 1, 'CD audio driver failed to initialize')
    assert(symbol('GRANDLEON_TRACK_COUNT_ADDRESS') == 7, 'mixed-mode disc is missing music tracks')
    assert(symbol('GRANDLEON_PLAYING_ADDRESS') == 0, 'title CD audio playback was not acknowledged')
    print('MIPSYNC_AUDIO_TRANSPORT_READY tracks=7 theme=0')
  end
  PCSX.getRegisters().GPR.n.a0 = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if mode == 'write' and frame == 230 then shot_name = 'audio-title' end
  if mode == 'write' and frame == 720 then shot_name = 'audio-battle' end
  if frame == (mode == 'write' and 750 or 70) then
    print('MIPSYNC_AUDIO_PROBE_DONE mode=' .. mode)
    PCSX.quit(0)
  end
  return true
end)
grandleon_audio_capture = PCSX.Events.createEventListener('GPU::Vsync', function()
  if not shot_name then return end
  local shot = PCSX.GPU.takeScreenShot()
  local out = Support.File.open('/out/' .. shot_name .. '.bin', 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  print('MIPSYNC_AUDIO_CAPTURE ' .. shot_name)
  shot_name = nil
end)
