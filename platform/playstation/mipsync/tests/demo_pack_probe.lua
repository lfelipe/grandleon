-- SPDX-License-Identifier: MIT
-- Open the real demo's starting pack using ordinary controller inputs.
-- Generate bindings with personal-inventory-bindings.py --hero rowan --stages 01.
local ffi, bit = require('ffi'), require('bit')
local bindings = dofile('/out/ps1-demo-inventory-bindings.lua')[1]
local frames, next_input, step, captured = 0, 20, 0, false
local function number(address, kind)
  assert(address >= 0x80000000 and address < 0x80200000)
  return tonumber(ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))[0])
end
local function check_pack()
  local L, S = bindings.layout, bindings.symbols.s_snapshot
  assert(number(S + L.EncounterSnapshot.inventory_mode, 'const uint8_t*') == 1)
  local first = number(S + L.EncounterSnapshot.units, 'const uint32_t*')
  local last = number(S + L.EncounterSnapshot.units + 4, 'const uint32_t*')
  for unit = first, last - L.UnitSnapshot.size, L.UnitSnapshot.size do
    local id = string.format('%08x%08x', number(unit + L.UnitSnapshot.id + 4, 'const uint32_t*'),
                            number(unit + L.UnitSnapshot.id, 'const uint32_t*'))
    if id == bindings.hero_id then
      assert(number(unit + L.UnitSnapshot.states_carry_capacity, 'const uint8_t*') == 1)
      assert(number(unit + L.UnitSnapshot.carry_capacity, 'const uint16_t*') == 5)
      local a = number(unit + L.UnitSnapshot.carried_inventory, 'const uint32_t*')
      local b = number(unit + L.UnitSnapshot.carried_inventory + 4, 'const uint32_t*')
      assert(b - a == 2 * L.CarriedStack.size, 'starting sword and tonic must be owned')
      for index = 0, 1 do
        local entry = a + index * L.CarriedStack.size
        assert(number(entry + L.CarriedStack.quantity, 'const uint16_t*') == 1)
        assert(number(entry + L.CarriedStack.category, 'const uint32_t*') == 4 + index)
      end
      return
    end
  end
  error('authored Rowan identity missing')
end
local function update()
  frames = frames + 1
  assert(frames < 1000, 'demo pack input timed out at step ' .. step)
  local held = 0
  if frames >= next_input then
    local S = bindings.symbols
    local campaign = number(S.s_campaign_mode, 'const uint8_t*')
    local mode = number(S.s_mode, 'const uint8_t*')
    if campaign ~= 5 or mode == 13 then held = 0x4000
    elseif mode == 7 then held = 0x2000
    elseif mode == 0 and step == 0 then held = 0x0008; step = 1
    elseif mode == 0 then held = 0x4000
    elseif mode == 1 then held = 0x4000
    elseif mode == 2 then held = 0x4000
    elseif mode == 10 then
      check_pack()
      if not captured then step = frames + 8; captured = true
      elseif frames >= step then
        local shot = PCSX.GPU.takeScreenShot()
        local file = Support.File.open('/out/ps1-demo-starting-pack.bin', 'TRUNCATE')
        file:writeMoveSlice(shot.data); file:close()
        print('PS1_DEMO_PACK_PASS slots=2 capacity=5 weapon=1 tonic=1')
        PCSX.quit(0)
      end
    end
    next_input = frames + 6
  end
  PCSX.getRegisters().GPR.n.a0 = held
  PCSX.getRegisters().GPR.n.a1 = 0
  return true
end
grandleon_demo_pack_handles = {
  PCSX.addBreakpoint(bindings.symbols.grandleon_runtime_update, 'Exec', 4,
    'Observe normal demo pack', function()
      local signature = bindings.signature
      local bytes = PCSX.getMemPtr() + bit.band(signature.address, 0x1fffff)
      if ffi.string(bytes, #signature.code) ~= signature.code then return true end
      local ok, result = pcall(update)
      if not ok then print('PS1_DEMO_PACK_FAIL ' .. tostring(result)); PCSX.quit(1) end
      return ok and result or true
    end)
}
