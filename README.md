# Audio-Reactive RGB LED Visualizer 🎵✨

## 📌 Project Overview
This repository contains the firmware for an interactive, audio-reactive LED visualizer built on the **STM32F407** microcontroller[cite: 1]. The system captures real-time audio via an analog sensor, processes the signal to detect beats and intensity, and drives a WS2812B addressable RGB LED strip[cite: 4]. Furthermore, it features a Bluetooth interface for remote control and bi-directional communication with a custom mobile application[cite: 1, 4].

## 🛠️ Hardware & Tech Stack
* **Microcontroller:** STM32F407VETX (ARM Cortex-M4)[cite: 4]
* **Development Environment:** STM32CubeIDE (HAL Library)[cite: 4]
* **LED Hardware:** WS2812B RGB LEDs (60 pixels)[cite: 4]
* **Key Peripherals:** ADC1, TIM1 (PWM), DMA2, USART1[cite: 4]

## 🚀 Core Features & Implementation
* **DMA-Driven LED Control:** Utilizes TIM1 and DMA to generate precise 800kHz PWM signals for the WS2812B protocol, ensuring zero CPU blocking during LED data transmission[cite: 4].
* **Real-Time Audio Processing:** Reads audio signals via ADC1 using polling, implements ambient noise filtering, and applies a dynamic threshold with a smoothing algorithm for accurate beat detection[cite: 4].
* **Dynamic Visual Effects:** Includes four distinct audio-reactive modes: Progressive Bar, Wave Effect, Center Pulse, and Color Temperature mapping[cite: 4].
* **Interactive Bluetooth Control:** Receives UART commands (e.g., `#1` to `#4` for effect switching, `#C` for sensor auto-calibration, and `#B` for manual beat triggers) via an interrupt-driven buffer system[cite: 4].
* **App Synchronization:** Packages and transmits the physical LED color states (`LED_RGB_Buffer`) back to the mobile application at a 25Hz refresh rate for virtual visualization[cite: 4].

## 📁 Repository Structure
* `/Core/Src` - Contains the main application logic (`main.c`), peripheral initialization (`adc.c`, `tim.c`, `usart.c`, `gpio.c`), and DMA configuration[cite: 4].
* `/Core/Inc` - Header files and hardware abstraction configurations[cite: 4].
* `CE224.ioc` - STM32CubeMX configuration file containing pinout and clock tree settings[cite: 4].

---
*Developed and maintained by Ho Ngoc Thien Phuoc.*
