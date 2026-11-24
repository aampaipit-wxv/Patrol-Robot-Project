# 🤖 Patrol Robot Project

An autonomous AI-powered smart security patrol robot integrating computer vision, facial recognition, real-time obstacle avoidance, animated OLED facial expressions, and instant cloud notifications via LINE Messenger.

---

## 📋 Table of Contents
- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Key Features](#-key-features)
- [Hardware Components & Pinout](#-hardware-components--pinout)
- [Serial Communication Protocol](#-serial-communication-protocol)
- [Software Requirements & Setup](#-software-requirements--setup)
  - [1. Arduino Firmware Setup](#1-arduino-firmware-setup)
  - [2. Python AI Host Setup](#2-python-ai-host-setup)
  - [3. Configuration & API Keys](#3-configuration--api-keys)
  - [4. Enrolling Owner Faces](#4-enrolling-owner-faces)
- [Running the System](#-running-the-system)
- [Project Structure](#-project-structure)
- [Security Notice](#-security-notice)

---

## 🔍 Overview

The **Patrol Robot Project** is designed for smart surveillance and indoor home/office patrolling. The system operates via two cooperative subsystems:
1. **Host Computer / Edge AI System (Python):** Decodes live camera video (e.g. from an ESP32-CAM), detects human presence with **YOLOv8**, identifies registered owners using **Face Recognition (dlib/128-d encodings)**, and dispatches real-time security alerts with captured snapshots to **LINE Messenger** using **ImgBB**.
2. **Autonomous Mobility & Reaction Subsystem (Arduino Nano):** Manages motor driving (**TB6612FNG**), obstacle avoidance (**HC-SR04**), motion detection (**PIR sensor**), audio alerts (**DFPlayer Mini**), and rendering dynamic expressive eyes on an **SSD1306 OLED display**.

---

## 🏗 System Architecture

```
                      +-----------------------------+
                      |   ESP32-CAM / IP Camera     |
                      +--------------+--------------+
                                     | MJPEG Stream (WiFi)
                                     v
+------------------+         +-------------------------------+
|  PIR Motion Det  |         |   Python AI Host (main.py)    |
|   (Front Range)  |         |-------------------------------|
+--------+---------+         | • YOLOv8 Human Detection      |
         |                   | • Face Recognition Engine     |
         | Hardware Trigger  | • Serial Listener & Controller|
         v                   +---------------+---------------+
+------------------+                         |
|   Arduino Nano   |<==== Serial (USB) =====>+
+--------+---------+  (Commands: S, O, C, N) | Alert Snapshots + Text
         |                                   v
         +---> TB6612FNG Motor Driver    +-------------------------+
         +---> HC-SR04 Ultrasonic       |  ImgBB + LINE API Cloud |
         +---> DFPlayer Mini (Audio)     +-------------------------+
         +---> SSD1306 OLED (Expressions)
```

---

## ✨ Key Features

- **Energy-Efficient Motion-Triggered Scanning:** The Python AI remains in low-overhead standby until the Arduino's PIR motion sensor signals a `MOTION` event over Serial.
- **YOLOv8 Human Presence Detection:** Highly accurate human detection (class 0) configured to spot people even in dim lighting or if face details are initially hidden.
- **Deep Learning Facial Recognition:** Extracts 128-d facial landmark encodings to accurately distinguish between registered family/owners and unknown intruders.
- **Covered Camera / Masked Intruder Logic:** If a human is detected without a recognizable face for 3 consecutive frames, the system automatically flags a security breach.
- **Dynamic OLED Facial Expressions:**
  - **Normal / Blinking:** Idle, natural eye-blinking animation.
  - **Happy Face:** Displayed when an enrolled owner is recognized.
  - **Angry / Alert Face:** Displayed when an intruder is identified.
  - **Surprised Face:** Triggered if an obstacle is dangerously close.
  - **Restless Searching Eyes:** Triggered if the camera view is blocked or obscured.
- **Hardware Audio Alarm:** Plays loud deterrence sirens/audio via DFPlayer Mini.
- **Instant LINE Push Notifications:** Automatically uploads incident snapshots to ImgBB and pushes real-time alert messages to a designated LINE group or user.

---

## 🔌 Hardware Components & Pinout

### Components:
- **Arduino Nano** (Microcontroller)
- **TB6612FNG** (Dual H-Bridge Motor Driver)
- **HC-SR04** (Ultrasonic Distance Sensor)
- **HC-SR501** or Mini **PIR Motion Sensor**
- **0.96" SSD1306 I2C OLED Display (128x64)**
- **DFRobot DFPlayer Mini** + Speaker + MicroSD card
- **ESP32-CAM** or USB Web Camera
- **2WD / 4WD Robot Chassis** with DC Gearmotors & Battery Pack (7.4V - 11.1V LiPo recommended)

### Arduino Nano Wiring Reference:

| Component | Pin Name | Arduino Nano Pin | Note |
|---|---|---|---|
| **HC-SR04** | TRIG | `D9` | Output trigger pulse |
| **HC-SR04** | ECHO | `D10` | Input pulse width |
| **PIR Sensor** | OUT | `D6` | Digital motion input |
| **TB6612FNG** | AIN1 | `D2` | Left Motor Direction 1 |
| **TB6612FNG** | AIN2 | `D3` | Left Motor Direction 2 |
| **TB6612FNG** | BIN1 | `D4` | Right Motor Direction 1 |
| **TB6612FNG** | BIN2 | `D5` | Right Motor Direction 2 |
| **DFPlayer Mini** | RX | `D12` (TX on Arduino)| Connect via 1k resistor |
| **DFPlayer Mini** | TX | `D11` (RX on Arduino)| SoftwareSerial |
| **SSD1306 OLED** | SDA | `A4` | I2C Data |
| **SSD1306 OLED** | SCL | `A5` | I2C Clock |

---

## 📡 Serial Communication Protocol

The Arduino and Python host communicate over bidirectional Serial at **9600 Baud**:

| Direction | Command / Payload | Description |
|---|---|---|
| **Arduino -> Python** | `MOTION` | PIR sensor detected human motion; wakes up AI detection pipeline for 5 seconds. |
| **Python -> Arduino** | `O` | **Owner Identified:** Triggers happy face on OLED and allows robot patrol. |
| **Python -> Arduino** | `S` | **Suspect / Intruder Detected:** Triggers angry face, stops robot, sounds siren. |
| **Python -> Arduino** | `C` | **Camera Blocked:** Robot stops and displays glancing/searching eyes. |
| **Python -> Arduino** | `N` | **Normal / Standby:** Resets display back to default blinking eyes. |

---

## 💻 Software Requirements & Setup

### 1. Arduino Firmware Setup
1. Open the [firmware/patrol_robot/patrol_robot.ino](firmware/patrol_robot/patrol_robot.ino) file in the **Arduino IDE**.
2. Install the following libraries via the Arduino Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
   - `DFRobotDFPlayerMini`
   - `SoftwareSerial` (built-in)
3. Connect your Arduino Nano via USB, select the correct Board and COM Port, then click **Upload**.

---

### 2. Python AI Host Setup
1. Ensure **Python 3.8 - 3.11** is installed.
2. Clone this repository:
   ```bash
   git clone https://github.com/aampaipit-wxv/Patrol-Robot-Project.git
   cd Patrol-Robot-Project
   ```
3. Install required Python packages:
   ```bash
   pip install -r requirements.txt
   ```
   > **Note for `face-recognition` / `dlib` on Windows:**  
   > If `dlib` fails to build, make sure Visual Studio C++ Build Tools and CMake are installed (`pip install cmake`), or use a precompiled wheel.

---

### 3. Configuration & API Keys
Open `src/main.py` and configure the placeholder variables:

```python
# LINE Messaging API Credentials
CHANNEL_TOKEN = "YOUR_LINE_CHANNEL_ACCESS_TOKEN"  # From LINE Developers Console
GROUP_ID      = "YOUR_LINE_GROUP_OR_USER_ID"       # Target LINE group or user ID

# Camera Stream URL
URL_STREAM    = "http://YOUR_ESP32_CAM_IP:81/stream"

# ImgBB API Key for Image Hosting
IMGBB_API_KEY = "YOUR_IMGBB_API_KEY"              # Free key from https://api.imgbb.com/

# Arduino Serial Connection
SERIAL_PORT   = "COM8"                            # Set your Arduino COM port
BAUD_RATE     = 9600
```

---

### 4. Enrolling Owner Faces
1. Take clear portrait photos of authorized individuals.
2. Place the image files into the `owner_faces/` folder:
   ```
   owner_faces/
   ├── Alice.jpg
   ├── Bob.png
   └── Charlie.jpeg
   ```
3. The system will automatically register each face upon launch and use the file name as the display name.

---

## 🚀 Running the System

1. Power on the robot chassis and ensure the Arduino is connected to your host machine via USB.
2. Ensure your ESP32-CAM stream is active and reachable on your local network.
3. Start the AI patrol controller:
   ```bash
   python src/main.py
   ```
4. A surveillance window will open.
   - When motion is detected, the status changes to `[ PIR ACTIVE: SCANNING ]`.
   - If an unrecognized individual is detected, an alert image is uploaded and dispatched to LINE.
   - Press **`q`** in the video window to safely exit and release all ports.

---

## 📁 Project Structure

```
Patrol-Robot-Project/
├── firmware/
│   └── patrol_robot/
│       └── patrol_robot.ino     # Arduino firmware (hardware, sensors, OLED, motors)
├── src/
│   └── main.py                  # Python AI surveillance engine (YOLOv8 + Face ID)
├── owner_faces/
│   └── .gitkeep                 # Directory for enrolled owner profile photos
├── .gitignore                   # Excludes secrets, caches, images, and model weights
├── requirements.txt             # Python dependencies
└── README.md                    # Project documentation
```

---

## 🔒 Security Notice

- **Never commit active API keys, access tokens, or private face photographs to public version control.**
- Use `.gitignore` to prevent secret leakage and keep `.env` or local configuration private.

---

## 📄 License
This project is open-source and available under the [MIT License](LICENSE).
