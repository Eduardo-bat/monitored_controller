#include <ArduinoMqttClient.h>
#include <FreeRTOS_SAMD21.h>
#include <SPI.h>
#include <WiFi101.h>

#define ERROR_LED_PIN 6
#define ERROR_LED_LIGHTUP_STATE HIGH
#define SERIAL SerialUSB

const int simPeriod_ms  = 10;
const int simCtrlratio  = 10;
const int ctrlPeriod_ms = simCtrlratio * simPeriod_ms;
const int updtPeriod_ms = 100;
const int mqttPeriod_ms = 1000;

const int sysAproachInvRate = 4;
const int sysNoiseAmplitude = 8;

TaskHandle_t Handle_simTask;
TaskHandle_t Handle_ctrlTask;
TaskHandle_t Handle_mqttTask;
TaskHandle_t Handle_updtTask;

const char ssid[] = "\0";
const char pass[] = "\0";
int status = WL_IDLE_STATUS;

double ctrlSP = 0.0;
double ctrlPV = 0.0;
double ctrlOP = 0.0;
double ctrlEr = 0.0;

double maxSP = 0.0;
double minSP = 0.0;

WiFiSSLClient wifiSSLClient;
MqttClient mqttClient(wifiSSLClient);

const char broker[]      = "\0";
const int  port          = 0000;
const char* mqttUsername = "\0";
const char* mqttpassword = "\0";

const char topicSP[]  = "setpoint";
const char topicPV[]  = "presentValue";

void myDelayUs(int us) {
  vTaskDelay( us / portTICK_PERIOD_US );  
}

void myDelayMs(int ms) {
  vTaskDelay( (ms * 1000) / portTICK_PERIOD_US );  
}

void myDelayMsUntil(TickType_t *previousWakeTime, int ms) {
  vTaskDelayUntil( previousWakeTime, (ms * 1000) / portTICK_PERIOD_US );  
}

inline double sgn(double val) {
  return (0.0 < val) - (val < 0.0);
}

inline void plotSys() {
  SERIAL.print("SP:");
  SERIAL.print((int) ctrlSP);
  SERIAL.print(",PV:");
  SERIAL.print((int) ctrlPV);
  SERIAL.println();
}

inline void maybeChangeSP() {
  if (rand() % 2) {
    ctrlSP = random(-10e3, 10e3);
  }
}

static void sysSimThread( void *pvParameters ) {
  while (true) {
    ctrlPV = ctrlPV + (ctrlOP - ctrlPV) / sysAproachInvRate
              + (rand() % sysNoiseAmplitude - sysNoiseAmplitude / 2);
    myDelayMs(simPeriod_ms);
  }
}
static void sysCtrlThread( void *pvParameters ) {
  while (true) {
    ctrlEr = ctrlSP - ctrlPV;
    ctrlOP = ctrlSP + sgn(ctrlEr) * sqrt(abs(ctrlEr));
    myDelayMs(ctrlPeriod_ms);
  }

}

static void mqttThread( void *pvParameters ) {
  while (true) {
    mqttClient.beginMessage(topicPV);
    mqttClient.print((int) ctrlPV);
    mqttClient.endMessage();

    mqttClient.beginMessage(topicSP);
    mqttClient.print((int) ctrlSP);
    mqttClient.endMessage();

    myDelayMs(mqttPeriod_ms);
  }
}

static void updtThread( void *pvParameters ) {
  while (true) {
    plotSys();
    maybeChangeSP();
    myDelayMs(updtPeriod_ms);
  }
}


void setup() {

  SERIAL.begin(115200);

  delay(1000);
  while (!SERIAL);

  SERIAL.println("******************************");
  SERIAL.println("        Program start         ");
  SERIAL.println("******************************");
  SERIAL.flush();

  while (status != WL_CONNECTED) {
    SERIAL.print("Attempting to connect to network: ");
    SERIAL.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }

  SERIAL.println("You're connected to the network");

  SERIAL.print("Attempting to connect to the MQTT broker: ");
  SERIAL.println(broker);

  mqttClient.setUsernamePassword(mqttUsername, mqttpassword);

  if (!mqttClient.connect(broker, port)) {
    SERIAL.print(F("MQTT connection failed! Error code = "));
    SERIAL.println(mqttClient.connectError());

    while (true);
  }

  SERIAL.println("You're connected to the MQTT broker!");

  // Set the led the rtos will blink when we have a fatal rtos error
  // RTOS also Needs to know if high/low is the state that turns on the led.
  // Error Blink Codes:
  //    3 blinks - Fatal Rtos Error, something bad happened. Think really hard about what you just changed.
  //    2 blinks - Stack overflow, Task needs more bytes defined for its stack! 
  //               Use the taskMonitor thread to help gauge how much more you need
  //    1 blink  - Malloc Failed, Happens when you couldn't create a rtos object. 
  //               Probably ran out of heap.
  vSetErrorLed(ERROR_LED_PIN, ERROR_LED_LIGHTUP_STATE);

  // sets the serial port to print errors to when the rtos crashes
  // if this is not set, serial information is not printed by default
  vSetErrorSerial(&SERIAL);

  xTaskCreate(sysSimThread,  "Simulation Task", 128,  NULL, tskIDLE_PRIORITY + 4, &Handle_simTask);
  xTaskCreate(sysCtrlThread, "Control Task",    128,  NULL, tskIDLE_PRIORITY + 3, &Handle_ctrlTask);
  xTaskCreate(mqttThread,    "MQTT Task",       1024, NULL, tskIDLE_PRIORITY + 2, &Handle_mqttTask);
  xTaskCreate(updtThread,    "Update Task",     512,  NULL, tskIDLE_PRIORITY + 1, &Handle_updtTask);

  // Start the RTOS, this function will never return and will schedule the tasks.
  vTaskStartScheduler();

  // error scheduler failed to start
  // should never get here
  while (true) {
	  SERIAL.println("Scheduler Failed! \n");
	  SERIAL.flush();
	  delay(1000);
  }

}

void loop() {
  mqttClient.poll();
}
