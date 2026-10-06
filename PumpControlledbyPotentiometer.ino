int switchstate = 1;
int prevswitchstate = 0;
const int switchpin = 23;
const int relaypin = 13;
int ledstate = 1;
const int pumpPin = 25;
const int potPin = 32;
const float refVoltage = 3.3;

const int freq = 5000;
const int channel = 0;
const int resolution = 8;
void setup() {
  pinMode(2, OUTPUT);
  pinMode(switchpin, INPUT_PULLUP);
  pinMode(relaypin, OUTPUT);
  digitalWrite(2, HIGH);
  digitalWrite(relaypin, HIGH);
  //pinMode(pumpPin, OUTPUT);
  pinMode(potPin, INPUT);
  Serial.begin(9600);

  ledcAttachChannel(pumpPin, freq, resolution, channel);
}

void loop() {
  // put your main code here, to run repeatedly:
  int potVal = analogRead(potPin); //0-4095 from ADC
  float voltage = (potVal/4095.0) * refVoltage;
  int dutycycle = map(potVal, 0, 4095, 0, 255);
  
  
  Serial.print("potVal: ");
  Serial.print(potVal);
  Serial.print(", Duty Cycle: ");
  Serial.print(dutycycle);
  Serial.print(", Voltage from Pot");
  Serial.println(voltage);
  
  ledcWrite(pumpPin, dutycycle);
  delay(15);

  switchstate = digitalRead(switchpin);
  if(switchstate != prevswitchstate){
    if(switchstate == LOW){
      ledstate = !ledstate;
    }
    delay(50);
  }
  if(ledstate == 1){
    digitalWrite(2, HIGH);
    digitalWrite(relaypin, HIGH);
  }
  else{
    digitalWrite(2, LOW);
    digitalWrite(relaypin, LOW);
  }
  prevswitchstate = switchstate;

}
