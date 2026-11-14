local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local skip_at = tonumber(os.getenv('GRANDLEON_OVERVIEW_SKIP_AT') or '0')
local frame, capture_index = 0, 0
local capture_due = false
local final = skip_at > 0 and 90 or 300
stage_overview_breakpoint = PCSX.addBreakpoint(update_address, 'Exec', 4,
  'Stage opening overview', function()
    frame = frame + 1
    PCSX.getRegisters().GPR.n.a0 = frame == skip_at and 0x4000 or 0
    PCSX.getRegisters().GPR.n.a1 = 0
    if frame % 2 == 0 then capture_due = true end
    return true
  end)
stage_overview_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
  if capture_due then
    capture_due = false
    local shot = PCSX.GPU.takeScreenShot()
    local file = Support.File.open(string.format('/out/frame-%04d.bin', capture_index), 'TRUNCATE')
    file:writeMoveSlice(shot.data)
    file:close()
    capture_index = capture_index + 1
  end
  if frame >= final then
    print(string.format('MIPSYNC_OVERVIEW_CAPTURE frames=%d skip_at=%d', capture_index, skip_at))
    PCSX.quit(0)
  end
end)
