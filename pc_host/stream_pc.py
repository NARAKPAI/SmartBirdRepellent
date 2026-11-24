import cv2
import serial
import numpy as np
import threading
import time

COM_PORT = "COM8"
BAUD_RATE = 921600
IMG_SIZE = 224

# Shared state ระหว่าง Thread แสดงผลกล้อง กับ Thread ส่งข้อมูลเข้า ESP32-S3
lock = threading.Lock()
latest_payload = None
latest_boxes = []
esp_fps = 0.0
running = True

def serial_worker():
    global latest_boxes, esp_fps, running
    ser = serial.Serial()
    ser.port = COM_PORT
    ser.baudrate = BAUD_RATE
    ser.timeout = 0.05
    ser.write_timeout = 1.0
    ser.dsrdtr = False
    ser.rtscts = False
    ser.open()
    ser.dtr = False
    ser.rts = False
    time.sleep(0.5)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    last_infer_time = time.time()
    

    while running:
        payload_to_send = None
        with lock:
            if latest_payload is not None:
                payload_to_send = latest_payload
                # เคลียร์ทิ้งทันทีเพื่อไม่ให้ส่งเฟรมซ้ำ รอเฟรมใหม่ล่าสุดจากกล้องเท่านั้น
                globals()['latest_payload'] = None

        if payload_to_send is None:
            time.sleep(0.005)
            continue

        try:
            ser.write(payload_to_send)
            ser.flush()
        except serial.SerialTimeoutException:
            ser.reset_output_buffer()
            continue

        # รออ่านผลลัพธ์ของเฟรมนี้จาก ESP32-S3 (สูงสุด 400ms)
        deadline = time.time() + 0.4
        new_boxes = []
        got_response = False

        while time.time() < deadline and running:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                if line == "RES:NONE":
                    got_response = True
                    break
                elif line.startswith("RES:PIGEON"):
                    parts = line.split(",")
                    if len(parts) == 6:
                        score = float(parts[1])
                        x1, y1, x2, y2 = map(int, parts[2:6])
                        new_boxes.append((score, x1, y1, x2, y2))
                        got_response = True
                    # อ่านบรรทัดที่เหลือเผื่อมีนกหลายตัวในเฟรมเดียว
                    time.sleep(0.002)
                    if not ser.in_waiting:
                        break
            else:
                time.sleep(0.002)

        if got_response:
            now = time.time()
            dt = now - last_infer_time
            last_infer_time = now
            with lock:
                latest_boxes = new_boxes
                esp_fps = 1.0 / dt if dt > 0 else 0.0

    ser.close()

# เริ่ม Background Thread สำหรับสื่อสารกับ ESP32-S3
t = threading.Thread(target=serial_worker, daemon=True)
t.start()

WIN_NAME = "ESP32-S3 Edge Offloading (Smooth UI)"
cv2.namedWindow(WIN_NAME, cv2.WINDOW_NORMAL)
cv2.resizeWindow(WIN_NAME, 640, 640)

cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

if not cap.isOpened():
    print("ERROR: Cannot open webcam!")
    running = False
    exit(1)

print("Running smooth UI! Press 'q' on video window to quit.")

while True:
    ret, frame = cap.read()
    if not ret:
        cv2.waitKey(5)
        continue

    # ย่อภาพ 224x224 เตรียมส่งให้ ESP32-S3 เฉพาะตอนที่ Worker ว่างรับเฟรมใหม่
    resized = cv2.resize(frame, (IMG_SIZE, IMG_SIZE))
    with lock:
        need_frame = (latest_payload is None)
        boxes_copy = list(latest_boxes)
        current_esp_fps = esp_fps

    if need_frame:
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.uint16)
        r = (rgb[:, :, 0] >> 3) << 11
        g = (rgb[:, :, 1] >> 2) << 5
        b = (rgb[:, :, 2] >> 3)
        rgb565 = (r | g | b).astype(">u2").tobytes()
        with lock:
            latest_payload = b"\xAA\x55" + rgb565

    # วาดกรอบลงบนภาพความละเอียดเต็มของกล้อง (ลื่น 30-60 FPS)
    h, w, _ = frame.shape
    scale_x = w / IMG_SIZE
    scale_y = h / IMG_SIZE

    for (score, x1, y1, x2, y2) in boxes_copy:
        bx1, by1 = int(x1 * scale_x), int(y1 * scale_y)
        bx2, by2 = int(x2 * scale_x), int(y2 * scale_y)
        cv2.rectangle(frame, (bx1, by1), (bx2, by2), (0, 255, 0), 2)
        cv2.putText(frame, f"Pigeon {score:.2f}", (bx1, max(20, by1 - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    cv2.putText(frame, f"ESP32 AI Rate: {current_esp_fps:.2f} FPS", (10, 28),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

    cv2.imshow(WIN_NAME, frame)
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

running = False
cap.release()
cv2.destroyAllWindows()