-- SPDX-License-Identifier: MIT
-- Observe the linked production binary. Only controller-mask arguments change.
local ffi,bit=require('ffi'),require('bit')
local modules=dofile('/out/ps1-inventory-bindings.lua')
local IDs={["focus"]="bf99116e64fbd4c1",["tonic"]="fb6e017d38252d42",["sword"]="4ede5d2ea4cc1edd"}
local weapon_fixture=os.getenv('GRANDLEON_INVENTORY_WEAPON')=='1'
local B={start=0x0008,cross=0x4000,circle=0x2000,up=0x10,right=0x20,down=0x40,left=0x80}
local frame,next_input,step=0,20,0
local pending_capture=nil
local had_module=false
local function ptr(address,kind)
 assert(address>=0x80000000 and address<0x80200000,'outside PS1 RAM')
 return ffi.cast(kind,PCSX.getMemPtr()+bit.band(address,0x1fffff))
end
local function num(address,kind) return tonumber(ptr(address,kind)[0]) end
local function vec(address,stride)
 local a,b=num(address,'const uint32_t*'),num(address+4,'const uint32_t*')
 if a==0 then assert(b==0);return 0,0 end
 assert(b>=a and (b-a)%stride==0 and b<=0x80200000)
 return a,(b-a)/stride
end
local function snapshot(m)
 local L,S=m.layout,m.symbols.s_snapshot
 local out={outcome=num(S+L.EncounterSnapshot.outcome,'const uint8_t*'),
  collecting=num(S+L.EncounterSnapshot.collecting_loot,'const uint8_t*')~=0,
  ground=select(2,vec(S+L.EncounterSnapshot.ground_items,L.GroundItem.size))}
 local a,n=vec(S+L.EncounterSnapshot.units,L.UnitSnapshot.size)
 for i=0,n-1 do
  local u=a+i*L.UnitSnapshot.size
  local x,y=num(u+L.UnitSnapshot.position,'const int16_t*'),num(u+L.UnitSnapshot.position+2,'const int16_t*')
  local identity=string.format('%08x%08x',num(u+L.UnitSnapshot.id+4,'const uint32_t*'),num(u+L.UnitSnapshot.id,'const uint32_t*'))
  if num(u+L.UnitSnapshot.side,'const uint8_t*')==1 then out.enemy_health=num(u+L.UnitSnapshot.health,'const int16_t*') end
  if identity==m.hero_id then
   local first,count=vec(u+L.UnitSnapshot.carried_inventory,L.CarriedStack.size)
   out.pack={};out.slots=0
   for j=0,count-1 do
    local item=first+j*L.CarriedStack.size
    local id=string.format('%08x%08x',num(item+L.CarriedStack.content_id+4,'const uint32_t*'),num(item+L.CarriedStack.content_id,'const uint32_t*'))
    local quantity=num(item+L.CarriedStack.quantity,'const uint16_t*')
    out.pack[id]=quantity;out.slots=out.slots+quantity
   end
   out.weapons=select(2,vec(u+L.UnitSnapshot.weapon_ids,8));out.x=x;out.y=y
  end
 end
 return out
end
local function capture(name) pending_capture=name end
local function update()
 frame=frame+1
 assert(frame<2200,'inventory probe timed out at step '..step)
 local held=0
 local m=nil
 for _,candidate in ipairs(modules) do
  local signature=candidate.signature
  if ffi.string(ptr(signature.address,'const char*'),#signature.code)==signature.code then m=candidate;break end
 end
 if not m and had_module then
  for _,candidate in ipairs(modules) do
   local sig=candidate.signature
   local actual=ffi.string(ptr(sig.address,'const char*'),#sig.code)
   local delta={}
   for i=1,#actual do if actual:byte(i)~=sig.code:byte(i) then delta[#delta+1]=string.format('%d:%02x/%02x',i,actual:byte(i),sig.code:byte(i)) end end
   print('PS1_MODULE_DIFFERENCE module='..candidate.name..' bytes='..table.concat(delta,','))
  end
  local old=modules[1].symbols
  print(string.format('PS1_MODULE_LOST frame=%d step=%d campaign=%d mode=%d result=%d',frame,step,num(old.s_campaign_mode,'const uint8_t*'),num(old.s_mode,'const uint8_t*'),num(old.s_result_frames,'const uint32_t*')))
  error('loaded module signature changed unexpectedly')
 end
 if m then
  had_module=true
  local sym=m.symbols
  local campaign=num(sym.s_campaign_mode,'const uint8_t*')
  local mode=num(sym.s_mode,'const uint8_t*')
  local result=num(sym.s_result_frames,'const uint32_t*')+num(sym.s_camera_return_frames,'const uint32_t*')
  if frame % 200 == 0 then print(string.format('PS1_PACK_OBSERVE frame=%d module=%s campaign=%d mode=%d result=%d step=%d',frame,m.name,campaign,mode,result,step)) end
  if frame>=next_input then
   if weapon_fixture and m.name=='02' and campaign==5 and step<300 then
    local current=snapshot(m)
    assert(current.slots==2 and current.weapons==2 and current.pack[IDs.sword]==1,'original primary did not survive weapon acquisition')
    local L=m.layout
    local first,count=vec(sym.s_campaign_save+L.CampaignSave.state+L.CampaignState.store,L.InventoryStack.size)
    local gold=0
    for index=0,count-1 do
     local at=first+index*L.InventoryStack.size
     if num(at+L.InventoryStack.item+L.ContentRef.category,'const uint32_t*')==17 then gold=gold+num(at+L.InventoryStack.quantity,'const uint32_t*') end
    end
    assert(gold==11,'wallet did not retain earned11gold')
    step=300
   end
   if weapon_fixture and step>=300 and step<400 then
    local current=snapshot(m)
    if step==300 then held=B.start;step=301
    elseif step==301 and mode==0 then held=B.start
    elseif step==301 and mode==7 then capture('ps1-weapon-gold-settled');held=B.circle;step=302
    elseif step==302 and mode==0 then held=B.cross;step=303
    elseif step==303 and mode==0 then held=B.cross
    elseif step==303 and mode==1 then held=B.cross;step=304
    elseif step==304 and mode==2 then capture('ps1-named-weapon-menu');held=B.down;step=305
    elseif step==305 and mode==2 then held=B.cross;step=306
    elseif step==306 and mode==3 then
     assert(current.enemy_health==20,'preview changed target health')
     capture('ps1-long-blade-forecast');step=307;next_input=frame+20
    elseif step==307 and mode==3 then held=B.cross;step=308
    elseif step==308 and result==0 then
     assert(current.enemy_health==8 and num(sym.s_last_result_damage,'const int32_t*')==12,'chosen second weapon did not deal forecast12 damage')
     assert(current.slots==2 and current.weapons==2 and current.pack[IDs.sword]==1,'attacking consumed or replaced original primary')
     print('PS1_ACQUIRED_WEAPON_PASS damage=12 enemy_health=8 primary_retained=1');PCSX.quit(0)
    end
   elseif m.name=='02' and campaign==5 and step~=100 then
    local s=snapshot(m)
    assert(s.slots==2 and s.weapons==0 and s.pack[IDs.tonic]==2 and s.pack[IDs.sword]==nil and s.pack[IDs.focus]==nil,'next stage did not preserve exact owned pack')
    local L=m.layout
    local gold=0
    local first,count=vec(sym.s_campaign_save+L.CampaignSave.state+L.CampaignState.store,L.InventoryStack.size)
    for i=0,count-1 do
     local stack=first+i*L.InventoryStack.size
     if num(stack+L.InventoryStack.item+L.ContentRef.category,'const uint32_t*')==17 then
      gold=gold+num(stack+L.InventoryStack.quantity,'const uint32_t*')
     end
    end
    assert(gold==11,'gold wallet did not survive stage transition')
    print('PS1_WALLET_CHECK gold='..gold)
    print('PS1_PERSONAL_INVENTORY_PASS steps='..step..' next_stage_slots='..s.slots)
    capture('ps1-pack-next-stage');step=100;next_input=frame+10
   elseif step==100 then
    PCSX.quit(0)
   elseif campaign~=5 then held=B.cross
   elseif result>0 or num(sym.s_motion+m.layout.BoardMotion.active,'const uint8_t*')~=0 then
   elseif step==0 and mode==0 then
    local x,y=num(sym.s_cursor,'const int16_t*'),num(sym.s_cursor+2,'const int16_t*')
    if x>1 then held=B.left elseif x<1 then held=B.right elseif y>1 then held=B.down elseif y<1 then held=B.up else held=B.cross;step=1 end
   elseif step==1 and mode==0 then held=B.cross
   elseif step==1 and mode==1 then held=B.cross;step=2
   elseif step==2 and mode==2 then held=B.cross;step=3
   elseif step==3 and mode==10 then capture('ps1-personal-pack');held=B.circle;step=4
   elseif step==4 and mode==2 then held=B.down;step=5
   elseif step==5 and mode==2 then assert(num(sym.s_menu_index,'const uint32_t*')==1);held=B.cross;step=6
   elseif step==6 and mode==3 then held=B.cross;step=7
   elseif step==7 and mode==0 then
    local s=snapshot(m);assert(s.collecting and s.ground==2 and s.slots==2,'missing final enemy loot')
    capture('ps1-ground-loot')
    local x,y=num(sym.s_cursor,'const int16_t*'),num(sym.s_cursor+2,'const int16_t*')
    if x>1 then held=B.left elseif x<1 then held=B.right elseif y>1 then held=B.down elseif y<1 then held=B.up else held=B.cross;step=8 end
   elseif step==8 and mode==1 then held=B.right;step=9
   elseif step==9 and mode==1 then held=B.cross;step=10
   elseif step==10 and mode==2 then held=B.down;step=11
   elseif step==11 and mode==2 then held=B.down;step=12
   elseif step==12 and mode==2 then assert(num(sym.s_menu_index,'const uint32_t*')==2);held=B.cross;step=13
   elseif step==13 and mode==10 then
    assert(snapshot(m).slots==2 and snapshot(m).ground==2,'opening full pack changed ownership')
    capture('ps1-full-pack-exchange');held=B.circle;step=14
   elseif step==14 and mode==2 then held=B.down;step=15
   elseif step==15 and mode==2 then held=B.down;step=16
   elseif step==16 and mode==2 then held=B.cross;step=17
   elseif step==17 and mode==10 then held=weapon_fixture and B.up or B.down;step=171
   elseif step==171 and mode==10 then held=B.cross;step=18
   elseif weapon_fixture and step==18 and mode==2 then
    local current=snapshot(m)
    assert(current.slots==2 and current.weapons==2 and current.pack[IDs.sword]==1,'compatible new weapon replaced the old primary')
    capture('ps1-both-owned-blades');held=B.circle;step=181
   elseif step==181 and mode==1 then held=B.circle;step=182
   elseif step==182 and mode==0 then held=B.start;step=183
   elseif step==183 and mode==7 then capture('ps1-weapon-gold-pending');held=B.cross;step=25
   elseif step==18 and mode==2 then
    local s=snapshot(m);assert(s.slots==2 and s.weapons==0 and s.ground==2 and s.pack[IDs.focus]==1 and s.pack[IDs.tonic]==1 and s.pack[IDs.sword]==nil,'typed exchange/affinity failed')
    capture('ps1-exchanged-pack');held=B.cross;step=19
   elseif step==19 and mode==10 then held=B.down;step=191
   elseif step==191 and mode==10 then held=B.cross;step=20
   elseif step==20 and mode==2 then
    local s=snapshot(m);assert(s.slots==1 and s.ground==3,'one-copy drop failed')
    held=B.down;step=21
   elseif step==21 and mode==2 then held=B.cross;step=22
   elseif step==22 and mode==10 then held=B.cross;step=23
   elseif step==23 and mode==2 then
    local s=snapshot(m);assert(s.slots==2 and s.weapons==0 and s.ground==2,'one-copy pickup failed')
    held=B.up;step=24
   elseif step==24 and mode==2 then held=B.cross;step=25
   elseif step==25 then held=B.cross
   end
   if held~=0 then
    print(string.format('PS1_PACK_INPUT frame=%d step=%d module=%s mode=%d held=%04x',frame,step,m.name,mode,held))
    next_input=frame+6
   end
  end
 end
 PCSX.getRegisters().GPR.n.a0=held
 PCSX.getRegisters().GPR.n.a1=0
 return true
end
grandleon_inventory_probe_handles={}
local addresses={}
grandleon_inventory_probe_handles.updates=addresses
for _,m in ipairs(modules) do
 local address=m.symbols.grandleon_runtime_update
 if not addresses[address] then
  addresses[address]=PCSX.addBreakpoint(address,'Exec',4,'Personal inventory normal controller input',function()
   local okay,value=pcall(update)
   if not okay then print('PS1_PERSONAL_INVENTORY_FAIL '..tostring(value));PCSX.quit(1);return true end
   return value
  end)
 end
end
local captures=PCSX.Events.createEventListener('GPU::Vsync',function()
 if pending_capture then
  local picture=PCSX.GPU.takeScreenShot()
  local file=Support.File.open('/out/'..pending_capture..'.bin','TRUNCATE')
  file:writeMoveSlice(picture.data);file:close();pending_capture=nil
 end
end)
print('PS1_PERSONAL_INVENTORY_PROBE_READY')

grandleon_inventory_probe_handles.captures=captures
