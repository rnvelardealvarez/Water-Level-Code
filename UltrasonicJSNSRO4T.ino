const int trigPin = 5;
const int echoPin = 18;

#define SOUND_SPEED 0.034

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

// Function to read a single distance pulse
float getSingleDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20); // Keep your stable 20us trigger pulse
  digitalWrite(trigPin, LOW);
  
  // Timeout added (26000us) so pulseIn doesn't hang if an echo is missed
  long duration = pulseIn(echoPin, HIGH, 26000); 
  
  if (duration == 0) return 0.0;
  return duration * SOUND_SPEED / 2;
}

void loop() {
  float readings[5];
  
  // 1. Take 5 quick readings
  for (int i = 0; i < 5; i++) {
    readings[i] = getSingleDistance();
    delay(15); // Short delay between pulses to let acoustic echoes die out
  }
  
  // 2. Simple Bubble Sort to arrange readings from smallest to largest
  for (int i = 0; i < 4; i++) {
    for (int j = i + 1; j < 5; j++) {
      if (readings[i] > readings[j]) {
        float temp = readings[i];
        readings[i] = readings[j];
        readings[j] = temp;
      }
    }
  }
  
  // 3. Pick the median (the middle value, which completely ignores the spikes/halves)
  float filteredDistance = readings[2];
  
  // Print results
  if (filteredDistance > 20 && filteredDistance < 450) {
    Serial.print("Filtered Distance (cm): ");
    Serial.println(filteredDistance);
  } else {
    Serial.println("Reading out of stable range (or blind zone)");
  }
  
  delay(1000); // Wait 1 second before the next main loop
}
