-- PlayStation Sevenfold texture experiment capture harness.
--
-- The guest names three completed, GPU-drained frames through exec slots:
-- flat baseline (10), textured medieval (11), textured mythical (12). This
-- writes the native 320x240 15-bit pictures without a front-end scaler. Slot
-- 13 is the guest's final verdict and ends the emulator run.

local band = bit.band
local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'
local names = {
  [10] = 'baseline',
  [11] = 'medieval-textured',
  [12] = 'mythical-textured',
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

PCSX.execSlots[13] = function()
  expect(captures == 3, 'all three comparison frames were captured')
  expect(signatures[10] ~= signatures[11],
         'medieval texture frame differs from the flat baseline')
  expect(signatures[11] ~= signatures[12],
         'mythical and medieval texture frames differ')
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  PCSX.quit(failures == 0 and 0 or 1)
end

say('installed comparison slots 10, 11, 12 and done slot 13')
