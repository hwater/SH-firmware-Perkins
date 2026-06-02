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

#define ESP32_CAN_TX_PIN GPIO_NUM_32
#define ESP32_CAN_RX_PIN GPIO_NUM_34

#ifdef ENABLE_NMEA2000_OUTPUT
#include "Nmea2kTwai.h"
#endif

#include "n2k_senders.h"
#include "sensesp/net/discovery.h"
#include "sensesp/sensors/analog_input.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp/sensors/sensor.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp/system/system_status_led.h"
#include "sensesp/transforms/lambda_transform.h"
#include "sensesp/transforms/linear.h"
#include "sensesp/ui/config_item.h"

#ifdef ENABLE_SIGNALK
#include "sensesp_app_builder.h"
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

// TCP log server — connect with: pio device monitor --port socket://10.0.1.89:23
static WiFiServer      log_server(23);
static WiFiClient      log_client;
static bool            log_server_started = false;
static vprintf_like_t  orig_log_vprintf   = nullptr;

static int log_vprintf(const char* fmt, va_list args) {
  // Copy args before the first consumer exhausts them
  va_list args_tcp;
  va_copy(args_tcp, args);

  // UART output via the original handler
  int len = orig_log_vprintf ? orig_log_vprintf(fmt, args) : vprintf(fmt, args);

  // TCP output to connected client
  if (log_client && log_client.connected()) {
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, args_tcp);
    if (n > 0) {
      log_client.write(reinterpret_cast<const uint8_t*>(buf), n);
      log_client.flush();
    }
  }

  va_end(args_tcp);
  return len;
}

#ifdef ENABLE_NMEA2000_OUTPUT
Nmea2kTwai* nmea2000;
elapsedMillis n2k_time_since_rx = 0;
elapsedMillis n2k_time_since_tx = 0;
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
  // Row 0: hostname header, Row 1: blank
  disp_row(d, 0, "%s", hdr);
  disp_row(d, 1, "");
  if (disp_page == 0) {
    // Page 1: sensor data (rows 2-7)
    disp_row(d, 2, "RPM  %5.0f", disp_rpm);
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
    auto st = nmea2000->getStatus();
    const char* can_s =
      st.state == Nmea2kTwai::ST_RUNNING    ? "running" :
      st.state == Nmea2kTwai::ST_BUS_OFF   ? "bus-off" :
      st.state == Nmea2kTwai::ST_RECOVERING ? "recover" : "error";
    disp_row(d, 2, "CAN %s", can_s);
    disp_row(d, 3, "TXe%lu RXe%lu", st.tx_errors, st.rx_errors);
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
void setup() {
  SetupLogging(ESP_LOG_DEBUG);
  orig_log_vprintf = esp_log_set_vprintf(log_vprintf);

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
                    ->set_wifi_access_point("PERKINS", "Sensorik2000")
                    ->enable_ota("nurichallein2000!")
                    ->get_app();

  if (SensESPBaseApp::get_hostname() == "SensESP") {
    SensESPBaseApp::get()->get_hostname_observable()->set("PERKINS");
  }

  strncpy(g_hostname, SensESPBaseApp::get_hostname().c_str(), sizeof(g_hostname) - 1);
  g_ap_boot_ms = millis();

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

  // Start TCP log server once WiFi is up; accept one client at a time.
  // Heartbeat + client management in one loop for tight coupling.
  event_loop()->onRepeat(1000, []() {
    // Start the server lazily once STA is connected
    if (!log_server_started) {
      if (WiFi.status() == WL_CONNECTED || WiFi.softAPgetStationNum() > 0) {
        log_server.begin();
        log_server_started = true;
        ESP_LOGI("log_server", "TCP log server started, IP=%s",
                 WiFi.localIP().toString().c_str());
      }
      return;
    }

    // Accept new connection (only one client at a time)
    WiFiClient incoming = log_server.available();
    if (incoming) {
      if (log_client) log_client.stop();
      log_client = incoming;
      ESP_LOGI("log_server", "Log client connected");
    }

    // Drop stale client
    if (log_client && !log_client.connected()) {
      log_client.stop();
    }

    // Heartbeat line sent directly via TCP (bypasses vprintf)
    if (log_client && log_client.connected()) {
      char line[128];
      snprintf(line, sizeof(line),
               "heap=%u rpm=%.0f tank=%.2f coolant=%.1fC\r\n",
               esp_get_free_heap_size(), disp_rpm, disp_tank, disp_coolant);
      log_client.print(line);
      log_client.flush();
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

  nmea2000 = new Nmea2kTwai(kCANTxPin, kCANRxPin, 200);

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

  // loop() handles bus-off recovery; ParseMessages() drives address claiming
  // and processes received N2K frames — 10ms is fine for NMEA2000.
  event_loop()->onRepeat(10, []() {
    nmea2000->loop();
    nmea2000->ParseMessages();
  });

  // CAN diagnostics via TWAI driver status
  auto* n2k_state_sensor = new RepeatSensor<String>(2000, []() -> String {
    switch (nmea2000->getStatus().state) {
      case Nmea2kTwai::ST_RUNNING:    return "running";
      case Nmea2kTwai::ST_BUS_OFF:    return "bus_off";
      case Nmea2kTwai::ST_RECOVERING: return "recover";
      default:                        return "error";
    }
  });
  auto* n2k_txerr_sensor = new RepeatSensor<float>(2000, []() -> float {
    return (float)nmea2000->getStatus().tx_errors;
  });
  auto* n2k_rxerr_sensor = new RepeatSensor<float>(2000, []() -> float {
    return (float)nmea2000->getStatus().rx_errors;
  });
  n2k_state_sensor->connect_to(
      new SKOutputString("diagnostics.n2k.state", "/diagnostics/n2k/state"));
  n2k_txerr_sensor->connect_to(
      new SKOutputFloat("diagnostics.n2k.txErrCnt", "/diagnostics/n2k/txErrCnt"));
  n2k_rxerr_sensor->connect_to(
      new SKOutputFloat("diagnostics.n2k.rxErrCnt", "/diagnostics/n2k/rxErrCnt"));
#endif  // ENABLE_NMEA2000_OUTPUT

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
  auto tank_a1_volume = ConnectTankSender(ads1115, 0, "Fuel", "fuel.main", 3000,
                                          enable_signalk_output);
  // auto tank_a2_volume = ConnectTankSender(ads1115, 1, "A2");
  // auto tank_a3_volume = ConnectTankSender(ads1115, 2, "A3");
  auto a4_pressure = ConnectTankSender(ads1115, 3, "Pressure", "engine.main",
                                       3010, enable_signalk_output);

#ifdef ENABLE_NMEA2000_OUTPUT
  // Tank 1, instance 0. Capacity 200 liters. You can change the capacity
  // in the web UI as well.
  // EDIT: Make sure this matches your tank configuration above.
  N2kFluidLevelSender* tank_a1_sender = new N2kFluidLevelSender(
      "/Tanks/Fuel/NMEA 2000", 0, N2kft_Fuel, 200, nmea2000);

  ConfigItem(tank_a1_sender)
      ->set_title("Tank A1 NMEA 2000")
      ->set_description("NMEA 2000 tank sender for tank A1")
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
      ->set_title("Analog Voltage A2")
      ->set_description("Voltage level of analog input A2")
      ->set_sort_order(3000);

  a2_voltage->connect_to(new LambdaConsumer<float>(
      [](float value) { debugD("Voltage A2: %f", value); }));

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
      new SKOutputFloat("sensors.a2.voltage", "Analog Voltage A2",
                        new SKMetadata("V", "Analog Voltage A2")));
  a4_pressure->connect_to(
      new SKOutputFloat("sensors.a4.pressure", "Analog Pressure A3",
                        new SKMetadata("P", "Analog Pressure A3")));
#endif
  }  // end if (ads_initialized)

  ///////////////////////////////////////////////////////////////////
  // Digital alarm inputs

  // EDIT: More alarm inputs can be defined by duplicating the lines below.
  // Make sure to not define a pin for both a tacho and an alarm.
  auto alarm_d2_input = ConnectAlarmSender(kDigitalInputPin2, "D2");
  auto alarm_d3_input = ConnectAlarmSender(kDigitalInputPin3, "D3");
  // auto alarm_d4_input = ConnectAlarmSender(kDigitalInputPin4, "D4");

  // Update the alarm states based on the input value changes.
  // EDIT: If you added more alarm inputs, uncomment the respective lines below.
  alarm_d2_input->connect_to(
      new LambdaConsumer<bool>([](bool value) { alarm_states[1] = value; }));
  // In this example, alarm_d3_input is active low, so invert the value.
  // auto alarm_d3_inverted = alarm_d3_input->connect_to(
  //    new LambdaTransform<bool, bool>([](bool value) { return !value; }));
  alarm_d3_input->connect_to(
      new LambdaTransform<bool, bool>([](bool value) { return value; }));

  alarm_d3_input->connect_to(
      new LambdaConsumer<bool>([](bool value) { alarm_states[2] = value; }));
  // alarm_d4_input->connect_to(
  //     new LambdaConsumer<bool>([](bool value) { alarm_states[3] = value; }));

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

  alarm_d2_input->connect_to(engine_dynamic_sender->low_oil_pressure_);

  // This is just an example -- normally temperature alarms would not be
  // active-low (inverted).
  // alarm_d3_inverted->connect_to(engine_dynamic_sender->over_temperature_);
  alarm_d3_input->connect_to(engine_dynamic_sender->over_temperature_);

#endif  // ENABLE_NMEA2000_OUTPUT

  // FIXME: Transmit the alarms over SK as well.

  ///////////////////////////////////////////////////////////////////
  // Digital tacho inputs

  // Connect the tacho senders. Engine name is "main".
  // EDIT: More tacho inputs can be defined by duplicating the line below.
  auto tacho_d1_frequency = ConnectTachoSender(kDigitalInputPin1, "main");

#ifdef ENABLE_NMEA2000_OUTPUT
  // Connect outputs to the N2k senders.
  // EDIT: Make sure this matches your tacho configuration above.
  //       Duplicate the lines below to connect more tachos, but be sure to
  //       use different engine instances.
  N2kEngineParameterRapidSender* engine_rapid_sender =
      new N2kEngineParameterRapidSender("/NMEA 2000/Engine 1 Rapid Update", 0,
                                        nmea2000);  // Engine 1, instance 0

  ConfigItem(engine_rapid_sender)
      ->set_title("Engine 1 Rapid Update")
      ->set_description("NMEA 2000 rapid update engine parameters for engine 1")
      ->set_sort_order(3015);

  tacho_d1_frequency->connect_to(&(engine_rapid_sender->engine_speed_));

#endif  // ENABLE_NMEA2000_OUTPUT

  if (display_present) {
    tacho_d1_frequency->connect_to(new LambdaConsumer<float>(
        [](float value) { disp_rpm = 60 * value; }));
  }

  ///////////////////////////////////////////////////////////////////
  // Display setup

  // 2-page OLED: page 0 = sensors, page 1 = CAN/SK/network. Switch every 7s.
  if (display_present) {
    event_loop()->onRepeat(7000, []() {
      disp_page = 1 - disp_page;
      disp_draw(display);
    });
    event_loop()->onRepeat(1000, []() {
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

  // To avoid garbage collecting all shared pointers created in setup(),
  // loop from here.
  while (true) {
    loop();
  }
}

void loop() { event_loop()->tick(); }