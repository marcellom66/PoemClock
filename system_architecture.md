# Poem Clock: Full System Architecture & Data Flow

This document provides a detailed breakdown of the "Poem Journey" — the complete technical process that powers the Poem Clock, from the moment you configure its personality to the final ink-drop on the E-Paper display.

---

## 🏗️ 1. Phase One: The Human Foundation (Configuration)
The journey begins with you. The clock isn't just a gadget; it's a reflection of your literary taste.

1.  **Trigger**: On boot, holding down the capacitive **PAD1** on the Inkplate board triggers **Configuration Mode**.
2.  **Captive Portal**: The ESP32 creates a local Wi-Fi Hotspot (**OROLOGIO-CONFIG**).
3.  **Local Web Server**: You connect your phone and open a simple, elegant web form.
4.  **Persistent Storage (NVRAM)**: Your choices for **Language**, **Author**, and **Poem Style** are saved to the ESP32's internal flash memory using the `Preferences.h` library. These settings survive power cycles and battery swaps.

## 🕰️ 2. Phase Two: Universal Synchronization (NITZ)
Before the clock can be poetic, it must be precise.

1.  **NB-IoT Wake-up**: The **SIM7020E/SIM7002E** module wakes up from Power Saving Mode (PSM).
2.  **Cellular Handshake**: The module connects to the nearest LTE-M/NB-IoT cell tower.
3.  **NITZ Sync**: Instead of traditional internet time (NTP), the clock uses **NITZ (Network Identity and Time Zone)**. It receives the atomic time directly from the cellular network provider.
4.  **External RTC Update**: This precise timestamp is written to the **PCF85063A External RTC**. The RTC keeps time with sub-second accuracy, even if the main processor is busy.

## 📡 3. Phase Three: The Request (Cellular Data)
Every 30 minutes, or when manually triggered, the quest for a new poem begins.

1.  **PDP Context Activation**: The ESP32 sends `AT+CNACT=1,1` to open a cellular data channel.
2.  **Dynamic URL Construction**: The "Request Engine" builds a structured URL:
    - Base: `http://[YOUR_SERVER_IP]:8000/rima`
    - Tokens: `?lang=en&hour=12&minute=30&author=Dante&style=Dark`
3.  **Modem Mutex Protection**: To prevent interference with the clock's time-sync task, a **FreeRTOS Recursive Mutex** (`modemMutex`) ensures that only one task talks to the SIM7020E at a time.

## 🧠 4. Phase Four: The AI Brain (Backend Logic)
The request hits the **FastAPI** server, where the "Soul" of the clock resides.

1.  **Mood & Season Engine**: The server doesn't just pass the prompt to an LLM. It calculates the **Mood** (e.g., *Morning Hope* or *Dreamlike Night*) and the **Season Context** (e.g., *Winter: cold, intimate*).
2.  **Groq Inference**: The request is sent to the **Groq LPU** (Language Processing Unit) using models like **Gemma 2** or **GPT-OSS**. Groq's high-speed inference ensures the poem is generated in milliseconds.
3.  **Prompt Engineering**: The system prompt combines the user's style preference with the current time and season to create a unique, evocative 2-line rhyme.
4.  **JSON Response**: The server sends back a structured JSON payload containing the rhyme, the detected mood, and the seasonal context.

## 🧼 5. Phase Five: The E-Ink Cleanse (Anti-Ghosting)
E-paper is beautiful but sensitive. Old text leaves behind "ghost" artifacts.

1.  **Poem Area Reset**: Before drawing the new text, the clock executes `cleanPoemArea()`.
2.  **The "Deep Clean" Flash**: The display controller performs a rapid series of full-black and full-white flashes specifically in the poem region. This resets the micro-capsules of ink.
3.  **Display Mutex**: A dedicated `displayMutex` ensures the clock seconds or other UI elements don't try to draw during this critical refresh phase.

## 🎨 6. Phase Six: The Final Render (Typography)
Finally, the poem is brought to life.

1.  **Word-Wrap Engine**: The ESP32 parses the long string from the JSON and breaks it into lines that fit the 1024/1280px width perfectly.
2.  **Premium Fonts**: Using **FreeSerifBoldItalic** fonts, the poem is rendered at the bottom of the screen.
3.  **Persistent Status**: The GSM signal icon (calculated from live RSSI) is redrawn to ensure it remains visible after the full-area clean.

---
*The Poem Clock is a perfect loop: from your heart (configuration), to the stars (atomic sync), through the cloud (AI generation), and back to your eyes (E-Ink).*
