#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ============================================================================
// HARDWARE CONFIGURATION & PIN DEFINITIONS
// ============================================================================

// --- OLED Display Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  // Shared reset with Arduino or unused

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- DFPlayer Mini (Audio Module) ---
SoftwareSerial mySoftwareSerial(11, 12); // RX = Pin 11, TX = Pin 12
DFRobotDFPlayerMini myDFPlayer;

// --- HC-SR04 Ultrasonic Sensor (Front) ---
#define TRIG_PIN   9
#define ECHO_FRONT 10

// --- PIR Motion Sensor ---
#define PIR_PIN    6

// --- TB6612FNG Dual Motor Driver ---
#define AIN1 2  // Left Motor Direction 1
#define AIN2 3  // Left Motor Direction 2
#define BIN1 4  // Right Motor Direction 1
#define BIN2 5  // Right Motor Direction 2

// ============================================================================
// SYSTEM THRESHOLDS & CONSTANTS
// ============================================================================

#define DISTANCE_SURPRISED 10  // Distance threshold to trigger surprised face (cm)
#define OBSTACLE_THRESHOLD 15  // Distance threshold to avoid obstacles (cm)

const int AI_FACE_DURATION       = 3000; // Duration to hold an AI facial expression (ms)
const int UNBLOCK_HAPPY_DURATION = 2000; // Duration to show happy face after camera is unblocked (ms)
const unsigned long soundCooldown = 5000; // Minimum interval between alarm sounds (ms)
const unsigned long debounce_delay = 200; // PIR sensor debounce interval (ms)

// ============================================================================
// GLOBAL STATE VARIABLES & TIMERS
// ============================================================================

// --- Distance Measurement ---
long distFront = 0; // Front distance in centimeters

// --- Eye Blinking Animation Timers ---
unsigned long previousMillis = 0;
unsigned long blinkInterval  = 2000; // Dynamic random interval between blinks (ms)
bool isBlinking              = false;
unsigned long blinkStartTime = 0;

// --- AI State Machine (Commands from Host/Python) ---
// States: 'N' = Normal, 'S' = Suspect detected, 'O' = Owner recognized, 'C' = Camera blocked
char aiState          = 'N';
char lastAiState      = 'N';
unsigned long aiStartTime = 0;

// --- Camera Unblock Reaction State ---
bool showUnblockHappy           = false;
unsigned long unblockHappyStartTime = 0;

// --- Camera Blocked Animation Timer ---
unsigned long blockedAnimStartTime = 0;

// --- Sound Playback Cooldown Timer ---
unsigned long lastSoundTime = 0;

// --- PIR Debounce & Motion Tracking ---
int stable_pir_state              = LOW;
unsigned long last_pir_change_time = 0;
bool motion_sent                  = false;

// ============================================================================
// SETUP FUNCTION
// ============================================================================
void setup() {
  Serial.begin(9600);           // Primary serial communication (Host / AI backend)
  mySoftwareSerial.begin(9600); // Software serial for DFPlayer Mini

  // Initialize Sensor Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_FRONT, INPUT);
  pinMode(PIR_PIN, INPUT);

  // Initialize Motor Driver Pins
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  stopRobot(); // Keep the robot stationary on startup

  // Initialize OLED Display (I2C address 0x3C)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Halt execution if display fails
  }
  display.clearDisplay();
  display.display();

  // Seed random number generator with noise from unconnected analog pin
  randomSeed(analogRead(0));

  // Initialize DFPlayer Mini
  if (myDFPlayer.begin(mySoftwareSerial)) {
    myDFPlayer.volume(25); // Set volume (range: 0 - 30)
  }
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  unsigned long currentMillis = millis();

  // --------------------------------------------------------------------------
  // 1. PIR Motion Detection (with Debounce)
  // --------------------------------------------------------------------------
  int reading = digitalRead(PIR_PIN);

  if (reading != stable_pir_state) {
    if ((currentMillis - last_pir_change_time) > debounce_delay) {
      stable_pir_state = reading;

      // Notify host only on the rising edge (motion newly detected)
      if (stable_pir_state == HIGH && !motion_sent) {
        Serial.println("MOTION");
        motion_sent = true;
      } else if (stable_pir_state == LOW) {
        motion_sent = false; // Reset trigger flag when motion clears
      }
    }
  } else {
    last_pir_change_time = currentMillis;
  }

  // --------------------------------------------------------------------------
  // 2. Serial Command Processing & Audio Alert Handling
  // --------------------------------------------------------------------------
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();

    if (incomingChar == 'S' || incomingChar == 's') {
      // 'S' - Suspect detected: trigger angry face and alarm sound
      aiState = 'S';
      aiStartTime = currentMillis;

      if (currentMillis - lastSoundTime >= soundCooldown) {
        myDFPlayer.play(1); // Play track 0001 (Alarm sound)
        lastSoundTime = currentMillis;
      }
    } 
    else if (incomingChar == 'O' || incomingChar == 'o') {
      // 'O' - Owner recognized: trigger happy face
      aiState = 'O';
      aiStartTime = currentMillis;
    } 
    else if (incomingChar == 'C' || incomingChar == 'c') {
      // 'C' - Camera covered/blocked: trigger restless searching eyes
      if (aiState != 'C') {
        blockedAnimStartTime = currentMillis;
      }
      aiState = 'C';
      aiStartTime = currentMillis;
    } 
    else if (incomingChar == 'N' || incomingChar == 'n') {
      // 'N' - Normal state: return to default behavior
      aiState = 'N';
    }
  }

  // Auto-reset AI expression back to normal after duration elapses
  if (aiState != 'N' && (currentMillis - aiStartTime >= AI_FACE_DURATION)) {
    aiState = 'N';
  }

  // Detect camera unblock event (transition from 'C' to any other state)
  if (lastAiState == 'C' && aiState != 'C') {
    showUnblockHappy = true;
    unblockHappyStartTime = currentMillis;
  }
  lastAiState = aiState;

  // Clear unblock happy face after the duration expires
  if (showUnblockHappy && (currentMillis - unblockHappyStartTime >= UNBLOCK_HAPPY_DURATION)) {
    showUnblockHappy = false;
  }

  // --------------------------------------------------------------------------
  // 3. Distance Measurement (Front Ultrasonic Sensor)
  // --------------------------------------------------------------------------
  distFront = getDistance(ECHO_FRONT);

  // --------------------------------------------------------------------------
  // 4. OLED Display Rendering (Evaluated by Priority)
  // --------------------------------------------------------------------------
  if (aiState == 'C') {
    // Priority 1: Camera blocked - animate eyes glancing side-to-side
    unsigned long animTime = (currentMillis - blockedAnimStartTime) % 5100;
    int currentOffset = 0;

    if (animTime < 2000)      currentOffset = 0;   // Center
    else if (animTime < 2800) currentOffset = -14; // Look left
    else if (animTime < 3300) currentOffset = 0;   // Center
    else if (animTime < 4500) currentOffset = 14;  // Look right
    else if (animTime < 4800) currentOffset = -14; // Quick glance left
    else                      currentOffset = 14;  // Quick glance right

    drawBlockedEyes(currentOffset);
  }
  else if (aiState == 'S') {
    // Priority 2: Suspect detected - show angry expression
    drawAngryFace();
  }
  else if (aiState == 'O') {
    // Priority 3: Owner recognized - show happy expression
    drawHappyFace();
  }
  else if (showUnblockHappy) {
    // Priority 4: Camera recently unblocked - show relieved/happy expression
    drawHappyFace();
    previousMillis = currentMillis;
    isBlinking = false;
  }
  else if (distFront > 0 && distFront < DISTANCE_SURPRISED) {
    // Priority 5: Close obstacle - show surprised expression
    drawSurprisedEyes();
    previousMillis = currentMillis;
    isBlinking = false;
  }
  else {
    // Priority 6: Idle state - natural blinking animation
    if (isBlinking) {
      if (currentMillis - blinkStartTime >= 150) { // Blink closed duration: 150 ms
        isBlinking = false;
        previousMillis = currentMillis;
        blinkInterval = random(2000, 5000); // Random delay until next blink
      } else {
        drawBlink();
      }
    } else {
      if (currentMillis - previousMillis >= blinkInterval) {
        isBlinking = true;
        blinkStartTime = currentMillis;
      } else {
        drawNormalEyes();
      }
    }
  }

  // --------------------------------------------------------------------------
  // 5. Robot Navigation & Movement Logic
  // --------------------------------------------------------------------------
  if (aiState != 'N') {
    // Stop movement while reacting to AI states (suspect, owner, camera blocked)
    stopRobot();
  }
  else if (distFront > 0 && distFront < OBSTACLE_THRESHOLD) {
    // Obstacle detected ahead: turn right to bypass
    turnRight();
  }
  else {
    // Path is clear: continue forward
    moveForward();
  }

  delay(50); // Loop pacing delay
}

// ============================================================================
// SENSOR FUNCTIONS
// ============================================================================

/**
 * Measures distance using the HC-SR04 ultrasonic sensor.
 * @param echoPin The digital input pin connected to the sensor's Echo pin.
 * @return Distance in centimeters, or 999 if out of range / timed out.
 */
long getDistance(int echoPin) {
  // Send a clean 10-microsecond trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Read echo pulse duration (timeout after 30,000 us ~= 5 meters)
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999; // Return out-of-range indicator

  // Calculate distance: sound speed = 0.034 cm/us, divided by 2 for round-trip
  return duration * 0.034 / 2;
}

// ============================================================================
// MOTOR CONTROL FUNCTIONS (TB6612FNG Driver)
// ============================================================================

void moveForward() {
  // Both left and right motors rotate forward
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
}

void moveBackward() {
  // Both left and right motors rotate in reverse
  digitalWrite(AIN1, LOW); digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, LOW); digitalWrite(BIN2, HIGH);
}

void turnLeft() {
  // Left motor reverses, right motor moves forward (pivot turn left)
  digitalWrite(AIN1, LOW);  digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
}

void turnRight() {
  // Left motor moves forward, right motor reverses (pivot turn right)
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);  digitalWrite(BIN2, HIGH);
}

void stopRobot() {
  // Disables drive outputs on both motors
  digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW);
}

// ============================================================================
// OLED GRAPHICS & FACIAL EXPRESSIONS
// ============================================================================

/**
 * Draws the default open eyes.
 */
void drawNormalEyes() {
  display.clearDisplay();
  // Left Eye
  display.fillRoundRect(25, 12, 30, 40, 8, SSD1306_WHITE);
  display.fillRoundRect(33, 22, 14, 20, 4, SSD1306_BLACK);
  // Right Eye
  display.fillRoundRect(73, 12, 30, 40, 8, SSD1306_WHITE);
  display.fillRoundRect(81, 22, 14, 20, 4, SSD1306_BLACK);
  display.display();
}

/**
 * Draws eyelids closed for the blinking animation.
 */
void drawBlink() {
  display.clearDisplay();
  display.fillRoundRect(25, 30, 30, 6, 2, SSD1306_WHITE); // Left eyelid slit
  display.fillRoundRect(73, 30, 30, 6, 2, SSD1306_WHITE); // Right eyelid slit
  display.display();
}

/**
 * Draws wide, surprised eyes with small pupils.
 */
void drawSurprisedEyes() {
  display.clearDisplay();
  // Wide open eyes with smaller pupils centered
  display.fillRoundRect(23, 8, 34, 48, 10, SSD1306_WHITE);
  display.fillRoundRect(37, 26, 6, 12, 2, SSD1306_BLACK);
  display.fillRoundRect(71, 8, 34, 48, 10, SSD1306_WHITE);
  display.fillRoundRect(85, 26, 6, 12, 2, SSD1306_BLACK);
  display.display();
}

/**
 * Draws happy smiling eyes and mouth.
 */
void drawHappyFace() {
  display.clearDisplay();
  // Inverted arc eyes (smiling crescents)
  display.fillCircle(35, 25, 14, SSD1306_WHITE);
  display.fillCircle(35, 32, 14, SSD1306_BLACK);
  display.fillCircle(93, 25, 14, SSD1306_WHITE);
  display.fillCircle(93, 32, 14, SSD1306_BLACK);
  // Smile mouth
  display.fillRoundRect(44, 42, 40, 12, 6, SSD1306_WHITE);
  display.display();
}

/**
 * Draws an angry expression with slanted eyebrows.
 */
void drawAngryFace() {
  display.clearDisplay();
  // Slanted sharp eyes
  display.fillTriangle(15, 15, 55, 32, 20, 35, SSD1306_WHITE);
  display.fillTriangle(113, 15, 73, 32, 108, 35, SSD1306_WHITE);
  // Mouth
  display.fillRect(54, 48, 20, 4, SSD1306_WHITE);
  // Eyebrows
  display.drawLine(25, 10, 50, 20, SSD1306_WHITE);
  display.drawLine(103, 10, 78, 20, SSD1306_WHITE);
  display.display();
}

/**
 * Draws glancing/searching eyes with horizontal offset when the camera is covered.
 * @param offsetX Horizontal pixel offset for glancing animation.
 */
void drawBlockedEyes(int offsetX) {
  display.clearDisplay();
  // Outer eye contours
  display.fillRoundRect(14 + offsetX, 14, 42, 36, 12, SSD1306_WHITE);
  display.fillRoundRect(72 + offsetX, 14, 42, 36, 12, SSD1306_WHITE);
  // Upper cutout angles
  display.fillTriangle(-20 + offsetX, 9, 64 + offsetX, 38, 64 + offsetX, 9, SSD1306_BLACK);
  display.fillTriangle(64 + offsetX, 38, 148 + offsetX, 9, 64 + offsetX, 9, SSD1306_BLACK);
  display.fillRect(0, 0, 128, 9, SSD1306_BLACK);
  display.display();
}
