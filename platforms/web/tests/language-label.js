// The label a language shows in the game's language menus. Catalogs listed with
// a leading "*" in texts.incomplete.txt show their "[language incomplete]" name
// (for example "मराठी - अपूर्ण") until their pending translations are reviewed.
const fs = require('node:fs');
const path = require('node:path');

const data = path.resolve(__dirname, '../../../content/data');

exports.languageLabel = code => {
  const incomplete = fs.readFileSync(path.join(data, 'texts.incomplete.txt'), 'utf8')
    .split('\n').includes(`*data/texts.${code}.txt`);
  const lines = fs.readFileSync(path.join(data, `texts.${code}.txt`), 'utf8').split('\n');
  const key = incomplete ? '[language incomplete]' : '[language]';
  const at = lines.indexOf(key);
  if (at < 0) throw new Error(`texts.${code}.txt has no ${key}`);
  return lines[at + 1].trim();
};
