-- The host half of the render check: what the emulator says is on screen.
--
-- Loaded with `-dofile`. PCSX-Redux compiles a LuaJIT VM into the emulator and
-- keeps it running under `-no-ui`, where the no-op text UI still pumps the
-- event loop, so this runs headlessly beside the executable with no display
-- server, no GL context and no second process.
--
-- ---------------------------------------------------------------------------
-- Why the capture is a callback and not a poll
--
-- A byte written to 0x1f802081 makes the emulator call `PCSX.execSlots[n]`
-- *from inside the store instruction* and resume the CPU when it returns. So
-- the play executable names the instant, at the end of its own main, with the
-- picture finished. Nothing here waits a guessed number of frames, and there is
-- no race to lose. `PCSX.nextTick` would have been the obvious alternative and
-- is a trap: the emulator clears the global after calling it, so a callback
-- that re-registers itself fires exactly once.
--
-- ---------------------------------------------------------------------------
-- What it measures, and what it refuses to take the executable's word for
--
-- `PCSX.GPU.takeScreenShot()` crops VRAM to the active display, from
-- `DisplayPosition` to `DisplayEnd`, which the GPU derives from GP1(0x05),
-- GP1(0x06), GP1(0x07) and GP1(0x08). So a coordinate in the frame it returns
-- is a coordinate the renderer computed, with no offset to agree about. A
-- machine that composited its picture per pixel and stored it nowhere would
-- need a front end built and the emulator patched to reach the same standard;
-- here the framebuffer is memory and the screenshot is that memory.
--
-- **But a screenshot is not by itself evidence that anything is on screen.**
-- This is measured rather than assumed: an executable that draws correctly and
-- then leaves the display *disabled* produces a byte-identical screenshot, and
-- a byte-identical console-side VRAM readback, and exits zero. `takeScreenShot`
-- never consults the display-enable flag. So the frame is not believed until
-- the GPU's own registers have been read, and they are not readable from the
-- guest: GP1 is write-only on the hardware and PCSX-Redux routes the port
-- straight into the GPU without mirroring it. The only exposed copy is the
-- shadow the emulator keeps in a save state, so that is where they are read
-- from.
--
-- Six things, in this order, and the first five are the harness's own:
--
--   1. the display is enabled, so GPUSTAT bit 23 is clear and GP1(0x03) is 0;
--   2. the display window is the one the renderer asked for, from GP1(0x05),
--      GP1(0x06) and GP1(0x07) rather than from the frame it produced;
--   3. the frame is exactly the size that window implies;
--   4. every pixel is a representable 15-bit colour with the top bit clear,
--      which is what this renderer's CLUTs can produce and nothing else;
--   5. the frame holds at least sixteen distinct colours, and no single colour
--      covers more than 60% of it.
--
-- 4 and 5 are the cheap ones, and they are what makes running the render-less
-- conformance executable through this harness a failure rather than a pass.
-- It draws nothing at all, so its frame is one colour.
--
-- The sixth is the per-cell comparison, and it is not done here: this writes
-- the frame out and `playstation_probe.c` joins it against the claims the
-- executable printed. Splitting it that way is deliberate. This script cannot
-- see the executable's stdout, and a harness that decided the gate from inside
-- the emulator would be checking one channel; the joiner reads both.

local SLOT = 13

-- LuaJIT is Lua 5.1, so there are no `&`, `|`, `<<` or `>>` operators and no
-- integer division. Everything below goes through LuaJIT's `bit` library, which
-- is what the emulator's own example scripts use.
local band, rshift = bit.band, bit.rshift

local frame_path = os.getenv('GRANDLEON_PLAYSTATION_FRAME') or '/out/frame.bin'

-- What the renderer asked the GPU for. These are `psx_gpu.cpp`'s numbers, and
-- they are repeated here rather than derived from the frame precisely so that a
-- renderer which changed one and produced a consistent-but-different picture is
-- caught rather than followed.
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
local fired = false

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

-- The GPU registers, out of a save state.
--
-- `control` is the emulator's 256-entry shadow of every GP1 command it has
-- executed, indexed by command number, four bytes each little-endian. It is the
-- only place the display window is legible from: GP1 is write-only on the
-- hardware and PCSX-Redux routes the port straight into the GPU without
-- mirroring it anywhere the guest or a memory read could reach.
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

local function measure()
  say('CAPTURE fired from the guest')

  -- 1 and 2: the machine, before the picture.
  local decoded, status, gp1 = pcall(gpu_registers)
  expect(decoded, 'the GPU registers decode')
  if not decoded then
    say('DETAIL ' .. tostring(status))
    status, gp1 = 0, function() return 0 end
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

  -- Bits 17..18 of GPUSTAT are the horizontal resolution, bit 19 the vertical
  -- and bit 21 the colour depth; 1, 0 and 0 are 320 by 240 in 15-bit colour,
  -- which is the mode `psx_gpu.cpp` sets.
  expect_equal(band(rshift(status, 17), 3), 1, 'GPUSTAT horizontal resolution')
  expect_equal(band(rshift(status, 19), 1), 0, 'GPUSTAT vertical resolution')
  expect_equal(band(rshift(status, 21), 1), 0,
               'GPUSTAT colour depth is 15-bit')

  -- 3, 4 and 5: the picture.
  local shot = PCSX.GPU.takeScreenShot()
  local width = tonumber(shot.width)
  local height = tonumber(shot.height)
  expect_equal(width, WANT_WIDTH, 'the frame is as wide as the display')
  expect_equal(height, WANT_HEIGHT, 'the frame is as tall as the display')

  local pixels = width * height
  expect_equal(shot.data.size, pixels * 2, 'the frame is 15-bit colour')

  local histogram = {}
  local distinct = 0
  local largest = 0
  local representable = true
  for offset = 0, shot.data.size - 2, 2 do
    local colour = shot.data[offset] + shot.data[offset + 1] * 256
    -- Every colour this renderer can put on screen comes out of a CLUT this
    -- repository generated, and the generator leaves the semi-transparency bit
    -- clear in every entry. A pixel with it set did not come from this art.
    if band(colour, 0x8000) ~= 0 then representable = false end
    local seen = (histogram[colour] or 0) + 1
    if seen == 1 then distinct = distinct + 1 end
    histogram[colour] = seen
    if seen > largest then largest = seen end
  end

  expect(representable, 'every pixel is a colour this art can produce')
  expect(distinct >= 16, 'the frame holds at least sixteen distinct colours')
  expect(largest * 100 < pixels * 60,
         'no single colour covers more than 60% of the frame')
  say(string.format('FRAME %dx%d distinct %d largest %d%%', width, height,
                    distinct,
                    pixels > 0 and math.floor(largest * 100 / pixels) or 0))

  -- The frame, for the joiner. `writeMoveSlice` hands the slice over rather
  -- than copying it, which is also why the histogram above runs first.
  local out = Support.File.open(frame_path, 'TRUNCATE')
  out:writeMoveSlice(shot.data)
  out:close()
  say('FRAME-FILE ' .. frame_path)

  say(string.format('RESULT %s %d/%d', failures == 0 and 'PASS' or 'FAIL',
                    checks - failures, checks))
  if failures ~= 0 then
    -- End the run here rather than let the executable write a passing exit code
    -- over the top of a failed measurement. The run script reads both channels
    -- and this is the one that has already decided.
    PCSX.quit(1)
  end
end

local function capture()
  if fired then return end
  fired = true
  local ok, err = pcall(measure)
  if not ok then
    say('CHECK the capture completes FAIL')
    say('DETAIL ' .. tostring(err))
    PCSX.quit(1)
  end
end

PCSX.execSlots[SLOT] = capture

-- The negative control, and it is the reason to trust anything above.
--
-- A harness whose checks a blank screen would also satisfy proves nothing, so
-- there has to be a way to point this at a program that draws nothing. The
-- conformance executable is exactly that program. It never touches the GPU, so
-- it never signals either, and with this set the capture is fired from the
-- first vertical retrace instead.
--
-- Off by default. On the good path the guest names the instant, and a listener
-- racing it would be a second answer to a question that already has one.
if os.getenv('GRANDLEON_PLAYSTATION_CAPTURE_ON_VSYNC') then
  PCSX.Events.createEventListener('GPU::Vsync', capture)
  say('capturing on the first vertical retrace')
end

say('installed on execSlot ' .. SLOT)
