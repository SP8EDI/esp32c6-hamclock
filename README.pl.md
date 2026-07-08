# HamQSL Solar Propagation Monitor
## Waveshare ESP32-C6-LCD-1.47 | PlatformIO | LovyanGFX

Monitor dla radioamatorów: propagacja HF, zegar PL/UTC, DX spoty i pogoda.

[English version](README.md)

---

## Ekrany (zmiana automatyczna co 10s)

| # | Ekran | Źródło | Odświeżanie |
|---|-------|--------|-------------|
| 1 | HF Propagation (pasma dzień/noc) | hamqsl.com | 60 min |
| 2 | Zegar POLSKA + UTC | NTP (auto DST) | 1 s |
| 3 | DX Spots (5 ostatnich) | dxsummit.fi | 5 min |
| 4 | Pogoda (temp/ciśnienie/trend/wiatr) | open-meteo.com | 15 min |

Domyślna lokalizacja pogody: **Jasionka** (lotnisko EPRZ, 50.110/22.019).

---

## Panel webowy

Po połączeniu z WiFi ekran pokazuje adres IP. W przeglądarce:

| Adres | Funkcja |
|-------|---------|
| `http://<IP>/` | Status + **zmiana lokalizacji pogody** (nazwa, lat, lon) |
| `http://<IP>/log` | Debug log (auto-refresh 5s) |
| `http://<IP>/refresh` | Wymuś odświeżenie wszystkich danych |

Lokalizacja zapisuje się w NVS — przetrwa restart. Przykłady:
Jasionka `50.110, 22.019` | Rzeszów `50.041, 21.999` | Solina `49.395, 22.462`

---

## Kompilacja (PlatformIO)

1. Otwórz folder w VSCode z PlatformIO
2. **✓ Build** → biblioteki (WiFiManager, LovyanGFX) pobiorą się same
3. Podłącz przez USB-C, **→ Upload**
4. Monitor: 115200 baud

Płytka ma **8MB flash** — własna tablica partycji w `partitions.csv` (app 3264 KB).

---

## Pierwsze uruchomienie

1. Urządzenie wystawia otwarte WiFi: **`HamQSL-Setup`** (bez hasła)
2. Połącz się i otwórz `192.168.4.1`
3. Wybierz sieć domową, wpisz hasło, Save
4. Po połączeniu ekran pokaże IP + adres panelu (3 s)

**Reset WiFi:** przytrzymaj przycisk **BOOT (GPIO9)** podczas startu.

---

## Sprzęt / piny (fabryczne, nie zmieniaj)

| Sygnał | GPIO |
|--------|------|
| MOSI | 6 |
| SCLK | 7 |
| CS | 14 |
| DC | 15 |
| RST | 21 |
| BLK (podświetlenie PWM) | 22 |

Kluczowe dla tego panelu: `invert=true` w konfiguracji LovyanGFX (bez tego kolory
są odwrócone — białe tło). Jasność: `BL_BRIGHT` w main.cpp (domyślnie 120/255,
żeby ekran się nie grzał).

---

## Obudowy do druku 3D (`enclosure/`)

- **`hamqsl_panel.scad`** — front panel w stylu ICOM IC-706 ze składanymi nóżkami
- **`hamqsl_crt_monitor.scad`** — mini monitor CRT (głowa + szyja + podstawa T)

Otwórz w [OpenSCAD](https://openscad.org), ustaw `PRINT_PART`, F6, eksport STL.
Szczegóły (śruby, materiał) w komentarzach na górze plików.

---

## Rozwiązywanie problemów

| Problem | Rozwiązanie |
|---------|-------------|
| Białe/jasne tło | Sprawdź `pcfg.invert = true` w klasie LGFX |
| Pogoda "Brak danych" | Sprawdź log `http://<IP>/log` — kod HTTP i JSON |
| DX pasmo "?" | Częstotliwość poza pasmami HAM (parser liczy pasmo z kHz) |
| Firmware za duży | Upewnij się że `partitions.csv` jest obok platformio.ini |
| Zegar --:--:-- | NTP nie zsynchronizował — sprawdź internet, poczekaj minutę |

Dane: hamqsl.com (N0NBH) | dxsummit.fi | open-meteo.com (CC BY 4.0)
