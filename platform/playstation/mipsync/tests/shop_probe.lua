-- SPDX-License-Identifier: MIT
-- Normal controller purchases and a fresh-process memory-card resume.
local ffi, bit = require('ffi'), require('bit')
local modules = dofile('/out/ps1-shop-bindings.lua')
local resume = os.getenv('GRANDLEON_SHOP_RESUME') == '1'
local B={cross=0x4000,circle=0x2000,up=0x10,down=0x40,start=8}
local tonic, focus
local offer_ids={tonic='3d94ac0b9038052f',focus='f984df7ec67d7916'}
local function stock_key(name)
  local key=modules[2].shop_keys[offer_ids[name]]
  assert(key,'missing authored offer identity')
  return key
end
local function num(address, kind)
  assert(address>=0x80000000 and address<0x80200000)
  return tonumber(ffi.cast(kind,PCSX.getMemPtr()+bit.band(address,0x1fffff))[0])
end
local function id(address)
  return string.format('%08x%08x',num(address+4,'const uint32_t*'),num(address,'const uint32_t*'))
end
local function vector(address,stride)
  local a,b=num(address,'const uint32_t*'),num(address+4,'const uint32_t*')
  if a==0 then assert(b==0);return 0,0 end
  assert(b>=a and b<=0x80200000 and (b-a)%stride==0)
  return a,(b-a)/stride
end
local function state(m)
  local L=m.layout
  local S=m.symbols.s_campaign_save+L.CampaignSave.state
  local out={gold=0,bags={},bought={}}
  local a,n=vector(S+L.CampaignState.store,L.InventoryStack.size)
  for i=0,n-1 do
    local at=a+i*L.InventoryStack.size
    if num(at+L.InventoryStack.item+L.ContentRef.category,'const uint32_t*')==17 then
      out.gold=out.gold+num(at+L.InventoryStack.quantity,'const uint32_t*')
    end
  end
  a,n=vector(S+L.CampaignState.units,L.PersistentUnit.size)
  for i=0,n-1 do
    local at=a+i*L.PersistentUnit.size
    local owner=num(at+L.PersistentUnit.id,'const uint32_t*')
    local items,count=vector(at+L.PersistentUnit.carried,L.InventoryStack.size)
    local total=0
    for j=0,count-1 do total=total+num(items+j*L.InventoryStack.size+L.InventoryStack.quantity,'const uint32_t*') end
    out.bags[owner]=total
  end
  a,n=vector(S+L.CampaignState.world,L.WorldFlag.size)
  for i=0,n-1 do
    local at=a+i*L.WorldFlag.size
    out.bought[id(at+L.WorldFlag.key+L.ContentRef.stable_id)]=num(at+L.WorldFlag.value+L.WorldValue.value,'const int64_t*')
  end
  return out
end
local frame,next_input,step=0,20,0
local pending=nil
local function capture(name) pending={name=name,after=frame+4} end
local function update(m)
  frame=frame+1
  assert(frame<2000,'shop probe timed out at '..step)
  if pending and frame>=pending.after then
    local shot=PCSX.GPU.takeScreenShot()
    local file=Support.File.open('/out/'..pending.name..'.bin','TRUNCATE')
    file:writeMoveSlice(shot.data);file:close();pending=nil
  end
  local S,L=m.symbols,m.layout
  local campaign=num(S.s_campaign_mode,'const uint8_t*')
  local mode=num(S.s_mode,'const uint8_t*')
  local held=0
  local function press(button,following) held=button;step=following;next_input=frame+6 end
  if frame>=next_input then
    if m.name=='02' and campaign==10 then
      local current=state(m)
      local page=num(S.s_shop_page,'const uint8_t*')
      local row=num(S.s_shop_index,'const uint16_t*')
      tonic,focus=stock_key('tonic'),stock_key('focus')
      if step<100 then
        if resume then
          assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3,'purchased wallet/bags did not survive restart')
          assert(current.bought[tonic]==2 and current.bought[focus]==1,'saved finite/unlimited counts changed')
          capture('ps1-shop-resumed');step=200;next_input=frame+12
        else
          assert(current.gold==11 and current.bags[1]==2 and current.bags[2]==1,'ordinary battle did not settle exact opening gold and kits')
          assert(page==0 and row==2,'Continue must be safe default')
          capture('ps1-shop-opening');step=100;next_input=frame+12
        end
      elseif step==100 then press(B.down,101)
      elseif step==101 and page==0 and row==0 then press(B.cross,102)
      elseif step==102 and page==1 and row==2 then capture('ps1-shop-recipients');press(B.down,103)
      elseif step==103 and page==1 and row==0 then press(B.cross,104)
      elseif step==104 and page==2 and row==1 then capture('ps1-shop-safe-back');step=1041;next_input=frame+10
      elseif step==1041 and page==2 and row==1 then press(B.up,105)
      elseif step==105 and page==2 and row==0 then press(B.cross,106)
      elseif step==106 and page==0 then
        assert(current.gold==9 and current.bags[1]==3 and current.bought[tonic]==1,'first purchase not atomic')
        capture('ps1-shop-purchased');press(B.down,107)
      elseif step==107 and page==0 then press(B.cross,108)
      elseif step==108 and page==1 then press(B.down,109)
      elseif step==109 and page==1 then press(B.cross,110)
      elseif step==110 and page==2 then press(B.up,111)
      elseif step==111 and page==2 then press(B.cross,112)
      elseif step==112 and page==2 then
        assert(current.gold==9 and current.bags[1]==3 and current.bought[tonic]==1,'full bag spent gold or stock')
        capture('ps1-shop-full');press(B.circle,113)
      elseif step==113 and page==1 then press(B.circle,114)
      elseif step==114 and page==0 then press(B.down,115)
      elseif step==115 and page==0 then press(B.cross,116)
      elseif step==116 and page==1 then press(B.up,117)
      elseif step==117 and page==1 and row==1 then press(B.cross,118)
      elseif step==118 and page==2 then press(B.up,119)
      elseif step==119 and page==2 then press(B.cross,120)
      elseif step==120 and page==0 then
        assert(current.gold==7 and current.bags[2]==2 and current.bought[tonic]==2,'second recipient did not buy last finite copy')
        press(B.down,121)
      elseif step==121 and page==0 then press(B.cross,122)
      elseif step==122 and page==1 then press(B.up,123)
      elseif step==123 and page==1 then press(B.cross,124)
      elseif step==124 and page==2 then press(B.up,125)
      elseif step==125 and page==2 then press(B.cross,126)
      elseif step==126 and page==2 then
        assert(current.gold==7 and current.bags[2]==2 and current.bought[tonic]==2,'sold-out purchase changed state')
        capture('ps1-shop-sold-out');press(B.circle,127)
      elseif step==127 and page==1 then press(B.circle,128)
      elseif step==128 and page==0 then press(B.up,129)
      elseif step==129 and page==0 and row==1 then press(B.cross,130)
      elseif step==130 and page==1 then press(B.up,131)
      elseif step==131 and page==1 then press(B.cross,132)
      elseif step==132 and page==2 then press(B.up,133)
      elseif step==133 and page==2 then press(B.cross,134)
      elseif step==134 and page==0 then
        assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3,'unlimited offer did not give exactly one owned copy')
        assert(current.bought[focus]==1 and current.bought[tonic]==2)
        capture('ps1-shop-final');step=140;next_input=frame+12
      elseif step==140 then print('PS1_SHOP_PURCHASE_PASS gold=4 finite_bought=2 unlimited_bought=1');PCSX.quit(0)
      elseif step==200 then press(B.cross,201)
      end
    elseif resume and step<100 then
      if campaign==0 and step==0 then press(B.down,1)
      elseif campaign==0 then press(B.cross,2)
      else press(B.cross,step) end
    elseif resume and step==201 and m.name=='02' and campaign==5 then
      local current=state(m)
      assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3)
      capture('ps1-shop-next-board');step=202;next_input=frame+12
    elseif resume and step==202 then print('PS1_SHOP_RESUME_PASS gold=4 packs=3,3 stock_not_refilled=1');PCSX.quit(0)
    elseif campaign~=5 then press(B.cross,step)
    elseif num(S.s_result_frames,'const uint32_t*')+num(S.s_camera_return_frames,'const uint32_t*')>0 then
    elseif step==0 and mode==0 then press(B.start,1)
    elseif step==1 and mode==7 then press(B.circle,2)
    elseif step<=2 and mode==0 then press(B.cross,3)
    elseif step==3 and mode==0 then press(B.cross,3)
    elseif step==3 and mode==1 then press(B.cross,4)
    elseif step==4 and mode==2 then press(B.down,5)
    elseif step==5 and mode==2 then press(B.cross,6)
    elseif step==6 and mode==3 then press(B.cross,7)
    end
  end
  PCSX.getRegisters().GPR.n.a0=held;PCSX.getRegisters().GPR.n.a1=0
  return true
end
grandleon_shop_probe_handles={}
for _,m in ipairs(modules) do
  grandleon_shop_probe_handles[#grandleon_shop_probe_handles+1]=PCSX.addBreakpoint(m.symbols.grandleon_runtime_update,'Exec',4,'Observe shop controller input',function()
    local sig=m.signature
    if ffi.string(PCSX.getMemPtr()+bit.band(sig.address,0x1fffff),#sig.code)~=sig.code then return true end
    local ok,result=pcall(update,m)
    if not ok then print('PS1_SHOP_FAIL '..tostring(result));PCSX.quit(1) end
    return ok and result or true
  end)
end
