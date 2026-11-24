import cv2
import requests
import numpy as np
import threading
import time
import json
import os
import serial
from ultralytics import YOLO
import face_recognition

# ============================================================================
# SYSTEM CONFIGURATION & API CREDENTIALS
# ============================================================================

# --- LINE Messaging API Configuration ---
# 1. Obtain your Channel Access Token from the LINE Developers Console (Messaging API channel)
CHANNEL_TOKEN = "YOUR_LINE_CHANNEL_ACCESS_TOKEN"

# 2. Set the destination User ID or Group ID where security alerts should be delivered
GROUP_ID = "YOUR_LINE_GROUP_OR_USER_ID"

# --- Video Stream Source (ESP32-CAM / IP Camera) ---
# Set the RTSP/HTTP MJPEG stream URL of your camera module (e.g., http://192.168.1.100:81/stream)
URL_STREAM = "http://YOUR_ESP32_CAM_IP:81/stream"

# --- ImgBB Image Hosting API ---
# Create a free API key at https://api.imgbb.com/ to host alert snapshots for LINE push messages
IMGBB_API_KEY = "YOUR_IMGBB_API_KEY"

# --- Serial Communication (Arduino Interface) ---
# Update SERIAL_PORT according to Device Manager (Windows, e.g. "COM8") or /dev/ttyUSB0 (Linux)
SERIAL_PORT = "COM8"
BAUD_RATE   = 9600

# --- Face Database Directory ---
# Folder path containing reference images of authorized owners/residents
OWNER_FACES_FOLDER = "owner_faces"

# ============================================================================
# ARDUINO SERIAL INITIALIZATION
# ============================================================================

try:
    print(f"[*] Connecting to Arduino on port {SERIAL_PORT}...")
    arduino = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)  # Allow Arduino time to reboot after establishing serial connection
    print("[+] Successfully connected to Arduino!")
except Exception as e:
    print(f"[-] Error: Unable to connect to port {SERIAL_PORT}. Please check connection and COM port number.")
    exit()

# ============================================================================
# OBJECT DETECTION MODEL INITIALIZATION (YOLOv8)
# ============================================================================

print("[*] Loading YOLOv8s object detection model...")
model = YOLO('yolov8s.pt')

# ============================================================================
# KNOWN FACE DATABASE INITIALIZATION
# ============================================================================

known_face_encodings = []
known_face_names = []

def load_owner_faces():
    """
    Loads reference face images from the configured folder and extracts
    128-dimensional facial encodings for identity matching.
    """
    global known_face_encodings, known_face_names
    print(f"[*] Loading owner face profiles from directory: '{OWNER_FACES_FOLDER}'...")
    
    if not os.path.exists(OWNER_FACES_FOLDER):
        os.makedirs(OWNER_FACES_FOLDER)
        return

    for filename in os.listdir(OWNER_FACES_FOLDER):
        if filename.lower().endswith((".jpg", ".png", ".jpeg")):
            try:
                img_path = os.path.join(OWNER_FACES_FOLDER, filename)
                image = face_recognition.load_image_file(img_path)
                encodings = face_recognition.face_encodings(image)
                
                if len(encodings) > 0:
                    known_face_encodings.append(encodings[0])
                    known_face_names.append(os.path.splitext(filename)[0])
                    print(f"  [+] Enrolled face: {os.path.splitext(filename)[0]}")
            except Exception as e:
                pass

load_owner_faces()

# ============================================================================
# ASYNCHRONOUS MJPEG VIDEO STREAM RECEIVER
# ============================================================================

class FastStream:
    """
    Spawns a background worker thread to continuously decode MJPEG streams,
    preventing frame buffer latency and keeping the video feed real-time.
    """
    def __init__(self, url):
        self.url = url
        self.frame = None
        self.running = True
        self.thread = threading.Thread(target=self.fetch_stream, daemon=True)
        self.thread.start()

    def fetch_stream(self):
        while self.running:
            try:
                r = requests.get(self.url, stream=True, timeout=5)
                bytes_data = bytes()
                for chunk in r.iter_content(chunk_size=1024):
                    if not self.running:
                        break
                    bytes_data += chunk
                    
                    # Locate JPEG start (0xFFD8) and end (0xFFD9) markers
                    a = bytes_data.find(b'\xff\xd8')
                    b = bytes_data.find(b'\xff\xd9')
                    if a != -1 and b != -1:
                        if a < b:
                            jpg = bytes_data[a:b+2]
                            bytes_data = bytes()
                            if len(jpg) > 0:
                                img = cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR)
                                if img is not None:
                                    self.frame = img
                        else:
                            bytes_data = bytes_data[a:]
            except Exception as e:
                time.sleep(0.5)

    def get_frame(self):
        return self.frame

# ============================================================================
# CLOUD UPLOAD & LINE ALERT NOTIFICATION
# ============================================================================

def send_line_image(img_frame, msg_text):
    """
    Uploads the captured alert frame to ImgBB and dispatches a push notification
    containing both the image and alert message to LINE Messenger.
    """
    try:
        temp_filename = "temp_alert.jpg"
        cv2.imwrite(temp_filename, img_frame)
        print("[*] Uploading alert snapshot to ImgBB...")

        with open(temp_filename, "rb") as f:
            upload_res = requests.post(
                "https://api.imgbb.com/1/upload",
                data={"key": IMGBB_API_KEY},
                files={"image": f}
            )

        if upload_res.status_code == 200:
            res_json = upload_res.json()
            image_url = res_json["data"]["url"]
            print(f"[+] Snapshot uploaded successfully! URL: {image_url}")

            # Send LINE Push Message
            line_url = 'https://api.line.me/v2/bot/message/push'
            headers = {
                'Content-Type': 'application/json',
                'Authorization': f'Bearer {CHANNEL_TOKEN}'
            }
            data = {
                "to": GROUP_ID,
                "messages": [
                    {
                        "type": "image",
                        "originalContentUrl": image_url,
                        "previewImageUrl": image_url
                    },
                    {
                        "type": "text",
                        "text": msg_text
                    }
                ]
            }

            print("[*] Sending push notification via LINE API...")
            line_res = requests.post(line_url, headers=headers, data=json.dumps(data))

            if line_res.status_code == 200:
                print("[+] LINE alert sent successfully!")
            else:
                print(f"[-] LINE API error. Status: {line_res.status_code}, Details: {line_res.text}")
        else:
            print(f"[-] ImgBB upload failed. Status: {upload_res.status_code}, Details: {upload_res.text}")

    except Exception as e:
        print("[-] System error during notification dispatch:", e)

# ============================================================================
# PIR MOTION LISTENER (SERIAL MONITOR THREAD)
# ============================================================================

ai_active_until = 0  # Timestamp until which active AI surveillance remains enabled

def pir_monitor_serial():
    """
    Monitors incoming serial messages from Arduino in the background.
    When 'MOTION' is received from the PIR sensor, extends the AI scanning window.
    """
    global ai_active_until
    while True:
        try:
            if arduino.in_waiting > 0:
                line = arduino.readline().decode('utf-8').strip()
                if line == "MOTION":
                    print(f"[!] Motion trigger received from PIR via Serial: '{line}'")
                    ai_active_until = time.time() + 5  # Enable active AI scanning for 5 seconds
        except Exception as e:
            pass  # Prevent thread termination on transient serial disconnects
        time.sleep(0.05)  # Yield CPU cycles

threading.Thread(target=pir_monitor_serial, daemon=True).start()

# ============================================================================
# MAIN SURVEILLANCE LOOP
# ============================================================================

print("[+] Smart CCTV system initialized. Monitoring for motion triggers... (Press 'q' to exit)")
stream = FastStream(URL_STREAM)

last_send_time  = 0   # Timestamp of the last LINE alert
no_face_counter = 0   # Track consecutive frames with human detected but no visible face
cooldown        = 10  # Minimum cooldown interval between LINE notifications (seconds)

while True:
    frame = stream.get_frame()
    if frame is not None:
        show_frame = cv2.resize(frame, (640, 480))
        current_time = time.time()

        # --------------------------------------------------------------------
        # 1. PIR Active Window: Perform Human & Facial Recognition
        # --------------------------------------------------------------------
        if current_time < ai_active_until:
            cv2.putText(show_frame, "[ PIR ACTIVE: SCANNING ]", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 165, 255), 2)

            # YOLO Human Detection (class 0 = person, conf=0.40 to capture masked/obscured subjects)
            results = model.predict(show_frame, classes=[0], conf=0.40, verbose=False)
            human_detected = (len(results[0].boxes) > 0)

            unknown_person_detected = False
            found_owner_name = ""
            display_text = "HUMAN (Scanning...)"

            if human_detected:
                # Extend scanning window while human presence is continuously detected
                ai_active_until = current_time + 5

                # Face Detection & Encoding
                rgb_frame = cv2.cvtColor(show_frame, cv2.COLOR_BGR2RGB)
                face_locations = face_recognition.face_locations(rgb_frame)
                face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

                if not face_encodings:
                    no_face_counter += 1
                    # If human is detected for 3 consecutive frames with no visible face, flag as masked intruder
                    if no_face_counter >= 3:
                        unknown_person_detected = True
                        display_text = "INTRUDER! (Face Covered)"
                else:
                    no_face_counter = 0
                    for face_encoding in face_encodings:
                        # Compare face with enrolled owner profiles (tolerance=0.45 for strict matching)
                        matches = face_recognition.compare_faces(known_face_encodings, face_encoding, tolerance=0.45)
                        if True in matches:
                            first_match_index = matches.index(True)
                            found_owner_name = known_face_names[first_match_index]
                        else:
                            unknown_person_detected = True
                            display_text = "UNKNOWN! Intruder"

                # Render Bounding Boxes & Transmit Control Codes to Arduino
                for box in results[0].boxes:
                    x1, y1, x2, y2 = map(int, box.xyxy[0])

                    if found_owner_name:
                        box_color = (0, 255, 0)  # Green for recognized owner
                        final_text = f"Owner: {found_owner_name}"
                        # Transmit 'O' (Owner recognized) to Arduino via Serial
                        try:
                            arduino.write(b'O')
                        except:
                            pass
                    elif unknown_person_detected:
                        box_color = (0, 0, 255)  # Red for intruder / unknown
                        final_text = display_text
                        # Transmit 'S' (Suspect / Intruder detected) to Arduino via Serial
                        try:
                            arduino.write(b'S')
                        except:
                            pass
                    else:
                        box_color = (0, 165, 255)  # Orange for unclassified human presence
                        final_text = display_text

                    cv2.rectangle(show_frame, (x1, y1), (x2, y2), box_color, 2)
                    cv2.putText(show_frame, final_text, (x1, y1 - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, box_color, 2)

                # Trigger LINE Alert for Confirmed Intruders (with Rate Limiting)
                if unknown_person_detected:
                    if current_time - last_send_time > cooldown:
                        print("[!] Intruder detected! Dispatching LINE notification...")
                        img_to_send = cv2.resize(frame, (640, 480))
                        msg = "🚨 Security Alert: AI detected an intruder!"
                        threading.Thread(target=send_line_image, args=(img_to_send, msg), daemon=True).start()
                        last_send_time = current_time
                        no_face_counter = 0
            else:
                no_face_counter = 0

        # --------------------------------------------------------------------
        # 2. Standby Mode: Waiting for Motion Detection
        # --------------------------------------------------------------------
        else:
            cv2.putText(show_frame, "System: Standby (Waiting for PIR)", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        cv2.imshow('Smart CCTV - Face ID', show_frame)

    # Exit on 'q' key press
    if cv2.waitKey(1) & 0xFF == ord('q'):
        stream.running = False
        break

# ============================================================================
# RESOURCE CLEANUP
# ============================================================================

cv2.destroyAllWindows()
try:
    arduino.close()
except:
    pass
