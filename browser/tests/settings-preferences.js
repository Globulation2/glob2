// Graphics effects are persisted independently; optionFlags no longer carries
// the legacy low-detail bit. Read each effect so durability tests still detect
// a lost graphics change or an unintended change to another effect.
const effectNames = [
  'clouds', 'cloudShadows', 'buildingParticles', 'fullMagicEffects',
  'translucentPanels', 'translucentPathLines', 'smoothProgressIndicators',
  'decorativeAnimations',
];
const lowPreferences = {optionFlags:0, mute:1, effects:Object.fromEntries(effectNames.map(name=>[name,0]))};
const cloudPreferences = {...lowPreferences, effects:{...lowPreferences.effects,clouds:1}};
const preferences = page=>page.evaluate(names=>{
  const path='/home/web_user/.glob2/preferences.txt';
  if (!FS.analyzePath(path).exists) return null;
  const text=FS.readFile(path,{encoding:'utf8'});
  const value=name=>{
    const match=text.match(new RegExp('^'+name+'=(\\d+)$','m'));
    return match ? Number(match[1]) : null;
  };
  return {optionFlags:value('optionFlags'),mute:value('mute'),effects:Object.fromEntries(names.map(name=>[name,value(name)]))};
},effectNames);
module.exports={preferences,lowPreferences,cloudPreferences};
