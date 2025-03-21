# Fish Tank Water Quality Monitoring System

## Overview
This project is a real-time water quality monitoring system for fish tanks, built using the ESP32 microcontroller and the ESP-IDF framework. The system monitors key water parameters such as temperature, pH, and turbidity, and displays the data on an OLED screen. It also integrates with the ESP RainMaker cloud platform to allow remote monitoring and control via a mobile app. The system can trigger alerts and control devices (e.g., MP3 alarms, drain valves) based on predefined thresholds.

## Features
- **Real-time Monitoring**: Measures temperature, pH, and turbidity using sensors.
- **Local Display**: Displays sensor data on an SSD1306 OLED screen.
- **Cloud Integration**: Sends data to ESP RainMaker for remote monitoring via MQTT.
- **Alerts and Control**: Triggers alerts and controls GPIO pins (e.g., MP3, drain valve) when pH exceeds thresholds.
- **FreeRTOS Tasks**: Uses FreeRTOS for multitasking (sensor reading, device control, and display).
- **MQTT Optimization**: Implements debouncing and budgeting to prevent MQTT message drops.

## Hardware Requirements
- **ESP32 Development Board**: Any ESP32-based board (e.g., ESP32-WROOM-32).
- **Sensors**:
  - Temperature sensor (e.g., DS18B20).
  - pH sensor.
  - Turbidity sensor.
- **SSD1306 OLED Display**: 128x64 resolution, I2C interface.
- **MP3 Module**: For audio alerts.
- **Drain Valve**: Controlled via GPIO.
- **Wi-Fi Network**: For connecting to ESP RainMaker.

## Software Requirements
- **ESP-IDF**: Version 4.4 or later.
- **ESP RainMaker**: For cloud integration.
- **SSD1306 Library**: For OLED display (included in the project).
- **FreeRTOS**: For task management (included in ESP-IDF).

## Project Structure
```
FishTankMonitoring/
├── components/           # External components (e.g., esp-insights, ssd1306)
├── main/                 # Main source code
│   ├── app_main.c        # Main application logic
│   ├── app_driver.c      # Driver handle logic
│   ├── app_riv.h         # File header        
│   └── CMakeLists.txt    # CMake configuration for the main component
├── CMakeLists.txt        # Project-level CMake configuration
└── README.md             # Project documentation
```

## Installation and Setup
### 1. Clone the Repository
```bash
git clone https://github.com/zanthanh17/Embedded-System-ESPIDF-freeRTOS
cd FishTankMonitoring
```
### 2. Set Up ESP-IDF
Follow the ESP-IDF Getting Started Guide to install ESP-IDF. Then, set up the environment:
```bash
. $HOME/esp/esp-idf/export.sh
```
### 3. Install Dependencies
Clone the required components (e.g., esp-insights for RainMaker):
```bash
cd components
git clone https://github.com/espressif/esp-insights.git
```
### 4. Configure the Project
Run menuconfig to configure Wi-Fi credentials, MQTT settings, and other parameters:
```bash
idf.py menuconfig
```
Set Wi-Fi SSID and password under **Example Connection Configuration**.
Adjust MQTT settings under **Component config -> ESP-MQTT Configurations**:
- MQTT Buffer Size: 2048 bytes
- MQTT Task Stack Size: 6144 bytes
- MQTT Budget Default: 200-300
- MQTT Revive Count: 3
- MQTT Revive Period: 10 seconds

### 5. Build and Flash
Build the project, flash it to the ESP32, and monitor the output:
```bash
idf.py build flash monitor
```

## Code Overview (master Branch)
The code in the master branch represents the stable version of the project with the following features and optimizations:

### Key Features in master
#### Task Management:
- **task_Sensor**: Reads temperature, pH, and turbidity every 10 seconds and sends data to two queues (display_queue and control_queue).
- **task_Display**: Receives data from display_queue and displays it on the OLED screen.
- **task_Device_control**: Receives data from control_queue, updates RainMaker, and controls GPIO based on pH thresholds.

#### OLED Display:
- Displays temperature as an integer (rounded from float) using the `ssd1306_display_text_box1` function.
- Example: 25.7°C is displayed as 26°C.

#### MQTT Optimization:
- Implements a 1-second debounce to prevent excessive MQTT messages when sensor data changes.
- Configures MQTT budgeting to avoid "MQTT budget, dropping message" errors:
  - MQTT Budget Default: 200
  - MQTT Revive Count: 3
  - MQTT Revive Period: 10 seconds

#### Device Control:
- Triggers an MP3 alert and opens a drain valve if pH > 8.10.
- Resets the alert and closes the valve if pH < 8.10.
- Limits alert frequency to once every 60 seconds.

### Code Details
#### Sensor Reading (**task_Sensor**):
- Reads data using `read_temp_sensor()`, `read_ph_sensor()`, and `read_turbidity_sensor()`.
- Sends data to both `display_queue` and `control_queue` for processing.

#### Display (**task_Display**):
- Uses `ssd1306_display_text_box1` to show temperature on the OLED.
- Temperature is rounded to an integer using `round()` from `<math.h>`.

#### Device Control (**task_Device_control**):
- Updates RainMaker with sensor data when values exceed thresholds (`TEMP_THRESHOLD`, `PH_THRESHOLD`, `TURB_THRESHOLD`).
- Implements debouncing (1-second minimum interval) to limit MQTT message frequency.
- Logs the total number of MQTT messages sent for monitoring.

#### MQTT Configuration:
- Adjusted in menuconfig to handle real-time updates without dropping messages.

### Known Issues in master
- If sensor noise causes frequent changes, the MQTT budget may still be exceeded. Consider increasing **MQTT Budget Default** to **300** or reducing **MIN_REPORT_INTERVAL_MS** to **500ms**.
- The OLED display currently shows only temperature. Future updates will include pH and turbidity.

## Usage
### 1. Connect the Hardware:
Wire the sensors, OLED, MP3 module, and drain valve to the ESP32 as per the pin configuration in the code.
### 2. Power On:
Power the ESP32 and ensure it connects to Wi-Fi.
### 3. Monitor Data:
View real-time data on the OLED screen.
Use the RainMaker app to monitor data remotely.
### 4. Receive Alerts:
If pH exceeds 8.10, an MP3 alert will play, and the drain valve will open.
Alerts are sent to the RainMaker app.

## Future Improvements
- Add pH and turbidity display on the OLED.
- Implement a more robust sensor noise filter.
- Support for additional sensors (e.g., dissolved oxygen).
- Enhance MQTT budgeting with dynamic adjustment based on network conditions.

## Contributing
Contributions are welcome! Please fork the repository, create a new branch, and submit a pull request with your changes.


