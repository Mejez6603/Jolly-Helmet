"use strict";
/* ==================================================================================================
   CONFIG - the only file you normally need to edit.

   token   You can paste the Blynk device Auth Token here. Do NOT do that on a public web site: anyone who opens the page source gets it,
           and a Blynk token can also WRITE values. Leave it empty and type it once in the page instead (kept only in that browser),
           or open the page once as   index.html#token=YOUR_TOKEN   (the address is cleaned afterwards).
   server  Blynk server: 'blynk.cloud' (default) or fra1 / lon1 / ny3 / sgp1 / blr1 .blynk.cloud
   pollSec How often this page asks Blynk. Only while the tab is open and visible.
   coin    Currency symbol.
   ================================================================================================== */
const CONFIG = {
  token: '',
  server: 'blynk.cloud',
  pollSec: 10,
  coin: '₱',
  // ---- the customer page (status.html) ----
  // publicToken  token of the SECOND, PUBLIC Blynk device that the Online Server writes the customer status to (Online Server setup page, "Public status
  //              device token"). It is meant to be public: it holds nothing but which machine is free, busy or out of order. NEVER put the
  //              private token here. Leave empty to give it in the address instead:  status.html#t=THE_PUBLIC_TOKEN  (that is what the QR code does).
  publicToken: '',
  statusPollSec: 30,                     // how often a customer's page asks Blynk (only while it is open and visible)
  prices: {                              // shown on the customer page, edit to match the Control Center prices
    'Box 1': '15 coins = 5 minutes',
    'Box 2': '10 coins = 3 minutes',
    'Box 3': '15 coins = 5 minutes'
  },
  contact: 'Ask the staff, or tell the owner.'     // "Something wrong?" line on the customer page
};

/* ---- demo mode:  index.html?demo   or   index.html?demo=alerts   (made-up data, no Blynk needed) ---- */
const demoParam = new URLSearchParams(location.search).get('demo');
const DEMO = demoParam !== null;

/* ---- a token in the address (#token=...) is stored and removed from the address bar ---- */
(function takeTokenFromAddress() {
  const m = /[#&?]token=([^&]+)/.exec(location.hash);
  if (m) { store.set('jh_token', decodeURIComponent(m[1])); history.replaceState(null, '', location.pathname + location.search); }
})();

/* ---- the settings in use: CONFIG, overridden by what the Settings window saved in this browser ---- */
const cfg = {
  token: CONFIG.token || store.get('jh_token') || '',
  server: store.get('jh_server') || CONFIG.server,
  pollSec: Number(store.get('jh_poll')) || CONFIG.pollSec
};
