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

uint8_t result, qty;
uint16_t start_register;
float temp, humid;
int maxID;
static int recordID;
#define RECORDS_TO_CREATE 10

char* db_name = "/db/test.db";
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
  modbus_iso.begin(9600);
  Serial.println(" Extended Database Library + External SD CARD storage demo");
  Serial.println();
  pinPeripheral(3, PIO_SERCOM);
  pinPeripheral(2, PIO_SERCOM);

  randomSeed(analogRead(0));
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_DE, LOW);

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
}

void loop() { 

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


void preTransmission() {
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_DE, LOW);
}

void SERCOM0_Handler() {
  modbus_iso.IrqHandler();
}
