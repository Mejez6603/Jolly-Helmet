# Dot-sourced by build_hub_pages.ps1: adds the "Screen (touch display)" section (force-refresh button + the per-relay refresh table) to a box page.
# Needs Sub1 (defined by the caller) and refresh_rows.js next to this file.
function AddScreenSection($h, $extra) {
  $sec = '<h2>Screen (touch display)</h2><div class="row"><button class="go" onclick="refreshScreen()">Force refresh the screen</button><span id="rfStatus" class="note"></span></div>' +
         '<div class="note">Restarts the box''s touch screen and redraws the page it should be showing. Use it when the screen is white, garbled or shows an old page. The box already does this by itself whenever the enclosure lock engages' + $extra + ' - this is the manual button for any other time.</div>' +
         '<div class="note"><b>Refresh by itself when a relay switches:</b> tick the relays whose switching dips the supply and turns the screen white. For the locks, ON = unlocked and OFF = locked (the solenoid engaging), so the lock normally has "when it turns OFF" ticked. A refresh restarts the screen for a moment, so tick only what is needed.</div>' +
         '<div class="row"><label>Auto hard refresh every</label><input class="n" id="arf_sec" type="number" min="0" max="60"><label>seconds</label><span class="note">(0 = never, 60 = max)</span><button class="go" onclick="sendAutoRefresh()">Send</button><span id="arfStatus" class="note"></span></div>' +
         '<div class="note">The box restarts its touch screen by itself this often, so a white screen cures itself even when no relay caused it. Every refresh blinks the screen for a moment, and the rating and time-entry screens are skipped (a refresh there would wipe what the customer typed). If the blinking bothers customers, use 10 to 30 seconds, or 0 to turn it off.</div>' +
         '<div id="rfRows"></div>' +
         '<div class="row"><button class="go" onclick="sendRefreshCfg()">Send REFRESH settings</button><button onclick="rfFill(S.refresh||'''')">Reload</button><span id="rfCfgStatus" class="note"></span></div>'
  $h = Sub1 $h '<h2>Maintenance</h2>' ($sec + '<h2>Maintenance</h2>') 'maintenance heading'

  $js = "async function refreshScreen(){const el=document.getElementById('rfStatus');el.className='note';el.textContent='sending...';await q('/refresh');el.className='note ok';el.textContent='sent at '+new Date().toLocaleTimeString()+' - the screen restarts within a second';}"
  $rxApi = [regex]"const API='[^']*';"
  if ($rxApi.Matches($h).Count -ne 1) { throw 'API declaration not found exactly once' }
  $h = $rxApi.Replace($h, [System.Text.RegularExpressions.MatchEvaluator]{ param($mm) $mm.Value + "`n" + $js })

  # the per-relay checkboxes: rows are built from the page's own RN list and kept in step with what the box reports
  $js2 = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'refresh_rows.js'))
  $rxEnd = [regex]'</script>'
  if ($rxEnd.Matches($h).Count -ne 1) { throw 'script end not found exactly once' }
  return $rxEnd.Replace($h, [System.Text.RegularExpressions.MatchEvaluator]{ param($mm) $js2 + '</script>' })
}
