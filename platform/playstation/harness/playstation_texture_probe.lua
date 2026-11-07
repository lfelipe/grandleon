-- PlayStation Sevenfold texture experiment capture harness.
--
-- The guest names matched flat/current/refined close views for medieval and
-- mythical art, then matched flat/refined tactical overviews. This writes the
-- native 320x240 15-bit pictures without a front-end scaler. Slot 18 is the
-- guest's final verdict and ends the emulator run.

local band = bit.band
local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'
local names = {
  [10] = 'medieval-flat',
  [11] = 'medieval-current',
  [12] = 'medieval-hybrid',
  [13] = 'mythical-flat',
  [14] = 'mythical-current',
  [15] = 'mythical-hybrid',
  [16] = 'overview-flat',
  [17] = 'overview-hybrid',
}

local checks = 0
local failures = 0
local captures = 0
local signatures = {}

local function say(text)
  print('HARNESS ' .. text)
end

local function expect(condition, name)
  checks = checks + 1
  if not condition then failures = failures + 1 end
  say('CHECK ' .. name .. (condition and ' PASS' or ' FAIL'))
end

local function capture(slot)
  local name = names[slot]
  local shot = PCSX.GPU.takeScreenShot()
  local width = tonumber(shot.width)
  local height = tonumber(shot.height)
  expect(width == 320 and height == 240, name .. ' is native 320x240')
  expect(shot.data.size == 320 * 240 * 2, name .. ' is 15-bit colour')

  local histogram = {}
  local distinct = 0
  local largest = 0
  local representable = true
  local signature = 2166136261
  for offset = 0, shot.data.size - 2, 2 do
    local colour = shot.data[offset] + shot.data[offset + 1] * 256
    if band(colour, 0x8000) ~= 0 then representable = false end
    local count = (histogram[colour] or 0) + 1
    if count == 1 then distinct = distinct + 1 end
    histogram[colour] = count
    if count > largest then largest = count end
    signature = band(signature * 16777619 + colour, 0xffffffff)
  end
  expect(representable, name .. ' uses representable opaque colours')
  expect(distinct >= 16, name .. ' has useful colour variety')
  expect(largest * 100 < 320 * 240 * 60,
         name .. ' is not mostly one blank colour')

  signatures[slot] = signature
  captures = captures + 1
  local path = string.format('%s/%s.bin', frame_dir, name)
  local out = Support.File.open(path, 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  say(string.format('FRAME %s 320x240 distinct %d largest %d%% signature %08x',
                    name, distinct, math.floor(largest * 100 / (320 * 240)),
                    signature))
end

for slot, _ in pairs(names) do
  PCSX.execSlots[slot] = function()
    local ok, err = pcall(capture, slot)
    if not ok then
      say('CHECK ' .. names[slot] .. ' capture completes FAIL')
      say('DETAIL ' .. tostring(err))
      PCSX.quit(1)
    end
  end
end

PCSX.execSlots[18] = function()
  expect(captures == 8, 'all eight comparison frames were captured')
  expect(signatures[10] ~= signatures[11],
         'medieval current frame differs from its flat baseline')
  expect(signatures[11] ~= signatures[12],
         'medieval hybrid differs from the current projection')
  expect(signatures[13] ~= signatures[14],
         'mythical current frame differs from its flat baseline')
  expect(signatures[14] ~= signatures[15],
         'mythical hybrid differs from the current projection')
  expect(signatures[16] ~= signatures[17],
         'hybrid tactical overview differs from its flat baseline')
  expect(signatures[12] ~= signatures[15],
         'medieval and mythical hybrid close views remain distinct')
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  PCSX.quit(failures == 0 and 0 or 1)
end

say('installed comparison slots 10..17 and done slot 18')
