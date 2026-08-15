-- The host half of the turn check: what the emulator says is on screen, at
-- every instant the executable settles.
--
-- `playstation_probe.lua` photographs one still picture at the end of one main.
-- This photographs a session: the turn executable signals once per settled
-- checkpoint, from inside the store that names the instant, and every one of
-- those frames is written out for the joiner to compare against the pixel
-- claims that were printed beside it.
--
-- The difference from the render harness is only that there are many, and it
-- has two consequences worth stating rather than discovering:
--
--   * **The GPU's registers are checked once, at the first capture.** They
--     cannot change afterwards without the executable having reset the GPU, and
--     decoding a save state eighty-five times to learn the same six facts would
--     be most of the run's wall clock.
--
--   * **The colour-spread guard applies to the opening board and not to every
--     frame.** "At least sixteen distinct colours and no colour over 60%" is
--     the blank-screen guard. It is exactly right for a board and exactly
--     wrong for the information sheet, which is deliberately a screen of
--     paper with letters on it. So the guard is asserted where a
--     board is guaranteed to be (the first checkpoint, which is the board as
--     the content authored it) and every frame is still required to be a
--     representable 15-bit picture with more than one colour in it. A run whose
--     display went blank half way through fails on the second of those.
--
-- The per-cell comparison is not done here, for the same reason it is not done
-- in the render harness: this script cannot see the executable's stdout, and a
-- harness that decided the gate from inside the emulator would be checking one
-- channel. `playstation_turn_probe.c` reads both.
--
-- ---------------------------------------------------------------------------
-- The second camera
--
-- Off unless `GRANDLEON_PLAYSTATION_FILM_DIR` is set, and then it writes every
-- composited frame between two checkpoint ordinals rather than one frame per
-- checkpoint. `film_from`/`film_to` are checkpoint ordinals, inclusive at both
-- ends, and a window is what makes a film a few seconds rather than the whole
-- session.
--
-- It touches nothing the verdict is computed from. `checks`, `failures` and
-- `captures` are not incremented here, no frame written here goes to the
-- joiner's directory, and the checkpoint camera above is unchanged, so a
-- filmed run makes exactly the checks an unfilmed one makes and reaches exactly
-- the same verdict. That is the property that lets a film be evidence: it is
-- only ever a recording of a run that passed, taken by the harness that judged
-- it, in the process that judged it.

-- The three slots this harness answers on, and they are written down in exactly
-- two places: here and in `platform/playstation/src/turn_exe.cpp`. None of them
-- is the render harness's 13, on purpose: a harness written for one executable
-- must not silently photograph the other.
--
-- A board and a campaign screen are photographed on *different* slots because
-- they are different pictures and the strongest thing that can be said about
-- one is false of the other. A board is terrain, tokens, washes and a cursor,
-- so a board holding three colours is a renderer that did not run; a campaign
-- screen is a page of text, so three colours is exactly what a title screen
-- has. Telling them apart here is what lets each be held to its own claim
-- rather than both to the weaker one.
local SLOT = 14
local SCREEN_SLOT = 12
local DONE_SLOT = 15

local band, rshift = bit.band, bit.rshift

local frame_dir = os.getenv('GRANDLEON_PLAYSTATION_FRAME_DIR') or '/out'

-- What the renderer asked the GPU for. `psx_gpu.cpp`'s numbers, repeated here
-- rather than derived from the frame precisely so that a renderer which changed
-- one and produced a consistent-but-different picture is caught rather than
-- followed.
local WANT_WIDTH = 320
local WANT_HEIGHT = 240
local WANT_START_X = 0
local WANT_START_Y = 0
local WANT_RANGE_X0 = 0x200
local WANT_RANGE_X1 = 0x200 + WANT_WIDTH * 8
local WANT_RANGE_Y0 = 16
local WANT_RANGE_Y1 = 16 + WANT_HEIGHT

local checks = 0
local failures = 0
local captures = 0

-- The second camera's settings, and its own counters, kept apart from the ones
-- above on purpose: nothing here may reach the verdict.
local film_dir = os.getenv('GRANDLEON_PLAYSTATION_FILM_DIR')
local film_from = tonumber(os.getenv('GRANDLEON_PLAYSTATION_FILM_FROM') or '') or 1
local film_to = tonumber(os.getenv('GRANDLEON_PLAYSTATION_FILM_TO') or '') or 0
-- A ceiling rather than a budget, and it is above anything a real window
-- reaches: the whole eighty-five-checkpoint session is 2,270 retraces, which is
-- 349 MB of raw frames, and this stops at 4,000. It exists so that a harness
-- pointed at a longer-running executable cannot quietly fill a disk; the run
-- script fails the run rather than shipping a truncated film.
local film_limit = tonumber(os.getenv('GRANDLEON_PLAYSTATION_FILM_LIMIT') or '') or 4000
local film_written = 0
local film_truncated = false

local function say(text)
  print('HARNESS ' .. text)
end

local function expect(condition, name)
  checks = checks + 1
  if not condition then failures = failures + 1 end
  say('CHECK ' .. name .. (condition and ' PASS' or ' FAIL'))
end

local function expect_equal(actual, wanted, name)
  expect(actual == wanted,
         string.format('%s (%s, wanted %s)', name, tostring(actual),
                       tostring(wanted)))
end

-- The GPU registers, out of a save state. GP1 is write-only on the hardware and
-- PCSX-Redux routes the port straight into the GPU without mirroring it, so the
-- emulator's own 256-entry shadow is the only legible copy.
local function gpu_registers()
  local protoc = require('protoc')
  local pb = require('pb')
  local compiler = protoc.new()
  assert(compiler:load(PCSX.getSaveStateProtoSchema()))
  local state = pb.decode('SaveState', tostring(PCSX.createSaveState()))
  local control = state.gpu.control
  local function gp1(command)
    local base = command * 4
    return control:byte(base + 1) + control:byte(base + 2) * 256
         + control:byte(base + 3) * 65536 + control:byte(base + 4) * 16777216
  end
  return state.gpu.status, gp1
end

local function check_registers()
  local decoded, status, gp1 = pcall(gpu_registers)
  expect(decoded, 'the GPU registers decode')
  if not decoded then
    say('DETAIL ' .. tostring(status))
    return
  end

  say(string.format('GPUSTAT %08x', status))
  expect(band(rshift(status, 23), 1) == 0,
         'GPUSTAT says the display is enabled')
  expect_equal(band(gp1(0x03), 1), 0, 'GP1(03) display enable')

  local start = gp1(0x05)
  expect_equal(band(start, 0x3FF), WANT_START_X, 'GP1(05) display start x')
  expect_equal(band(rshift(start, 10), 0x1FF), WANT_START_Y,
               'GP1(05) display start y')

  local horizontal = gp1(0x06)
  expect_equal(band(horizontal, 0xFFF), WANT_RANGE_X0,
               'GP1(06) horizontal range x0')
  expect_equal(band(rshift(horizontal, 12), 0xFFF), WANT_RANGE_X1,
               'GP1(06) horizontal range x1')

  local vertical = gp1(0x07)
  expect_equal(band(vertical, 0x3FF), WANT_RANGE_Y0,
               'GP1(07) vertical range y0')
  expect_equal(band(rshift(vertical, 10), 0x3FF), WANT_RANGE_Y1,
               'GP1(07) vertical range y1')

  expect_equal(band(rshift(status, 17), 3), 1, 'GPUSTAT horizontal resolution')
  expect_equal(band(rshift(status, 19), 1), 0, 'GPUSTAT vertical resolution')
  expect_equal(band(rshift(status, 21), 1), 0,
               'GPUSTAT colour depth is 15-bit')
end

-- Whether the first frame of each kind has been judged. The strong claims below
-- are about the first one the run produces, because that is the one that says
-- the renderer works at all; every frame after it is held to the claims that
-- are true of all of them, plus the joiner's exact pixels.
local board_judged = false
local screen_judged = false

local function measure(is_board)
  local index = captures
  captures = captures + 1
  if index == 0 then check_registers() end

  local shot = PCSX.GPU.takeScreenShot()
  local width = tonumber(shot.width)
  local height = tonumber(shot.height)
  if index == 0 then
    expect_equal(width, WANT_WIDTH, 'the frame is as wide as the display')
    expect_equal(height, WANT_HEIGHT, 'the frame is as tall as the display')
    expect_equal(shot.data.size, width * height * 2,
                 'the frame is 15-bit colour')
  elseif width ~= WANT_WIDTH or height ~= WANT_HEIGHT then
    expect(false, string.format('checkpoint %d keeps the display size', index))
  end

  local pixels = width * height
  local histogram = {}
  local distinct = 0
  local largest = 0
  local representable = true
  for offset = 0, shot.data.size - 2, 2 do
    local colour = shot.data[offset] + shot.data[offset + 1] * 256
    -- Every colour this executable can put on screen comes out of a CLUT this
    -- repository generated or an interface constant it chose, and neither ever
    -- sets the semi-transparency bit.
    if band(colour, 0x8000) ~= 0 then representable = false end
    local seen = (histogram[colour] or 0) + 1
    if seen == 1 then distinct = distinct + 1 end
    histogram[colour] = seen
    if seen > largest then largest = seen end
  end

  -- Every frame: a picture, in colours this art can make, with something on it.
  expect(representable,
         string.format('checkpoint %d is drawn in colours this art can produce',
                       index))
  expect(distinct > 1,
         string.format('checkpoint %d is not a single flat colour', index))
  -- The first board, and only it: a sheet is deliberately a screen of paper.
  if is_board and not board_judged then
    board_judged = true
    expect(distinct >= 16,
           'the opening board holds at least sixteen distinct colours')
    expect(largest * 100 < pixels * 60,
           'no single colour covers more than 60% of the opening board')
  end
  -- The first campaign screen, and only it. A page is paper, whatever the panel
  -- stands on, and ink, so three colours is the floor and two is a panel with
  -- nothing written on it. The second claim is what says the third colour is
  -- *words*: a title screen's ink covers 843 of 76,800 pixels, and a page that
  -- drew one letter would cover about fifteen.
  --
  -- These are deliberately looser than the board's pair and they are not the
  -- weight this frame carries. A screen's real proof is in the joiner, which
  -- compares the panel at both ends, the first ink pixel of the first written
  -- row, the caret, the footer and the scene's own backdrop band against the
  -- executable's claims, the GPU's readback and this frame: five exact pixels
  -- with no tolerance, which no histogram can be.
  if not is_board and not screen_judged then
    screen_judged = true
    local commonest, second = 0, 0
    for _, seen in pairs(histogram) do
      if seen > commonest then
        second = commonest
        commonest = seen
      elseif seen > second then
        second = seen
      end
    end
    expect(distinct >= 3, 'the opening screen is paper, ground and ink')
    expect(pixels - commonest - second >= 256,
           'and at least 256 pixels of it are words')
  end

  local path = string.format('%s/frame-%04d.bin', frame_dir, index)
  local out = Support.File.open(path, 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  say(string.format('FRAME %d %dx%d distinct %d largest %d%%', index, width,
                    height, distinct,
                    pixels > 0 and math.floor(largest * 100 / pixels) or 0))
  -- Where this checkpoint fell in the film, so that choosing a window is
  -- reading rather than guessing.
  if film_dir then
    say(string.format('FILM-AT %d checkpoint %d', film_written, index + 1))
  end
end

local function capture_kind(is_board)
  local ok, err = pcall(measure, is_board)
  if not ok then
    say('CHECK the capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    say(string.format('RESULT FAIL %d/%d', checks - failures - 1, checks + 1))
    PCSX.quit(1)
  end
  if failures ~= 0 then
    -- End the run here rather than let the executable write a passing exit code
    -- over the top of a failed measurement.
    say(string.format('FRAMES %d %dx%d', captures, WANT_WIDTH, WANT_HEIGHT))
    say(string.format('RESULT FAIL %d/%d', checks - failures, checks))
    PCSX.quit(1)
  end
end

local function capture() capture_kind(true) end
local function capture_screen() capture_kind(false) end

PCSX.execSlots[SLOT] = capture
PCSX.execSlots[SCREEN_SLOT] = capture_screen

-- The film, one frame per vertical retrace.
--
-- A retrace is where a single-buffered machine's picture is finished: the
-- executable draws and then waits for one, so what the display holds at this
-- instant is a whole frame the console showed. This listens to the emulator's
-- own retrace event, which is why the film needs no wall clock to say how long
-- a frame lasted.
local function film()
  if captures < film_from then return end
  if film_to > 0 and captures > film_to then return end
  if film_written >= film_limit then
    film_truncated = true
    return
  end
  local shot = PCSX.GPU.takeScreenShot()
  local path = string.format('%s/film-%05d.bin', film_dir, film_written)
  local out = Support.File.open(path, 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  film_written = film_written + 1
end

-- Global, and it has to be. `createEventListener` hands back an object that
-- *is* the subscription: let it be collected and the listener goes with it, and
-- a `local` in a `-dofile` chunk nothing else closes over is collectable the
-- moment the chunk returns. `playstation_probe.lua` gets away with dropping its
-- listener only because that one fires on the very first retrace, before a
-- collection can happen; this one has to survive forty-odd checkpoints first,
-- and does not get away with it. Dropped, it wrote no frames at all.
grandleon_film_listener = nil

if film_dir then
  grandleon_film_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
    local ok, err = pcall(film)
    if not ok then
      say('FILM-FAILED ' .. tostring(err))
      PCSX.quit(1)
    end
  end)
  say(string.format('FILM-DIR %s checkpoints %d..%s', film_dir, film_from,
                    film_to > 0 and tostring(film_to) or 'end'))
end

-- The verdict, written when the guest says it has finished settling, because
-- only the guest knows that. A second slot rather than an emulator event: the
-- events this emulator publishes are about the machine's own lifecycle (a
-- shell reached, a state loaded) and none of them means "the program has
-- stopped photographing itself". The executable signals this one immediately
-- before it returns from `main`, and the joiner requires the line to be
-- present, so a run that ended early is a failure rather than a silence.
PCSX.execSlots[DONE_SLOT] = function()
  if film_dir then
    say(string.format('FILM %d frames%s', film_written,
                      film_truncated and ' TRUNCATED' or ''))
  end
  say(string.format('FRAMES %d %dx%d', captures, WANT_WIDTH, WANT_HEIGHT))
  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
end

say('installed on execSlot ' .. SLOT)
say('FRAME-DIR ' .. frame_dir)
