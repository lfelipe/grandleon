-- SPDX-License-Identifier: MIT
-- Reach the actual campaign board through ordinary controller input before
-- running the Bell Orchard combat sequence. Read the UI's normal completed
-- calls; never invoke it or write campaign, inventory, camera or save state.
local ffi, bit = require('ffi'), require('bit')
local menu_address = assert(grandleon_campaign_menu_address)
local update_address = assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS')))
local add_breakpoint = PCSX.addBreakpoint
local boot_frame, next_input, started = 0, 20, false
local observed, settled, output, returning = nil, false, nil, nil
local function read(address, kind)
  return tonumber(ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))[0])
end
grandleon_project_ui_returns = {}
grandleon_project_ui_entry = add_breakpoint(grandleon_project_ui_address, 'Exec', 4,
  'Observe real campaign UI', function()
    local regs = PCSX.getRegisters().GPR.n
    output, returning = tonumber(regs.a0), tonumber(regs.ra)
    if not grandleon_project_ui_returns[returning] then
      local address = returning
      grandleon_project_ui_returns[address] = add_breakpoint(address, 'Exec', 4,
        'Read completed campaign UI', function()
          if returning == address then
            observed = read(output, 'const uint8_t*')
            settled = read(output + grandleon_project_settled_offset, 'const uint8_t*') ~= 0
            returning = nil
          end
          return true
        end)
    end
    return true
  end)
PCSX.addBreakpoint = function(address, kind, size, label, callback)
  if address ~= update_address or label ~= 'Grandleon combat/action probe' then
    return add_breakpoint(address, kind, size, label, callback)
  end
  return add_breakpoint(address, kind, size, label, function()
    if started then return callback() end
    if observed == 0 and settled then
      started = true
      print('MIPSYNC_PROJECT_COMBAT_STARTED ordinary_campaign=1')
      return callback()
    end
    boot_frame = boot_frame + 1
    assert(boot_frame < 1800, 'normal campaign did not reach its first board')
    local held = 0
    if observed and boot_frame >= next_input then
      local row = read(menu_address, 'const uint32_t*')
      if observed == 14 then held = row == 0 and 0x4000 or 0x10 -- New Game.
      elseif observed == 15 then held = row == 1 and 0x4000 or 0x40 -- 2P.
      elseif observed == 22 then held = 0x1000 -- Keep the authored name.
      elseif observed == 16 or observed == 17 or observed == 13 then held = 0x4000 -- One real page.
      elseif observed ~= 0 and observed ~= 23 then error('unexpected campaign UI before battle: '..observed) end
      next_input = boot_frame + 10
    end
    PCSX.getRegisters().GPR.n.a0 = held
    PCSX.getRegisters().GPR.n.a1 = 0
    return true
  end)
end
