-- Actual controller input and read-only observation of the compiled castle.
-- No unit, camera, visibility, simulation or timing state is edited.
local ffi = require('ffi')
local function pointer(name, kind)
  local address = assert(tonumber(os.getenv(name)), name)
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local campaign = pointer('GRANDLEON_CAMPAIGN_MODE_ADDRESS', 'uint8_t*')
local menu = pointer('GRANDLEON_MENU_ADDRESS', 'uint32_t*')
local moment = pointer('GRANDLEON_MOMENT_ADDRESS', 'uint8_t*')
local cursor = pointer('GRANDLEON_CURSOR_ADDRESS', 'int16_t*')
local revealed = pointer('GRANDLEON_REVEALED_ADDRESS', 'uint8_t*')
local cells = pointer('GRANDLEON_COVER_CELLS_ADDRESS', 'uint16_t*')
local count = pointer('GRANDLEON_COVER_COUNT_ADDRESS', 'uint32_t*')
local camera_min_x = pointer('GRANDLEON_CAMERA_MIN_X', 'int32_t*')
local camera_max_x = pointer('GRANDLEON_CAMERA_MAX_X', 'int32_t*')
local camera_min_z = pointer('GRANDLEON_CAMERA_MIN_Z', 'int32_t*')
local camera_max_z = pointer('GRANDLEON_CAMERA_MAX_Z', 'int32_t*')
local per_cell = pointer('GRANDLEON_PER_CELL_ADDRESS', 'uint32_t*')
local players = tonumber(os.getenv('GRANDLEON_ROOM_PLAYERS'))
local frame, start = 0, nil
local capture, motion_due, motion_index = nil, false, 0
local initial_x=assert(tonumber(os.getenv('GRANDLEON_INITIAL_X')))
local initial_y=assert(tonumber(os.getenv('GRANDLEON_INITIAL_Y')))
local rowan_x=players==2 and 6 or 8
local actions, at={}, 60
local function queue(button)
  actions[at]=button;at=at+10
end
for i=1,math.abs(rowan_x-initial_x) do queue(rowan_x>initial_x and 0x0020 or 0x0080) end
for i=1,math.abs(5-initial_y) do queue(5>initial_y and 0x0040 or 0x0010) end
queue(0x4000)
for i=1,10-rowan_x do queue(0x0020) end
local move_at=at
queue(0x4000)
-- Complete Rowan's action normally before moving the board cursor away.
actions[move_at+240]=0x4000
for i=0,31 do actions[move_at+270+i*6]=0x0020 end
for i=0,23 do actions[move_at+480+i*6]=0x0040 end

local function update()
  frame=frame+1
  local held=0
  if campaign[0] ~= 5 then
    if frame % 15 == 10 then
      if campaign[0] == 1 and menu[0] ~= players-1 then held=0x0040
      elseif campaign[0] == 2 then held=0x0008
      else held=0x4000 end
    end
  elseif moment[24] ~= 0 then
    if frame % 15 == 10 then held=0x4000 end
  else
    if not start then
      assert(per_cell[0]==1 and count[0]==170, 'actual modular castle must have 170 individual roof cells')
      assert(cursor[0]==initial_x and cursor[1]==initial_y, 'actual authored army start')
      for i=0,count[0]-1 do assert(revealed[i]==0, 'rooms must begin covered') end
      start=frame
      print('MIPSYNC_MODULAR_CASTLE_CLOSED cells=170 players='..players)
    end
    local elapsed=frame-start
    held=actions[elapsed] or 0
    if elapsed==40 then capture='closed' end
    if elapsed==move_at-1 then assert(cursor[0]==10 and cursor[1]==5, 'ordinary cursor navigation reaches the guardhouse') end
    if elapsed==move_at+220 then
      local seen=0
      for i=0,count[0]-1 do
        local cell=tonumber(cells[i])
        local expected=math.abs(cell%32-10)+math.abs(math.floor(cell/32)-5)<=2
        assert((revealed[i]~=0)==expected, 'reveal must follow the authored radius and leave other rooms covered')
        if revealed[i]~=0 then seen=seen+1 end
      end
      assert(seen==9, 'legal guardhouse entry reveals nine local cells')
      print('MIPSYNC_MODULAR_CASTLE_PASS explored='..seen..' total=170 players='..players)
      capture='entry'
    end
    if elapsed==move_at+700 then
      assert(cursor[0]==31 and cursor[1]==23, 'larger map cursor clamps at its authored southeast corner')
      assert(camera_max_x[0]-camera_min_x[0]>=20, 'camera must actually scroll horizontally')
      assert(camera_max_z[0]-camera_min_z[0]>=12, 'camera must actually scroll vertically')
      local seen=0
      for i=0,count[0]-1 do if revealed[i]~=0 then seen=seen+1 end end
      assert(seen==9, 'moving only the cursor must not uncover other rooms')
      print('MIPSYNC_MODULAR_SCROLL_PASS cursor=31,23 explored=9 players='..players)
      capture='far-corner'
    end
    if elapsed>=40 and elapsed<=move_at+220 and elapsed%2==0 then motion_due=true end
  end
  assert(frame<2000, 'castle input scenario timed out')
  PCSX.getRegisters().GPR.n.a0=held
  PCSX.getRegisters().GPR.n.a1=0
  return true
end
grandleon_room_update=PCSX.addBreakpoint(assert(tonumber(os.getenv('GRANDLEON_UPDATE_ADDRESS'))),
  'Exec',4,'Authored larger castle entry',function()
    local ok,message=xpcall(update,debug.traceback)
    if not ok then print('MIPSYNC_MODULAR_CASTLE_FAIL '..message);PCSX.quit(1) end
    return true
  end)
grandleon_room_vsync=PCSX.Events.createEventListener('GPU::Vsync',function()
  if motion_due then
    motion_due=false
    local shot=PCSX.GPU.takeScreenShot()
    local out=Support.File.open(string.format('/out/frame-%04d.bin',motion_index),'TRUNCATE')
    out:writeMoveSlice(shot.data); out:close(); motion_index=motion_index+1
  end
  if capture then
    local shot=PCSX.GPU.takeScreenShot()
    assert(shot.data.size==153600, 'complete 320 by 240 framebuffer')
    local out=Support.File.open('/out/castle-'..capture..'.bin','TRUNCATE')
    out:writeMoveSlice(shot.data);out:close()
    local done=capture=='far-corner';capture=nil
    if done then PCSX.quit(0) end
  end
end)
