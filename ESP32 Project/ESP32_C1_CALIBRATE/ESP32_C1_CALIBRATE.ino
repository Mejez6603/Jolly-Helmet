#include <TFT_eSPI.h>

// -------------------------------------------------------------
// TEMPORARY sketch - NOT part of the vending machine firmware.
// Flash this to the C1 board ONLY to get fresh touch calibration
// numbers after rotating its TFT 180 (setRotation(3)).
//
// 1. Flash this sketch to C1.
// 2. Open Serial Monitor at 115200 baud.
// 3. Touch the 4 crosshair targets on screen as they appear.
// 4. Copy the printed "uint16_t calData[5] = {...}" line.
// 5. Send that line back - it gets pasted into the real C1.ino
//    (replacing the current calData array), then reflash C1.ino
//    (the real firmware) instead of this sketch.
// -------------------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
uint16_t calData[5];

void setup() {
    Serial.begin(115200);
    delay(300);

    tft.init();
    tft.setRotation(3); // MUST match production (C1.ino now uses rotation 3 - upside-down fix)
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 20);
    tft.println("Touch each crosshair");
    tft.println("as it appears.");

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    Serial.println();
    Serial.println("=======================================================");
    Serial.println("COPY THIS LINE INTO C1.ino (replacing the calData line):");
    Serial.print("    uint16_t calData[5] = { ");
    for (uint8_t i = 0; i < 5; i++) {
        Serial.print(calData[i]);
        if (i < 4) Serial.print(", ");
    }
    Serial.println(" };");
    Serial.println("=======================================================");

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 20);
    tft.println("Calibration done!");
    tft.println("Check Serial Monitor.");
    tft.println("");
    tft.println("Tap anywhere below");
    tft.println("to test touch:");
}

void loop() {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        tft.fillCircle(x, y, 3, TFT_RED);
        Serial.printf("Touch registered at: X=%d, Y=%d\n", x, y);
    }
}
