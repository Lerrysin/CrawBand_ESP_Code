#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== MINIMAL TEST OK ===");
    Serial.println("If you see this, basic Arduino works.");
}

void loop() {
    Serial.println("alive");
    delay(2000);
}
