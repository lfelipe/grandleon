-- SPDX-License-Identifier: MIT
-- Normal one-player flow: main/settings, complete authored opening, then Rowan
-- walks (1,2)->(4,1) and previews the legal enemy at (5,1). No game-state writes.
local ffi = require('ffi')
local function ptr(key,kind)
 return ffi.cast(kind,PCSX.getMemPtr()+bit.band(assert(tonumber(os.getenv(key))),0x1fffff))
end
local campaign=ptr('CAMP','uint8_t*')
local mode=ptr('MODE','uint8_t*')
local moment=ptr('MOMENT','uint8_t*')
local menu=ptr('MENU','uint32_t*')
local settings=ptr('SETTINGS','uint32_t*')
local cursor=ptr('CURSOR','int16_t*')
local selected=ptr('SELECTED','uint64_t*')
local frame,battle,capture=0,nil,nil
local function shot(name)
 capture=name
 print(string.format('PS1_UI_STATE name=%s frame=%d campaign=%d mode=%d x=%d y=%d',name,frame,campaign[0],mode[0],cursor[0],cursor[1]))
end
grandleon_ui_update=PCSX.addBreakpoint(assert(tonumber(os.getenv('UPDATE'))),'Exec',4,'Normal production UI comparison',function()
 frame=frame+1
 local held=0
 if frame==20 then assert(campaign[0]==0 and menu[0]==0);shot('main') end
 local title_actions={[30]=0x0010,[50]=0x4000,[70]=0x0040,[90]=0x0040,[130]=0x2000,[150]=0x4000}
 if frame<=150 then held=title_actions[frame] or 0
 elseif campaign[0]~=5 then
  if frame%20==10 then held=campaign[0]==2 and 0x0008 or 0x4000 end
 elseif moment[24]~=0 then
  if frame%20==10 then held=0x4000 end
 else
  if not battle then assert(cursor[0]==1 and cursor[1]==2);battle=frame end
  local elapsed=frame-battle
  local actions={[20]=0x0008,[60]=0x2000,[80]=0x4000,[120]=0x0010,[128]=0x0020,[136]=0x0020,[144]=0x0020,[154]=0x4000,[300]=0x4000}
  held=actions[elapsed] or 0
  if elapsed==40 then assert(mode[0]==7);shot('pause') end
  if elapsed==100 then assert(mode[0]==1 and selected[0]==7543913367842920562ULL);shot('movement') end
  if elapsed==270 then assert(mode[0]==2 and cursor[0]==4 and cursor[1]==1);shot('actions') end
  if elapsed==330 then assert(mode[0]==3 and cursor[0]==5 and cursor[1]==1);shot('targeting') end
  if elapsed==360 then print('PS1_UI_CAPTURE_PASS states=6 players=1 route=1,2:4,1 target=5,1');PCSX.quit(0) end
 end
 if frame==110 then assert(campaign[0]==9 and settings[0]==2);shot('settings') end
 PCSX.getRegisters().GPR.n.a0=held
 PCSX.getRegisters().GPR.n.a1=0
 assert(frame<1600)
 return true
end)
grandleon_ui_vsync=PCSX.Events.createEventListener('GPU::Vsync',function()
 if not capture then return end
 local image=PCSX.GPU.takeScreenShot()
 assert(tonumber(image.width)==320 and tonumber(image.height)==240)
 local file=Support.File.open('/out/'..capture..'.bin','TRUNCATE')
 file:writeMoveSlice(image.data);file:close();capture=nil
end)
