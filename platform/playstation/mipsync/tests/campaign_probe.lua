local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_CAMPAIGN_MODE') or 'new'
local frame = 0
local pending_capture = nil
local actions = {}
local captures = {}

if mode == 'new' then
  actions[30] = 0x4000 -- New Game
  actions[50] = 0x0040 -- 2 Players
  actions[70] = 0x4000
  actions[100] = 0x4000 -- prologue
  actions[130] = 0x4000 -- play Bell Orchard
  actions[200] = 0x8000 -- complete Bell from the live board
  actions[230] = 0x4000 -- story, persist and LoadExec Deepwood
  actions[290] = 0x4000 -- play Deepwood
  captures[20] = 'title'
  captures[180] = 'bell'
  captures[340] = 'deepwood'
else
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
end

local function update()
  frame = frame + 1
  local held = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a0 = held
  if held ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT frame=%d held=%04x', frame, held))
  end
  if captures[frame] then pending_capture = captures[frame] end
  if frame >= (mode == 'new' and 1000 or 1350) then PCSX.quit(0) end
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
