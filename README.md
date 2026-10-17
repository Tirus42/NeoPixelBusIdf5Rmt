# NeoPixelBusIdf5Rmt

A drop-in [NeoPixelBus](https://github.com/Makuna/NeoPixelBus) output method for
**WS2812x / SK6812** addressable LEDs that is built on the **ESP-IDF 5 "new"
RMT TX driver** (`driver/rmt_tx.h`, a.k.a. `driver_ng`).

## Why

NeoPixelBus' built-in `NeoEsp32RmtMethod` uses the *legacy* RMT driver
(`rmt_driver_install()`). ESP-IDF ≥ 5 refuses to run the legacy driver once the
new driver has been initialised by another component. On Arduino-ESP32 3.x the
core ships an RMT HAL based on the new driver, so the built-in RMT method aborts
at boot with:

```
rmt(legacy): CONFLICT! driver_ng is not allowed to be used with the legacy driver
```

The only built-in NeoPixelBus method that works on that stack is the bit-bang
method, which disables interrupts while shifting a frame out and can produce
visible flicker under load (e.g. BLE).

This method uses only the new driver, so it coexists with the Arduino core, and
it lets you choose a **high RMT interrupt priority** so the streaming refill
cannot be starved.

## Install

PlatformIO `platformio.ini`:

```ini
lib_deps =
    makuna/NeoPixelBus@^2.8.4
    tirus/NeoPixelBusIdf5Rmt@^1.0.0
```

## Usage

Use it exactly like any other NeoPixelBus method — the method type is the only
change:

```cpp
#include <NeoPixelBus.h>
#include <NeoPixelBusIdf5Rmt.h>

// SK6812 RGBW strip on pin 0
NeoPixelBus<NeoGrbwFeature, NeoEsp32Idf5RmtSk6812Method> leds(200, 0);

void setup() {
    leds.Begin();
}

void loop() {
    leds.SetPixelColor(0, RgbwColor(255, 0, 0, 0));
    leds.Show();
    delay(10);
}
```

Available method types:

| Method | For |
| --- | --- |
| `NeoEsp32Idf5RmtSk6812Method` | SK6812 / SK6812-RGBW (80 µs latch) |
| `NeoEsp32Idf5RmtWs2812xMethod` | WS2812 / WS2812B / WS2813 … (300 µs latch) |
| `NeoEsp32Idf5Rmt800KbpsMethod` | alias of `NeoEsp32Idf5RmtWs2812xMethod` |

It also works with wrapper types such as
[`LedControlAndAnimation`](https://github.com/Tirus42/LedControlAndAnimation)'s
`LedStrip_NeoPixelBus<Feature, Method>`:

```cpp
LedStrip_NeoPixelBus<NeoGrbwFeature, NeoEsp32Idf5RmtSk6812Method> leds1(100, 0);
LedStrip_NeoPixelBus<NeoGrbFeature,  NeoEsp32Idf5RmtSk6812Method> leds2(100, 1);
```

## Notes & limitations

- **ESP32-C3 has no RMT DMA** (`SOC_RMT_SUPPORT_DMA` is undefined) and only 48
  RMT symbols of RAM per channel, so a whole 200-LED frame (6400 symbols) is
  streamed from RAM by the driver's refill ISR — it does not fit in hardware.
  The mitigation is the interrupt priority (`intr_priority = 1`), not buffer
  size. Change `InterruptPriority` in `NeoPixelBusIdf5Rmt.h` if it conflicts
  with another driver.
- Each strip gets its own RMT TX channel. The ESP32-C3 has **2** TX channels.
- Supported clock: **800 kHz** families listed above. `NeoEsp32Idf5RmtSk6812Method`
  and the WS2812x variants share the same bit timings and differ only in the
  latch/reset time.
- Requires **ESP-IDF 5.x or newer** (Arduino-ESP32 3.x). Including this header
  on an older IDF produces a compile error.
- Not tied to the C3 specifically: it works on any ESP32 variant whose ESP-IDF
  has the new RMT driver (frames are streamed with the driver's refill ISR).

## License

GPL-3.0-or-later.

## Sidenote

This repo is mostly LLM-generated via DeepSeek V4.1 Flash
