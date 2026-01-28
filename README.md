# D02Bus

D0-2-Bus ("D0 to Bus") is an Open Hardware project designed to convert data from an energy smart meter D0 optical interface to RS485 Modbus.

## Overview

 This project aims to be compatible with various commercial smart meters (such as Eastron SDM, Kostal EnergyMeter, etc.), enabling easy integration into Modbus-based energy management systems.

## Inspiration & Attribution

This project is inspired by (and is essentially a "shameless copy" of) the "Smartmeter" project by **Niklas Menke**.
*   Original Project: [https://www.niklas-menke.de/projekte/smartmeter-auslesen/modbus/](https://www.niklas-menke.de/projekte/smartmeter-auslesen/modbus/)

## Key Differences

While based on the original concept, D02Bus implements specific changes for better usability:

*   **Microcontroller**: The hardware uses a **CH552G** microcontroller instead of the original ATtiny. This change provides better component availability and features a built-in USB controller for easier configuration.
*   **Firmware Flexibility**: The firmware aims to be more easily adjustable to support different smart meter models.

## Project Status

**⚠️ Warning: Work in Progress**

Currently, this project only contains **unvalidated** hardware designs. The repository will be updated once the first PCBs have been assembled and tested.

## License

This project is licensed under the **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)** license.
