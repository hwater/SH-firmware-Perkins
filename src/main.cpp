// Signal K application template file.
//
// This application demonstrates core SensESP concepts in a very
// concise manner. You can build and upload the application as is
// and observe the value changes on the serial port monitor.
//
// You can use this source file as a basis for your own projects.
// Remove the parts that are not relevant to you, and add your own code
// for external hardware libraries.

#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// #define ESP32_CAN_TX_PIN GPIO_NUM_32
// #define ESP32_CAN_RX_PIN GPIO_NUM_34
//  Pin definitions for normal test hardware
// ── CAN-Pins VOR NMEA2000_CAN.h definieren ──────────────────────────────
//#define ESP32_CAN_TX_PIN GPIO_NUM_5
//#define ESP32_CAN_RX_PIN GPIO_NUM_4

static const uint8_t A4_ADC_PIN = 34;
static const uint8_t A5_ADC_PIN = 35;
static const uint8_t Beeper_LED_PIN = 12;
static const uint8_t Taster_PIN = 0;

#ifdef ENABLE_NMEA2000_OUTPUT
#include "NMEA2000_esp32.h"
#include "ESP32_CAN_def.h"  // MODULE_CAN SJA1000 register access for diagnostics
#endif

#include "n2k_senders.h"
#include "sensesp/net/discovery.h"
#include "sensesp/sensors/analog_input.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp/sensors/sensor.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp/system/system_status_led.h"
#include "sensesp/transforms/curveinterpolator.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/lambda_transform.h"
#include "sensesp/transforms/linear.h"
#include "sensesp/ui/config_item.h"

#ifdef ENABLE_SIGNALK
#include "sensesp_app_builder.h"
#include "secrets.h"  // AP_SSID, AP_PASS, OTA_PASSWORD (gitignored)
#define BUILDER_CLASS SensESPAppBuilder
#else
#include "sensesp_minimal_app_builder.h"
#endif

#include "halmet_analog.h"
#include "halmet_const.h"
#include "halmet_digital.h"
#include "halmet_display.h"
#include "halmet_serial.h"
#include "sensesp/net/http_server.h"
#include "sensesp/net/networking.h"
#include "sensesp_onewire/onewire_temperature.h"
#include "sensesp/system/saveable.h"

using namespace sensesp;
using namespace halmet;
using namespace sensesp::onewire;

// Helper to reach a registered ConfigItem's underlying object. SensESP 3.3's
// ConfigItemT::get_config_object() does not compile, so this subclass exposes
// the protected config_object_ pointer (used to grab the HTTPServer).
template <typename T>
struct ConfigItemPeek : public ConfigItemT<T> {
  static std::shared_ptr<T> grab(ConfigItemT<T>& it) {
    return static_cast<ConfigItemPeek<T>&>(it).config_object_;
  }
};

// AP auto-shutdown — configurable, default 3 minutes (0 = never)
class APShutoffConfig : public FileSystemSaveable {
 public:
  int shutoff_min = 3;
  APShutoffConfig() : FileSystemSaveable("/ap/config") { load(); }
  bool from_json(const JsonObject& obj) override {
    if (obj["shutoff_min"].is<int>()) shutoff_min = obj["shutoff_min"];
    return true;
  }
  bool to_json(JsonObject& obj) override {
    obj["shutoff_min"] = shutoff_min;
    return true;
  }
};
const String ConfigSchema(const APShutoffConfig&) {
  return R"###({"type":"object","properties":{"shutoff_min":{"title":"AP Abschaltung (Min)","type":"integer","description":"AP nach N Minuten abschalten. 0 = nie."}}})###";
}

// Engine hour meter, fed to PGN 127489 "Engine Total Hours of Operation".
// The engine counts as running whenever the fuel rate exceeds threshold_lph.
// The reading is base_hours (what the analogue gauge showed when the meter was
// last set) plus run_seconds accumulated since.
class EngineHours : public FileSystemSaveable {
 public:
  double base_hours = 1445.7;   // analogue gauge reading, 2026-07-16
  double threshold_lph = 0.1;   // above this fuel rate the engine is running
  uint32_t run_seconds = 0;     // accumulated since base_hours was set

  EngineHours() : FileSystemSaveable("/engine/hours") { load(); }

  double total_hours() const { return base_hours + run_seconds / 3600.0; }
  uint32_t total_seconds() const {
    return (uint32_t)(base_hours * 3600.0 + 0.5) + run_seconds;
  }

  bool from_json(const JsonObject& obj) override {
    if (obj["base_hours"].is<double>()) base_hours = obj["base_hours"];
    if (obj["threshold_lph"].is<double>()) threshold_lph = obj["threshold_lph"];
    // run_seconds is deliberately absent from ConfigSchema, so a form submit
    // from the web UI carries no value here and zeroes the accumulator: typing
    // a gauge reading sets the meter to exactly that. Loading from flash does
    // carry it, and restores the running total.
    run_seconds = obj["run_seconds"].is<uint32_t>() ? obj["run_seconds"] : 0;
    return true;
  }
  bool to_json(JsonObject& obj) override {
    obj["base_hours"] = base_hours;
    obj["threshold_lph"] = threshold_lph;
    obj["run_seconds"] = run_seconds;
    return true;
  }
};
// Plausibility limit for the fuel-flow reading. The turbine sensor emits
// isolated spikes of 68-90 L/h with the engine stopped (seen 2026-07-18,
// three of them), which are physically impossible on this engine — it draws
// 2-6 L/h under way. Anything above the limit is treated as interference:
// dropped rather than published, so it reaches neither the hour meter nor
// NMEA 2000 / Signal K.
class FuelPlausibility : public FileSystemSaveable {
 public:
  // 35 L/h sits above the calibrated curve's own maximum (200 Hz -> 30 L/h),
  // so no genuine full-load reading is ever rejected, while the interference
  // spikes seen on 2026-07-18 (68-90 L/h) are well clear of it. Do not lower
  // this below the curve's top output.
  double max_lph = 35.0;

  FuelPlausibility() : FileSystemSaveable("/Fuel Flow/Plausibility") { load(); }

  bool from_json(const JsonObject& obj) override {
    if (obj["max_lph"].is<double>()) max_lph = obj["max_lph"];
    return true;
  }
  bool to_json(JsonObject& obj) override {
    obj["max_lph"] = max_lph;
    return true;
  }
};
const String ConfigSchema(const FuelPlausibility&) {
  return R"###({"type":"object","properties":{"max_lph":{"title":"Max. plausibler Verbrauch (L/h)","type":"number","description":"Messwerte darueber gelten als Stoerimpuls und werden verworfen (nicht gezaehlt, nicht gesendet). NICHT unter den Hoechstwert der Durchfluss-Kurve setzen (derzeit 30 L/h bei 200 Hz), sonst werden echte Volllastwerte verworfen. 0 schaltet die Pruefung ab."}}})###";
}

const String ConfigSchema(const EngineHours&) {
  return R"###({"type":"object","properties":{"base_hours":{"title":"Zaehlerstand (h)","type":"number","description":"Motorstunden-Zaehlerstand. Eingeben = Zaehler exakt auf diesen Wert setzen (z.B. vom analogen Messgeraet ablesen). Die seither gezaehlte Laufzeit wird dabei zurueckgesetzt."},"threshold_lph":{"title":"Laufschwelle (L/h)","type":"number","description":"Ueber diesem Kraftstoffverbrauch gilt der Motor als laufend."}}})###";
}


#ifndef ENABLE_SIGNALK
#define BUILDER_CLASS SensESPMinimalAppBuilder
SensESPMinimalApp* sensesp_app;
Networking* networking;
MDNSDiscovery* mdns_discovery;
HTTPServer* http_server;
SystemStatusLed* system_status_led;
#endif

/////////////////////////////////////////////////////////////////////
// Declare some global variables required for the firmware operation.

// (TCP log server on port 23 removed — logging is UART/serial only.)

#ifdef ENABLE_NMEA2000_OUTPUT
// Counts TX and RX CAN frames at driver level so all frames are tallied,
// including NMEA2000 protocol frames (address claiming, ISO requests) that
// the library intercepts before calling the user MsgHandler.
class CountingN2k : public tNMEA2000_esp32 {
 public:
  using tNMEA2000_esp32::tNMEA2000_esp32;
  uint32_t tx_frames = 0;
  uint32_t rx_frames = 0;

 protected:
  bool CANSendFrame(unsigned long id, unsigned char len,
                    const unsigned char* buf, bool wait_sent = true) override {
    bool ok = tNMEA2000_esp32::CANSendFrame(id, len, buf, wait_sent);
    if (ok) tx_frames++;
    return ok;
  }
  bool CANGetFrame(unsigned long &id, unsigned char &len,
                   unsigned char *buf) override {
    bool got = tNMEA2000_esp32::CANGetFrame(id, len, buf);
    if (got) rx_frames++;
    return got;
  }
};

CountingN2k* nmea2000;
elapsedMillis n2k_time_since_rx = 0;
elapsedMillis n2k_time_since_tx = 0;
uint32_t g_can_rx_pkts = 0;  // cached from nmea2000->rx_frames in the event loop
// BUS_OFF auto-recovery bookkeeping at the SJA1000 register level (achtern02 style).
uint32_t g_can_recoveries = 0;
uint32_t g_can_last_recovery_ms = 0;
// Cached CAN values for the /api/data handler. Updated only from the event-loop
// task so the HTTP server task never touches the CAN peripheral directly
// (concurrent register access caused intermittent empty responses).
char g_can_state[12] = "init";
uint8_t g_n2k_addr = 255;
uint32_t g_can_tx = 0, g_can_txerr = 0, g_can_rxerr = 0;

// CAN controller state read straight from the SJA1000 status register.
// NMEA2000_esp32 bypasses the IDF TWAI driver, so there is no getStatus().
inline const char* CanStateStr() {
  uint32_t sr = MODULE_CAN->SR.U;
  if ((sr >> 7) & 1) return "bus_off";    // bus-off
  if ((sr >> 6) & 1) return "err_warn";   // error status (passive/warn)
  return "running";
}
#endif

TwoWire* i2c;
Adafruit_SSD1306* display;

// Store alarm states in an array for local display output
bool alarm_states[4] = {false, false, false, false};

// Display state — updated by sensor lambdas, rendered by page timer
static uint8_t disp_page    = 0;
static float   disp_rpm     = 0;
static float   disp_tank    = -1;     // ratio 0-1, -1 = no data
static float   disp_coolant = -300;   // °C, <-200 = no data
static float   disp_exhaust = -300;
static float   disp_alt     = -300;
static float   g_fuel_lph   = 0;      // current fuel rate, L/h
static float   g_tank_ohms  = -1;     // live fuel sender resistance (ohms), -1 = no data
static float   g_tacho_hz   = 0;      // raw tacho input pulse frequency, Hz
static float   g_fuel_hz    = 0;      // raw fuel-flow input pulse frequency (D1), Hz
static float   g_volt_b     = NAN;    // analog B (ADS1115 ch1) voltage, V
static double  g_engine_h   = 0;      // engine hour meter total, h
static bool    g_engine_run  = false; // engine currently burning fuel
static float   g_fuel_last_ok = 0;    // last plausible fuel rate, L/h
static uint32_t g_fuel_spikes = 0;    // implausible readings dropped since boot
static char    g_hostname[32] = "PERKINS";

// AP shutdown state
static bool     g_ap_on          = true;
static uint32_t g_ap_boot_ms     = 0;
static int      g_ap_shutoff_min = 3;

static void disp_row(Adafruit_SSD1306* d, int row, const char* fmt, ...) {
  char buf[22]; va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  d->setCursor(0, 8 * row); d->print(buf);
}

static void disp_draw(Adafruit_SSD1306* d) {
  d->clearDisplay();
  d->setTextSize(1);
  d->setTextColor(SSD1306_WHITE);
  char hdr[22];
  snprintf(hdr, sizeof(hdr), "-- %s --", g_hostname);
  // Row 0: hostname header, Row 1: RPM + fuel rate (shown on both pages)
  disp_row(d, 0, "%s", hdr);
  disp_row(d, 1, "%4.0f U/min %3.1fL/h", disp_rpm, g_fuel_lph);
  if (disp_page == 0) {
    // Page 1: sensor data (rows 2-7)
    if (isnan(g_volt_b)) disp_row(d, 2, "AnalogB    --");
    else                 disp_row(d, 2, "AnalogB %5.2fV", g_volt_b);
    if (disp_tank >= 0) disp_row(d, 3, "Tank  %4.0f%%", disp_tank * 100);
    else                disp_row(d, 3, "Tank    --");
    if (disp_coolant > -200) disp_row(d, 4, "Kuehw %4.1fC", disp_coolant);
    else                     disp_row(d, 4, "Kuehw    --");
    if (disp_exhaust > -200) disp_row(d, 5, "Auspf %4.1fC", disp_exhaust);
    else                     disp_row(d, 5, "Auspf    --");
    if (disp_alt > -200)     disp_row(d, 6, "Ladr  %4.1fC", disp_alt);
    else                     disp_row(d, 6, "Ladr     --");
    disp_row(d, 7, "D2:%c D3:%c",
             alarm_states[1] ? '!' : 'o', alarm_states[2] ? '!' : 'o');
  } else {
    // Page 2: CAN + SK + network (rows 2-7)
#ifdef ENABLE_NMEA2000_OUTPUT
    disp_row(d, 2, "CAN %s", CanStateStr());
    disp_row(d, 3, "TXe%lu RXe%lu",
             (unsigned long)MODULE_CAN->TXERR.B.TXERR,
             (unsigned long)MODULE_CAN->RXERR.B.RXERR);
    uint8_t src = nmea2000->GetN2kSource();
    if (src != 255) disp_row(d, 4, "N2K addr %d", src);
    else            disp_row(d, 4, "N2K claim..");
#else
    disp_row(d, 2, "CAN disabled");
    disp_row(d, 3, ""); disp_row(d, 4, "");
#endif
    disp_row(d, 5, WiFi.status() == WL_CONNECTED ? "SK  WiFi OK" : "SK  no WiFi");
    disp_row(d, 6, "IP %s", WiFi.localIP().toString().c_str());
    // AP status line
    if (!g_ap_on) {
      disp_row(d, 7, "AP aus");
    } else if (g_ap_shutoff_min == 0) {
      disp_row(d, 7, "AP an | immer");
    } else {
      uint32_t elapsed_s = (millis() - g_ap_boot_ms) / 1000;
      uint32_t shutoff_s = (uint32_t)g_ap_shutoff_min * 60;
      int32_t  remain    = (int32_t)shutoff_s - (int32_t)elapsed_s;
      if (remain > 0) disp_row(d, 7, "AP an | %ds", remain);
      else            disp_row(d, 7, "AP aus");
    }
  }
  d->display();
}

// Set the ADS1115 GAIN to adjust the analog input voltage range.
// On HALMET, this refers to the voltage range of the ADS1115 input
// AFTER the 33.3/3.3 voltage divider.

// GAIN_TWOTHIRDS: 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
// GAIN_ONE:       1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
// GAIN_TWO:       2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
// GAIN_FOUR:      4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
// GAIN_EIGHT:     8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
// GAIN_SIXTEEN:   16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV

const adsGain_t kADS1115Gain = GAIN_ONE;

/////////////////////////////////////////////////////////////////////
// Test output pin configuration. If ENABLE_TEST_OUTPUT_PIN is defined,
// GPIO 33 will output a pulse wave at 380 Hz with a 50% duty cycle.
// If this output and GND are connected to one of the digital inputs, it can
// be used to test that the frequency counter functionality is working.
#define ENABLE_TEST_OUTPUT_PIN
#ifdef ENABLE_TEST_OUTPUT_PIN
const int kTestOutputPin = GPIO_NUM_18;
// With the default pulse rate of 100 pulses per revolution (configured in
// halmet_digital.cpp), this frequency corresponds to 3.8 r/s or about 228 rpm.
const int kTestOutputFrequency = 380;
#endif

/////////////////////////////////////////////////////////////////////
// The setup function performs one-time application initialization.
// Signal-K WebSocket-Client Verbindungsstatus als kurzer Anzeige-String.
// get_connection_state() ist protected, daher das public get_connection_status()
// (liefert englische Strings) nutzen und ins Deutsche uebersetzen. Ohne SignalK
// (Minimal-App, kein ws_client) faellt es auf den WLAN-Status zurueck.
#ifdef ENABLE_SIGNALK
static const char* skStatusStr() {
  if (!sensesp_app) return "n/a";
  auto ws = sensesp_app->get_ws_client();
  if (!ws) return "n/a";
  String s = ws->get_connection_status();
  if (s == "Connected")   return "verbunden";
  if (s == "Connecting")  return "verbindet…";
  if (s.startsWith("Authorizing")) return "autorisiert…";
  if (s == "Disconnected") return "getrennt";
  return "unbekannt";
}
#else
static const char* skStatusStr() {
  return (WiFi.status() == WL_CONNECTED) ? "WLAN ok" : "getrennt";
}
#endif

// Build the live-data JSON served at GET /api/data.
static String BuildDataJson() {
  String s = "{";
  char b[96];
  snprintf(b, sizeof(b), "\"hostname\":\"%s\",\"uptime_s\":%lu,", g_hostname,
           (unsigned long)(millis() / 1000));
  s += b;
  float rpm_v = (isnan(disp_rpm) || isinf(disp_rpm)) ? 0.0f : disp_rpm;
  float fuel_v = (isnan(g_fuel_lph) || isinf(g_fuel_lph)) ? 0.0f : g_fuel_lph;
  snprintf(b, sizeof(b), "\"rpm\":%.0f,\"fuel_lph\":%.2f,", rpm_v, fuel_v);
  s += b;
  snprintf(b, sizeof(b), "\"tacho_hz\":%.1f,", g_tacho_hz);
  s += b;
  snprintf(b, sizeof(b), "\"fuel_hz\":%.1f,", g_fuel_hz);
  s += b;
  snprintf(b, sizeof(b), "\"engine_h\":%.1f,\"engine_run\":%s,", g_engine_h,
           g_engine_run ? "true" : "false");
  s += b;
  snprintf(b, sizeof(b), "\"fuel_spikes\":%lu,", (unsigned long)g_fuel_spikes);
  s += b;
  if (!isnan(g_volt_b)) {
    snprintf(b, sizeof(b), "\"volt_b\":%.2f,", g_volt_b);
    s += b;
  }
  if (disp_coolant > -200) {
    snprintf(b, sizeof(b), "\"coolant_c\":%.1f,", disp_coolant);
    s += b;
  }
  if (disp_exhaust > -200) {
    snprintf(b, sizeof(b), "\"exhaust_c\":%.1f,", disp_exhaust);
    s += b;
  }
  if (disp_alt > -200) {
    snprintf(b, sizeof(b), "\"alt_c\":%.1f,", disp_alt);
    s += b;
  }
  if (disp_tank >= 0) {
    snprintf(b, sizeof(b), "\"tank_pct\":%.0f,", disp_tank * 100.0f);
    s += b;
  }
  if (g_tank_ohms >= 0) {
    snprintf(b, sizeof(b), "\"tank_ohm\":%.0f,", g_tank_ohms);
    s += b;
  }
  snprintf(b, sizeof(b), "\"alarm_d2\":%s,\"alarm_d3\":%s,",
           alarm_states[1] ? "true" : "false",
           alarm_states[2] ? "true" : "false");
  s += b;
#ifdef ENABLE_NMEA2000_OUTPUT
  // All cached values — no CAN peripheral access from the HTTP server task.
  snprintf(b, sizeof(b), "\"can_state\":\"%s\",\"n2k_addr\":%u,", g_can_state,
           g_n2k_addr);
  s += b;
  snprintf(b, sizeof(b), "\"can_tx\":%lu,\"can_rx\":%lu,",
           (unsigned long)g_can_tx, (unsigned long)g_can_rx_pkts);
  s += b;
  snprintf(b, sizeof(b),
           "\"can_txerr\":%lu,\"can_rxerr\":%lu,\"can_recoveries\":%lu,",
           (unsigned long)g_can_txerr, (unsigned long)g_can_rxerr,
           (unsigned long)g_can_recoveries);
  s += b;
#endif
  snprintf(b, sizeof(b), "\"sk\":\"%s\",", skStatusStr());
  s += b;
  snprintf(b, sizeof(b), "\"wifi\":%s,\"ip\":\"%s\",\"free_heap\":%lu}",
           (WiFi.status() == WL_CONNECTED) ? "true" : "false",
           WiFi.localIP().toString().c_str(),
           (unsigned long)ESP.getFreeHeap());
  s += b;
  return s;
}

// Self-contained live dashboard served at GET /dash. Polls /api/data.
const char DASH_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>perkins</title><style>
:root{--bg:#0b1620;--card:#13202c;--fg:#e7eef5;--mut:#7f97a8;--ok:#2ecc71;--warn:#e74c3c;--acc:#00ff7f}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,Arial,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 18px;background:#0e1c28;display:flex;justify-content:space-between;align-items:center}
header h1{font-size:18px;margin:0;letter-spacing:.04em}#conn{font-size:13px;color:var(--mut)}
.brand{display:flex;align-items:center;gap:12px}
.seslogo{display:inline-flex;align-items:center;transition:opacity .15s}
.seslogo img{height:30px;width:auto;display:block}
.seslogo:hover{opacity:.75}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));padding:14px}
.card{background:var(--card);border-radius:10px;padding:14px 16px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 10px}
.row{display:flex;justify-content:space-between;align-items:baseline;padding:4px 0}
.row .l{color:var(--mut);font-size:14px}.row .v{font-variant-numeric:tabular-nums;font-weight:600;color:var(--acc)}
.big{font-size:34px;font-weight:700;color:var(--acc)}.unit{font-size:14px;color:var(--mut);font-weight:400;margin-left:4px}
.stat .row .v{color:var(--fg)}
.pill{padding:2px 9px;border-radius:11px;font-size:12px;font-weight:600}
.pill.ok{background:rgba(46,204,113,.15);color:var(--ok)}.pill.bad{background:rgba(231,76,60,.15);color:var(--warn)}
.pill.off{background:rgba(127,151,168,.15);color:var(--mut)}
</style></head><body>
<header><div class="brand"><a class="seslogo" href="/" title="SensESP Konfiguration"><img src="/SensESP_logo_symbol.svg" alt="SensESP"></a><h1>perkins</h1></div><span id="conn">verbinde…</span></header>
<div class="grid">
<div class="card"><h2>Drehzahl</h2>
<div class="big"><span id="rpm">--</span><span class="unit">U/min</span></div>
<div class="row"><span class="l">W-Puls</span><span class="v"><span id="thz">--</span> Hz</span></div></div>
<div class="card"><h2>Verbrauch</h2>
<div class="big"><span id="fuel">--</span><span class="unit">L/h</span></div>
<div class="row"><span class="l">Durchfluss</span><span class="v"><span id="fhz">--</span> Hz</span></div>
<div class="row"><span class="l">Motorstunden</span><span class="v"><span id="eh">--</span> h</span></div>
<div class="row"><span class="l">Motor</span><span class="pill" id="erun">--</span></div>
<div class="row"><span class="l">Stoerimpulse</span><span class="v" id="fsp">--</span></div></div>
<div class="card"><h2>Temperaturen</h2>
<div class="row"><span class="l">Kühlwasser</span><span class="v"><span id="coolant">--</span> °C</span></div>
<div class="row"><span class="l">Auspuff</span><span class="v"><span id="exhaust">--</span> °C</span></div>
<div class="row"><span class="l">Lichtmaschine</span><span class="v"><span id="alt">--</span> °C</span></div></div>
<div class="card"><h2>Tank &amp; Alarme</h2>
<div class="row"><span class="l">Tank</span><span class="v"><span id="tank">--</span> %</span></div>
<div class="row"><span class="l">Sender</span><span class="v"><span id="tohm">--</span> Ω</span></div>
<div class="row"><span class="l">Analog B</span><span class="v"><span id="vb">--</span> V</span></div>
<div class="row"><span class="l">Öldruck D4</span><span class="pill" id="d2">--</span></div>
<div class="row"><span class="l">Alarm D3</span><span class="pill" id="d3">--</span></div></div>
<div class="card stat"><h2>NMEA 2000</h2>
<div class="row"><span class="l">Status</span><span class="v" id="can">--</span></div>
<div class="row"><span class="l">N2K-Adresse</span><span class="v" id="addr">--</span></div>
<div class="row"><span class="l">TX / RX Pakete</span><span class="v"><span id="tx">--</span> / <span id="rx">--</span></span></div>
<div class="row"><span class="l">TX / RX Fehler</span><span class="v"><span id="txe">--</span> / <span id="rxe">--</span></span></div>
<div class="row"><span class="l">Recoveries</span><span class="v" id="rec">--</span></div></div>
<div class="card stat"><h2>System</h2>
<div class="row"><span class="l">Hostname</span><span class="v" id="host">--</span></div>
<div class="row"><span class="l">IP</span><span class="v" id="ip">--</span></div>
<div class="row"><span class="l">Signal K</span><span class="v" id="sk">--</span></div>
<div class="row"><span class="l">Laufzeit</span><span class="v" id="up">--</span></div>
<div class="row"><span class="l">Freier Speicher</span><span class="v" id="heap">--</span></div></div>
</div>
<script>
var $=function(i){return document.getElementById(i)};
function f(v,d){return (v==null)?'--':(typeof v==='number'?v.toFixed(d):v)}
function upt(s){if(s==null)return'--';var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}
function pill(el,on){el.textContent=on?'AKTIV':'ok';el.className='pill '+(on?'bad':'ok')}
function rpill(el,on){el.textContent=on?'LÄUFT':'aus';el.className='pill '+(on?'ok':'off')}
function setConn(ok){$('conn').textContent=ok?'● live':'○ offline';$('conn').style.color=ok?'#2ecc71':'#e74c3c'}
function render(d){
$('rpm').textContent=f(d.rpm,0);$('thz').textContent=f(d.tacho_hz,1);$('fuel').textContent=f(d.fuel_lph,1);$('fhz').textContent=f(d.fuel_hz,1);
$('eh').textContent=f(d.engine_h,1);rpill($('erun'),d.engine_run);$('fsp').textContent=f(d.fuel_spikes);
$('coolant').textContent=f(d.coolant_c,1);$('exhaust').textContent=f(d.exhaust_c,1);$('alt').textContent=f(d.alt_c,1);
$('tank').textContent=f(d.tank_pct,0);$('tohm').textContent=f(d.tank_ohm,0);pill($('d2'),d.alarm_d2);pill($('d3'),d.alarm_d3);
$('can').textContent=f(d.can_state);$('addr').textContent=f(d.n2k_addr);
$('tx').textContent=f(d.can_tx);$('rx').textContent=f(d.can_rx);
$('txe').textContent=f(d.can_txerr);$('rxe').textContent=f(d.can_rxerr);$('rec').textContent=f(d.can_recoveries);
$('host').textContent=f(d.hostname);$('ip').textContent=f(d.ip);
$('sk').textContent=d.sk||'--';$('up').textContent=upt(d.uptime_s);$('vb').textContent=f(d.volt_b,2);
$('heap').textContent=d.free_heap!=null?Math.round(d.free_heap/1024)+' kB':'--'}
var fails=0;
function schedule(){setTimeout(tick,3000)}
function tick(){
var ac=new AbortController();var to=setTimeout(function(){ac.abort()},5000);
fetch('/api/data',{cache:'no-store',signal:ac.signal})
.then(function(r){if(!r.ok)throw 0;return r.json()})
.then(function(d){clearTimeout(to);fails=0;setConn(true);render(d);schedule()})
.catch(function(e){clearTimeout(to);if(++fails>=3)setConn(false);schedule()})}
tick();
</script></body></html>)rawhtml";

void setup() {
  // Test build: API/sensor debug logs aus. INFO-Level lässt INFO/WARN/ERROR
  // durch, unterdrückt aber alle debugD/debugV (z.B. das pro Messwert geloggte
  // "Pressure A3" und das SensESP-Debug-Geschwätz). Für volles Debug wieder auf
  // ESP_LOG_DEBUG setzen.
  SetupLogging(ESP_LOG_INFO);

  // These calls can be used for fine-grained control over the logging level.
  // esp_log_level_set("*", esp_log_level_t::ESP_LOG_DEBUG);

  Serial.begin(115200);

  /////////////////////////////////////////////////////////////////////
  // Initialize the application framework

  // Construct the global SensESPApp() object
  BUILDER_CLASS builder;
  // WICHTIG: set_hostname() NICHT in der Builder-Chain — es würde bei jedem
  // Boot den gespeicherten Hostnamen überschreiben. Stattdessen nur beim
  // ersten Boot setzen, wenn noch der SensESP-Default gilt.
  sensesp_app = (&builder)
                    ->set_wifi_access_point(AP_SSID, AP_PASS)
                    ->enable_ota(OTA_PASSWORD)
                    ->get_app();

  // Set a lowercase hostname on first boot, and migrate the earlier uppercase
  // "PERKINS" -> "perkins" so the mDNS name matches the case-sensitive Origin
  // check in SensESP 3.3's restart/reset endpoints (accessed via perkins.local).
  {
    String hn = SensESPBaseApp::get_hostname();
    if (hn == "SensESP" || hn == "PERKINS") {
      SensESPBaseApp::get()->get_hostname_observable()->set("perkins");
    }
  }

  // Customize the hostname configuration page.
  auto hostname_ci = ConfigItemBase::get_config_item("/system/hostname");
  if (hostname_ci) {
    hostname_ci->set_title("Geraetename (Hostname)")
        ->set_description(
            "Netzwerkname des Geraets, zugleich mDNS-Name (&lt;name&gt;.local), "
            "Anzeigename und Basis der SignalK-Quelle. Bitte KLEIN schreiben: "
            "Restart/Reset in der Web-UI vergleichen den Hostnamen case-sensitiv "
            "mit der aufgerufenen URL (z. B. perkins.local); bei Grossschreibung "
            "schlaegt das mit \"403 Forbidden\" fehl. Aenderung wirkt nach "
            "Neustart.");
  }

  strncpy(g_hostname, SensESPBaseApp::get_hostname().c_str(), sizeof(g_hostname) - 1);
  g_ap_boot_ms = millis();

  // Disable WiFi modem sleep. Default modem-sleep causes erratic latency and
  // dropped/timed-out HTTP requests on a marginal link; turning it off keeps
  // the radio responsive (at a small extra current draw).
  WiFi.setSleep(false);
  // Re-assert periodically in case the WiFi stack is re-initialised on reconnect.
  event_loop()->onRepeat(30000, []() { WiFi.setSleep(false); });

  // WiFi watchdog: after a router outage the Arduino WiFi stack can hang
  // (e.g. AP channel change after router reboot); SensESP's auto-reconnect
  // does not recover from that (observed 2026-07-21, 21 h offline).
  // Stage 1: after 2 min offline, force a hard reconnect (fresh scan).
  // Stage 2: after 15 min offline, restart - only while the engine is
  // stopped (no RPM gap under way) and WiFi has been connected at least
  // once since boot (avoids a reboot loop while the router stays down).
  event_loop()->onRepeat(30000, []() {
    static uint32_t down_since_ms  = 0;
    static bool     ever_connected = false;
    if (WiFi.status() == WL_CONNECTED) {
      if (!ever_connected) {
        WiFi.setAutoReconnect(true);
        WiFi.persistent(false);  // keep reconnects out of NVS
      }
      ever_connected = true;
      down_since_ms  = 0;
      return;
    }
    if (down_since_ms == 0) { down_since_ms = millis(); return; }
    uint32_t down_min = (millis() - down_since_ms) / 60000UL;
    float rpm = (isnan(disp_rpm) || isinf(disp_rpm)) ? 0.0f : disp_rpm;
    if (down_min >= 15 && ever_connected && rpm < 1.0f) {
      ESP_LOGW("wifi_wd", ">=15 min offline - restarting");
      ESP.restart();
    } else if (down_min >= 2) {
      ESP_LOGW("wifi_wd", "%lu min offline - forcing reconnect",
               (unsigned long)down_min);
      WiFi.disconnect();
      WiFi.reconnect();
    }
  });

  // AP auto-shutdown
  auto ap_cfg = std::make_shared<APShutoffConfig>();
  ConfigItem(ap_cfg)
      ->set_title("WLAN Access-Point")
      ->set_description("AP nach N Minuten abschalten. 0 = nie. Aenderung wirkt nach Neustart.")
      ->set_sort_order(50);
  g_ap_shutoff_min = ap_cfg->shutoff_min;

  event_loop()->onRepeat(5000, [ap_cfg]() {
    if (!g_ap_on || ap_cfg->shutoff_min == 0) return;
    uint32_t elapsed_s = (millis() - g_ap_boot_ms) / 1000;
    if (elapsed_s >= (uint32_t)(ap_cfg->shutoff_min * 60)) {
      WiFi.softAPdisconnect(true);
      g_ap_on = false;
    }
  });


  // initialize the I2C bus
  i2c = new TwoWire(0);
  i2c->begin(kSDAPin, kSCLPin);

  // I2C scan — logs all responding addresses at boot
  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c->beginTransmission(addr);
    if (i2c->endTransmission() == 0) {
      ESP_LOGI("i2c_scan", "Device found at 0x%02X", addr);
    }
  }

  // Initialize ADS1115
  auto ads1115 = new Adafruit_ADS1115();

  ads1115->setGain(kADS1115Gain);
  bool ads_initialized = ads1115->begin(kADS1115Address, i2c);
  debugD("ADS1115 initialized: %d", ads_initialized);

#ifdef ENABLE_TEST_OUTPUT_PIN
  pinMode(kTestOutputPin, OUTPUT);
  // Set the LEDC peripheral to a 13-bit resolution
  ledcSetup(0, kTestOutputFrequency, 13);
  // Attach the channel to the GPIO pin to be controlled
  ledcAttachPin(kTestOutputPin, 0);
  // Set the duty cycle to 50%
  // Duty cycle value is calculated based on the resolution
  // For 13-bit resolution, max value is 8191, so 50% is 4096
  ledcWrite(0, 4096);
#endif

#ifdef ENABLE_NMEA2000_OUTPUT
  /////////////////////////////////////////////////////////////////////
  // Initialize NMEA 2000 functionality

  nmea2000 = new CountingN2k(kCANTxPin, kCANRxPin);

  // Reserve enough buffer for sending all messages.
  nmea2000->SetN2kCANSendFrameBufSize(250);
  nmea2000->SetN2kCANReceiveFrameBufSize(250);

  // Set Product information
  // EDIT: Change the values below to match your device.
  nmea2000->SetProductInformation(
      "20250614",  // Manufacturer's Model serial code (max 32 chars)
      104,         // Manufacturer's product code
      "PERKINS",   // Manufacturer's Model ID (max 33 chars)
      "1.0.2",     // Manufacturer's Software version code (max 40 chars)
      "1.0.2"      // Manufacturer's Model version (max 24 chars)
  );

  // For device class/function information, see:
  // http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf

  // For mfg registration list, see:
  // https://actisense.com/nmea-certified-product-providers/
  // The format is inconvenient, but the manufacturer code below should be
  // one not already on the list.

  // EDIT: Change the class and function values below to match your device.
  nmea2000->SetDeviceInformation(
      GetBoardSerialNumber(),  // Unique number. Use e.g. Serial number.
      140,                     // Device function: Engine
      50,                      // Device class: Propulsion
      2046);                   // Manufacturer code

  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint8_t n2k_addr = ((mac[4] << 8) | mac[5]) % 128;
  nmea2000->SetMode(tNMEA2000::N2km_NodeOnly, n2k_addr);
  nmea2000->EnableForward(false);
  nmea2000->Open();


  // ParseMessages() drives address claiming and processes received N2K frames.
  event_loop()->onRepeat(10, []() { nmea2000->ParseMessages(); });

  // BUS_OFF auto-recovery (achtern02 style): on bus-off the SJA1000 enters reset
  // mode; writing MOD.RM=0 restarts the recovery sequence so the controller
  // rejoins the bus. NMEA2000_esp32 does not do this on its own.
  event_loop()->onRepeat(5000, []() {
    if (MODULE_CAN->SR.B.BS) {
      uint32_t now = millis();
      if (now - g_can_last_recovery_ms < 5000) return;
      g_can_last_recovery_ms = now;
      g_can_recoveries++;
      MODULE_CAN->MOD.B.RM = 1;
      MODULE_CAN->TXERR.U  = 0;
      MODULE_CAN->RXERR.U  = 0;
      (void)MODULE_CAN->ECC;
      (void)MODULE_CAN->IR.U;
      MODULE_CAN->MOD.B.RM = 0;
    }
  });

  // CAN diagnostics via SJA1000 status/error registers.
  auto* n2k_state_sensor = new RepeatSensor<String>(2000, []() -> String {
    return String(CanStateStr());
  });
  auto* n2k_txerr_sensor = new RepeatSensor<float>(2000, []() -> float {
    return (float)MODULE_CAN->TXERR.B.TXERR;
  });
  auto* n2k_rxerr_sensor = new RepeatSensor<float>(2000, []() -> float {
    return (float)MODULE_CAN->RXERR.B.RXERR;
  });
  n2k_state_sensor->connect_to(
      new SKOutputString("diagnostics.n2k.state", "/diagnostics/n2k/state"));
  n2k_txerr_sensor->connect_to(
      new SKOutputFloat("diagnostics.n2k.txErrCnt", "/diagnostics/n2k/txErrCnt"));
  n2k_rxerr_sensor->connect_to(
      new SKOutputFloat("diagnostics.n2k.rxErrCnt", "/diagnostics/n2k/rxErrCnt"));

  // CAN-bus status on the web UI status page (group "CAN-Bus"), like achtern02.
  auto* st_can_state = new StatusPageItem<String>("Status", "unbekannt", "CAN-Bus", 100);
  auto* st_n2k_addr  = new StatusPageItem<uint8_t>("N2K-Adresse", 0, "CAN-Bus", 110);
  auto* st_can_txpkt = new StatusPageItem<uint32_t>("TX Pakete", 0, "CAN-Bus", 120);
  auto* st_can_rxpkt = new StatusPageItem<uint32_t>("RX Pakete", 0, "CAN-Bus", 130);
  auto* st_can_txerr = new StatusPageItem<uint32_t>("TX Fehler", 0, "CAN-Bus", 140);
  auto* st_can_rxerr = new StatusPageItem<uint32_t>("RX Fehler", 0, "CAN-Bus", 150);
  auto* st_can_recov = new StatusPageItem<uint32_t>("Recoveries", 0, "CAN-Bus", 160);

  event_loop()->onRepeat(1000, [st_can_state, st_n2k_addr, st_can_txpkt,
                                st_can_rxpkt, st_can_txerr, st_can_rxerr,
                                st_can_recov]() {
    // Read the CAN peripheral once here (event-loop task) and cache for both
    // the status page items and the /api/data HTTP handler.
    strncpy(g_can_state, CanStateStr(), sizeof(g_can_state) - 1);
    g_can_state[sizeof(g_can_state) - 1] = '\0';
    g_n2k_addr = nmea2000->GetN2kSource();
    g_can_tx = nmea2000->tx_frames;
    g_can_rx_pkts = nmea2000->rx_frames;
    g_can_txerr = (uint32_t)MODULE_CAN->TXERR.B.TXERR;
    g_can_rxerr = (uint32_t)MODULE_CAN->RXERR.B.RXERR;

    st_can_state->set(String(g_can_state));
    st_n2k_addr->set(g_n2k_addr);
    st_can_txpkt->set(g_can_tx);
    st_can_rxpkt->set(g_can_rx_pkts);
    st_can_txerr->set(g_can_txerr);
    st_can_rxerr->set(g_can_rxerr);
    st_can_recov->set(g_can_recoveries);
  });
#endif  // ENABLE_NMEA2000_OUTPUT

  // Engine & sensor values on the web UI status page (group "Sensoren"),
  // compact — mirrors the /dash readout. Not CAN-dependent, so it lives outside
  // the ENABLE_NMEA2000_OUTPUT guard. Values are formatted strings with units
  // and show "--" when the sensor has no data.
  auto* st_rpm     = new StatusPageItem<String>("Drehzahl", "--", "Sensoren", 10);
  auto* st_tacho_hz = new StatusPageItem<String>("Drehzahl Puls", "--", "Sensoren", 15);
  auto* st_fuel    = new StatusPageItem<String>("Kraftstoff", "--", "Sensoren", 20);
  auto* st_engine_h = new StatusPageItem<String>("Motorstunden", "--", "Sensoren", 25);
  auto* st_fuel_spikes = new StatusPageItem<String>("Stoerimpulse Verbrauch", "--", "Sensoren", 26);
  auto* st_coolant = new StatusPageItem<String>("Kühlwasser", "--", "Sensoren", 30);
  auto* st_exhaust = new StatusPageItem<String>("Auspuff", "--", "Sensoren", 40);
  auto* st_alt     = new StatusPageItem<String>("Lichtmaschine", "--", "Sensoren", 50);
  auto* st_tank    = new StatusPageItem<String>("Tank", "--", "Sensoren", 60);
  auto* st_tank_ohm = new StatusPageItem<String>("Tank Sender", "--", "Sensoren", 65);
  auto* st_al_d2   = new StatusPageItem<String>("Öldruck D4", "--", "Sensoren", 70);
  auto* st_al_d3   = new StatusPageItem<String>("Alarm D3", "--", "Sensoren", 80);
  auto* st_volt_b  = new StatusPageItem<String>("Analog B", "--", "Sensoren", 90);

  event_loop()->onRepeat(1000, [st_rpm, st_tacho_hz, st_fuel, st_engine_h,
                                st_fuel_spikes, st_coolant, st_exhaust, st_alt,
                                st_tank, st_tank_ohm, st_al_d2, st_al_d3,
                                st_volt_b]() {
    char v[24];
    float rpm = (isnan(disp_rpm) || isinf(disp_rpm)) ? 0.0f : disp_rpm;
    snprintf(v, sizeof(v), "%.0f U/min", rpm);
    st_rpm->set(v);
    snprintf(v, sizeof(v), "%.1f Hz", g_tacho_hz);
    st_tacho_hz->set(v);
    float fuel = (isnan(g_fuel_lph) || isinf(g_fuel_lph)) ? 0.0f : g_fuel_lph;
    snprintf(v, sizeof(v), "%.2f L/h", fuel);
    st_fuel->set(v);
    snprintf(v, sizeof(v), "%.1f h%s", g_engine_h, g_engine_run ? " (läuft)" : "");
    st_engine_h->set(v);
    snprintf(v, sizeof(v), "%lu verworfen", (unsigned long)g_fuel_spikes);
    st_fuel_spikes->set(v);
    if (disp_coolant > -200) {
      snprintf(v, sizeof(v), "%.1f °C", disp_coolant);
      st_coolant->set(v);
    } else {
      st_coolant->set("--");
    }
    if (disp_exhaust > -200) {
      snprintf(v, sizeof(v), "%.1f °C", disp_exhaust);
      st_exhaust->set(v);
    } else {
      st_exhaust->set("--");
    }
    if (disp_alt > -200) {
      snprintf(v, sizeof(v), "%.1f °C", disp_alt);
      st_alt->set(v);
    } else {
      st_alt->set("--");
    }
    if (disp_tank >= 0) {
      snprintf(v, sizeof(v), "%.0f %%", disp_tank * 100.0f);
      st_tank->set(v);
    } else {
      st_tank->set("--");
    }
    if (g_tank_ohms >= 0) {
      snprintf(v, sizeof(v), "%.0f Ω", g_tank_ohms);
      st_tank_ohm->set(v);
    } else {
      st_tank_ohm->set("--");
    }
    st_al_d2->set(alarm_states[1] ? "ALARM" : "OK");
    st_al_d3->set(alarm_states[2] ? "ALARM" : "OK");
    if (isnan(g_volt_b)) {
      st_volt_b->set("--");
    } else {
      snprintf(v, sizeof(v), "%.2f V", g_volt_b);
      st_volt_b->set(v);
    }
  });

#ifndef ENABLE_SIGNALK
  // Initialize components that would normally be present in SensESPApp
  networking = new Networking("/System/WiFi Settings", "", "");
  ConfigItem(networking);
  mdns_discovery = new MDNSDiscovery();
  http_server = new HTTPServer();
  system_status_led = new SystemStatusLed(LED_BUILTIN);
#endif

  // Initialize the OLED display
  bool display_present = InitializeSSD1306(sensesp_app.get(), &display, i2c);

  ///////////////////////////////////////////////////////////////////
  // Analog inputs

#ifdef ENABLE_SIGNALK
  bool enable_signalk_output = true;
#else
  bool enable_signalk_output = false;
#endif

  // Connect the tank senders — only when ADS1115 was found.
  // Without this guard, failed I2C reads lock up the bus and block the display.
  if (ads_initialized) {
  // Fuel tank sender on analog C (ADS1115 channel 2).
  auto tank_a1_volume = ConnectTankSender(ads1115, 2, "Fuel", "fuel.main", 3000,
                                          enable_signalk_output, &g_tank_ohms);
  // analog A (ch0) now free; analog B (ch1) is read as a voltage below.
  auto a4_pressure = ConnectTankSender(ads1115, 3, "Pressure", "engine.main",
                                       3010, enable_signalk_output);

#ifdef ENABLE_NMEA2000_OUTPUT
  // Tank 1, instance 0. Capacity 200 liters. You can change the capacity
  // in the web UI as well.
  // EDIT: Make sure this matches your tank configuration above.
  N2kFluidLevelSender* tank_a1_sender = new N2kFluidLevelSender(
      "/Tanks/Fuel/NMEA 2000", 0, N2kft_Fuel, 200, nmea2000);

  ConfigItem(tank_a1_sender)
      ->set_title("Tank C NMEA 2000")
      ->set_description("NMEA 2000 tank sender for the fuel tank (analog C)")
      ->set_sort_order(3005);

  tank_a1_volume->connect_to(&(tank_a1_sender->tank_level_));
#endif  // ENABLE_NMEA2000_OUTPUT

  if (display_present) {
    tank_a1_volume->connect_to(new LambdaConsumer<float>(
        [](float value) { disp_tank = value; }));
  }

  // Read the voltage level of analog input A2
  auto a2_voltage = new ADS1115VoltageInput(ads1115, 1, "/Voltage A2");

  ConfigItem(a2_voltage)
      ->set_title("Analog Voltage B")
      ->set_description("Voltage level of analog input B")
      ->set_sort_order(3000);

  a2_voltage->connect_to(new LambdaConsumer<float>(
      [](float value) { g_volt_b = value; }));

  a4_pressure->connect_to(new LambdaConsumer<float>(
      [](float value) { debugD("Pressure A3: %f", value); }));

  // If you want to output something else than the voltage value,
  // you can insert a suitable transform here.
  // For example, to convert the voltage to a distance with a conversion
  // factor of 0.17 m/V, you could use the following code:
  // auto a2_distance = new Linear(0.17, 0.0);
  // a2_voltage->connect_to(a2_distance);

#ifdef ENABLE_SIGNALK
  a2_voltage->connect_to(
      new SKOutputFloat("sensors.a2.voltage", "Analog Voltage B",
                        new SKMetadata("V", "Analog Voltage B")));
  a4_pressure->connect_to(
      new SKOutputFloat("sensors.a4.pressure", "Analog Pressure A3",
                        new SKMetadata("P", "Analog Pressure A3")));
#endif
  }  // end if (ads_initialized)

  ///////////////////////////////////////////////////////////////////
  // Digital alarm inputs

  // EDIT: More alarm inputs can be defined by duplicating the lines below.
  // Make sure to not define a pin for both a tacho and an alarm.
  // D2 (GPIO 13) repurposed as the alternator-W RPM input (see Tacho section).
  auto alarm_d3_input = ConnectAlarmSender(kDigitalInputPin3, "D3");
  // Low-oil-pressure alarm moved from D2 to D4 (GPIO 12). NOTE: GPIO 12 is a
  // strapping pin — if D4 is HIGH at boot the ESP32 may fail to start. A low-oil
  // switch that closes (to GND) when pressure is low keeps D4 LOW at boot (engine
  // off), which is safe.
  auto alarm_d4_input = ConnectAlarmSender(kDigitalInputPin4, "D4");

  // Update the alarm states based on the input value changes.
  // EDIT: If you added more alarm inputs, uncomment the respective lines below.
  // In this example, alarm_d3_input is active low, so invert the value.
  // auto alarm_d3_inverted = alarm_d3_input->connect_to(
  //    new LambdaTransform<bool, bool>([](bool value) { return !value; }));
  alarm_d3_input->connect_to(
      new LambdaTransform<bool, bool>([](bool value) { return value; }));

  alarm_d3_input->connect_to(
      new LambdaConsumer<bool>([](bool value) { alarm_states[2] = value; }));
  // D4 = low-oil-pressure alarm; reuses the alarm_states[1] slot (the former D2
  // tile on /dash + /api/data, relabelled "Oeldruck").
  alarm_d4_input->connect_to(
      new LambdaConsumer<bool>([](bool value) { alarm_states[1] = value; }));

#ifdef ENABLE_NMEA2000_OUTPUT
  // EDIT: This example connects the D2 alarm input to the low oil pressure
  // warning. Modify according to your needs.
  N2kEngineParameterDynamicSender* engine_dynamic_sender =
      new N2kEngineParameterDynamicSender("/NMEA 2000/Engine 1 Dynamic", 0,
                                          nmea2000);

  ConfigItem(engine_dynamic_sender)
      ->set_title("Engine 1 Dynamic")
      ->set_description("NMEA 2000 dynamic engine parameters for engine 1")
      ->set_sort_order(3010);

  // This is just an example -- normally temperature alarms would not be
  // active-low (inverted).
  // alarm_d3_inverted->connect_to(engine_dynamic_sender->over_temperature_);
  alarm_d3_input->connect_to(engine_dynamic_sender->over_temperature_);
  alarm_d4_input->connect_to(engine_dynamic_sender->low_oil_pressure_);

#endif  // ENABLE_NMEA2000_OUTPUT

  // FIXME: Transmit the alarms over SK as well.

  ///////////////////////////////////////////////////////////////////
  // Engine RPM from the alternator "W" terminal (AC frequency proportional to
  // engine RPM), wired to D2 (GPIO 13). The web-configurable "Tacho main
  // Multiplier" converts W pulses -> revolutions: set it to
  // 1 / (W-pulses per engine revolution) = 1 / (alternator pole pairs x pulley
  // ratio). RPM = 60 * W-Hz * multiplier.
  auto tacho_w_frequency = ConnectTachoSender(kDigitalInputPin2, "main", &g_tacho_hz);

#ifdef ENABLE_NMEA2000_OUTPUT
  N2kEngineParameterRapidSender* engine_rapid_sender =
      new N2kEngineParameterRapidSender("/NMEA 2000/Engine 1 Rapid Update", 0,
                                        nmea2000);  // Engine 1, instance 0
  ConfigItem(engine_rapid_sender)
      ->set_title("Engine 1 Rapid Update")
      ->set_description("NMEA 2000 rapid update engine parameters for engine 1")
      ->set_sort_order(3015);
  tacho_w_frequency->connect_to(&(engine_rapid_sender->engine_speed_));
#endif  // ENABLE_NMEA2000_OUTPUT

  if (display_present) {
    tacho_w_frequency->connect_to(new LambdaConsumer<float>(
        [](float value) { disp_rpm = 60 * value; }));
  }

  ///////////////////////////////////////////////////////////////////
  // Fuel flow sensor on D1 (GPIO 15) -> PGN 127489 (Engine Dynamic) fuel rate.

  // Count pulses on D1 and report the count every 500 ms, then convert to Hz.
  auto* fuel_flow_counter = new DigitalInputCounter(
      kDigitalInputPin1, INPUT, RISING, 500, "/Fuel Flow/Counter");
  ConfigItem(fuel_flow_counter)
      ->set_title("Kraftstoff-Durchfluss Eingang")
      ->set_description("Digital-Eingang des Durchflusssensors (D1).");

  auto* fuel_flow_hz = new Frequency(1.0);
  fuel_flow_counter->connect_to(fuel_flow_hz);
  // Expose the raw D4 pulse frequency for the dashboard / diagnostics.
  fuel_flow_hz->connect_to(
      new LambdaConsumer<float>([](float hz) { g_fuel_hz = hz; }));

  // Non-linear turbine sensor: map frequency (Hz) -> fuel rate (L/h) with a
  // configurable interpolation curve. Defaults from the measured points:
  //   0 Hz=0, 21 Hz=4, 66 Hz=10, 200 Hz=20 L/h. Editable in the web UI.
  auto* fuel_curve_defaults = new std::set<CurveInterpolator::Sample>({
      CurveInterpolator::Sample(0.0f, 0.0f),
      CurveInterpolator::Sample(21.0f, 4.0f),
      CurveInterpolator::Sample(66.0f, 10.0f),
      CurveInterpolator::Sample(200.0f, 20.0f),
  });
  auto* fuel_flow_lph =
      new CurveInterpolator(fuel_curve_defaults, "/Fuel Flow/Curve");
  fuel_flow_lph->set_input_title("Frequenz (Hz)")
      ->set_output_title("Durchfluss (L/h)");
  ConfigItem(fuel_flow_lph)
      ->set_title("Kraftstoff-Durchfluss Kurve")
      ->set_description(
          "Stuetzstellen Frequenz (Hz) -> Durchfluss (L/h) des Sensors an D4, "
          "linear interpoliert. Werksdaten: 21 Hz=4, 66 Hz=10, 200 Hz=20 L/h.")
      ->set_sort_order(3020);
  fuel_flow_hz->connect_to(fuel_flow_lph);

  auto* fuel_plausibility = new FuelPlausibility();
  ConfigItem(fuel_plausibility)
      ->set_title("Kraftstoff-Plausibilitaet")
      ->set_description(
          "Verwirft unmoegliche Verbrauchswerte (Stoerimpulse am Eingang D1).")
      ->set_sort_order(3025);

  // Sanitize the curve output: it can be NaN before the first valid reading,
  // which would poison the N2K fuel rate and make /api/data invalid JSON.
  // An implausible spike is dropped by holding the last good value — zeroing
  // instead would punch a hole into the reading mid-run, and the next regular
  // sample (every 500 ms) recovers on its own either way.
  auto* fuel_flow_clean =
      new LambdaTransform<float, float>([fuel_plausibility](float lph) -> float {
        if (isnan(lph) || isinf(lph)) return 0.0f;
        if (fuel_plausibility->max_lph > 0 && lph > fuel_plausibility->max_lph) {
          g_fuel_spikes++;
          return g_fuel_last_ok;
        }
        g_fuel_last_ok = lph;
        return lph;
      });
  fuel_flow_lph->connect_to(fuel_flow_clean);

#ifdef ENABLE_NMEA2000_OUTPUT
  // PGN 127489 fuel rate is in L/h (float -> double for the sender).
  auto* fuel_lph_to_double = new LambdaTransform<float, double>(
      [](float lph) -> double { return (double)lph; });
  fuel_flow_clean->connect_to(fuel_lph_to_double);
  fuel_lph_to_double->connect_to(engine_dynamic_sender->fuel_rate_);
#endif

#ifdef ENABLE_SIGNALK
  // Signal K fuel rate is in m^3/s: L/h / 3.6e6.
  auto* fuel_flow_m3s = new LambdaTransform<float, float>(
      [](float lph) -> float { return lph / 3.6e6f; });
  fuel_flow_clean->connect_to(fuel_flow_m3s);
  fuel_flow_m3s->connect_to(
      new SKOutputFloat("propulsion.main.fuel.rate", "/Fuel Flow/SK Path"));
#endif

  ///////////////////////////////////////////////////////////////////
  // Engine hour meter -> PGN 127489 (Engine Dynamic) total engine hours.

  auto* engine_hours = new EngineHours();
  ConfigItem(engine_hours)
      ->set_title("Motorstunden")
      ->set_description(
          "Betriebsstundenzaehler. Zaehlt, solange der Kraftstoffverbrauch "
          "ueber der Laufschwelle liegt.")
      ->set_sort_order(3030);

  // Tick on a 1 s timer rather than per fuel sample, so the count does not
  // depend on the flow sensor's reporting rate. Flushing to flash on every
  // tick would wear it out, so the accumulator is written when the engine
  // stops and every kHoursSaveSeconds of running: a power cut while running
  // loses at most that much (5 min = 0.08 h, below the gauge's 0.1 h step).
  static const uint32_t kHoursSaveSeconds = 300;
  event_loop()->onRepeat(1000, [engine_hours]() {
    static bool was_running = false;
    static uint32_t unsaved = 0;

    float fuel = (isnan(g_fuel_lph) || isinf(g_fuel_lph)) ? 0.0f : g_fuel_lph;
    bool running = fuel > engine_hours->threshold_lph;
    if (running) {
      engine_hours->run_seconds++;
      unsaved++;
    }
    if (unsaved > 0 && ((was_running && !running) || unsaved >= kHoursSaveSeconds)) {
      engine_hours->save();
      unsaved = 0;
    }
    was_running = running;
    g_engine_run = running;
    g_engine_h = engine_hours->total_hours();
  });

#ifdef ENABLE_NMEA2000_OUTPUT
  // PGN 127489 carries engine hours in seconds. Refresh every second, well
  // inside the sender's 5 s input expiry, or the field would drop to N/A.
  event_loop()->onRepeat(1000, [engine_hours, engine_dynamic_sender]() {
    engine_dynamic_sender->total_engine_hours_->set(engine_hours->total_seconds());
  });
#endif

#ifdef ENABLE_SIGNALK
  // Signal K propulsion.main.runTime is the total run time in seconds.
  auto* engine_hours_sk =
      new SKOutputInt("propulsion.main.runTime", "/Engine Hours/SK Path");
  event_loop()->onRepeat(10000, [engine_hours, engine_hours_sk]() {
    engine_hours_sk->set((int)engine_hours->total_seconds());
  });
#endif

  ///////////////////////////////////////////////////////////////////
  // Display setup

  // 2-page OLED: page 0 = sensors (default), page 1 = CAN/SK/network.
  // Show the sensor page most of the time; once per minute flip to the status
  // page for 5 seconds. Redraw once a second so live values stay current.
  if (display_present) {
    event_loop()->onRepeat(1000, []() {
      uint32_t phase_s = (millis() / 1000) % 60;  // 0..59 within each minute
      disp_page = (phase_s >= 55) ? 1 : 0;        // last 5s of the minute = page 1
      disp_draw(display);
    });
  }

  /*
       Find all the sensors and their unique addresses. Then, each new instance
       of OneWireTemperature will use one of those addresses. You can't specify
       which address will initially be assigned to a particular sensor, so if
       you have more than one sensor, you may have to swap the addresses around
     on the configuration page for the device. (You get to the configuration
     page by entering the IP address of the device into a browser.)
    */

  /*
     Tell SensESP where the sensor is connected to the board
     ESP32 pins are specified as just the X in GPIOX
  */

  DallasTemperatureSensors* dts = new DallasTemperatureSensors(ONEWIRE_PIN);

  // Define how often SensESP should read the sensor(s) in milliseconds
  uint read_delay = 500;

  // Below are temperatures sampled and sent to Signal K server
  // To find valid Signal K Paths that fits your need you look at this link:
  // https://signalk.org/specification/1.4.0/doc/vesselsBranch.html

  // Measure coolant temperature
  auto coolant_temp =
      new OneWireTemperature(dts, read_delay, "/coolantTemperature/oneWire");

  ConfigItem(coolant_temp)
      ->set_title("Coolant Temperature")
      ->set_description("Temperature of the engine coolant")
      ->set_sort_order(100);

  auto coolant_temp_calibration =
      new Linear(1.0, 0.0, "/coolantTemperature/linear");

  ConfigItem(coolant_temp_calibration)
      ->set_title("Coolant Temperature Calibration")
      ->set_description("Calibration for the coolant temperature sensor")
      ->set_sort_order(110);

  auto coolant_temp_sk_output = new SKOutputFloat(
      "propulsion.mainEngine.coolantTemperature", "/coolantTemperature/skPath");

  ConfigItem(coolant_temp_sk_output)
      ->set_title("Coolant Temperature Signal K Path")
      ->set_description("Signal K path for the coolant temperature")
      ->set_sort_order(120);

  coolant_temp->connect_to(coolant_temp_calibration)
      ->connect_to(coolant_temp_sk_output);

  if (display_present) {
    coolant_temp->connect_to(new LambdaConsumer<float>([](float value) {
      disp_coolant = value - 273.15;
    }));
  }

  // Measure exhaust temperature
  auto* exhaust_temp =
      new OneWireTemperature(dts, read_delay, "/exhaustTemperature/oneWire");

  ConfigItem(exhaust_temp)
      ->set_title("Exhaust Temperature")
      ->set_description("Temperature of the Exhaust")
      ->set_sort_order(200);

  auto* exhaust_temp_calibration =
      new Linear(1.0, 0.0, "/exhaustTemperature/linear");

  ConfigItem(exhaust_temp_calibration)
      ->set_title("Exhaust Temperature Calibration")
      ->set_description("Calibration for the Exhaust temperature sensor")
      ->set_sort_order(210);

  auto* exhaust_temp_sk_output = new SKOutputFloat(
      "propulsion.mainEngine.exhaustTemperature", "/exhaustTemperature/skPath");

  ConfigItem(exhaust_temp_sk_output)
      ->set_title("Exhaust Temperature Signal K Path")
      ->set_description("Signal K path for the Ehaust temperature")
      ->set_sort_order(220);

  exhaust_temp->connect_to(exhaust_temp_calibration)
      ->connect_to(exhaust_temp_sk_output);

  if (display_present) {
    exhaust_temp->connect_to(new LambdaConsumer<float>([](float value) {
      disp_exhaust = value - 273.15;
    }));
  }

  // Measure temperature of 12v alternator
  auto* alt_12v_temp =
      new OneWireTemperature(dts, read_delay, "/12vAltTemperature/oneWire");

  ConfigItem(alt_12v_temp)
      ->set_title("12V-Alternator Temperature")
      ->set_description("Temperature of the Alternator")
      ->set_sort_order(300);

  auto* alt_12v_temp_calibration =
      new Linear(1.0, 0.0, "/12AltTemperature/linear");

  ConfigItem(alt_12v_temp_calibration)
      ->set_title("12V Alternator Temperature Calibration")
      ->set_description("Calibration for the 12V-Alternator temperature sensor")
      ->set_sort_order(310);

  auto* alt_12v_temp_sk_output = new SKOutputFloat(
      "electrical.alternators.12V.temperature", "/12vAltTemperature/skPath");

  ConfigItem(alt_12v_temp_sk_output)
      ->set_title("12V-Alternator Temperature Signal K Path")
      ->set_description("Signal K path for the 12V Alternator temperature")
      ->set_sort_order(320);

  alt_12v_temp->connect_to(alt_12v_temp_calibration)
      ->connect_to(alt_12v_temp_sk_output);

  if (display_present) {
    alt_12v_temp->connect_to(new LambdaConsumer<float>([](float value) {
      disp_alt = value - 273.15;
    }));
  }

  // Keep the latest fuel rate available for the /api/data endpoint.
  fuel_flow_clean->connect_to(
      new LambdaConsumer<float>([](float lph) { g_fuel_lph = lph; }));

  // Custom live-data JSON endpoint on the SensESP web server: GET /api/data
  auto httpserver_ci = ConfigItemBase::get_config_item("/system/httpserver");
  if (httpserver_ci) {
    auto cit = std::static_pointer_cast<ConfigItemT<HTTPServer>>(httpserver_ci);
    std::shared_ptr<HTTPServer> http_srv =
        ConfigItemPeek<HTTPServer>::grab(*cit);
    if (http_srv) {
      auto data_handler = std::make_shared<HTTPRequestHandler>(
          1 << HTTP_GET, "/api/data", [](httpd_req_t* req) -> esp_err_t {
            String json = BuildDataJson();
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            httpd_resp_send(req, json.c_str(), json.length());
            return ESP_OK;
          });
      http_srv->add_handler(data_handler);

      // Live dashboard: GET /dash
      auto dash_handler = std::make_shared<HTTPRequestHandler>(
          1 << HTTP_GET, "/dash", [](httpd_req_t* req) -> esp_err_t {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_send(req, DASH_HTML, strlen(DASH_HTML));
            return ESP_OK;
          });
      http_srv->add_handler(dash_handler);

      // Status shortcut: GET /status -> redirect to the SensESP web UI at /,
      // which hosts the device status/config page (restart/reset, SK paths).
      auto status_handler = std::make_shared<HTTPRequestHandler>(
          1 << HTTP_GET, "/status", [](httpd_req_t* req) -> esp_err_t {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/");
            httpd_resp_send(req, "", 0);
            return ESP_OK;
          });
      http_srv->add_handler(status_handler);
    }
  }

  // To avoid garbage collecting all shared pointers created in setup(),
  // loop from here.
  while (true) {
    loop();
  }
}

void loop() { event_loop()->tick(); }