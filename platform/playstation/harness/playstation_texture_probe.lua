-- PlayStation Sevenfold texture experiment capture harness.
--
-- The guest names matched flat/current/rejected close views for medieval and
-- mythical art, then captures elevated camera-v2 close and tactical views.
-- This writes native 320x240 pictures without a front-end scaler. Slot 20 is the
-- guest's final verdict and ends the emulator run.

local band = bit.band
local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'
local names = {
  [10] = 'medieval-flat',
  [11] = 'medieval-current',
  [12] = 'medieval-low-angle',
  [13] = 'medieval-screen-stage-rejected',
  [14] = 'mythical-flat',
  [15] = 'mythical-current',
  [16] = 'mythical-low-angle',
  [17] = 'mythical-screen-stage-rejected',
  [18] = 'overview-flat',
  [19] = 'overview-hybrid',
  [22] = 'medieval-camera-v2',
  [23] = 'mythical-camera-v2',
  [24] = 'overview-camera-v2',
  [27] = 'medieval-camera-v2-96',
}

local checks = 0
local failures = 0
local captures = 0
local animation_frames = 0
local camera_v2_animation_frames = 0
local exchange_frames = 0
local comparison_frames = 0
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

PCSX.execSlots[25] = function()
  local name = string.format('movement-camera-v2-%02d',
                             camera_v2_animation_frames)
  local key = 200 + camera_v2_animation_frames
  local ok, err = pcall(capture, 25, name, key)
  if not ok then
    say('CHECK ' .. name .. ' capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  camera_v2_animation_frames = camera_v2_animation_frames + 1
end

PCSX.execSlots[26] = function()
  local name = string.format('close-exchange-camera-v2-%02d', exchange_frames)
  local key = 300 + exchange_frames
  local ok, err = pcall(capture, 26, name, key)
  if not ok then
    say('CHECK ' .. name .. ' capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  exchange_frames = exchange_frames + 1
end

PCSX.execSlots[29] = function()
  local name = string.format('comparison-sequence-%02d', comparison_frames)
  local key = 400 + comparison_frames
  local ok, err = pcall(capture, 29, name, key)
  if not ok then
    say('CHECK ' .. name .. ' capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  comparison_frames = comparison_frames + 1
end

PCSX.execSlots[20] = function()
  expect(captures == 130, 'all still and animation frames were captured')
  expect(signatures[10] ~= signatures[11],
         'medieval current frame differs from its flat baseline')
  expect(signatures[11] ~= signatures[12],
         'medieval low-angle hybrid differs from the current projection')
  expect(signatures[12] ~= signatures[13],
         'medieval rejected screen stage differs from the low-angle hybrid')
  expect(signatures[13] ~= signatures[22],
         'medieval camera v2 differs from the rejected screen stage')
  expect(signatures[27] ~= signatures[22],
         '128px close art differs from its matched 96px baseline')
  expect(signatures[14] ~= signatures[15],
         'mythical current frame differs from its flat baseline')
  expect(signatures[15] ~= signatures[16],
         'mythical low-angle hybrid differs from the current projection')
  expect(signatures[16] ~= signatures[17],
         'mythical rejected screen stage differs from the low-angle hybrid')
  expect(signatures[17] ~= signatures[23],
         'mythical camera v2 differs from the rejected screen stage')
  expect(signatures[18] ~= signatures[19],
         'hybrid tactical overview differs from its flat baseline')
  expect(signatures[19] ~= signatures[24],
         'camera-v2 overview differs from the earlier hybrid')
  expect(signatures[13] ~= signatures[17],
         'medieval and mythical rejected close views remain distinct')
  expect(signatures[22] ~= signatures[23],
         'medieval and mythical camera-v2 views remain distinct')
  expect(animation_frames == 8, 'all eight movement frames were captured')
  expect(signatures[100] ~= signatures[107],
         'the movement film changes position and pose')
  expect(camera_v2_animation_frames == 16,
         'all sixteen eased camera-v2 movement frames were captured')
  expect(signatures[200] ~= signatures[215],
         'camera-v2 movement changes position and pose')
  expect(exchange_frames == 40,
         'all forty eased fixed-side exchange frames were captured')
  expect(signatures[300] ~= signatures[312],
         'left attack reaches a distinct camera-v2 impact frame')
  expect(signatures[320] ~= signatures[332],
         'right counter reaches a distinct camera-v2 impact frame')
  expect(comparison_frames == 52,
         'all 52 matched renderer-comparison landmarks were captured')
  expect(signatures[400] == signatures[401],
         'matched scene begins with a deterministic board hold')
  expect(signatures[418] ~= signatures[438],
         'matched attack and counter impacts are visually distinct')
  expect(signatures[450] == signatures[451],
         'matched scene ends on a deterministic restored board hold')
  expect(signatures[400] ~= signatures[450],
         'restored board exposes the authoritative health change')
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  PCSX.quit(failures == 0 and 0 or 1)
end

say('installed still slots 10..19,22..24,27; animation slots 21,25,26,29; done slot 20')
