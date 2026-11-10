-- Exact production disc, ordinary title/story/1P controller flow. The update
-- seam uses logical directions; no unit, camera, rule or clock state is edited.
-- Record every second GPU retrace at 30 fps, retaining repeats and pauses.
local ffi = require('ffi')
local function pointer(name, kind)
  local address = assert(tonumber(os.getenv(name)), name .. ' required')
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local campaign = pointer('GRANDLEON_CAMPAIGN_MODE_ADDRESS', 'uint8_t*')
local mode = pointer('GRANDLEON_INPUT_MODE_ADDRESS', 'uint8_t*')
local moment = pointer('GRANDLEON_MOMENT_ADDRESS', 'uint8_t*')
local cursor = pointer('GRANDLEON_CURSOR_ADDRESS', 'int16_t*')
local selected = pointer('GRANDLEON_SELECTED_ADDRESS', 'uint64_t*')
local remaining = pointer('GRANDLEON_RESULT_FRAMES_ADDRESS', 'uint32_t*')
local returning = pointer('GRANDLEON_CAMERA_RETURN_ADDRESS', 'uint32_t*')
local attacker = pointer('GRANDLEON_ATTACKER_ADDRESS', 'uint64_t*')
local target = pointer('GRANDLEON_TARGET_ADDRESS', 'uint64_t*')
local attacker_cell = pointer('GRANDLEON_ATTACKER_CELL_ADDRESS', 'int16_t*')
local target_cell = pointer('GRANDLEON_TARGET_CELL_ADDRESS', 'int16_t*')
local damage = pointer('GRANDLEON_DAMAGE_ADDRESS', 'int16_t*')
local counter_damage = pointer('GRANDLEON_COUNTER_DAMAGE_ADDRESS', 'int16_t*')
local counter = pointer('GRANDLEON_COUNTER_ADDRESS', 'uint8_t*')
local rowan = 7543913367842920562ULL
local raider = 7878946346193111832ULL
local frame, battle_start, vblanks, filmed = 0, nil, 0, 0
local recording, forecast_seen, combat_seen, return_seen = false, false, false, false
local first_vblank, last_vblank, forecast_index
local forecast_capture = false
-- Keep callback handles alive: Lua GC may otherwise release a live debugger
-- breakpoint/event listener during a long sequence of screenshot allocations.
grandleon_readme_update = PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS'))),
  'Exec', 4, 'Normal playable matched README engagement', function()
  frame = frame + 1
  local held = 0
  if campaign[0] ~= 5 then
    if frame >= 30 and frame % 20 == 10 then
      if campaign[0] == 2 then held = 0x0008 else held = 0x4000 end
    end
  elseif moment[24] ~= 0 then
    if frame % 20 == 10 then held = 0x4000 end
  else
    if not battle_start then
      assert(mode[0] == 0 and cursor[0] == 1 and cursor[1] == 2,
             'normal one-player Rowan must start at (1,2)')
      battle_start = frame
      print('MIPSYNC_README_BOARD start=1,2 actor=7543913367842920562 target=7878946346193111832')
    end
    local elapsed = frame - battle_start
    local actions = {[60]=0x4000,[76]=0x0010,[84]=0x0020,[92]=0x0020,
                     [100]=0x0020,[110]=0x4000,[240]=0x4000,[270]=0x4000}
    held = actions[elapsed] or 0
    if elapsed == 50 then recording = true end
    if elapsed == 61 then assert(selected[0] == rowan, 'the selected actor must be Rowan') end
    if elapsed == 109 then
      assert(cursor[0] == 4 and cursor[1] == 1, 'ordinary movement destination must be (4,1)')
    end
    if elapsed == 239 then assert(mode[0] == 2, 'walk must finish before choosing Attack') end
    if elapsed == 260 then
      assert(mode[0] == 3 and selected[0] == rowan and cursor[0] == 5 and cursor[1] == 1,
             'the normal legal target must be focused before attack commitment')
      forecast_capture = true
    end
    if elapsed == 269 then assert(forecast_seen, 'the legal target forecast must be captured before commitment') end
    if elapsed == 271 then
      print(string.format('MIPSYNC_README_RESULT remaining=%d damage=%d counter=%d counter_damage=%d',
                          remaining[0], damage[0], counter[0], counter_damage[0]))
      assert(attacker[0] == rowan and target[0] == raider, 'combat actors must match the cross-platform engagement')
      assert(attacker_cell[0] == 4 and attacker_cell[1] == 1 and target_cell[0] == 5 and target_cell[1] == 1,
             'combat must begin at the real tactical cells')
      assert(remaining[0] > 0 and counter[0] == 1 and damage[0] == 6 and counter_damage[0] == 1,
             'the real attack/counter result must match its forecast')
      combat_seen = true
      print('MIPSYNC_README_COMBAT attacker=4,1 defender=5,1 damage=6 counter_damage=1')
    end
    if elapsed == 391 then
      assert(remaining[0] == 0 and returning[0] == 0 and mode[0] == 0,
             'the normal close view must return to the tactical board')
      return_seen = true
      print('MIPSYNC_README_RETURN elapsed=391')
    end
    if elapsed == 450 then
      assert(forecast_seen and combat_seen and return_seen and filmed >= 190, 'incomplete matched capture')
      recording = false
      print(string.format('MIPSYNC_README_CAPTURE_PASS frames=%d fps=30 first_vblank=%d last_vblank=%d route=1,2:4,1 target=5,1',
                          filmed, first_vblank, last_vblank))
      print('MIPSYNC_README_FORECAST_FRAME index=' .. forecast_index)
      PCSX.quit(0)
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  assert(frame < 2000, 'normal title/story/battle capture did not complete')
  return true
end)
grandleon_readme_vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
  vblanks = vblanks + 1
  if not recording or vblanks % 2 ~= 0 then return end
  local shot = PCSX.GPU.takeScreenShot()
  assert(tonumber(shot.width) == 320 and tonumber(shot.height) == 240, 'unexpected production framebuffer')
  local file = Support.File.open(string.format('/out/frame-%04d.bin', filmed), 'TRUNCATE')
  file:writeMoveSlice(shot.data)
  file:close()
  if forecast_capture then
    forecast_capture = false
    forecast_seen = true
    forecast_index = filmed
    print('MIPSYNC_README_FORECAST_CAPTURE target=5,1 expected_hit=100 expected_damage=6 expected_counter=1')
  end
  filmed = filmed + 1
  first_vblank = first_vblank or vblanks
  last_vblank = vblanks
end)
