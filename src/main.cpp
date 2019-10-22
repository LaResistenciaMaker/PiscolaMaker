/*
 *
 * Piscola maker
 * 
 * v0.1
 * Nothing tested, only pure code
 *
 */#include <Arduino.h>
#include <ArduinoLog.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define LOG_ERROR F("[ERROR]")
#define LOG_INFO F("[INFO]")
#define LOG_DEBUG F("[DEBUG]")

#define LOGV Log.verbose
#define LOGD Log.trace
#define LOGN Log.notice
#define LOGW Log.warning
#define LOGE Log.error
#define LOGF Log.fatal

#define SLOGV(W) Log.verbose(F(W))
#define SLOGD(W) Log.trace(F(W))
#define SLOGN(W) Log.notice(F(W))
#define SLOGW(W) Log.warning(F(W))
#define SLOGE(W) Log.error(F(W))
#define SLOGF(W) Log.fatal(F(W))

// --------------- HW declarations --------------------


// --------------- Hardware declarations --------------------

//pins
const uint8_t hw_pisco_motor_pin = 1;
const uint8_t hw_cocacola_motor_pin = 1;
//manual controls
const uint8_t hw_dispenser_1_serve_pin = 98; //buttons for individual serving
const uint8_t hw_dispenser_2_serve_pin = 98;
const uint8_t hw_dispenser_3_serve_pin = 98;
const uint8_t hw_dispenser_4_serve_pin = 98;
const uint8_t hw_dispenser_1_set_pin = 98; //pots to set
const uint8_t hw_dispenser_2_set_pin = 98;
const uint8_t hw_dispenser_3_set_pin = 98;
const uint8_t hw_dispenser_4_set_pin = 98;
const uint8_t hw_dispense_all_pin = 98;
//actuators
const uint8_t hw_dispenser_1_motor_pin = 99; //motors
const uint8_t hw_dispenser_2_motor_pin = 99;
const uint8_t hw_dispenser_3_motor_pin = 99;
const uint8_t hw_dispenser_4_motor_pin = 99;
//signals
const uint8_t hw_LEDr = 99;
const uint8_t hw_LEDg = 99;
const uint8_t hw_LEDb = 99;

const uint8_t hw_confirm = 99; //wifi orders confirm


// --------------- Software declarations --------------------

//Wifi
const char *ssid = "YourSSIDHere";
const char *password = "YourPSKHere";

bool online_request = false;
bool still_serving = true;
WebServer server(80);

// --------------- Route Handlers ---------------

void handleRoot() {
  //Do the Send Server Stuff
  server.send(200, "text/html", "<form action=\"/Servir\" method=\"<POST>\"><input type=\"submit\" value=\"Servir\"></form>");
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleDebug() {

}
//TODO: Cambiar a leer bebidas desde un JSON, en un archivo
/*
 * DONE: Listo
 * WAITING:
 */
enum task_status{
  NOT_VALID,
  DONE,
  WAITING,
  IN_PROCESS,
};

#define DISPENSERS_AVAILABLE 2
#define DRINK_MAX_INSTRUCTIONS 10

#define DRINKS_QUEUE_MAX_LEN 20

//Status of all piscolaMaker
enum global_status{
  IDLE,
  DISPENSING,
  UPDATING
};

enum dispensers_available{
  NOT_USED,
  PISCO,
  COCACOLA
};

int hw_dispensers_motor_pins[] = {
  99,
  hw_dispenser_1_motor_pin,
  hw_dispenser_2_motor_pin
};

int hw_dispensers_serve_pins[] = {
  99,
  hw_dispenser_1_serve_pin,
  hw_dispenser_2_serve_pin
};

int hw_dispensers_set_pins[] = {
  99,
  hw_dispenser_1_set_pin,
  hw_dispenser_2_set_pin
};

String status_names[] = {
  "IDLE",
  "DISPENSING",
  "UPDATING"
};

char* dispenser_names[] = {
  "PISCO",
  "COCACOLA"
};

global_status act_status = IDLE;

/*
 * A drink contains
 * name: any name
 * quantity: measured in cc
 * task_status: as in the enum
 *
 * instructions: an array composed of segments. A drink in Piscoleitor supports DRINKS_MAX_INSTRUCTIONS instructions
 * start is relative to the first element
 * len is duration in ms
 * end is marked at 0 and it's reserved for runtime
 */

struct instruction{
  dispensers_available dispenser = NOT_USED;
  unsigned long start = 0;
  unsigned long len = 0;
  unsigned long end = 0;
  task_status status = task_status::WAITING;
};
typedef struct instruction Instruction;

struct drink{
  String name; //BEWARE: memory leaks possible
  task_status status;
  int n_instructions;
  instruction instructions[DRINK_MAX_INSTRUCTIONS];
};

struct queue{
  int queueStart = 0;
  int queueEnd = 0;
  int queueLen = 0;
  drink* drink_queue[DRINKS_QUEUE_MAX_LEN];
}drink_queue;

typedef struct queue Queue;
typedef struct instruction Instruction;
typedef struct drink Drink;

// --------------- Queue functions ---------------


/*Drink* drink_queue[DRINKS_QUEUE_MAX_LEN];

int queueStart = 0; //Points to the first element
int queueEnd = 0;  //Points to the next empty element
int queueLen = 0;*/

int now_executing = 0;

Drink* pushQueue(Queue* queue){
  if(queue->queueLen == DRINKS_QUEUE_MAX_LEN){
    return NULL;
  }
  queue->queueLen++;
  queue->queueEnd++;
  int selected = 0;
  if(queue->queueEnd == DRINKS_QUEUE_MAX_LEN){
    queue->queueEnd = 0;
    selected = DRINKS_QUEUE_MAX_LEN - 1;
  } else {
    selected = queue->queueEnd - 1;
  }
  return queue->drink_queue[selected];
}

Drink* getQueue(Queue* queue, int i){
  if(i > ((queue->queueLen)-1)){
    return NULL;
  }
  int index = queue->queueStart + i;
  if(index>DRINKS_QUEUE_MAX_LEN){
    index-=DRINKS_QUEUE_MAX_LEN;
  }
  return queue->drink_queue[index];
}

Drink* popQueue(Queue* queue){
  if(queue->queueLen == 0){
    return NULL;
  }
  queue->queueLen--;
  Drink* selected = queue->drink_queue[queue->queueStart];
  queue->queueStart++;
  if(queue->queueStart == DRINKS_QUEUE_MAX_LEN){
    queue->queueStart = 0;
  }
  return selected;
}

/*
 * Drinks API
 * GET Get drinks queue
 * POST Create new drink
 * DELETE Cancel a drink
 */

void handleQuery(){
  WiFiClient cli = server.client();
  SLOGD("Drink ops");
  StaticJsonDocument<100> payload;
  StaticJsonDocument<500> response;
  if(server.hasArg("getDrinkOn")){
    if(drink_queue.queueLen==0){
      response["status"] = "no_drinks";
    } else {
      Drink* selected = getQueue(&drink_queue,server.arg("getDrinkOn").toInt());
      if(selected == NULL){
        response["status"] = 404;
        response["reason"] = "out_of_range";
      } else {
        response["status"] = 200;
        auto e_drink = payload.createNestedObject("drink");
        e_drink["name"] = selected->name;
        e_drink["status"] = selected->status;
        e_drink["n_instructions"] = selected->n_instructions;
        auto array = e_drink.createNestedArray("instructions");
        char inst[40];
        for (int instruction = 0; instruction < drink_queue.queueLen; instruction++)
        {
          memset(inst,0,sizeof(inst));
          snprintf(inst,sizeof(inst),"%i,%i,%s",selected->instructions[instruction].start,\
                                      selected->instructions[instruction].len,\
                                      dispenser_names[selected->instructions[instruction].dispenser]);
          array.add(inst);
        }
      }
    }
  }
  WriteBufferingStream bufferedWifiClient(cli, 500);
  bufferedWifiClient.print("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n");
  serializeJson(response, bufferedWifiClient);
  bufferedWifiClient.flush();
}
void handleServe(){
  WiFiClient cli = server.client();
  SLOGD("Drink ops");
  StaticJsonDocument<100> payload;
  StaticJsonDocument<500> response;
  if(server.method() == HTTP_GET){
    if(server.args() == 0){
      response["drinks_in_queue"] = drink_queue.queueLen;
      response["max_queue"] = DRINKS_QUEUE_MAX_LEN;
      response["status"] = status_names[act_status];
    }
  } else if (server.method() == HTTP_POST){
    /*
     * Adding drink
     * name
     * instructions
     */
    deserializeJson(payload,server.arg("plain"));
    if(payload.containsKey("name")){
      auto newDrink = pushQueue(&drink_queue);
      if(newDrink == NULL){
        payload["status"] = 503;
        payload["reason"] = "queue_full";
      } else {
        newDrink->name = String((const char*)payload["name"]);
        newDrink->status = WAITING;
        int instruction_count = atoi(payload["instructionCount"]);
        if(instruction_count > DRINKS_QUEUE_MAX_LEN){
          payload["status"] = 400;
          payload["reason"] = "too_much_instructions";
        } else {
          int payload_instruction = 0;
          newDrink->n_instructions = instruction_count;
          for (int instruction = 0; instruction < instruction_count; instruction++){
            newDrink->instructions[instruction].dispenser = (dispensers_available)atoi(payload["instructions"][payload_instruction][0]); //dispenser
            newDrink->instructions[instruction].start = atoi(payload["instructions"][payload_instruction][1]); //start
            newDrink->instructions[instruction].len = atoi(payload["instructions"][payload_instruction][2]); //len
            newDrink->instructions[instruction].end = 0;
            payload_instruction++;
          }
          online_request = true;
          payload["status"] = 200;
          payload["reason"] = "OK";
        }
      }
    } else {
      response["status"] = "400";
      payload["reason"] = "malformed_request";
    }
  } else if (server.method() == HTTP_DELETE){
    deserializeJson(payload,server.arg("plain"));
    server.send(200, "text/html", F("{\"status\":\"ok\"}"));
  }
  WriteBufferingStream bufferedWifiClient(cli, 800);
  bufferedWifiClient.print("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n");
  serializeJson(response, bufferedWifiClient);
  bufferedWifiClient.flush();
}
// --------------- Drinks functions ---------------
void LED(bool a){

}

void Servir(){
  int P = 20;
  int B = 80;
  Serial.println("SIRVIENDO:");
  //LED(false);
  int Proportioner = P+B;
  int PiscoQ = P/Proportioner;
  int BebidaQ = B/Proportioner;
  Serial.print(PiscoQ);
  Serial.print(BebidaQ);
  digitalWrite(hw_dispensers_serve_pins[PISCO],1);
  delay(25000);
  digitalWrite(hw_dispensers_serve_pins[PISCO],0);
  delay(8000);
  digitalWrite(hw_dispensers_serve_pins[COCACOLA],1);
  delay(22000);
  digitalWrite(hw_dispensers_serve_pins[COCACOLA],0);
  LED(true);
  delay(5000);
  LED(false);
}



// --------------- Other functions ---------------


// --------------- Arduino functions ---------------
void setup(void) {

  Log.begin(LOG_LEVEL_TRACE, &Serial, true);
  LOGN(F("System initialized \n"));
  Serial.begin(115200);
  for (int i = 0; i < sizeof(hw_dispensers_serve_pins)/sizeof(hw_dispensers_serve_pins[0]); i++)
  {
    pinMode(hw_dispensers_motor_pins[i],OUTPUT);
    pinMode(hw_dispensers_serve_pins[i],INPUT_PULLUP);
    pinMode(hw_dispensers_set_pins[i],INPUT);
  }
  pinMode(hw_dispense_all_pin,INPUT_PULLUP);
  pinMode(hw_LEDr,OUTPUT);
  pinMode(hw_LEDg,OUTPUT);
  pinMode(hw_LEDb,OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  SLOGN("Init succesful\n");
  LOGN("Connected to %s", ssid);
  LOGN("IP address: to %s", WiFi.localIP().toString().c_str());

  if (MDNS.begin("piscoleitor")) {
    SLOGN("MDNS responder started");
  }
  server.on("/", handleRoot);
  server.on("/serve", handleServe);
  server.on("/query", handleQuery);
  server.on("/op", handleDebug);
  server.onNotFound(handleNotFound);
  server.begin();
  SLOGN("HTTP server started");
}
bool hw_confirm_serve = false;
void loop(void) {
  server.handleClient();
  //Manual functions
  if(!digitalRead(hw_dispensers_serve_pins[PISCO])){
    int q1 = analogRead(hw_dispensers_set_pins[PISCO]);
    Drink* newDrink = pushQueue(&drink_queue); //get drink pushed to queue
    newDrink->name = "PiscoPuro";
    newDrink->status = WAITING;
    newDrink->instructions[0].dispenser = PISCO; //type
    newDrink->instructions[0].start = 0; //start
    newDrink->instructions[0].len = q1; //len
    newDrink->instructions[0].end = 0; //end
    hw_confirm_serve = true;
  }
  if(!digitalRead(hw_dispensers_serve_pins[COCACOLA])){
    int q2 = analogRead(hw_dispensers_set_pins[COCACOLA]);
    drink* newDrink = pushQueue(&drink_queue);
    newDrink->name = "Custom";
    newDrink->status = WAITING;
    newDrink->instructions[0].dispenser = COCACOLA; //type
    newDrink->instructions[0].start = 0; //start
    newDrink->instructions[0].len = q2; //len
    newDrink->instructions[0].end = 0; //end
    hw_confirm_serve = true;
  }
  if(!digitalRead(hw_dispense_all_pin)){
    int q1 = analogRead(hw_dispensers_set_pins[PISCO]);
    int q2 = analogRead(hw_dispensers_set_pins[COCACOLA]);
    //create a drink and put it on the queue
    drink* newDrink = pushQueue(&drink_queue);
    newDrink->name = "Custom";
    newDrink->status = WAITING;
    newDrink->instructions[0].dispenser = PISCO; //type
    newDrink->instructions[0].start = 0; //start
    newDrink->instructions[0].len = q1; //len
    newDrink->instructions[0].end = 0; //end
    newDrink->instructions[1].dispenser = COCACOLA; //type
    newDrink->instructions[1].start = q1; //start
    newDrink->instructions[1].len = q2; //len
    newDrink->instructions[1].end = 0; //end
    hw_confirm_serve = true;
  }
  drink* actual_drink = NULL;
  //select order
  if(act_status == IDLE){
    if(drink_queue.queueLen>=0){
      act_status = DISPENSING;
      actual_drink = popQueue(&drink_queue);
      actual_drink->status = IN_PROCESS;
      uint32_t start_epoch_time = millis()+2000;
      //adjust all times of instructions
      for (int instruction = 0; instruction < actual_drink->n_instructions; instruction++){
        actual_drink->instructions[instruction].status = IN_PROCESS;
        actual_drink->instructions[instruction].start = start_epoch_time + actual_drink->instructions[instruction].start;
        actual_drink->instructions[instruction].end = actual_drink->instructions[instruction].start + actual_drink->instructions[instruction].len;

      }
    }
  }
  if(actual_drink->status == task_status::IN_PROCESS && act_status == DISPENSING){
    //check what instructions apply now
    for (int instruction = 0; instruction < actual_drink->n_instructions; instruction++)
    {
      if(actual_drink->instructions[instruction].dispenser!= NOT_USED &&\
        actual_drink->instructions[instruction].status != DONE &&\
        millis() > actual_drink->instructions[instruction].start &&\
        millis() < actual_drink->instructions[instruction].end){
        //on time
        digitalWrite(hw_dispensers_motor_pins[actual_drink->instructions[instruction].dispenser],HIGH);
      } else if(millis() > actual_drink->instructions[instruction].end) {
        //done
        digitalWrite(hw_dispensers_motor_pins[actual_drink->instructions[instruction].dispenser],LOW);
        actual_drink->instructions[instruction].status = DONE;
      } else {
        //still not executing
        digitalWrite(hw_dispensers_motor_pins[actual_drink->instructions[instruction].dispenser],LOW);
      }
    }
    bool serve_done = true;
    for (int instruction = 0; instruction < actual_drink->n_instructions; instruction++){
      //Check if we have any instruction left
      if(actual_drink->instructions[instruction].status != DONE){
        serve_done = false;
        break;
      }
    }
    if(serve_done){
      actual_drink->status = DONE;
      act_status = IDLE;
    }
  }
}