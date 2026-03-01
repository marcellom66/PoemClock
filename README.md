
# Poem Clock: E-Ink NB-IoT Timepiece

An ultra-realistic, IoT-enabled clock that displays dynamic poetry fetched via cellular network (NB-IoT).

![Poem Clock Preview](poem_clock_preview.png)

## 🕰️ Overview
The **Poem Clock** is more than just a timepiece; it's a piece of kinetic art that updates its time via atomic-accuracy cellular signals and displays curated poetry or classic quotes. Designed for low power and high visual impact, it uses a custom-coded 7-segment engine to mimic premium hardware displays on e-paper.

## 🛠️ Hardware Requirements
*   **Board**: Inkplate 10 (Inkplate 5 V2 compatible).
*   **NB-IoT Module**: SIM7020E or SIM7002E.
*   **RTC**: PCF85063A (External for accuracy).
*   **MCU**: ESP32 Dual-Core.

## ✨ Key Features
*   **AI-Powered "Poem Journey"**: Fetches personalized poems based on **Language**, **Author**, **Style**, and **Time of day** from a Groq-powered FastAPI backend.
*   **NB-IoT & NITZ**: Synchronizes time directly from cellular towers without needing WiFi.
*   **Anti-Ghosting Engine**: Specific localized "deep clean" refreshes for pristine E-Ink quality.
*   **Ultra-Realistic UI**: Custom segment-drawing logic with realistic spacing and "breathing" transitions.
*   **WiFi Config Portal**: Easy setup via a temporary smartphone Access Point.
*   **Thread-Safe Core**: Uses FreeRTOS mutexes to manage concurrent modem and display access.

## 🚀 Getting Started
1.  **Clone the Repo**.
2.  **Configure Secrets**: 
    - Copy `config.h.example` to `config.h`.
    - Set the IP/URL of your AI backend.
3.  **Upload to Arduino**: Use the Inkplate core in Arduino IDE.
4.  **Connect & Enjoy**: Hold the capacitive pad (PAD1) on boot to enter Configuration Mode.

## 📱 Architecture
The project follows an **Edge-to-Cloud** architecture, delegating heavy AI generation to a remote server while keeping the ESP32 optimized for low-power display management and cellular timing.

For a deep dive into the end-to-end data flow (from Config Portal to AI generation and E-Ink rendering), check out our [Detailed System Architecture](system_architecture.md).

## 📄 License
This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

---
*Created by Marcello Mangione (Elettronica Mangione - R&D Hardware/Embedded).*
