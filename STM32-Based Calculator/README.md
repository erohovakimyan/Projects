STM32-Based Calculator with 16x2 LCD and Button Matrix

This project is a basic calculator built using an STM32 microcontroller, a 16x2 character LCD, and a 4x4 button matrix. 
It demonstrates how embedded systems can handle user input and display output in real time while performing arithmetic operations.

Key Features:
Microcontroller: STM32 (e.g., STM32F103C8T6 "Blue Pill")
Display: 16x2 character LCD (HD44780 controller, interfaced in 4-bit mode)
Input: 4x4 button matrix for digits (0–9), operators (+, –, ×, ÷), equals (=), and clear (C)
Functionality: Performs basic arithmetic operations and displays input/result on the LCD

Technical Highlights:
Interfaced the LCD using GPIOs and the HD44780 driver in 4-bit mode to reduce pin usage.
Implemented a button matrix scanning algorithm using GPIO rows as outputs and columns as inputs with pull-up resistors.
Parsed multi-digit user input and operator commands using simple logic and displayed the expression/result in real time.
Applied software debouncing to ensure reliable button detection.
Programmed in C using STM32CubeIDE with HAL libraries.

This project helped reinforce key embedded systems concepts such as GPIO handling, LCD interfacing, 
keypad scanning, and building interactive applications with limited resources.