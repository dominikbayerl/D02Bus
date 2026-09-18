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

The OBIS-to-Modbus mapping is defined in [`src/meter_values.c`](src/meter_values.c)
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
| `1-0:81.7.4*255` | Current/voltage angle L1 | Internal calculation input |
| `1-0:81.7.15*255` | Current/voltage angle L2 | Internal calculation input |
| `1-0:81.7.26*255` | Current/voltage angle L3 | Internal calculation input |

Offsets are zero-based: offset 0 corresponds to 30001. Each exposed value is a
32-bit float occupying two registers, high word first. The following values are
calculated once per CRC-valid SML telegram, before publishing the new bank:

| Measurement | Offsets | Calculation |
|-------------|---------|-------------|
| Apparent power L1/L2/L3 (VA) | 18, 20, 22 | `S = V * I` |
| Reactive power L1/L2/L3 (var), estimated | 24, 26, 28 | `Q = CFG_REACTIVE_SIGN * S * sin(angle)` |
| Signed power factor L1/L2/L3 | 30, 32, 34 | `P / S`, clamped to -1..1; zero when S is zero |
| Total apparent power (VA) | 56 | Sum of three phase apparent powers |
| Total reactive power (var), estimated | 60 | Signed sum of three phase reactive powers |

The angle calculation uses a 182-byte quarter-wave integer lookup table in code
flash (one-degree steps, scaled by 32767), with integer quadrant folding. There
are no runtime trigonometric or square-root calls. Basic software float arithmetic
is still used for products and power-factor division. Modbus reads only serialize
the stored results. The lookup's absolute sine error at integer degrees is at most
about 0.000016; meter rounding and waveform distortion dominate the Q estimate.

`CFG_REACTIVE_SIGN` in `include/config.h` defaults to +1, matching direct sine of
the reported EFR angle. Its direction relative to the Solis reactive-power sign
convention needs checking with a known reactive load; use -1 to invert it.
The calculation assumes sinusoidal voltage/current and is not a direct reactive
power measurement. PF uses measured active power, not cosine of the angle.

Missing inputs leave the affected derived values at zero. Totals require valid
inputs for all three phases. Angles outside 0..360 degrees are rejected; fractional
angles are rounded to the nearest degree. Reserved gaps at offsets 54–55 and
58–59 remain zero, as do other unmapped registers. Reads are limited to 27
16-bit registers per request; split larger blocks accordingly.

Tariff-specific energy values (`*.8.1` through `*.8.8`) are accumulated when
the total (`*.8.0`) is not present. The total active power mapping also accepts
`1-0:1.7.0*255`, which is commonly emitted by EFR meters.

## Data integrity and memory use

`CFG_MODBUS_MAX_AGE_MS` in `include/config.h` limits the age of the published
register set (default `10000UL`, configurable from 1 through `0x7fffffff` ms).
FC03 and FC04 reads return exception `0x04` (Server/Slave Device Failure) before
the first measurement publication or when the set is older than this limit.
At exactly 10,000 ms a default-configured set is still accepted; at 10,001 ms it
is rejected. Exception responses contain the address, function OR `0x80`, code
`0x04`, and CRC, with no register payload. Request validation takes precedence.

Only CRC-valid telegrams containing at least one of the 13 supported measurement
fields publish a new bank and refresh its timestamp, even if the values are
unchanged. Empty or angle-only telegrams do not replace the bank. Invalid or
incomplete frames do not renew freshness. A new valid measurement publication
automatically restores normal responses. Missing individual fields in an otherwise
valid measurement telegram still become zero; freshness applies to the whole set.

The main loop checks the existing atomic systick `millis()` and latches expiration
until the next publication, so counter rollover cannot revive an expired set.
Read handling checks again before constructing its response. No new timer or
ISR work is added. A response already in progress is allowed to finish.

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
| Application flash | 11,387 bytes | 14,336 bytes | 79.4% |
| Physical flash | 11,387 bytes | 16,384 bytes | 69.5% |
| XRAM | 493 bytes | 1,024 bytes | 48.1% |

The USB CDC Debug build uses 14,246 bytes of application flash (90 bytes spare)
and 504 bytes of XRAM above the 256-byte USB DMA reservation. Release is the
smaller build for normal operation.

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

Host regression tests use the production SML parser and measurement calculations:

```sh
cc -std=c99 -DNDEBUG -DHOST_TEST -include tests/host_mocks.h -Itests -Iinclude \
  tests/test_sml.c src/sml.c src/meter_values.c -lm -o /tmp/d02bus-test-sml
/tmp/d02bus-test-sml --self-test
/tmp/d02bus-test-sml --emh-regression
/tmp/d02bus-test-sml --derived-test
/tmp/d02bus-test-sml --freshness-test
cc -std=c99 -fno-strict-aliasing -DHOST_TEST -include tests/host_mocks.h \
  -Itests -Iinclude tests/test_modbus_freshness.c src/modbus_rtu.c \
  src/meter_values.c -o /tmp/d02bus-test-freshness
/tmp/d02bus-test-freshness
```

The derived test uses a generated SML frame containing the EFR sample, checks
CRC isolation and missing inputs, and compares all 361 integer angles against
host `sinf`. Only the host test links libm; the firmware does not.
Freshness tests cover both read functions and response CRCs, startup, timeout
boundaries, recovery, missing measurements, corrupted/incomplete SML frames,
and systick rollover. To exercise another timeout, compile the Modbus test with
`-DCFG_MODBUS_MAX_AGE_MS=250UL`. The host UART transmitter is mocked; the response
handler, CRC implementation, and measurement store are production code.

Run `bash tests/run_modbus_sdcc.sh` with SDCC and uCsim s51 installed to test the
actual 8051-compiled response handler. This catches an SDCC response-length
miscompilation that host tests cannot detect. It checks the startup exception,
a 20-register response with nonzero data, CRCs, and an unmapped raw address of
30000. The latter must return 40 zero data bytes, not an empty response. Client
requests must use raw offset 0 for register 30001, not raw address 30000.

Run `python3 tests/run_systick_sdcc.py` to test the production Timer2 handler and
`millis()` under SDCC/uCsim. It injects a higher-priority interrupt between counter
byte writes at 8-, 16-, 24- and 32-bit rollover boundaries, and checks that reads
preserve the caller's interrupt-enable state. The simulator uses INT0 to model
UART1's priority because its C52 model does not include the CH552 UART1 peripheral.

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
  meter_values.c          OBIS mapping, buffered values, derived power calculations
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
