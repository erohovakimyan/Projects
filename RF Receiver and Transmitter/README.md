From Concept to Reality.
Building a Custom RF Transmitter, Receiver and Smart FM Receiver using Arduino.

I am thrilled to share a recent electronics project that challenged and educated me at every turn. Working with my talented teammate Yeva Yeghoyan, 
we designed and built a complete RF system featuring both a custom analog transmitter, receiverand a digitally-enhanced Arduino-based receiver.

1. The Transmitter: Analog Engineering at Its Finest
Our transmitter circuit combines:
• 2N2222A transistors for clean signal amplification
• Precision LC tank circuits for stable frequency generation
• Hand-wound inductors tuned for optimal performance
• 9V battery power for portability

After several iterations (and debugging sessions), we achieved reliable transmission with minimal noise.

2. Pure Hardware Receiver (The "No-Code" Solution)
• BF494 RF amplifier stage
• Discrete component design (no ICs)
• LM386M-1 audio amplifier
• Tuned LC circuits for station selection
• 0.22μF coupling caps for optimal signal transfer
• 9V battery power for portability

3. The Receiver: Digital Precision Meets Analog Signals
The receiver side features:
• RDA5807 chip for crystal-clear digital tuning
• Arduino Nano for intelligent control
• OLED display showing real-time frequency
• Tactile tuning buttons with debouncing