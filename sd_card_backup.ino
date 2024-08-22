#include "Arduino.h"
#include <EDB.h>
#include <SPI.h>
#include "SdFat.h"
#include "wiring_private.h"
#include <ModbusMaster.h>
#include <ArduinoJson.h>

SdFat SD;
#define SD_CS_PIN SS
File myFile;
#define TABLE_SIZE 8192
Uart modbus_iso(&sercom0, 3, 2, SERCOM_RX_PAD_3, UART_TX_PAD_2);
Uart LTEserial(&sercom3, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2);  // Create the new UART instance assigning it to pin 7 and 6
#define MAX485_DE 4
#define WORK_DE 5
ModbusMaster node;


uint32_t last1, last2, last3, last4;
uint8_t result, qty;
uint16_t start_register;
float temp, humid;
int maxID;
static int recordID;
#define RECORDS_TO_CREATE 10

char* db_name = "/db/edb_test.db";
File dbFile;

struct LogEvent {
  int id;
  float temperature;
  float humidity;
} logEvent;

void writer(unsigned long address, byte data) {
  digitalWrite(13, HIGH);
  dbFile.seek(address);
  dbFile.write(data);
  dbFile.flush();
  digitalWrite(13, LOW);
}

byte reader(unsigned long address) {
  digitalWrite(13, HIGH);
  dbFile.seek(address);
  byte b = dbFile.read();
  digitalWrite(13, LOW);
  return b;
}

EDB db(&writer, &reader);

void setup() {
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);

  Serial.begin(9600);
  LTEserial.begin(115200);
  pinPeripheral(7, PIO_SERCOM_ALT);  //Assign RX function to pin 7
  pinPeripheral(6, PIO_SERCOM_ALT);  //Assign TX function to pin 6
  modbus_iso.begin(9600);
  Serial.println(" Extended Database Library + External SD CARD storage demo");
  Serial.println();
  pinPeripheral(3, PIO_SERCOM);
  pinPeripheral(2, PIO_SERCOM);

  randomSeed(analogRead(0));
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_DE, LOW);
  pinMode(A1, OUTPUT);    //SIM7600_ENA TX Pin (U9)
  digitalWrite(A1, LOW);  //SIM7600_ENA set Active High

  //QUECTEL_PCIE_RX
  pinMode(A5, OUTPUT);    //QUECTEL/SIM7600_ENA RX pin (U10 PIN19)
  digitalWrite(A5, LOW);  //QUECTEL/SIM7600_ENA set Active Low

  //Disable SIM7600,Neoway
  pinMode(A6, OUTPUT);     //NERO_ENA U10 PIN 1
  digitalWrite(A6, HIGH);  //NERO_ENA Active Low

  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("No SD-card.");
    return;
  }

  if (!SD.exists("/db")) {
    Serial.println("Dir for Db files does not exist, creating...");
    SD.mkdir("/db");
  }

  if (SD.exists(db_name)) {
    dbFile = SD.open(db_name, FILE_WRITE);
    if (!dbFile) {
      dbFile = SD.open(db_name, FILE_WRITE);
    }

    if (dbFile) {
      Serial.print("Openning current table... ");
      EDB_Status result = db.open(0);
      if (result == EDB_OK) {
        Serial.println("DONE");
      } else {
        Serial.println("ERROR");
        Serial.println("Did not find da    tabase in the file " + String(db_name));
        Serial.print("Creating new table... ");
        db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
        Serial.println("DONE");
        return;
      }
    } else {
      Serial.println("Could not open file " + String(db_name));
      return;
    }
  } else {
    Serial.print("Creating table... ");
    dbFile = SD.open(db_name, FILE_WRITE);
    db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
    Serial.println("DONE");
  }
  if (db.count() == 0) {
    recordID = 1;  // เริ่มที่ 1 หากฐานข้อมูลว่างเปล่า
  } else {
    maxID = getMaxID();
    Serial.print("Max ID found: ");
    Serial.println(maxID);
    recordID = maxID + 1;  // เริ่มต่อจาก maxID ที่พบ
  }
  countRecords();
  // deleteAll(); ลบข้อมูลใหม่
  // CMD("AT+CMQTTDISC=0,120");
  // CMD("AT+CMQTTREL=0");
  // CMD("AT+CMQTTSTOP");
  // CMD("AT+CMQTTACCQ=0,\"client test0\"");
  // CMD("AT+CMQTTWILLTOPIC=0,10");
  // CMD("0123456789");
  // CMD("AT+CMQTTWILLMSG=0,6,1");
  // CMD("qwerty");
  // CMD("AT+CMQTTSTART");
  // CMD("AT+CMQTTCONNECT=0,\"tcp://broker.emqx.io:1883\",60,1");
  // delay(1000);
}

void loop() {
  // ตัวแปรสำหรับเก็บ ID ของ record

  // อ่านค่าจาก Modbus
  node.begin(22, modbus_iso);  // Modbus ID = 22
  start_register = 1;          // Starting register
  qty = 2;                     // Number of registers to read

  result = node.readInputRegisters(start_register, qty);
  delay(1500);

  if (result == node.ku8MBSuccess) {
    temp = node.getResponseBuffer(0) / 10.0;
    humid = node.getResponseBuffer(1) / 10.0;
    // Serial.print("Temperature: ");
    // Serial.println(temp);
    // Serial.print("Humidity: ");
    // Serial.println(humid);

    // บันทึกข้อมูลลงในฐานข้อมูล
    logEvent.id = recordID++;
    logEvent.temperature = temp;
    logEvent.humidity = humid;
    EDB_Status result = db.appendRec(EDB_REC logEvent);
    if (result != EDB_OK) {
      printError(result);
    } else {
      Serial.println("Record saved successfully");
    }
    countRecords();
    selectAll();
    sendMQTTData();
  }
}
// utility functions
void countRecords() {
  Serial.print("Record Count: ");
  Serial.println(db.count());
}

void printError(EDB_Status err) {
  Serial.print("ERROR: ");
  switch (err) {
    case EDB_OUT_OF_RANGE:
      Serial.println("Recno out of range");
      break;
    case EDB_TABLE_FULL:
      Serial.println("Table full");
      break;
    case EDB_OK:
    default:
      Serial.println("OK");
      break;
  }
}
void selectAll() {
  for (int recno = 1; recno <= db.count(); recno++) {
    EDB_Status result = db.readRec(recno, EDB_REC logEvent);
    if (result == EDB_OK) {
      Serial.print("Record Number: ");
      Serial.print(recno);
      Serial.print(" | ID: ");
      Serial.print(logEvent.id);
      Serial.print(" | Temperature: ");
      Serial.print(logEvent.temperature);
      Serial.print(" | Humidity: ");
      Serial.println(logEvent.humidity);
    } else {
      printError(result);
    }
  }
}
void selectFirstTen() {
  int maxRecordsToDisplay = 10;  // จำนวนบรรทัดสูงสุดที่ต้องการแสดง
  int recordCount = db.count();

  Serial.println("Displaying first 10 records:");

  for (int recno = 1; recno <= recordCount && recno <= maxRecordsToDisplay; recno++) {
    EDB_Status result = db.readRec(recno, EDB_REC logEvent);
    if (result == EDB_OK) {
      Serial.print("Record Number: ");
      Serial.print(recno);
      Serial.print(" | ID: ");
      Serial.print(logEvent.id);
      Serial.print(" | Temperature: ");
      Serial.print(logEvent.temperature);
      Serial.print(" | Humidity: ");
      Serial.println(logEvent.humidity);
    } else {
      printError(result);
    }
  }
}


void sendMQTTData() {
  // สร้าง JSON document ด้วย ArduinoJson
  StaticJsonDocument<1024> doc;
  JsonArray records = doc.createNestedArray("records");

  for (int recno = 1; recno <= db.count(); recno++) {
    EDB_Status result = db.readRec(recno, EDB_REC logEvent);
    if (result == EDB_OK) {
      JsonObject record = records.createNestedObject();
      record["id"] = logEvent.id;
      record["temperature"] = logEvent.temperature;
      record["humidity"] = logEvent.humidity;
    }
  }

  // แปลงเป็นสตริง JSON
  String jsonOutput;
  serializeJson(doc, jsonOutput);
  Serial.println(jsonOutput);

  // คำสั่ง AT
  CMD("AT+NETOPEN");
  CMD("AT+CIPOPEN=1,\"UDP\",,,9958");

  // ส่ง JSON ผ่าน UDP
  String sendCmd = "AT+CIPSEND=1," + String(jsonOutput.length()) + ",\"188.166.178.255\",9958";
  CMD(sendCmd);
  CMD(jsonOutput);  // ส่งข้อมูล JSON

  CMD("AT+CIPCLOSE=1");
  CMD("AT+NETCLOSE");

  // delay(1000);
  // CMD("AT+CMQTTTOPIC=0,5");
  // CMD("jason");
  // delay(1000);
  // // ส่งข้อมูล
  // CMD("AT+CMQTTPAYLOAD=0," + String(jsonOutput.length()));
  // delay(1000);
  // CMD(jsonOutput);
  // delay(1000);
}
// ฟังก์ชันสำหรับส่งข้อมูลผ่าน MQTT

int getMaxID() {
  maxID = 0;
  for (int recno = 1; recno <= db.count(); recno++) {
    EDB_Status result = db.readRec(recno, EDB_REC logEvent);
    if (result == EDB_OK) {
      if (logEvent.id > maxID) {
        maxID = logEvent.id;
      }
    }
  }
  return maxID;
}

void deleteAll() {
  Serial.print("Truncating table... ");
  db.clear();
  Serial.println("DONE");
}

String CMD(String at) {
  String txt = "";
  LTEserial.println(at);
  delay(100);
  while (LTEserial.available()) {
    txt += (char)LTEserial.read();
  }
  Serial.println(txt);
  return txt;
}


void preTransmission() {
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_DE, LOW);
}

void SERCOM0_Handler() {
  modbus_iso.IrqHandler();
}

void SERCOM3_Handler() {
  LTEserial.IrqHandler();
}