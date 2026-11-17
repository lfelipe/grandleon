-- Exercise Music and Effects through the actual title and battle menus.
local address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local mode = os.getenv('GRANDLEON_AUDIO_MODE') or 'write'
local frame, shot_name = 0, nil
local actions = {}
local ffi = require('ffi')
local ui_mode, battle_start
local ui_returns = {}
local function observe_ui()
  local result_address, return_address
  return PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_UI_ADDRESS'))),
    'Exec', 4, 'Observe audio menu screen', function()
      local registers = PCSX.getRegisters().GPR.n
      result_address, return_address = tonumber(registers.a0), tonumber(registers.ra)
      if not ui_returns[return_address] then
        local address = return_address
        ui_returns[address] = PCSX.addBreakpoint(address, 'Exec', 4,
          'Read completed audio menu screen', function()
            if return_address == address then
              ui_mode = ffi.cast('uint8_t*', PCSX.getMemPtr() + bit.band(result_address, 0x1fffff))[0]
              return_address = nil
            end
            return true
          end)
      end
      return true
    end)
end
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
    [250]=0x2000,
  }
  grandleon_audio_ui = observe_ui()
end
local battle_actions = {
  [0]=0x0008, [20]=0x0010, [40]=0x4000, -- Pause, wrap to Settings.
  [60]=0x0040, [80]=0x0040, [100]=0x0020, -- Raise music.
  [120]=0x0040, [140]=0x0080, -- Lower effects.
}
local function write_input()
  if frame <= 250 then return actions[frame] or 0 end
  -- Confirm each actual story and dialogue page. The number of pages changes
  -- with the text and language, so wait for browse before opening pause.
  if not battle_start and ui_mode == 0 then battle_start = frame end
  if battle_start then return battle_actions[frame - battle_start] or 0 end
  if frame % 20 == 10 and
     (ui_mode == 14 or ui_mode == 15 or ui_mode == 16 or ui_mode == 17 or
      ui_mode == 13 or ui_mode == 23) then return 0x4000 end
  return 0
end
grandleon_audio_probe = PCSX.addBreakpoint(address, 'Exec', 4, 'Audio menus', function()
  frame = frame + 1
  if frame == 10 then
    assert(symbol('GRANDLEON_CD_READY_ADDRESS') == 1, 'CD audio driver failed to initialize')
    assert(symbol('GRANDLEON_TRACK_COUNT_ADDRESS') == 7, 'mixed-mode disc is missing music tracks')
    assert(symbol('GRANDLEON_PLAYING_ADDRESS') == 0, 'title CD audio playback was not acknowledged')
    print('MIPSYNC_AUDIO_TRANSPORT_READY tracks=7 theme=0')
  end
  PCSX.getRegisters().GPR.n.a0 = mode == 'write' and write_input() or 0
  PCSX.getRegisters().GPR.n.a1 = 0
  if mode == 'write' and frame == 230 then shot_name = 'audio-title' end
  if battle_start and frame == battle_start + 160 then shot_name = 'audio-battle' end
  if mode == 'write' and not battle_start and frame == 1200 then
    print('MIPSYNC_AUDIO_PROBE_FAIL opening story did not reach the board')
    PCSX.quit(1)
  end
  if (battle_start and frame == battle_start + 190) or (mode == 'read' and frame == 70) then
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
