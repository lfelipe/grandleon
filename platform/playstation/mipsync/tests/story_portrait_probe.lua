-- Drive the exact production title/settings/new-game route. Only ordinary
-- controller inputs are injected; all portrait and story observations are reads.
local ffi = require('ffi')
local function ptr(name, kind)
  local address = assert(tonumber(os.getenv(name)), name .. ' required')
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local mode = ptr('GRANDLEON_CAMPAIGN_MODE_ADDRESS', 'uint8_t*')
local menu = ptr('GRANDLEON_CAMPAIGN_MENU_ADDRESS', 'uint32_t*')
local language = ptr('GRANDLEON_LANGUAGE_INDEX_ADDRESS', 'uint32_t*')
local line = ptr('GRANDLEON_STORY_LINE_ADDRESS', 'uint32_t*')
local page = ptr('GRANDLEON_STORY_PAGE_ADDRESS', 'uint32_t*')
local ids = ptr('GRANDLEON_SPEAKER_IDS_ADDRESS', 'uint64_t*')
local textures = ptr('GRANDLEON_SPEAKER_TEXTURES_ADDRESS', 'uint32_t*')
local count = ptr('GRANDLEON_SPEAKER_COUNT_ADDRESS', 'uint32_t*')
local desired = assert(tonumber(os.getenv('GRANDLEON_STORY_LANGUAGE_INDEX')))
local cast = {ffi.new('uint64_t', 9937314275672741875ULL),
              ffi.new('uint64_t', 12773256886657188408ULL),
              ffi.new('uint64_t', 9937314275672741875ULL)}
local frame, phase, settings_rows, expected_line, line_ticks = 0, 0, 0, 0, 0
local observed, capture = {}, nil
local player_ticks = 0
local function expected_texture(index)
  for binding = 0, tonumber(count[0]) - 1 do
    if ids[binding] == cast[index + 1] then return tonumber(textures[binding]) end
  end
  error('authored story actor has no portrait registry entry')
end
grandleon_story_texture = PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_TEXTURE_ENSURE_ADDRESS'))),
  'Exec', 4, 'Current story portrait upload', function()
  if mode[0] == 3 and line[0] < 3 then
    observed[tonumber(line[0])] = tonumber(PCSX.getRegisters().GPR.n.a0)
  end
  return true
end)
grandleon_story_update = PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS'))),
  'Exec', 4, 'Complete production story and correct actor portraits', function()
  frame = frame + 1
  local held = 0
  if mode[0] == 1 then
    player_ticks = player_ticks + 1
    if player_ticks == 8 then capture = 'players-ptbr' end
  end
  if mode[0] == 3 then
    assert(line[0] == expected_line, 'story skipped or reordered an authored speaker')
    line_ticks = line_ticks + 1
    if line_ticks == 20 then
      assert(page[0] == 0, 'short authored story line must begin on page one')
      assert(observed[expected_line] == expected_texture(expected_line),
             'story portrait must follow the current authored cast, never the first board unit')
      capture = 'story-' .. expected_line
      print('MIPSYNC_STORY_PORTRAIT actor=' .. expected_line .. ' texture=' .. observed[expected_line])
    end
    if line_ticks == 30 then
      held = 0x4000
      expected_line = expected_line + 1
      line_ticks = 0
    end
  elseif mode[0] == 4 then
    assert(expected_line == 3, 'node advanced before all three story speakers were read')
    print('MIPSYNC_PRODUCTION_STORY_PASS lines=3 portraits=3 language_index=' .. language[0])
    PCSX.quit(0)
  elseif frame >= 30 and frame % 15 == 0 then
    if phase == 0 then held = 0x0010; phase = 1
    elseif phase == 1 then
      assert(mode[0] == 0 and menu[0] == 2, 'title Up must wrap backwards to Settings')
      held = 0x4000; phase = 2
    elseif phase == 2 then
      assert(mode[0] == 9, 'Settings must be reachable from the normal title')
      if settings_rows < 4 then held = 0x0040; settings_rows = settings_rows + 1
      elseif language[0] ~= desired then held = 0x4000
      else held = 0x2000; phase = 3 end
    elseif phase == 3 then
      assert(mode[0] == 0 and menu[0] == 0, 'Back must return to New Game')
      capture = 'title-ptbr'; phase = 4
    elseif phase == 4 then held = 0x4000; phase = 5
    elseif mode[0] == 1 then held = 0x4000
    elseif mode[0] == 2 then held = 0x0008
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  assert(frame < 1000, 'production story did not finish within normal menu flow')
  return true
end)
grandleon_story_vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
  if not capture then return end
  local shot = PCSX.GPU.takeScreenShot()
  local output = Support.File.open('/out/' .. capture .. '.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data)
  output:close()
  capture = nil
end)
