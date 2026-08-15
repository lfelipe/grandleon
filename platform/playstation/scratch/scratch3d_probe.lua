-- The scratch program's observer: every frame it asks to be photographed.
--
-- Loaded with `-dofile` by run-scratch3d-film.sh. It is not a check and it is
-- in no gate. `platform/playstation/harness/playstation_probe.lua` is the one
-- that decides anything, and this deliberately does not copy its measurements:
-- a scratch that graded its own pictures would be two claims where the point is
-- to produce one picture.
--
-- The mechanism is the render harness's, unchanged and for the same reason.
-- `PCSX.execSlots[13]` is called from inside the store instruction that writes
-- to 0x1f802081, so the guest names the instant with the picture finished and
-- nothing here waits a guessed number of frames. The guest fires it once per
-- still and once per filmed frame, in order, and prints a `SHOT` line saying
-- which ordinal is which; this script only has to number what it is given.
--
-- `PCSX.GPU.takeScreenShot()` crops VRAM to the active display, so a pixel here
-- is a pixel the renderer computed. It is implemented on the software
-- rasteriser only, which is why the run script passes -softgpu.

local SLOT = 13

local out_dir = os.getenv('GRANDLEON_SCRATCH3D_FRAMES') or '/out'
local taken = 0
local width = 0
local height = 0

local function capture()
  local shot = PCSX.GPU.takeScreenShot()
  if taken == 0 then
    width = tonumber(shot.width)
    height = tonumber(shot.height)
    print(string.format('OBSERVER frame size %dx%d', width, height))
  end
  local path = string.format('%s/frame-%04d.bin', out_dir, taken)
  local file = Support.File.open(path, 'TRUNCATE')
  file:writeMoveSlice(shot.data)
  file:close()
  taken = taken + 1
end

PCSX.execSlots[SLOT] = function()
  local ok, err = pcall(capture)
  if not ok then
    print('OBSERVER FAILED ' .. tostring(err))
    PCSX.quit(1)
  end
end

print('OBSERVER installed on execSlot ' .. SLOT .. ', writing to ' .. out_dir)
