const lang = require('C:/Users/dario/AppData/Local/npm-cache/_npx/e7fba138d43c49e9/node_modules/@usebruno/lang');
const fs = require('fs');
console.log('exports:', Object.keys(lang).join(', '));
const bru = fs.readFileSync('C:/Users/dario/Projects/IndietroTutta/IndietroTutta/bruno/remove-wifi.bru', 'utf8');
const fn = lang.bruToJson || (lang.v2 && lang.v2.bruToJson);
const out = fn(bru);
console.log(JSON.stringify(out, null, 1));
