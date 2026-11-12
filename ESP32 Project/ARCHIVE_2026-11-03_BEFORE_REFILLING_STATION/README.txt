ARCHIVE - what was replaced on 2026-11-03 (the Refilling Station redesign). Nothing here is used by the current system; it is kept so you can go back.

  ESP32_ACS1, ESP32_ACS2, ESP32_ACS_CONFIG_BENCH, ACS_FIRMWARE_PLAN.md
      the two alcohol-mixing refill boards (ACS 1 for Box 1, ACS 2 for Box 3), their config bench and their plan. Replaced by ONE Refilling Station (ESP32_REFILL).
      They still compile with today's Shared_Common.h (the ACS packet definitions are still in that header, marked as old, because Box 1's S3A still refers to them).
  ESP32_S3B_old
      the Box 3 main controller as it was before the rewrite (a copy of Box 1's S3A: ratings, time picker, mist choice, free retry, ACS requests, container checks).
      ESP32_S3B.ino = the original; S3B_Screens.h = recreated from the original text. Needs a Shared_Common.h in the same folder (copy today's: it is a superset).
  ESP32_LOCALSRV_old
      the LocalServer files as they were before the Refilling Station (settings_pages.h with the /acs page and the old Box 3 page, settings_hub.h, index_html.h, script_js.h, ...).
  ONLINE_WEB_before_station
      the monitoring web page as it was before the Refilling Station (percent dials, 13-board flags with ACS 1 / ACS 2).
  Old B2 (Box 3 sensor board with the two container ultrasonics and the water sensors) = ESP32_A2 with DEVICE_B2 as the device id.
