# Smart Light Control System 🏠💡
A home automation system using ESP32 that controls an AC bulb based on prioritized logic: **Manual Switch > IR Sensor > RTC Schedule**. Built as part of a virtual internship with Upskill Campus & The IoT Academy.

---

## 🔧 Features
- Prioritized control: 
  - Manual push button (highest priority)
  - IR sensor motion detection
  - RTC-based scheduled lighting
- Real-time control using **MQTT** and visual workflow via **Node-RED**
- Safe AC load switching using **solid-state relay (SSR)** and **BC547 transistor driver**

---
## 🛠️ Hardware Components
- ESP32 Lolin D32
- IR Sensor (Data to GPIO4)
- RTC Module (I2C: SDA & SCL)
- 5V Solid-State Relay (SSR)
- BC547 NPN Transistor (Relay driver)
- Push Button (Manual input)
- 230V AC Bulb

---

## 📡 Communication
- MQTT used for device communication and status updates
- Node-RED for flow control, monitoring, and scheduling

---

## 🧪 How It Works
1. **Manual Control** (Button Press): Overrides all other methods and toggles the light.
2. **IR Sensor**: Triggers light on motion, unless manually overridden.
3. **RTC Schedule**: Turns on/off light at predefined time slots (lowest priority).
4. **MQTT Broker**: Publishes/receives control commands and status.
5. **Node-RED Dashboard**: Visual interface to monitor and control lighting remotely.


