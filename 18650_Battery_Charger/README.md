Battery Charger

This project is a custom-designed dual 18650 lithium-ion battery charger and power delivery board, 
suitable for embedded and portable systems. The system includes safe battery charging, efficient power path management, 
and regulated voltage output for powering external devices.

Features

Charging Circuit: Based on a Li-Ion battery charging IC, the charger safely charges two 18650 cells connected in parallel.
DC Input Protection: Includes diode-based reverse polarity and overvoltage protection.
Power Path Management: Automatically switches between USB power and battery power based on availability.
Battery Protection: Battery protection circuitry is embedded, including overcharge, overdischarge, and thermal feedback.
Boost Converter: Converts battery voltage (3.0V–4.2V) to a regulated output voltage.
Temperature Monitoring: Integrated thermistor pin support for safe charging under temperature limits.
Compact PCB Design: 3D modeled PCB includes dual battery holders, status LEDs, and screw terminal/Barrel jack for output.

Schematic Overview

Top Left: Charging circuit with LED status indicators.
Center: Power path controller with MOSFET for auto-switching.
Bottom: Boost converter circuit for regulated output (typically 5V).
Right: Battery input section for dual 18650 cell holders.

3D PCB View

Two 18650 holders in parallel configuration.
DC barrel jack and terminal block output options.
Clearly labeled input/output connectors and debug LEDs.

Applications
Power bank circuits

Portable embedded systems
Battery-backed IoT projects
Solar battery charging stations

Requirements
2x 18650 Li-Ion cells (3.7V nominal)
5V DC adapter input
Load < output rating of boost converter