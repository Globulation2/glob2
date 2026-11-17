// Graphics effects are persisted independently; optionFlags no longer carries
// the legacy low-detail bit. Read each effect so durability tests still detect
// a lost graphics change or an unintended change to another effect.
const effectNames = [
  'clouds', 'cloudShadows', 'buildingParticles', 'fullMagicEffects',
  'translucentPanels', 'translucentPathLines', 'smoothProgressIndicators',
  'decorativeAnimations',
];
// A new browser profile is muted and lets the eight effects follow the device
// tier together, so the detail level (all on, or all off on a Low device)
// depends on the machine running the test, never one effect alone.
const freshPreferences = detail=>({optionFlags:0, mute:1, adaptive:1,
  effects:Object.fromEntries(effectNames.map(name=>[name,detail]))});
// Toggling clouds is the player's own choice: it ends the adaptive default.
const cloudToggled = fresh=>({...fresh, adaptive:0, effects:{...fresh.effects, clouds:1-fresh.effects.clouds}});
const preferences = page=>page.evaluate(names=>{
  const path='/home/web_user/.glob2/preferences.txt';
  if (!FS.analyzePath(path).exists) return null;
  const text=FS.readFile(path,{encoding:'utf8'});
  const value=name=>{
    const match=text.match(new RegExp('^'+name+'=(\\d+)$','m'));
    return match ? Number(match[1]) : null;
  };
  return {optionFlags:value('optionFlags'),mute:value('mute'),adaptive:value('graphicsDetailAdaptive'),effects:Object.fromEntries(names.map(name=>[name,value(name)]))};
},effectNames);
module.exports={preferences,freshPreferences,cloudToggled};
