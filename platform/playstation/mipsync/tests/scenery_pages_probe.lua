-- Traverse the real Cedar Watch board and observe the renderer's draw calls.
-- Never change entity flags, transforms, camera positions or simulation state.
local ffi, bit = require('ffi'), require('bit')
dofile('/out/campaign-ui-abi.lua')
local function env(name) return assert(tonumber(os.getenv(name)), name .. ' required') end
local base, stride = env('GRANDLEON_ENTITY_BASE'), env('GRANDLEON_ENTITY_STRIDE')
local count, foliage_count = env('GRANDLEON_ENTITY_COUNT'), env('GRANDLEON_FOLIAGE_COUNT')
local function pointer(address, kind)
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local indices = pointer(env('GRANDLEON_FOLIAGE_ADDRESS'), 'const uint32_t*')
local expected, seen, returns = {}, {}, {}
local initialized = false
local function initialize_expected()
  if initialized then return end
  local total, last = 0, 0
  for i=0,foliage_count-1 do
    local index = tonumber(indices[i])
    if index < count and not expected[index] then
      expected[index], total, last = true, total + 1, math.max(last, index)
    end
  end
  assert(total == 9 and last >= 64, 'fixture must exercise late canopy entries')
  initialized = true
end
local points = {
  {name='south-west', down=16, left=16}, {name='south-middle', right=8},
  {name='south-east', right=16}, {name='middle-east', up=6},
  {name='middle-middle', left=7}, {name='middle-west', left=16},
  {name='north-west', up=16}, {name='north-middle', right=8},
  {name='north-east', right=16},
}
local frame, next_input, task, action, last_input = 0, 20, 1, 1, 0
local observed, pending, ui_output, ui_return
local failed = false
local actions = nil
local queued, mapped_this_tick = 0, 0
-- Inject raw pad input before the production screen-direction mapping, not
-- after it. The second local controller remains idle.
scenery_pad = PCSX.addBreakpoint(env('GRANDLEON_MAP_ADDRESS'), 'Exec', 4,
  'Raw controller input', function()
    PCSX.getRegisters().GPR.n.a0 = mapped_this_tick == 0 and queued or 0
    mapped_this_tick = mapped_this_tick + 1
    return true
end)
local function fail(reason)
  failed = true
  print('MIPSYNC_SCENERY_FAIL ' .. reason)
  PCSX.quit(0) -- The runner turns the failure marker into a nonzero test exit.
end
scenery_draw = PCSX.addBreakpoint(env('GRANDLEON_DRAW_ADDRESS'), 'Exec', 4,
  'Observe real entity submission', function()
    initialize_expected()
    local address = tonumber(PCSX.getRegisters().GPR.n.a3)
    local offset = address - base
    if offset >= 0 and offset < count * stride and offset % stride == 0 then
      local index = offset / stride
      if expected[index] then seen[index] = true end
    end
    return true
end)
scenery_ui = PCSX.addBreakpoint(env('GRANDLEON_UI_ADDRESS'), 'Exec', 4,
  'Observe real UI state', function()
    local regs = PCSX.getRegisters().GPR.n
    ui_output, ui_return = tonumber(regs.a0), tonumber(regs.ra)
    if not returns[ui_return] then
      local address = ui_return
      returns[address] = PCSX.addBreakpoint(address, 'Exec', 4,
        'Observe completed UI state', function()
          if ui_return == address then
            local state = pointer(ui_output, 'const grandleon_ui_state*')[0]
            observed = {mode=tonumber(state.mode), settled=state.camera_settled ~= 0}
          end
          return true
      end)
    end
    return true
end)
scenery_update = PCSX.addBreakpoint(env('GRANDLEON_UPDATE_ADDRESS'), 'Exec', 4,
  'Controller camera traversal', function()
    frame = frame + 1
    if frame > 3500 then fail('controller traversal timed out') end
    local pressed = 0
    if observed and not pending and frame >= next_input then
      if observed.mode == 13 or observed.mode == 16 or observed.mode == 17 then
        pressed, next_input = 0x4000, frame + 12
      elseif observed.mode == 0 and task <= #points then
        if not actions then
          actions, action = {}, 1
          for _, direction in ipairs({{'down',0x0010},{'left',0x0080},{'right',0x0020},{'up',0x0040}}) do
            for _=1,points[task][direction[1]] or 0 do actions[#actions+1] = direction[2] end
          end
        end
        if action <= #actions then
          pressed, action, next_input, last_input = actions[action], action + 1, frame + 4, frame
        elseif observed.settled and frame - last_input >= 20 then
          pending = {name=points[task].name, after=frame + 4}
        end
      end
    end
    queued, mapped_this_tick = pressed, 0
    return true
end)
scenery_vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
  if failed or not pending or frame < pending.after then return end
  local shot = PCSX.GPU.takeScreenShot()
  local output = Support.File.open('/out/' .. pending.name .. '.bin', 'TRUNCATE')
  output:writeMoveSlice(shot.data); output:close()
  print('MIPSYNC_SCENERY_CAPTURE ' .. pending.name)
  pending, actions, task = nil, nil, task + 1
  if task > #points then
    for index in pairs(expected) do
      if not seen[index] then fail('canopy entity not submitted: ' .. index); return end
      print('MIPSYNC_SCENERY_DRAWN entity=' .. index)
    end
    print('MIPSYNC_SCENERY_PASS pages=9')
    PCSX.quit(0)
  end
end)
