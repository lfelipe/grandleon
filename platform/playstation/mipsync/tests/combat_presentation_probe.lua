-- Read-only presentation assertions around the normal Bell Orchard encounter.
-- Never changes rules, health, controller arguments or scene state.
local ffi = require('ffi')
local controlled = os.getenv('GRANDLEON_CONTROLLED_LETHAL_FIXTURE') == '1'
local updates = 0
local function pointer(variable, ctype)
  local address = assert(tonumber(os.getenv(variable)), variable .. ' required')
  return ffi.cast(ctype, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local remaining = pointer('GRANDLEON_RESULT_FRAMES_ADDRESS', 'uint32_t*')
local duration = pointer('GRANDLEON_RESULT_DURATION_ADDRESS', 'uint32_t*')
local attacker = pointer('GRANDLEON_ATTACKER_ADDRESS', 'uint64_t*')
local target = pointer('GRANDLEON_TARGET_ADDRESS', 'uint64_t*')
local close = pointer('GRANDLEON_CLOSE_ADDRESS', 'uint8_t*')
local onboard = pointer('GRANDLEON_ON_BOARD_ADDRESS', 'uint8_t*')
local ids = pointer('GRANDLEON_IDS_ADDRESS', 'uint64_t*')
local bindings = pointer('GRANDLEON_BINDINGS_ADDRESS', 'uint32_t*')
local poses = pointer('GRANDLEON_POSES_ADDRESS', 'uint32_t*')
local scene = pointer('GRANDLEON_SCENE_ADDRESS', 'uint32_t*')
local unit_count = pointer('GRANDLEON_UNIT_COUNT_ADDRESS', 'uint32_t*')
local count, stride, entities
local array_bytes = assert(tonumber(os.getenv('GRANDLEON_ENTITY_ARRAY_BYTES')))

local enemy_exchanges, fatal_samples, side_samples, restores = 0, 0, 0, 0
local was_close = false
local previous_a, previous_b
local attacker_cell = pointer('GRANDLEON_ATTACKER_CELL_ADDRESS', 'int16_t*')
local target_cell = pointer('GRANDLEON_TARGET_CELL_ADDRESS', 'int16_t*')
local camera_return = pointer('GRANDLEON_CAMERA_RETURN_ADDRESS', 'uint32_t*')
local function binding(id)
  for index = 0, count - 1 do if ids[index] == id then return index end end
  error('combat actor has no scene binding')
end
local function entity(index)
  return entities + tonumber(bindings[index]) * stride
end
PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS'))),
  'Exec', 4, 'Combat presentation invariants', function()
  if not stride then
    count = tonumber(unit_count[0])
    stride = array_bytes / tonumber(scene[1])
    assert(stride % 4 == 0, 'invalid scene entity stride')
    entities = PCSX.getMemPtr() + bit.band(tonumber(scene[0]), 0x1fffff)
  end
  updates = updates + 1
  if controlled and updates == 420 then PCSX.quit(0) end
  local active = remaining[0] ~= 0 and close[0] ~= 0
  if active then
    local elapsed = tonumber(duration[0] - remaining[0])
    local a, b = binding(attacker[0]), binding(target[0])
    previous_a, previous_b = a, b
    if elapsed == 10 then
      -- The authored Bell fixture's first four bindings belong to players.
      if a >= 4 then enemy_exchanges = enemy_exchanges + 1 end
    end
    if elapsed >= 10 and elapsed <= 110 then
      local ax = ffi.cast('int32_t*', entity(a) + 12)[0]
      local bx = ffi.cast('int32_t*', entity(b) + 12)[0]
      assert(ax < bx, 'counterattack must preserve actor screen sides')
      side_samples = side_samples + 1
      for _, index in ipairs({a,b}) do
        if onboard[index] == 0 and elapsed >= 100 then
          local actor = entity(index)
          assert(actor[72] == 1, 'dying actor disappeared before cinematic ended')
          assert(actor[63] == poses[index * 9 + 8], 'dying actor must retain hurt art')
          fatal_samples = fatal_samples + 1
        end
      end
    end
  elseif was_close then
    -- The restored tactical state must appear atomically; a separate camera
    -- glide with actors still on the close-view stage causes diagonal drift.
    assert(camera_return[0] == 0, 'camera must not glide while actors remain staged')
    for _, pair in ipairs({{previous_a,attacker_cell},{previous_b,target_cell}}) do
      local index, cell = pair[1], pair[2]
      local actor = entity(index)
      if onboard[index] ~= 0 then
        local position = ffi.cast('int32_t*', actor + 12)
        assert(position[0] == (cell[0] * 2 + 1 - 8) * 65536 and
               position[2] == (6 - cell[1] * 2 - 1) * 65536,
               'close actors must return directly to their tactical cells')
      else
        assert(actor[72] == 0, 'dying artwork must leave after cinematic completes')
      end
    end
    restores = restores + 1
  end
  was_close = active
  return true
end)
local quit = PCSX.quit
PCSX.quit = function(code)
  if not controlled then assert(enemy_exchanges >= 1, 'no enemy-initiated close view was exercised') end
  assert(side_samples >= 100, 'attack/counter screen-side coverage is incomplete')
  assert(restores >= (controlled and 1 or 2), 'close-view tactical returns were not exercised')
  if controlled then assert(fatal_samples >= 1, 'controlled lethal fixture did not exercise dying artwork') end
  print(string.format('MIPSYNC_COMBAT_PRESENTATION_PASS enemy=%d side_samples=%d restores=%d fatal_samples=%d',
    enemy_exchanges, side_samples, restores, fatal_samples))
  quit(code)
end
if controlled then dofile('/probe/combat_probe.lua') else dofile('/probe/combat_audio_probe.lua') end
