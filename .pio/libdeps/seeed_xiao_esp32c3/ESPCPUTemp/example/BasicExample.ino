#include <ESPCPUTemp.h>

ESPCPUTemp tempSensor;

void setup() {
  Serial.begin(115200);
  delay(100);

  if (tempSensor.begin()) {
    Serial.println("Temperature sensor initialized successfully");
  } else {
    Serial.println("Failed to initialize temperature sensor");
  }
}

void loop() {
  if (tempSensor.tempAvailable()) {
    float temp = tempSensor.getTemp();
    if (!isnan(temp)) {
      Serial.print("CPU Temperature: ");
      Serial.print(temp);
      Serial.println(" °C");
    } else {
      Serial.println("Failed to read temperature");
    }
  }
  delay(5000); // Read every 5 seconds
}
