-- PlayStation Sevenfold fixed-camera 2.5D capture harness.
--
-- The guest exposes only the selected tactical, movement, close battle and
-- matched comparison frames. Slot 20 carries its final verdict.

local band = bit.band
local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'
local checks = 0
local failures = 0
local captures = 0
local movement_frames = 0
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

local function capture(name, key)
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

  signatures[key] = signature
  captures = captures + 1
  local out = Support.File.open(frame_dir .. '/' .. name .. '.bin', 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  say(string.format('FRAME %s 320x240 distinct %d signature %08x',
                    name, distinct, signature))
end

local function install(slot, name, key)
  PCSX.execSlots[slot] = function()
    local ok, err = pcall(capture, name, key)
    if not ok then
      say('CHECK ' .. name .. ' capture completes FAIL')
      say('DETAIL ' .. tostring(err))
      PCSX.quit(1)
    end
  end
end

install(24, 'tactical-board', 24)
install(33, 'combat-backdrop', 33)

PCSX.execSlots[25] = function()
  local index = movement_frames
  local ok, err = pcall(capture,
    string.format('movement-%02d', index), 100 + index)
  if not ok then
    say('CHECK movement capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  movement_frames = movement_frames + 1
end

PCSX.execSlots[26] = function()
  local index = exchange_frames
  local ok, err = pcall(capture,
    string.format('exchange-%02d', index), 200 + index)
  if not ok then
    say('CHECK exchange capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  exchange_frames = exchange_frames + 1
end

PCSX.execSlots[29] = function()
  local index = comparison_frames
  local ok, err = pcall(capture,
    string.format('comparison-%02d', index), 300 + index)
  if not ok then
    say('CHECK comparison capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
  comparison_frames = comparison_frames + 1
end

PCSX.execSlots[20] = function()
  expect(captures == 110, 'all selected renderer frames were captured')
  expect(movement_frames == 16, 'all sixteen movement frames were captured')
  expect(signatures[100] ~= signatures[115],
         'movement changes position and pose')
  expect(exchange_frames == 40, 'all forty exchange frames were captured')
  expect(signatures[200] ~= signatures[212],
         'attack reaches a distinct impact frame')
  expect(signatures[220] ~= signatures[232],
         'counter reaches a distinct impact frame')
  expect(comparison_frames == 52,
         'all fifty-two comparison landmarks were captured')
  expect(signatures[300] == signatures[301],
         'comparison begins with a deterministic board hold')
  expect(signatures[318] ~= signatures[338],
         'attack and counter impacts are distinct')
  expect(signatures[350] == signatures[351],
         'comparison ends with a deterministic restored board hold')
  expect(signatures[300] ~= signatures[350],
         'restored board exposes the health change')
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  PCSX.quit(failures == 0 and 0 or 1)
end

say('installed tactical, movement, close battle and comparison capture slots')
