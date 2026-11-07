-- PlayStation Sevenfold texture experiment capture harness.
--
-- The guest names matched flat/current/old-low-angle/corrected close views for
-- medieval and mythical art, then matched flat/refined tactical overviews.
-- This writes native 320x240 pictures without a front-end scaler. Slot 20 is the
-- guest's final verdict and ends the emulator run.

local band = bit.band
local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'
local names = {
  [10] = 'medieval-flat',
  [11] = 'medieval-current',
  [12] = 'medieval-low-angle',
  [13] = 'medieval-corrected',
  [14] = 'mythical-flat',
  [15] = 'mythical-current',
  [16] = 'mythical-low-angle',
  [17] = 'mythical-corrected',
  [18] = 'overview-flat',
  [19] = 'overview-hybrid',
}

local checks = 0
local failures = 0
local captures = 0
local animation_frames = 0
local signatures = {}

local function say(text)
  print('HARNESS ' .. text)
end

local function expect(condition, name)
  checks = checks + 1
  if not condition then failures = failures + 1 end
  say('CHECK ' .. name .. (condition and ' PASS' or ' FAIL'))
end

local function capture(slot, name_override, key_override)
  local name = name_override or names[slot]
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

  signatures[key_override or slot] = signature
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

PCSX.execSlots[21] = function()
  local name = string.format('movement-%02d', animation_frames)
  local key = 100 + animation_frames
  local ok, err = pcall(capture, 21, name, key)
  if not ok then
    say('CHECK ' .. name .. ' capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  animation_frames = animation_frames + 1
end

PCSX.execSlots[20] = function()
  expect(captures == 18, 'all still and movement frames were captured')
  expect(signatures[10] ~= signatures[11],
         'medieval current frame differs from its flat baseline')
  expect(signatures[11] ~= signatures[12],
         'medieval low-angle hybrid differs from the current projection')
  expect(signatures[12] ~= signatures[13],
         'medieval corrected camera differs from the low-angle hybrid')
  expect(signatures[14] ~= signatures[15],
         'mythical current frame differs from its flat baseline')
  expect(signatures[15] ~= signatures[16],
         'mythical low-angle hybrid differs from the current projection')
  expect(signatures[16] ~= signatures[17],
         'mythical corrected camera differs from the low-angle hybrid')
  expect(signatures[18] ~= signatures[19],
         'hybrid tactical overview differs from its flat baseline')
  expect(signatures[13] ~= signatures[17],
         'medieval and mythical corrected close views remain distinct')
  expect(animation_frames == 8, 'all eight movement frames were captured')
  expect(signatures[100] ~= signatures[107],
         'the movement film changes position and pose')
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  PCSX.quit(failures == 0 and 0 or 1)
end

say('installed comparison slots 10..19, movement slot 21 and done slot 20')
