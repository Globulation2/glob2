const base = require('./playwright.config');
module.exports = {...base, testDir:'./visibility', projects:[{name:'chromium-visibility'}]};
