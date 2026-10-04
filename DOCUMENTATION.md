# Setup and technical reference

See the [README](README.md) for the story behind the station, its custom wind sensors, and photographs of the prototype. This document covers wiring, configuration, and the behavior of the firmware in this repository.

The project was built for **Embedded Systems Architectures** at the **University of Aveiro**. The original [presentation (PDF)](presentation.pdf) and [editable slides (PPTX)](presentation.pptx) include the hardware architecture, custom sensor diagrams, electrical schematic, and project discussion. Hardware diagrams are on pages 5–7 of the PDF.

## Hardware and development environment

The prototype uses:

- An ESP32-C3 development board.
- A BME280 for temperature, relative humidity, and atmospheric pressure.
- The custom WSP420 anemometer and WVA420 wind vane, built with magnets and Hall-effect sensors. The presentation identifies the Hall sensors as A3144 variants.
- An SPI microSD card reader and a FAT32-formatted card.
- An 18650 battery UPS module, shown in the original hardware design.
- A Wi-Fi network and an MQTT broker for live publication.

The included [`sdkconfig`](sdkconfig) was generated with **ESP-IDF 5.4.1** and targets **`esp32c3`**. Use an activated ESP-IDF environment with `idf.py` available. The firmware is written in C and built with ESP-IDF's CMake integration.

Clone the repository:

```bash
git clone https://github.com/miguelovila/smart-weather-station.git
cd smart-weather-station
```

## Wiring

These are the GPIO assignments used by [`main/main.c`](main/main.c). The numbers are ESP32-C3 GPIO numbers, rather than physical header positions.

| Device | Signal | GPIO |
| --- | --- | ---: |
| SD card | MISO | 10 |
| SD card | MOSI | 1 |
| SD card | Clock | 9 |
| SD card | Chip select | 0 |
| BME280 | SDA | 3 |
| BME280 | SCL | 2 |
| WSP420 | Hall sensor pulse input | 8 |
| WVA420 | North, yellow wire | 7 |
| WVA420 | South, green wire | 5 |
| WVA420 | East, blue wire | 4 |
| WVA420 | West, brown wire | 6 |

Use the [electrical schematic in the presentation](presentation.pdf#page=7) for the power connections and sensor circuitry. The firmware enables internal pull-ups on the wind sensor inputs.

The BME280 uses I²C address **`0x76`** at **100 kHz**. If a board uses `0x77`, change `BME280_I2C_ADDR` in `main/main.c` to `BME280_I2C_ADDR_SECONDARY`. The driver also supports SPI, although this application uses I²C.

Pin changes belong in the definitions near the top of `main/main.c`. Wind direction depends on both the wiring and the physical orientation of the vane; align its north reference when installing the station.

## Prepare the SD card

Format the card as **FAT32**, then put the following two files in its root directory. Their names contain **underscores**.

`wifi_settings.txt`:

```ini
ssid=your_wifi_ssid
password=your_wifi_password
```

`mqtt_settings.txt`:

```ini
broker_url=mqtt://your_broker_address:1883
client_id=weather_station_001
```

Save these as plain text with Unix **LF** line endings. Keep the keys lowercase, include both keys in each file, and do not add spaces around `=` or quotes around values. The parser removes the newline but does not trim whitespace or Windows carriage returns.

The application buffers hold up to 31 characters each for the Wi-Fi SSID and password, and 63 for the client ID. Keep the broker URL short enough to fit its 128-byte configuration line, including the `broker_url=` prefix and newline. The current parser does not check for missing keys.

These settings are read at boot. Restart the board after changing them. If a settings file cannot be read, the application falls back to historical defaults compiled into `main/main.c`; supply your own files instead of relying on those values.

**Keep long filename support enabled before building.** Names such as `wifi_settings.txt` and `bme280_data.csv` exceed FAT's 8.3 naming limit. The included configuration already enables `CONFIG_FATFS_LFN_STACK=y` with `CONFIG_FATFS_MAX_LFN=255`, and disables `CONFIG_FATFS_LFN_NONE`.

If you regenerate the configuration, use `idf.py menuconfig` and select long filename support under **Component config → FAT Filesystem support**. Choosing the no-long-filenames option will prevent the application from opening its expected files.

The card mounts at `/sdcard`, with a maximum of five open files. The application does not format a card after a failed mount. A working card is needed at startup: failure to create the initial data files returns from `app_main` before sensor tasks begin.

## Build, flash, and monitor

With the hardware connected, the prepared card inserted, and the ESP-IDF environment active:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your board's serial port. ESP-IDF can also detect the port automatically with `idf.py flash monitor`. Exit the serial monitor with `Ctrl+]`.

The repository already selects the ESP32-C3 target. If starting from a fresh configuration, select `esp32c3` with `idf.py set-target esp32c3`, then check the FAT long filename setting described above before building.

On startup, the serial output reports the SD mount, loaded settings, Wi-Fi connection, time synchronization, MQTT connection, and sensor initialization. Subsequent messages show sensor readings and the outcome of CSV writes and MQTT publications.

## What happens after reset

The application follows this sequence:

1. Initialize NVS, mount the SD card, and create missing CSV files with their headers.
2. Load Wi-Fi and MQTT settings from the card.
3. Start Wi-Fi in station mode and wait for an initial connection.
4. Request system time from `pool.ntp.org` through SNTP, waiting up to 10 seconds.
5. Connect to MQTT, waiting up to 10 seconds, and publish a startup message if successful.
6. Initialize the BME280 and both custom wind sensor drivers.
7. Start one FreeRTOS task for each sensor group.

| Task | Reading behavior |
| --- | --- |
| `BME280_Task` | Read all three environmental values, log/publish, then delay 2 seconds. |
| `WVA420_Task` | Read the four vane inputs, log/publish the direction, then delay 2 seconds. |
| `WSP420_Task` | Count pulses for 7 seconds, calculate/log/publish speed, then delay 3 seconds. |

Each task has a 4096-byte stack and priority 10. They run under FreeRTOS on the ESP32-C3's single core. The delays follow sensor handling, so they are approximate reporting intervals rather than precise periodic deadlines.

The anemometer reports roughly every 10 seconds, plus processing time. Its interrupt handler counts pulses only during the seven-second measurement window; the following three-second gap is not included.

For every successful reading, the data handler obtains a Unix timestamp, appends a CSV row to the card, and then attempts an MQTT publication. A failed SD write is logged and does not prevent the MQTT attempt. A failed sensor read produces neither a CSV row nor a message for that pass.

If SNTP does not synchronize, the firmware continues and warns that timestamps may be wrong. The timestamp records when a reading reaches the data handler; for wind speed, this is after the measurement window.

## Measurement notes

### BME280

The application configures the sensor in normal mode with 1× oversampling for temperature, pressure, and humidity, the IIR filter disabled, and a 500 ms standby interval. The driver reads factory calibration coefficients and compensates the raw measurements.

Temperature is expressed in °C and relative humidity in %. **Pressure has a unit mismatch in the current implementation:** the API comment says hPa, but the compensation code divides its fixed-point result by `256000.0`, producing a value on the kPa scale. For example, approximately `101.325` corresponds to `1013.25 hPa`.

The CSV and MQTT layers forward this value unchanged. Account for the mismatch when interpreting old records or configuring a dashboard; the example payload below preserves the current output scale.

### WVA420 wind vane

Four active-low GPIO inputs represent north, east, south, and west. A single active input identifies a cardinal direction. Two adjacent active inputs identify an intermediate direction, such as north-east. No active input produces `Unknown`.

The driver returns eight compass directions rather than a continuous angle. It checks combinations in a fixed order and does not separately reject physically inconsistent input patterns. Sensor positioning and magnet alignment therefore matter to the reading.

### WSP420 anemometer

The driver increments a counter on rising GPIO edges during each seven-second window, then calculates:

```text
speed = (pulse_count / 7) × (radius_cm / 100) × calibration_factor
```

The application uses `radius_cm = 4` and `calibration_factor = 24.0`. That factor is marked as an example in the source. The resulting speed depends on the mechanical assembly, pulses per revolution, and calibration; these constants are not evidence of a verified physical speed scale or measurement accuracy.

## MQTT topics and payloads

The station publishes to fixed topics under `ase-proj/`. Changing `client_id` does not change the topics or add a station identifier to the payload. All publications use **QoS 0** and **retain disabled**.

| Topic | Content |
| --- | --- |
| `ase-proj/info` | Startup text: `Weather Station has started!` |
| `ase-proj/bme280` | Unix timestamp, temperature, pressure, and humidity. |
| `ase-proj/wva420` | Unix timestamp and direction label. |
| `ase-proj/wsp420` | Unix timestamp and speed, encoded as a JSON string. |

Illustrative BME280 message:

```json
{
  "timestamp": 1752080400,
  "temperature": 23.500000,
  "pressure": 101.325000,
  "humidity": 56.200000
}
```

Illustrative wind direction message:

```json
{
  "timestamp": 1752080400,
  "wind_direction": "North-East"
}
```

Illustrative wind speed message:

```json
{
  "timestamp": 1752080400,
  "wind_speed": "12.340000"
}
```

The direction strings are `North`, `North-East`, `East`, `South-East`, `South`, `South-West`, `West`, `North-West`, and `Unknown`.

With a Mosquitto client installed, subscribe using your own broker address:

```bash
mosquitto_sub -h your_broker_address -p 1883 -t 'ase-proj/#' -v
```

The MQTT utility accepts optional username/password fields in its C configuration structure, but the current application and SD settings parser do not supply them. The example configuration uses an unencrypted `mqtt://` connection.

## CSV files

The application creates these files automatically and appends to existing files across restarts. Do not create the headers by hand.

`bme280_data.csv`:

```csv
timestamp,temperature,pressure,humidity
1752080400,23.500000,101.325000,56.200000
```

`wva420_data.csv`:

```csv
timestamp,wind_direction
1752080400,North-East
```

`wsp420_data.csv`:

```csv
timestamp,wind_speed
1752080400,12.340000
```

These rows are examples, not captured measurements. CSV and MQTT use the same timestamp and measurement values for each reading. There is no file rotation or storage quota in the application.

## Implementation notes

The Wi-Fi event handler retries after disconnection with no attempt limit in the current configuration. The configured `5000 ms` value determines the initial connection wait, which is 50 seconds here; it does not schedule five-second intervals between retries.

After a successful MQTT connection, reconnection behavior is handled by ESP-MQTT. If the initial connection fails or times out, the wrapper destroys the client and the application does not start another connection attempt.

Local logging and live publication are independent outputs. **The firmware does not replay CSV records to MQTT after an outage.** QoS 0 does not provide delivery acknowledgments, and a successful publish call is not proof that a subscriber received the sample.

The code also needs a memory cleanup pass before prolonged unattended use: each data handler allocates a CSV string, replaces its pointer with a JSON allocation, and frees only the latter. The repository preserves the original prototype implementation.

The presentation's future-work list includes energy management, battery-level reporting, wind vane self-calibration, improvements to sensor friction and weight, and Wi-Fi troubleshooting. Those items should be treated as proposed work. The battery discussion is an estimate, not a documented endurance test.

## Source map

| Location | Responsibility |
| --- | --- |
| [`main/main.c`](main/main.c) | GPIO assignments, application settings, startup, and sensor tasks. |
| [`main/bme280/`](main/bme280/) | BME280 register access, configuration, calibration, and compensation. |
| [`main/wind_direction/`](main/wind_direction/) | Four-input WVA420 direction decoding. |
| [`main/wind_speed/`](main/wind_speed/) | WSP420 interrupt handling, pulse counting, and speed calculation. |
| [`main/data_handler/`](main/data_handler/) | Timestamping, CSV formatting, JSON payloads, and topic names. |
| [`main/sd_utils/`](main/sd_utils/) | SPI/FAT mounting, file operations, and connection settings parsing. |
| [`main/wifi_utils/`](main/wifi_utils/) | Wi-Fi station initialization and connection events. |
| [`main/mqtt_utils/`](main/mqtt_utils/) | ESP-MQTT client lifecycle and publication. |
| [`images/`](images/) | Prototype photographs, demonstration GIF, and dashboard screenshot. |

The available academic material is the [PDF presentation](presentation.pdf) and its [PPTX source](presentation.pptx). Separate proposal and report files are not included in this checkout. The dashboard shown in the project material is not accompanied by a backend deployment or dashboard configuration in this repository.

## Original academic credits

**Embedded Systems Architectures — University of Aveiro**

- **Francisco Ribeiro** — student number **107993**; [GitHub](https://github.com/FranciscoRibeiro03).
- **Miguel Vila** — student number **107276**; [GitHub](https://github.com/miguelovila).

Both authors were students at the [University of Aveiro](https://www.ua.pt/) when the project was developed.
