#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <TZ.h>
#include <FS.h>
#include <LittleFS.h>
#include <CertStoreBearSSL.h>
#include "credentials.hpp"

BearSSL::CertStore cert_store;

WiFiClientSecure esp_client;
PubSubClient * client;

const unsigned long pub_period = 10000;
      unsigned long last_pub   = 0;

const unsigned long sym_period = 50;
      unsigned long last_sym   = 0;
      
const unsigned long ctrl_period = 500;
      unsigned long last_ctrl   = 0;

double ctrlSP = 0.0;
double ctrlPV = 0.0;
double ctrlOP = 0.0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WiFiCredentials::ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WiFiCredentials::ssid, WiFiCredentials::password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setDateTime() {
  configTime(TZ_America_Santiago, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(100);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.printf("%s %s", tzname[0], asctime(&timeinfo));
}

void reconnect() {
  while (!client->connected()) {
    Serial.print("Attempting MQTT connection…");
    if (client->connect("esp8266_analog_ctrl", MQTTCredentials::username, MQTTCredentials::password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc = ");
      Serial.print(client->state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void publish_PV() {
  char pvString[10];

  sprintf(pvString, "%lf", ctrlPV);
  client->publish("ctrlPV", pvString);
}

void publish_SP() {
  char spString[10];

  sprintf(spString, "%lf", ctrlSP);
  client->publish("ctrlSP", spString);
}

void publish_OP() {
  char opString[10];

  sprintf(opString, "%lf", ctrlOP);
  client->publish("ctrlOP", opString);
}

void sim_sys() {
  ctrlPV = ctrlPV + (ctrlOP - ctrlPV) / 4 + (rand() % 8 - 4);
}

void ctrl_sys() {
  double error = ctrlSP - ctrlPV;
  double sign = (error > 0) - (error < 0);
  ctrlOP = ctrlSP + sign * sqrt(abs(error));
}

void setup() {
  delay(500);
  Serial.begin(115200);
  delay(500);

  LittleFS.begin();
  setup_wifi();
  setDateTime();

  int numCerts = cert_store.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0) {
    Serial.printf("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n");
    return;
  }

  BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();
  bear->setCertStore(&cert_store);

  client = new PubSubClient(*bear);

  client->setServer(MQTTCredentials::mqtt_server, 8883);
}

void loop() {
  if (!client->connected()) {
    reconnect();
  }
  client->loop();

  unsigned current_sym = millis();
  if (current_sym - last_sym > sym_period) {
    last_sym = current_sym;
    sim_sys();
    Serial.printf("SP:%lf,PV:%lf,OP:%lf\n", ctrlSP, ctrlPV, ctrlOP);
  }

  unsigned current_ctrl = millis();
  if (current_ctrl - last_ctrl > ctrl_period) {
    last_ctrl = current_ctrl;
    ctrl_sys();
  }

  unsigned current_pub = millis();
  if (current_pub - last_pub > pub_period) {
    last_pub = current_pub;

    publish_PV();
    publish_SP();

    ctrlSP = random(-10e3, 10e3);
  }
}
