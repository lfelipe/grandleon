local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')),
                              'GRANDLEON_UPDATE_ADDRESS is required')
local mode = os.getenv('GRANDLEON_CAMPAIGN_MODE') or 'new'
local frame = 0
local capture_title = false
local capture_deep = false
local actions = {}

if mode == 'new' then
  actions[30] = 0x4000 -- New Game
  actions[50] = 0x0040 -- 2 Players
  actions[70] = 0x4000
  actions[100] = 0x4000 -- prologue
  actions[130] = 0x8000 -- explicit debug stage jump
  actions[160] = 0x4000 -- aftermath, persist and LoadExec Deepwood
  actions[220] = 0x4000 -- Deepwood objective
else
  actions[30] = 0x0040 -- Continue row
  actions[50] = 0x4000 -- LoadExec saved Deepwood stage
  actions[110] = 0x4000 -- Deepwood objective
end

local function update()
  frame = frame + 1
  local held = actions[frame] or 0
  PCSX.getRegisters().GPR.n.a0 = held
  if held ~= 0 then
    print(string.format('MIPSYNC_CAMPAIGN_INPUT frame=%d held=%04x', frame, held))
  end
  if frame == 20 then capture_title = true end
  if frame == 360 then capture_deep = true end
  if frame >= 900 then PCSX.quit(0) end
  return true
end

grandleon_campaign_update = PCSX.addBreakpoint(
    update_address, 'Exec', 4, 'Grandleon campaign update', update)

grandleon_campaign_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if capture_title then
    capture_title = false
    local shot = PCSX.GPU.takeScreenShot()
    local output = Support.File.open('/out/' .. mode .. '-title.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_CAMPAIGN_TITLE_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height), shot.data.size))
  end
  if capture_deep then
    capture_deep = false
    local shot = PCSX.GPU.takeScreenShot()
    local output = Support.File.open('/out/' .. mode .. '-deepwood.bin', 'TRUNCATE')
    output:writeMoveSlice(shot.data)
    output:close()
    print(string.format('MIPSYNC_CAMPAIGN_DEEP_CAPTURE %dx%d bytes=%d',
                        tonumber(shot.width), tonumber(shot.height), shot.data.size))
  end
end)

print(string.format('MIPSYNC_CAMPAIGN_PROBE_READY mode=%s update=%08x',
                    mode, update_address))
