-- SPDX-License-Identifier: MIT
-- Normal controller inputs only. Breakpoints observe real production calls;
-- the only register writes are the two runtime controller-mask arguments.
local ffi, bit = require('ffi'), require('bit')
local data = dofile('/out/personal-character-bindings.lua')
local phase = os.getenv('GRANDLEON_PERSONAL_PHASE') or 'new'
local players = tonumber(os.getenv('GRANDLEON_PERSONAL_PLAYERS') or '1')
local test_supplies = os.getenv('GRANDLEON_PERSONAL_SUPPLIES') == '1'
assert(phase == 'new' or phase == 'loss' or phase == 'reload')
assert(players == 1 or players == 2)
local U = {browse=0,move=1,action=2,attack=3,result=7,opponent=8,complete=9,
           moment=13,title=14,players=15,story=16,objective=17,aftermath=18,
           save_error=19,name=22,overview=23,rewards=29,supplies=30}
local B = {cross=0x4000,circle=0x2000,triangle=0x1000,start=0x0008,up=0x0010,right=0x0020,down=0x0040,left=0x0080}
local frame,next_input,revision,input_revision = 0,30,0,-1
local observed,stage,actor,ui_output,ui_return,pending_quit = nil,nil,nil,nil,nil,nil
local initial,checks,captured,returns,hooks = {},{casualties=false,required=false,route=false}, {},{},{}
local pending_capture
local supply_step = 0

local function ptr(address, kind)
  assert(address >= 0x80000000 and address < 0x80200000, 'observation outside main RAM')
  return ffi.cast(kind, PCSX.getMemPtr() + bit.band(address, 0x1fffff))
end
local function num(address, kind) return tonumber(ptr(address, kind)[0]) end
local function id(address)
  return string.format('%08x%08x', num(address+4,'const uint32_t*'),num(address,'const uint32_t*'))
end
local function text(address)
  if address == 0 then return '' end
  return ffi.string(ptr(address,'const char*'),math.min(192,0x80200000-address)):match('^[^%z]*')
end
local function vector(address,stride,limit)
  local first,last = num(address,'const uint32_t*'),num(address+4,'const uint32_t*')
  if first == 0 then assert(last == 0); return 0,0 end
  assert(last >= first and (last-first)%stride == 0 and (last-first)/stride <= limit,'invalid observed vector')
  ptr(first,'const uint8_t*'); assert(last <= 0x80200000)
  return first,(last-first)/stride
end
local function snapshot()
  local s, u = stage.layout.EncounterSnapshot,stage.layout.UnitSnapshot
  assert(u.defeat_ends_battle,'linked executable predates personal rules')
  local out = {units={},side=num(stage.snapshot+s.active_side,'const uint8_t*'),
               round=num(stage.snapshot+s.round,'const uint32_t*'),
               outcome=num(stage.snapshot+s.outcome,'const uint8_t*'),objectives={}}
  assert(num(stage.snapshot+s.width,'const uint16_t*')==5 and num(stage.snapshot+s.height,'const uint16_t*')==5)
  local start,count = vector(stage.snapshot+s.units,u.size,16)
  for i=0,count-1 do
    local p=start+i*u.size
    local weapons={}
    local first,n = vector(p+u.weapon_ids,8,8)
    for j=0,n-1 do weapons[#weapons+1]=id(first+j*8) end
    local items = {}
    first,n = vector(p+u.item_counts,2,8)
    for j=0,n-1 do items[#items+1]=num(first+j*2,'const uint16_t*') end
    local unit={id=id(p+u.id),side=num(p+u.side,'const uint8_t*'),
                x=num(p+u.position,'const int16_t*'),y=num(p+u.position+2,'const int16_t*'),
                health=num(p+u.health,'const int16_t*'),maximum=num(p+u.maximum_health,'const int16_t*'),
                acted=num(p+u.has_acted,'const uint8_t*')~=0,
                required=num(p+u.defeat_ends_battle,'const uint8_t*')~=0,
                player=num(p+u.local_player,'const uint8_t*'), weapons=weapons,items=items}
    out.units[#out.units+1]=unit
    for name,authored in pairs(stage.units) do if authored.id==unit.id then out[name]=unit end end
  end
  local o=stage.layout.ObjectiveResult
  start,count=vector(stage.snapshot+s.objectives,o.size,8)
  for i=0,count-1 do out.objectives[#out.objectives+1]=num(start+i*o.size+o.state,'const uint8_t*') end
  return out
end
local function log_check(name)
  print(string.format('PERSONAL_CHARACTER_CHECK phase=%s players=%d stage=%s assertion=%s',phase,players,stage.name,name))
end
local function capture(name)
  if captured[name] or pending_capture then return end
  pending_capture={name=name,after=frame+4}
end
local function fail(reason)
  if pending_quit==nil then print('PERSONAL_CHARACTER_FAIL '..tostring(reason));pending_quit=1 end
end
local function finish()
  print(string.format('PERSONAL_CHARACTER_PASS phase=%s players=%d',phase,players));pending_quit=0
end
local function inspect(s)
  assert(s.Scholar and s.Scholar.health>0,'Scholar must survive')
  assert(#s.Scholar.weapons==1 and s.Scholar.weapons[1]==stage.sword,'personal sword must replace Mage focus across saves')
  if not initial[stage.name] then
    if test_supplies then
      assert(#s.Scholar.items==1 and s.Scholar.items[1]==1,'one assigned starting tonic must remain with Scholar')
      log_check('shared_supply_reached_the_live_battle')
    end
    if stage.name=='I. Casualties' then
      assert(phase=='new' and #s.units==5 and s.Perman and s.Willow and not s.Rhea)
      assert(s.Scholar.maximum==30,'Scholar starts with authored health')
    else
      assert(#s.units==4 and not s.Perman,'permanent casualty must be absent, not a living/default fallback')
      assert(s.Willow and s.Willow.health==s.Willow.maximum and s.Rhea and s.Rhea.health==s.Rhea.maximum,'withdrawn/required heroes must return at full health')
      assert(s.Rhea.required and not s.Scholar.required and not s.Willow.required)
      assert(s.Scholar.maximum==32,'attack and counterattack XP must carry across battles and after reloading')
      log_check('attack_and_counterattack_xp_persist')
    end
    initial[stage.name]=true; actor=nil
    log_check('authored_ids_personal_sword_roster_and_policy')
    capture(stage.name:sub(1,3):gsub('[^IV]','')..'-start')
  end
  if stage.name=='I. Casualties' and s.Perman.health==0 and s.Willow.health==0 and not checks.casualties then
    assert(s.outcome==0,'ordinary casualties must not end the battle')
    checks.casualties=true; log_check('permanent_and_withdraw_defeated_by_real_combat');capture('ordinary-casualties')
  end
  if stage.name=='II. Required Hero' then
    if phase=='new' then
      assert(checks.casualties,'must fight the casualty stage before advancing')
      if not pending_capture then finish() end
    elseif phase=='loss' and s.Rhea.health==0 and not checks.required then
      assert(s.outcome==2,'required hero must lose while other allies live')
      assert(s.Willow.health>0 and s.Scholar.health>0)
      assert(#s.objectives==1 and s.objectives[1]==2,'authored first-side objective must fail')
      checks.required=true;log_check('required_defeat_overrides_live_allies_and_fails_goal');capture('required-defeat')
    end
  elseif stage.name=='III. Return' then
    assert(phase=='loss' or phase=='reload','unexpected recovery route')
    if phase=='loss' then assert(checks.required and checks.route,'must reach recovery through authored defeat story') end
    if not pending_capture then finish() end
  end
end
local function matches(b)
  return ffi.string(ptr(b.address,'const char*'),#b.code)==b.code
end
local function direction(x,y)
  local cx,cy=num(stage.cursor,'const int16_t*'),num(stage.cursor+2,'const int16_t*')
  if cx<x then return B.right elseif cx>x then return B.left elseif cy<y then return B.down elseif cy>y then return B.up end
  return B.cross
end
local function battle_input(s)
  if s.outcome~=0 or s.side~=0 then return 0,1 end
  local ui=observed.mode
  if ui~=U.browse and ui~=U.move and ui~=U.action and ui~=U.attack then return 0,1 end
  if not actor then
    if ui~=U.browse then return 0,1 end
    for _,unit in ipairs(s.units) do
      if unit.side==0 and unit.health>0 and not unit.acted then
        actor={id=unit.id,player=unit.player,attack=stage.name=='I. Casualties' and checks.casualties}
        return B.circle,actor.player -- Normal focus handoff to the correct controller.
      end
    end
    return 0,1
  end
  local unit
  for _,candidate in ipairs(s.units) do if candidate.id==actor.id then unit=candidate end end
  if not unit or unit.health<=0 or unit.acted then actor=nil;return 0,1 end
  local player=actor.player
  if ui==U.browse then
    if id(stage.selected)~='0000000000000000' then return 0,player end -- Wait for real walk animation.
    return direction(unit.x,unit.y),player
  elseif ui==U.move then
    assert(id(stage.selected)==actor.id,'selected another character')
    if actor.attack then return direction(3,2),player end
    return direction(unit.x,unit.y),player
  elseif ui==U.action then
    local row=actor.attack and 0 or observed.count-1
    assert(not actor.attack or observed.count>=2,'personal sword must offer a legal adjacent attack')
    return observed.menu==row and B.cross or B.up,player
  elseif ui==U.attack then
    assert(actor.attack and observed.forecast,'normal attack forecast must be valid')
    return B.cross,player
  end
  return 0,player
end
for _,binding in ipairs(data.ui) do
  hooks[#hooks+1]=PCSX.addBreakpoint(binding.address,'Exec',4,'Observe normal personal-rule UI',function()
    if not matches(binding) then return true end
    local regs=PCSX.getRegisters().GPR.n
    ui_output,ui_return=tonumber(regs.a0),tonumber(regs.ra)
    if not returns[ui_return] then
      local address=ui_return
      returns[address]=PCSX.addBreakpoint(address,'Exec',4,'Observe completed personal-rule UI',function()
        if ui_return==address then
          local value=ptr(ui_output,'const grandleon_ui_state*')[0]
          observed={mode=tonumber(value.mode),menu=tonumber(value.menu_index),count=tonumber(value.menu_count),
                    campaign_menu=tonumber(value.campaign_menu_index),forecast=value.forecast_valid~=0,
                    settled=value.camera_settled~=0,headline=text(tonumber(value.headline)),body=text(tonumber(value.body))}
          revision=revision+1
          if observed.mode==U.objective then
            local found
            for _,entry in ipairs(data.stages) do if entry.name==observed.headline then found=entry end end
            if not found then fail('unexpected authored objective '..observed.headline) else stage=found end
          elseif (observed.mode==U.story or observed.mode==U.aftermath) and observed.body:find('Rhea fell.',1,true) then checks.route=true end
          if observed.headline=='Campaign ended' then fail('wrong authored outcome branch') end
          ui_return=nil
        end
        return true
      end)
    end
    return true
  end)
end
for _,binding in ipairs(data.update) do
  hooks[#hooks+1]=PCSX.addBreakpoint(binding.address,'Exec',4,'Normal hero-rule controller input',function()
    if not matches(binding) then return true end
    frame=frame+1
    local held,player=0,1
    local ok,err=pcall(function()
      if not observed or pending_quit~=nil then return end
      local ui=observed.mode
      assert(ui~=U.save_error,'production memory-card error')
      local s
      if stage and (ui<=U.complete or ui==U.overview) then s=snapshot();inspect(s) end
      if frame<next_input or revision<=input_revision or pending_capture or pending_quit~=nil then return end
      if ui==U.title then
        local row=phase=='new' and 0 or 1
        held=observed.campaign_menu==row and B.cross or B.down
      elseif ui==U.players then held=observed.campaign_menu==players-1 and B.cross or B.down
      elseif ui==U.name then held=B.triangle
      elseif ui==U.rewards then
        assert(phase=='new' and observed.count==1,'victory should show one XP recipient')
        if not captured['battle-rewards'] then
          assert(observed.headline=='BATTLE REWARDS')
          log_check('battle_rewards_screen')
          capture('battle-rewards')
        else held=B.cross end
      elseif ui==U.supplies then
        if test_supplies and phase=='new' and stage==nil and supply_step<4 then
          local steps={B.down,B.cross,B.circle,B.cross}
          assert(observed.count==2,'the store and personal sword should be listed')
          supply_step=supply_step+1;held=steps[supply_step]
        elseif test_supplies and phase=='new' and not captured[stage and 'stage-supplies' or 'starting-supplies'] then
          capture(stage and 'stage-supplies' or 'starting-supplies')
        else held=B.start end
      elseif ui==U.story or ui==U.objective or ui==U.aftermath or ui==U.moment or ui==U.overview then held=B.cross
      elseif s then held,player=battle_input(s) end
    end)
    if not ok then fail(err) end
    local regs=PCSX.getRegisters().GPR.n
    regs.a0=player==1 and held or 0;regs.a1=player==2 and held or 0
    if held~=0 then
      assert(held~=0x8000,'debug completion is forbidden')
      print(string.format('PERSONAL_CHARACTER_INPUT frame=%d player=%d held=%04x',frame,player,held))
      next_input=frame+10;input_revision=revision
    end
    if frame>10000 then fail('normal-input traversal timed out') end
    return true
  end)
end
grandleon_personal_hooks,hooks_returns=hooks,returns
grandleon_personal_capture=PCSX.Events.createEventListener('GPU::Vsync',function()
  if pending_capture and frame>=pending_capture.after then
    local name=pending_capture.name;pending_capture=nil
    local shot=PCSX.GPU.takeScreenShot()
    assert(shot.data.size==320*240*2)
    local file=Support.File.open('/out/'..players..'-'..phase..'-'..name..'.bin','TRUNCATE')
    file:writeMoveSlice(shot.data);file:close();captured[name]=true
  end
  if pending_quit~=nil then PCSX.quit(pending_quit) end
end)
print(string.format('PERSONAL_CHARACTER_READY phase=%s players=%d',phase,players))
