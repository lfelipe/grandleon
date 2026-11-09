local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_CAMPAIGN_MODE') or 'new'
local frame = 0
local pending_capture = nil
local actions = {}
local player_two_actions = {}
local captures = {}

if mode == 'new' then
  actions[30] = 0x4000 -- New Game
  actions[50] = 0x0040 -- 2 Players
  actions[70] = 0x4000
  actions[100] = 0x4000 -- prologue
  actions[130] = 0x4000 -- play Bell Orchard
  player_two_actions[150] = 0x4000 -- player two selects their authored unit
  player_two_actions[160] = 0x2000 -- and can cancel independently
  actions[200] = 0x8000 -- complete Bell from the live board
  actions[230] = 0x4000 -- story, persist and LoadExec Deepwood
  actions[290] = 0x4000 -- play Deepwood
  captures[20] = 'title'
  captures[180] = 'bell'
  captures[340] = 'deepwood'
elseif mode == 'continue' then
  actions[30] = 0x0040 -- Continue row
  actions[50] = 0x4000 -- LoadExec saved Deepwood stage
  actions[110] = 0x4000 -- play Deepwood
  actions[170] = 0x8000 -- complete Deepwood from the live board
  actions[200] = 0x4000 -- story, LoadExec Cedar Watch
  actions[260] = 0x4000 -- play Cedar Watch
  actions[320] = 0x8000 -- complete Cedar Watch from the live board
  actions[350] = 0x4000 -- story, LoadExec Gate Village
  actions[410] = 0x4000 -- play Gate Village
  actions[470] = 0x8000 -- complete Gate Village from the live board
  actions[500] = 0x4000 -- village story
  actions[530] = 0x4000 -- parley story, LoadExec Twin Gate
  actions[590] = 0x4000 -- play the real Twin Gate encounter
  captures[20] = 'title'
  captures[150] = 'deepwood'
  captures[300] = 'watch'
  captures[450] = 'village'
  captures[650] = 'gate'
else
  -- A separate fresh card exercises the authored solo definitions, including
  -- their distinct starts and enemy scaling, through all five executables.
  actions[30] = 0x4000 -- New Game
  actions[50] = 0x4000 -- 1 Player
  actions[80] = 0x4000 -- prologue
  actions[110] = 0x4000 -- Bell Orchard
  actions[170] = 0x8000 -- complete Bell
  actions[200] = 0x4000 -- story, Deepwood
  actions[260] = 0x4000 -- play Deepwood
  actions[320] = 0x8000 -- complete Deepwood
  actions[350] = 0x4000 -- story, Cedar Watch
  actions[410] = 0x4000 -- play Cedar Watch
  actions[470] = 0x8000 -- complete Watch
  actions[500] = 0x4000 -- story, Gate Village
  actions[560] = 0x4000 -- play Village
  actions[620] = 0x8000 -- complete Village
  actions[650] = 0x4000 -- village story
  actions[680] = 0x4000 -- parley, Twin Gate
  actions[740] = 0x4000 -- play Twin Gate
  captures[150] = 'solo-bell'
  captures[300] = 'solo-deepwood'
  captures[450] = 'solo-watch'
  captures[600] = 'solo-village'
  captures[790] = 'solo-gate'
end

local function update()
  frame = frame + 1
  local held = actions[frame] or 0
  local player_two = player_two_actions[frame] or 0
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = player_two
  if held ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT frame=%d held=%04x', frame, held))
  end
  if player_two ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT player=2 frame=%d held=%04x',
                        frame, player_two))
  end
  if captures[frame] then pending_capture = captures[frame] end
  if frame >= (mode == 'new' and 1000 or mode == 'continue' and 1350 or 900) then
    PCSX.quit(0)
  end
  return true
end

grandleon_campaign_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon campaign update', update)

grandleon_campaign_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if pending_capture then
    local name = pending_capture
    pending_capture = nil
    local shot = PCSX.GPU.takeScreenShot()
    local output = Support.File.open('/out/' .. mode .. '-' .. name .. '.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_CAMPAIGN_CAPTURE name=%s %dx%d bytes=%d',
                        name, tonumber(shot.width), tonumber(shot.height), shot.data.size))
  end
end)

print(string.format('MIPSYNC_CAMPAIGN_PROBE_READY mode=%s update=%08x',
                    mode, update_address))
