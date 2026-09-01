# SML on CH552G — SML to SDM630-compatible Modbus RTU bridge

This firmware runs on the WCH **CH552G** 8-bit USB microcontroller.

The firmware:

1. listens for **SML** smart-meter telegrams on **UART1** (e.g. coming from a
   reading head pointed at the meter's IR diode);
2. validates the complete SML transport frame with CRC-16/X25;
3. extracts the supported **OBIS** values (voltages, currents, frequency, power,
   and import/export energy counters);
4. exposes them through an **Eastron SDM630-compatible Modbus RTU** interface on
   **UART0** / RS&#x2011;485.

Both Modbus function 03 (holding registers) and function 04 (input registers)
are accepted.

## Supported OBIS registers

The OBIS-to-Modbus mapping is defined in [`src/modbus_rtu.c`](src/modbus_rtu.c)
and can be adapted there for another meter. The default mapping is tuned for an
EFR SGM-C4:

| OBIS code | Measurement | SDM630 register |
|-----------|-------------|----------------:|
| `1-0:1.8.0*255` | Imported active energy, total (T0) | 72 |
| `1-0:2.8.0*255` | Exported active energy, total (T0) | 74 |
| `1-0:16.7.0*255` | Total active power | 52 |
| `1-0:36.7.0*255` | Active power L1 | 12 |
| `1-0:56.7.0*255` | Active power L2 | 14 |
| `1-0:76.7.0*255` | Active power L3 | 16 |
| `1-0:32.7.0*255` | Voltage L1 | 0 |
| `1-0:52.7.0*255` | Voltage L2 | 2 |
| `1-0:72.7.0*255` | Voltage L3 | 4 |
| `1-0:31.7.0*255` | Current L1 | 6 |
| `1-0:51.7.0*255` | Current L2 | 8 |
| `1-0:71.7.0*255` | Current L3 | 10 |
| `1-0:14.7.0*255` | Frequency | 70 |

Tariff-specific energy values (`*.8.1` through `*.8.8`) are accumulated when
the total (`*.8.0`) is not present. The total active power mapping also accepts
`1-0:1.7.0*255`, which is commonly emitted by EFR meters.

## Data integrity and memory use

The Modbus readings use a double buffer. While an SML frame is being received,
the inactive register bank is cleared and decoded values are written into it.
The firmware atomically publishes that bank only after the frame trailer and
transport CRC are valid.
Truncated, malformed, buffer-overflowed, and bad-CRC frames therefore leave the
last valid readings visible to Modbus clients.

The implementation is designed for the CH552G's 1 KiB XRAM and 16 KiB flash.
Immutable OBIS/register metadata is kept in code flash, and the SML receiver uses
a 64-byte circular buffer. For a Release build, `firmware.mem` reports:

| Resource | Used | Available | Utilization |
|----------|-----:|----------:|------------:|
| Application flash | 8,256 bytes | 14,336 bytes | 57.6% |
| Physical flash | 8,256 bytes | 16,384 bytes | 50.4% |
| XRAM | 374 bytes | 1,024 bytes | 36.5% |

The 14 KiB application limit reserves 2 KiB of physical flash for the bootloader.
Memory figures can change with the compiler version; always inspect the newly
generated `build/firmware.mem` after modifying the firmware.

## Hardware

| Function              | CH552G pin | Notes                           |
|-----------------------|-----------|----------------------------------|
| Modbus RXD0           | P3.0      | RS-485 receiver output           |
| Modbus TXD0           | P3.1      | RS-485 driver input              |
| Modbus /RE            | P1.4      | active low, see `config.h`       |
| Modbus DE             | P1.5      | active high, see `config.h`      |
| SML RXD1              | P1.6      | from IR head                     |
| SML TXD1              | P1.7      | unused on reading heads          |
| LED Modbus            | P3.4      | flashes on TX/RX activity        |
| LED SML               | P1.1      | flashes on telegram reception    |

All pin assignments and protocol parameters live in
[`include/config.h`](include/config.h) and are compile-time only — the CH552
project intentionally does not use writable Data-Flash for runtime config.

## Building

Required tools on Debian / Ubuntu:

```sh
sudo apt install sdcc cmake python3 python3-pip
python3 -m pip install pyusb
```

> SDCC's binaries are installed with an `sdcc-` prefix on Debian (`sdcc-sdcc`,
> `sdcc-packihx`, `sdcc-makebin`). The build system picks them up automatically.

Then:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

Debug builds expose a USB CDC port. Whenever the SML parser decodes a supported
value, it emits a line such as:

```text
OBIS 1-0:16.7.0*255 = 563.500000
```

The output is enabled only when `NDEBUG` is absent. It is discarded rather than
delaying SML parsing when the CDC terminal is closed or its endpoint is busy.

For a production image without the USB stack or CDC logging, build Release:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build artifacts land in `build/`:

* `firmware.ihx` — Intel HEX (raw SDCC output)
* `firmware.hex` — Intel HEX (packed)
* `firmware.bin` — binary image, ready for the WCH ISP tool
* `firmware.mem` — memory usage report (read this if you change the design)

## Flashing

Hold the **BOOT** button while plugging the CH552 USB cable in, then:

```sh
cmake --build build --target flash
```

This invokes `tools/chprog.py` against `build/firmware.bin`.

## Project layout

```
include/                  Project and CH552 headers
  config.h                Pin / baud / ID configuration
  systick.h               1 ms millis() (Timer2)
  modbus_rtu.h            Modbus slave API
  sml.h                   SML parser API
  ch554.h, gpio.h         CH552 register / pin macros (from wagiminator)
  system.h, delay.h       CH552 system / delay (from wagiminator)
  uart.h                  CH552 UART helpers (from wagiminator)
src/
  main.c                  Entry point and top-level loop
  systick.c               Timer2-driven 1 ms tick
  modbus_rtu.c            Modbus RTU slave (UART0, RS-485)
  sml.c                   SML telegram parser (UART1)
  delay.c                 CH552 delay routines (from wagiminator)
  cdc_debug.c             USB CDC debug logging (Debug builds)
tools/
  chprog.py               WCH USB bootloader uploader
CH552/                    CH552 reference libraries and documentation
```

## License

This project is licensed under [Creative Commons Attribution-ShareAlike 3.0
Unported](https://creativecommons.org/licenses/by-sa/3.0/) (CC BY-SA 3.0).
