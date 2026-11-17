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
local pending,observed=nil,nil
local function capture(name) pending={name=name,after=frame+4} end
local function update(m)
  frame=frame+1
  assert(frame<2200,'shop probe timed out at '..step)
  if pending and frame>=pending.after then
    local shot=PCSX.GPU.takeScreenShot()
    local file=Support.File.open('/out/'..pending.name..'.bin','TRUNCATE')
    file:writeMoveSlice(shot.data);file:close();pending=nil
  end
  local S=m.symbols
  local campaign=num(S.s_campaign_mode,'const uint8_t*')
  local mode=num(S.s_mode,'const uint8_t*')
  local held,held2=0,0
  local function press(a,b,following) held=a;held2=b;step=following;next_input=frame+8 end
  if frame>=next_input then
    if m.name=='02' and campaign==10 and observed and observed.mode==25 then
      local current=state(m)
      tonic,focus=stock_key('tonic'),stock_key('focus')
      assert(observed.players==2 and observed.members[1]==1 and observed.members[2]==2,'each controller must own its hero')
      if step<100 then
        if resume then
          assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3,'purchased wallet/bags did not survive restart')
          assert(current.bought[tonic]==2 and current.bought[focus]==1,'saved stock changed')
          assert(observed.ready[1]==0 and observed.ready[2]==0,'readiness must reset on resume')
          capture('ps1-shop-coop-resumed');step=200;next_input=frame+12
        else
          assert(current.gold==11 and current.bags[1]==2 and current.bags[2]==1,'ordinary battle opening gold and kits')
          capture('ps1-shop-coop-opening');step=100;next_input=frame+12
        end
      elseif step==100 then press(B.cross,B.cross,101)
      elseif step==101 then
        assert(observed.confirm[1]==1 and observed.confirm[2]==1 and observed.buy[1]==0 and observed.buy[2]==0,'safe Back for both')
        capture('ps1-shop-coop-confirm');step=1011;next_input=frame+12
      elseif step==1011 then press(B.up,B.up,102)
      elseif step==102 then press(B.cross,B.cross,103)
      elseif step==103 then
        assert(current.gold==7 and current.bags[1]==3 and current.bags[2]==2 and current.bought[tonic]==2,'simultaneous owned purchases commit atomically')
        capture('ps1-shop-coop-purchased');press(B.cross,B.cross,104)
      elseif step==104 then press(B.up,B.up,105)
      elseif step==105 then press(B.cross,B.cross,106)
      elseif step==106 then
        assert(current.gold==7 and current.bags[1]==3 and current.bags[2]==2 and current.bought[tonic]==2,'soldout/full refusal must preserve state')
        capture('ps1-shop-coop-refused');press(0,B.down,107)
      elseif step==107 then press(B.start,B.cross,108)
      elseif step==108 then
        assert(observed.ready[1]==1 and observed.ready[2]==0,'P1 cannot leave P2 behind')
        press(0,B.up,109)
      elseif step==109 then press(0,B.cross,110)
      elseif step==110 then
        assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3 and current.bought[focus]==1,'P2 buys while P1 ready')
        assert(observed.ready[1]==1 and observed.ready[2]==0,'P2 buy preserves P1 readiness')
        capture('ps1-shop-coop-final');step=111;next_input=frame+12
      elseif step==111 then print('PS1_SHOP_PURCHASE_PASS players=2 gold=4 independent=1 safe_back=1 ready_gate=1');PCSX.quit(0)
      elseif step==200 then press(B.start,0,201)
      elseif step==201 then
        assert(observed.ready[1]==1 and observed.ready[2]==0,'one Ready must stay in shop');press(0,B.start,202)
      end
    elseif resume and step<100 then
      if campaign==0 and step==0 then press(B.down,0,1)
      elseif campaign==0 then press(B.cross,0,2)
      else press(B.cross,0,step) end
    elseif resume and step==202 and m.name=='02' and campaign==5 then
      local current=state(m)
      assert(current.gold==4 and current.bags[1]==3 and current.bags[2]==3)
      capture('ps1-shop-coop-next-board');step=203;next_input=frame+12
    elseif resume and step==203 then print('PS1_SHOP_RESUME_PASS players=2 gold=4 both_ready=1');PCSX.quit(0)
    elseif campaign==1 and step==0 then press(B.down,0,10)
    elseif campaign==1 and step==10 then press(B.cross,0,0)
    elseif campaign~=5 then press(B.cross,0,step)
    elseif num(S.s_result_frames,'const uint32_t*')+num(S.s_camera_return_frames,'const uint32_t*')>0 then
    elseif step==0 and mode==0 then press(B.start,0,1)
    elseif step==1 and mode==7 then press(B.circle,0,2)
    elseif step<=2 and mode==0 then press(B.cross,0,3)
    elseif step==3 and mode==0 then press(B.cross,0,3)
    elseif step==3 and mode==1 then press(B.cross,0,4)
    elseif step==4 and mode==2 then press(B.down,0,5)
    elseif step==5 and mode==2 then press(B.cross,0,6)
    elseif step==6 and mode==3 then press(B.cross,0,7)
    end
  end
  PCSX.getRegisters().GPR.n.a0=held;PCSX.getRegisters().GPR.n.a1=held2
  return true
end
grandleon_shop_probe_handles={}
local returns={}
for _,m in ipairs(modules) do
  local output,ret
  grandleon_shop_probe_handles[#grandleon_shop_probe_handles+1]=PCSX.addBreakpoint(m.symbols.grandleon_game_ui_state,'Exec',4,'Observe normal shop UI',function()
    local sig=m.signature
    if ffi.string(PCSX.getMemPtr()+bit.band(sig.address,0x1fffff),#sig.code)~=sig.code then return true end
    local regs=PCSX.getRegisters().GPR.n
    output,ret=tonumber(regs.a0),tonumber(regs.ra)
    if not returns[ret] then
      local address=ret
      returns[address]=PCSX.addBreakpoint(address,'Exec',4,'Read completed shop UI',function()
        if ret==address then
          local L=m.layout.grandleon_ui_state
          observed={mode=num(output+L.mode,'const uint8_t*'),players=num(output+L.shop_players,'const uint8_t*'),members={},ready={},confirm={},buy={}}
          for p=1,2 do
            observed.members[p]=num(output+L.shop_members+(p-1)*8,'const uint32_t*')
            observed.ready[p]=num(output+L.shop_ready+p-1,'const uint8_t*')
            observed.confirm[p]=num(output+L.shop_confirming+p-1,'const uint8_t*')
            observed.buy[p]=num(output+L.shop_buy_selected+p-1,'const uint8_t*')
          end
          ret=nil
        end
        return true
      end)
    end
    return true
  end)
  grandleon_shop_probe_handles[#grandleon_shop_probe_handles+1]=PCSX.addBreakpoint(m.symbols.grandleon_runtime_update,'Exec',4,'Observe shop controller input',function()
    local sig=m.signature
    if ffi.string(PCSX.getMemPtr()+bit.band(sig.address,0x1fffff),#sig.code)~=sig.code then return true end
    local ok,result=pcall(update,m)
    if not ok then print('PS1_SHOP_FAIL '..tostring(result));PCSX.quit(1) end
    return ok and result or true
  end)
end
grandleon_shop_return_handles=returns
