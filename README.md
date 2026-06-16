# ESP32-C3 WiFi Deauther

A feature-rich WiFi deauthentication tool built for the ESP32-C3 SuperMini. It includes a custom menu system, deep sleep capabilities to conserve battery, and interactive tools for network analysis.

## Hardware Components
- **ESP32-C3 SuperMini** (Microcontroller)
- **SSD1306 0.96" OLED Display** (I2C)
- **3x Tactile Push Buttons** (For navigation)
- **TP4056 Module** (Lithium battery charging and protection)
- **3.7V Li-Po / Li-Ion Battery**

---

## Pin Layout & Connections

### 1. Power & Battery Management (TP4056)
The TP4056 handles charging the battery via USB-C and provides safe power output to the ESP32-C3.

| TP4056 Pin | Connection |
| :--- | :--- |
| **B+** | Battery Positive (+) |
| **B-** | Battery Negative (-) |
| **OUT+** | ESP32-C3 **5V** (or VBUS) Pin |
| **OUT-** | ESP32-C3 **GND** Pin |

> [!CAUTION]
> Ensure you connect the battery to `B+` and `B-` correctly. Reversing polarity on a Li-Po battery can be dangerous.

### 2. SSD1306 OLED Display (I2C)
| OLED Pin | ESP32-C3 Pin |
| :--- | :--- |
| **VCC** | 3.3V |
| **GND** | GND |
| **SCL** | GPIO 7 |
| **SDA** | GPIO 6 |

### 3. Navigation Buttons
The buttons use the ESP32-C3's internal pull-up resistors. Connect one side of each button to the respective GPIO pin, and the other side to ground.

| Button | ESP32-C3 Pin | Connection |
| :--- | :--- | :--- |
| **UP** | GPIO 0 | Button -> GND |
| **DOWN** | GPIO 1 | Button -> GND |
| **SELECT** | GPIO 2 | Button -> GND |

### 4. NRF24L01+ (Optional / Experimental)
If you plan to wire up the external NRF module for extended capabilities:

| NRF24L01+ | ESP32-C3 Pin |
| :--- | :--- |
| **VCC** | 3.3V |
| **GND** | GND |
| **CE** | GPIO 4 |
| **CSN** | GPIO 5 |
| **SCK** | GPIO 8 |
| **MISO** | GPIO 9 |
| **MOSI** | GPIO 10 |

---

## Power Saving Features
The device is designed to be battery-efficient. If no attack or scan is running and the device is idle for 60 seconds, it will automatically enter **Deep Sleep Mode**. 
To wake the device back up, simply press **any** of the 3 navigation buttons.
