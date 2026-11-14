-- Exercise the production campaign UI; observe its existing C output rather
-- than assuming that a story line always occupies one screenful of text.
local ffi = require('ffi')
local bit = require('bit')
dofile('/out/campaign-ui-abi.lua')
local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local ui_address = assert(tonumber(os.getenv('GRANDLEON_UI_ADDRESS')),
                          'GRANDLEON_UI_ADDRESS is required')
local mode = os.getenv('GRANDLEON_CAMPAIGN_MODE') or 'new'
assert(mode == 'new' or mode == 'continue' or mode == 'solo', 'unknown campaign mode')
local UI = {browse=0, move=1, deployment=6, moment=13, title=14, players=15,
            story=16, objective=17, aftermath=18, save_error=19, name=22}
local frame, next_input, state_since = 0, 30, 0
local observed, current_stage = nil, nil
local captured, returns = {}, {}
local pending_capture, pending_quit = nil, nil
local ui_output, ui_return = nil, nil
local p2_phase, p2_since = 0, 0
local final_stage = mode == 'new' and 'deepwood' or 'gate'

local function pointer(address, kind)
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local function text(address)
  if address < 0x80000000 or address >= 0x80200000 then return '' end
  return ffi.string(pointer(address, 'const char*'),
                    math.min(192, 0x80200000 - address)):match('^[^%z]*')
end
local function stage_for(headline)
  local lower = headline:lower()
  if lower:find('orchard', 1, true) then return 'bell' end
  if lower:find('deepwood', 1, true) then return 'deepwood' end
  if lower:find('watch', 1, true) then return 'watch' end
  if lower:find('village', 1, true) then return 'village' end
  if lower:find('keep', 1, true) or lower:find('twin gate', 1, true) then return 'gate' end
  return nil
end
local function fail(reason)
  if not pending_quit then
    print('MIPSYNC_CAMPAIGN_PROBE_FAIL ' .. reason)
    pending_quit = 1
  end
end
local function capture(name)
  if captured[name] or pending_capture then return end
  -- Let the UI's completed draw reach the front buffer before readback.
  pending_capture = {name=name, after=frame + 4}
end

-- The UI is called normally by ps1_ui_render. Read the populated output only
-- when that real call returns; never invoke it or write any game state.
grandleon_campaign_ui = PCSX.addBreakpoint(
    ui_address, 'Exec', 4, 'Observe campaign UI call', function()
  local registers = PCSX.getRegisters().GPR.n
  ui_output, ui_return = tonumber(registers.a0), tonumber(registers.ra)
  assert(ui_output >= 0x80000000 and ui_output < 0x80200000 - ffi.sizeof('grandleon_ui_state'),
         'UI output must be inside main RAM')
  if not returns[ui_return] then
    local address = ui_return
    returns[address] = PCSX.addBreakpoint(address, 'Exec', 4,
        'Observe completed campaign UI', function()
      if ui_return == address then
        local output = pointer(ui_output, 'const grandleon_ui_state*')[0]
        local previous = observed and observed.mode or -1
        observed = {mode=tonumber(output.mode), menu=tonumber(output.campaign_menu_index),
                    settled=output.camera_settled ~= 0, headline=text(tonumber(output.headline))}
        if observed.mode ~= previous then state_since = frame end
        if observed.mode == UI.objective then
          local stage = stage_for(observed.headline)
          if not stage then fail('unrecognized objective: ' .. observed.headline)
          elseif current_stage ~= stage then
            current_stage = stage
            print('MIPSYNC_CAMPAIGN_OBSERVED stage=' .. stage .. ' headline=' .. observed.headline)
          end
        end
        if observed.mode ~= previous then
          print(string.format('MIPSYNC_CAMPAIGN_UI frame=%d mode=%d stage=%s',
                              frame, observed.mode, current_stage or 'title'))
        end
        ui_return = nil
      end
      return true
    end)
  end
  return true
end)
grandleon_campaign_returns = returns

grandleon_campaign_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon campaign update', function()
  frame = frame + 1
  local held, player_two = 0, 0
  if observed and not pending_quit then
    local ui = observed.mode
    if ui == UI.save_error then fail('production UI reported a memory-card/save error') end
    if ui == UI.title then capture('title') end
    if ui == UI.moment and mode == 'new' and current_stage == 'bell' and
       frame - state_since >= 6 then capture('bell-dialogue') end
    if ui == UI.browse and current_stage and observed.settled and
       frame - state_since >= 12 then
      capture((mode == 'solo' and 'solo-' or '') .. current_stage)
    end
    if frame >= next_input and not pending_capture then
      if ui == UI.title then
        local row = mode == 'continue' and 1 or 0
        held = observed.menu == row and 0x4000 or 0x0040
      elseif ui == UI.players then
        local row = mode == 'solo' and 0 or 1
        held = observed.menu == row and 0x4000 or 0x0040
      elseif ui == UI.name then
        held = 0x1000 -- Keep the authored default name through the normal editor.
      elseif ui == UI.story or ui == UI.objective or ui == UI.aftermath or ui == UI.moment then
        held = 0x4000 -- Advance exactly one real page/line; poll UI again afterwards.
      elseif ui == UI.deployment then
        held = 0x0008 -- Normal Begin battle, only if deployment is actually present.
      elseif current_stage and (ui == UI.browse or ui == UI.move) then
        local name = (mode == 'solo' and 'solo-' or '') .. current_stage
        if captured[name] then
          if mode == 'new' and current_stage == 'bell' and p2_phase < 2 then
            if p2_phase == 0 then
              player_two, p2_phase, p2_since = 0x4000, 1, frame
            elseif ui == UI.move then
              player_two, p2_phase = 0x2000, 2
            elseif frame - p2_since > 40 then fail('controller two did not select its army') end
          elseif current_stage == final_stage then
            if frame - state_since >= 900 then
              print('MIPSYNC_CAMPAIGN_PROBE_DONE mode=' .. mode .. ' stage=' .. current_stage)
              pending_quit = 0
            end
          elseif ui == UI.browse and frame - state_since >= 60 then
            held = 0x8000 -- Existing supported debug completion, from the live board only.
          end
        end
      end
      if held ~= 0 or player_two ~= 0 then next_input = frame + 10 end
    end
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = player_two
  if held ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT frame=%d held=%04x', frame, held))
  end
  if player_two ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT player=2 frame=%d held=%04x', frame, player_two))
  end
  if frame > 6000 then fail('campaign traversal did not reach its final live stage') end
  return true
end)

grandleon_campaign_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if pending_capture and frame >= pending_capture.after then
    local name = pending_capture.name
    pending_capture = nil
    local shot = PCSX.GPU.takeScreenShot()
    local bytes = shot.data.size
    assert(bytes == 320 * 240 * 2, 'campaign capture must contain a complete framebuffer')
    local output = Support.File.open('/out/' .. mode .. '-' .. name .. '.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    captured[name] = true
    print(string.format('MIPSYNC_CAMPAIGN_CAPTURE name=%s %dx%d bytes=%d',
                        name, tonumber(shot.width), tonumber(shot.height), bytes))
  end
  if pending_quit ~= nil then PCSX.quit(pending_quit) end
end)

print(string.format('MIPSYNC_CAMPAIGN_PROBE_READY mode=%s update=%08x ui=%08x',
                    mode, update_address, ui_address))
