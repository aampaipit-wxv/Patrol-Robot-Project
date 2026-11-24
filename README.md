# Patrol Robot Project

An autonomous surveillance robot platform integrating edge computer vision, facial recognition, obstacle avoidance, dynamic OLED facial animations, and cloud-based alert notifications via LINE Messenger.

---

## Table of Contents
- [Project Overview](#project-overview)
- [System Architecture](#system-architecture)
- [Key Features](#key-features)
- [Hardware Specifications and Pinout](#hardware-specifications-and-pinout)
- [Serial Communication Protocol](#serial-communication-protocol)
- [Software Prerequisites and Installation](#software-prerequisites-and-installation)
  - [1. Arduino Firmware Setup](#1-arduino-firmware-setup)
  - [2. Python AI Host Environment](#2-python-ai-host-environment)
  - [3. Configuration and API Parameters](#3-configuration-and-api-parameters)
  - [4. Enrolling Authorized Face Profiles](#4-enrolling-authorized-face-profiles)
- [System Operation](#system-operation)
- [Directory Structure](#directory-structure)
- [Security Guidelines](#security-guidelines)
- [License](#license)

---

## Project Overview

The Patrol Robot Project is an autonomous security robotic system designed for indoor surveillance, perimeter monitoring, and property protection. The platform consists of two primary operational layers:

1. **Host Processing Layer (Python):** Executes real-time video stream ingestion (e.g., from an ESP32-CAM module), human detection utilizing the YOLOv8 deep learning model, and biometric facial recognition using 128-dimensional encodings. When an unauthorized individual or covered face is identified, an alert snapshot is archived to ImgBB and transmitted to a designated LINE group.
2. **Embedded Control Layer (Arduino Nano):** Manages low-level locomotion through a TB6612FNG dual motor driver, distance monitoring via an HC-SR04 ultrasonic sensor, wake-on-motion detection via a PIR sensor, audio deterrence via a DFPlayer Mini module, and state visualization through animated eyes rendered on an SSD1306 OLED display.

---

## System Architecture

```
                      +-----------------------------+
                      |    ESP32-CAM / IP Camera    |
                      +--------------+--------------+
                                     | HTTP MJPEG Stream
                                     v
+------------------+         +-------------------------------+
|  PIR Motion Det  |         |    Python Host (src/main.py)  |
|  (Digital Input) |         |-------------------------------|
+--------+---------+         | - YOLOv8 Person Detection     |
         |                   | - Face Recognition Algorithm  |
         | Hardware Interrupt| - Serial Protocol Engine      |
         v                   +---------------+---------------+
+------------------+                         |
|   Arduino Nano   |<==== Bidirectional =====>+
+--------+---------+        Serial (USB)     | Cloud Alert Snapshots
         |                                   v
         +---> TB6612FNG Motor Driver    +-------------------------+
         +---> HC-SR04 Ultrasonic       | ImgBB & LINE Messaging  |
         +---> DFPlayer Mini (Audio)     +-------------------------+
         +---> SSD1306 OLED (Display)
```

---

## Key Features

- **Event-Driven Power Management:** The surveillance pipeline remains in a low-power standby state until the hardware PIR sensor signals physical motion.
- **YOLOv8 Deep Learning Detection:** Accurately classifies human presence with confidence threshold tuning for partially obscured subjects.
- **Biometric Identity Verification:** Compares detected facial landmarks against a pre-registered database of authorized individuals using Euclidean distance metrics (tolerance set to 0.45).
- **Masked Intruder Detection:** Automatically raises an alarm if a human silhouette is tracked across multiple frames without a visible facial structure.
- **OLED State Visualization:**
  - *Default:* Natural eye blinking animation.
  - *Owner Recognized:* Displays a positive confirmation face.
  - *Intruder Detected:* Displays an alert warning expression.
  - *Obstacle Warning:* Displays wide surprised eyes when an obstacle is within 10 cm.
  - *Camera Obscured:* Displays shifting glance animation to indicate vision obstruction.
- **Hardware Deterrence:** Directly controls an audio module to sound alarms upon security breaches.
- **Instant Cloud Telemetry:** Transmits compressed image evidence and incident logs to LINE Messenger.

---

## Hardware Specifications and Pinout

### Required Components
- Microcontroller: Arduino Nano (ATmega328P)
- Motor Driver: TB6612FNG Dual H-Bridge
- Vision Module: ESP32-CAM or USB Video Class (UVC) Camera
- Ultrasonic Sensor: HC-SR04
- Passive Infrared Sensor: HC-SR501 or Mini PIR
- Audio Player: DFRobot DFPlayer Mini + 3W Speaker + MicroSD Card
- Graphic Display: 0.96-inch SSD1306 I2C OLED (128x64 pixels)
- Chassis: 2WD or 4WD Differential Drive Robotic Chassis with DC Gearmotors

### Pin Connection Table

| Peripheral | Peripheral Pin | Arduino Nano Pin | Notes |
|---|---|---|---|
| HC-SR04 Ultrasonic | TRIG | D9 | Trigger Pulse (10 microseconds) |
| HC-SR04 Ultrasonic | ECHO | D10 | Echo Pulse Duration |
| PIR Motion Sensor | OUT | D6 | Digital Logic High on Motion |
| TB6612FNG Driver | AIN1 | D2 | Left Motor Direction Control 1 |
| TB6612FNG Driver | AIN2 | D3 | Left Motor Direction Control 2 |
| TB6612FNG Driver | BIN1 | D4 | Right Motor Direction Control 1 |
| TB6612FNG Driver | BIN2 | D5 | Right Motor Direction Control 2 |
| DFPlayer Mini | RX | D12 (Arduino TX) | Series 1k ohm resistor recommended |
| DFPlayer Mini | TX | D11 (Arduino RX) | SoftwareSerial connection |
| SSD1306 OLED | SDA | A4 | Hardware I2C Data Line |
| SSD1306 OLED | SCL | A5 | Hardware I2C Clock Line |

---

## Serial Communication Protocol

The host and embedded system communicate over USB Serial interface at **9600 Baud (8-N-1)**:

| Direction | Command | Description |
|---|---|---|
| Arduino to Host | `MOTION` | Transmitted when the PIR sensor detects movement; activates the vision processing window for 5 seconds. |
| Host to Arduino | `O` | Owner Verified: Robot renders confirmation face and proceeds with forward locomotion. |
| Host to Arduino | `S` | Suspect Detected: Robot halts locomotion, sounds the audio siren, and renders alert expression. |
| Host to Arduino | `C` | Vision Blocked: Robot halts and renders searching eye animation. |
| Host to Arduino | `N` | Normal / Idle: Reverts OLED display to regular eye blinking state. |

---

## Software Prerequisites and Installation

### 1. Arduino Firmware Setup
1. Launch the **Arduino IDE**.
2. Install the necessary libraries via the Library Manager:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
   - `DFRobotDFPlayerMini`
3. Open `firmware/patrol_robot/patrol_robot.ino`.
4. Select the target board (`Arduino Nano`) and processor (`ATmega328P` or `ATmega328P (Old Bootloader)`), then upload the sketch.

### 2. Python AI Host Environment
1. Ensure Python 3.8 to 3.11 is installed on the host system.
2. Clone the repository:
   ```bash
   git clone https://github.com/aampaipit-wxv/Patrol-Robot-Project.git
   cd Patrol-Robot-Project
   ```
3. Install package dependencies:
   ```bash
   pip install -r requirements.txt
   ```
   *Note:* The `face-recognition` module requires CMake and C++ build tools to compile `dlib`. Precompiled wheels may be used on Windows if compilation is not configured.

### 3. Configuration and API Parameters
Edit `src/main.py` and supply your deployment values:

```python
# LINE Messaging API Credentials
CHANNEL_TOKEN = "YOUR_LINE_CHANNEL_ACCESS_TOKEN"  # Generated from LINE Developers Console
GROUP_ID      = "YOUR_LINE_GROUP_OR_USER_ID"       # Target recipient or group identifier

# Video Stream URL
URL_STREAM    = "http://YOUR_ESP32_CAM_IP:81/stream"

# ImgBB API Key
IMGBB_API_KEY = "YOUR_IMGBB_API_KEY"              # Acquired from https://api.imgbb.com/

# Serial Interface
SERIAL_PORT   = "COM8"                            # Target serial port (Windows: COMx, Linux: /dev/ttyUSBx)
BAUD_RATE     = 9600
```

### 4. Enrolling Authorized Face Profiles
1. Place clear frontal portrait images (.jpg, .jpeg, or .png format) into the `owner_faces/` folder.
2. Format file names with the person's identity (for example: `John.jpg`, `Sarah.png`).
3. The system will encode and index all images in this folder at initialization.

---

## System Operation

1. Connect the Arduino Nano to the host system via USB.
2. Ensure the camera module is streaming on the local network.
3. Start the host application:
   ```bash
   python src/main.py
   ```
4. Operation workflow:
   - The application stays in standby until motion is detected.
   - Upon a motion trigger, YOLOv8 scans the frame for human presence.
   - Detected faces are matched against authorized profiles.
   - Unrecognized subjects trigger a cloud push notification and immediate robot halt with audible alarm.
   - Press `q` while focused on the video window to terminate the application cleanly.

---

## Directory Structure

```
Patrol-Robot-Project/
|-- firmware/
|   `-- patrol_robot/
|       `-- patrol_robot.ino      # Microcontroller firmware
|-- src/
|   `-- main.py                   # Host vision and control script
|-- owner_faces/
|   `-- .gitkeep                  # Authorized facial image directory
|-- .gitignore                    # Version control exclusion rules
|-- requirements.txt              # Python library dependencies
`-- README.md                     # Technical documentation
```

---

## Security Guidelines

- Never commit active API keys, personal authentication tokens, or private biometric images to public repositories.
- Use `.gitignore` to prevent confidential local configurations and cache files from being tracked.

---

## License

This project is licensed under the MIT License.
