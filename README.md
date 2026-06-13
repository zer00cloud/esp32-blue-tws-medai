# ESP32 A2DP Source → Media Player TWS

ESP32 (NodeMCU-32S) sebagai **Bluetooth Classic A2DP Source** yang mengirim audio ke TWS earbuds / speaker Bluetooth.

## Fitur

- Classic Bluetooth A2DP Source (Bluedroid stack)
- Auto scan & connect ke TWS via GAP inquiry
- Audio callback model — stack panggil aplikasi saat butuh data PCM
- Stereo 44.1kHz, square wave 1000Hz test tone
- Auto-reconnect saat disconnect
- Target: NodeMCU-32S (ESP32) dengan ESP-IDF 5.5.0

## Hardware

| Komponen | Keterangan |
|----------|------------|
| NodeMCU-32S | ESP32 dev board (4MB flash) |
| TWS Earbuds | Bluetooth Classic A2DP Sink |

## Cara Pakai

### 1. Build & Flash

```bash
# Install PlatformIO CLI jika belum ada
pip install platformio

# Clone repo
git clone https://github.com/zer00cloud/esp32-blue-tws-medai.git
cd esp32-blue-tws-medai

# Build
pio run

# Flash (ganti /dev/ttyUSB0 dengan port kamu)
pio run --target upload -p /dev/ttyUSB0

# Monitor serial
pio device monitor --baud 115200
```

### 2. Pairing TWS

1. Matikan TWS earbuds
2. Nyalakan TWS → otomatis masuk **pairing mode** (LED berkedip cepat)
3. ESP32 akan scan dan connect otomatis
4. Tunggu log di serial monitor:

```
A2DP Source OK
A2DP CONNECTED!
Media Ctrl ACK
Audio State: 1
```

5. **Bunyi beep 1000Hz** terdengar dari TWS

### 3. Ganti Alamat TWS

Edit `src/main.c` line 14:

```c
static uint8_t TWS_BDADDR[6] = {0xCF, 0xD6, 0x75, 0xA4, 0x81, 0x00};
```

Ganti dengan alamat MAC TWS kamu. Cari pakai HP:
- Android: Settings → Bluetooth → Scan → lihat detail perangkat
- iOS: tidak menampilkan MAC langsung, pakai app seperti **LightBlue**

## Arsitektur

```
┌─────────────┐     A2DP Source      ┌──────────────┐
│   ESP32      │ ──────────────────→ │  TWS Earbuds  │
│  (SBC Enc)   │   Bluetooth Classic │  (A2DP Sink)  │
└──────┬──────┘                      └──────────────┘
       │
       │  audio_data_callback()
       │  (dipanggil stack saat butuh PCM)
       ▼
  Square Wave / Audio Data
  44.1kHz, Stereo, 16-bit
```

## Flow Kode

1. **Init** — NVS → BT Controller (Classic BT mode) → Bluedroid
2. **Register** — GAP callback, A2DP Source callback, audio data callback
3. **Scan** — GAP inquiry mencari device Bluetooth nearby
4. **Connect** — `esp_a2d_source_connect()` ke TWS
5. **Stream** — Setelah connect, stack panggil `audio_data_callback()` secara periodik
6. **Audio** — Callback isi buffer PCM (square wave 1000Hz stereo)

## Debugging

### Serial Log Penting

| Log | Arti |
|-----|------|
| `BT ctrl enable fail` | Mode BT salah, cek sdkconfig |
| `A2DP CONNECTED!` | Berhasil connect ke TWS |
| `Media Ctrl ACK: status=0` | Stream mulai |
| `Audio State: 1` | Streaming aktif |
| `UNDERFLOW` | Buffer kosong, data PCM kurang cepat |

### Masalah Umum

**BT ctrl enable fail: ESP_ERR_INVALID_ARG**
- Pastikan `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` di sdkconfig
- Pastikan `esp_bt_controller_enable()` pakai mode yang sesuai

**Tidak ada suara / Underflow**
- Pakai callback model (`esp_a2d_source_register_data_callback`), bukan push model
- Jangan pakai `esp_a2d_source_audio_data_send()` untuk streaming

**TWS tidak connect**
- Pastikan TWS dalam pairing mode
- Cek alamat MAC benar
- Matikan TWS dari HP dulu (supaya tidak conflict)

## Config Penting (sdkconfig)

```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_A2DP_ENABLE=y
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=y
```

## Referensi

- [ESP-IDF A2DP API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_a2d.html)
- [ESP-IDF GAP BT API](https://docs.espressif.com/projects/esp-idf/en/latest/esp-idf/api-reference/bluetooth/esp_gap_bt.html)
- [Project Referensi](https://github.com/Mpraveenkumar61/ESP32-A2DP-Source-Carvaan) — A2DP Source dengan btstack

## Lisensi

MIT
