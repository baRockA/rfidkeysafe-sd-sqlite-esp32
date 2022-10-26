/*
RFID Reader keysafe code by rockypi @ make-es
On ESP32
Using MFRC522 RFID Reader
        RST   = 27
        IRQ   = -
        MISO  = 19
        MOSI  = 23
        SCK   = 18
        SDA   = 5

Using SQLite3 on a SPI Reader Micro SD TF Memory Card Shield Modul
    SD card attached to SPI bus as follows:
        CS    = 15
        MOSI  = 13
        MISO  = 32
        SCK   = 14
        
Using a Servo that opens the lock
        CTRL = 26

GUI is accessable via WIFI 
        SSID = keysafe-AP
        PW = 72Make-ES622
        IP = 192.168.42.100
        DNS = http://keysafe
Admin user with id=0 in DB
*/
//

#include <SD.h>
#include <SPI.h>
#include <sqlite3.h>
#include <TimeLib.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <FS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ESP32_Servo.h>
#include <ArduinoJson.h>

#define SS_PIN 5      // Pin: G5 (SDA MFRC522)
#define RST_PIN 27    // Pin: G27 (RST MFRC522)
#define SERVO_PIN 26  // Pin: G26
#define SD_CS 15      // PIN: G15 (Chip select for SD card)
#define RED_LED 17    //Pin: G16 and G17 for access LEDs
#define GREEN_LED 16

#define POS_OPEN 10  //Servopositions
#define POS_CLOSE 180

//create Servo Object and variable for position
Servo myservo;
int pos = 180;

// create MFRC522-instance via VSPI
MFRC522 rfid(SS_PIN, RST_PIN);
String chipID;

// SPI fo SD
SPIClass spiSD(HSPI);

//Webserver as AccessPoint
const char *ssid = "keysafe-AP";
const char *password = "72Make-ES622";
IPAddress ip(192, 168, 42, 100);
IPAddress gateway(192, 168, 42, 1);
IPAddress subnet(255, 255, 255, 0);
const char *dns_name = "keysafe";

AsyncWebServer server(80);

const char *htmlfile = "/index.html";  //stored in SPIFFs
String http_username = "";
String http_password = "";

//DB Variables for sqlite
sqlite3 *userdb;
sqlite3_stmt *res;
const char *tail;
char *zErrMsg = 0;
int rc;

const char logout_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <p>Logged out or <a href="/">return to homepage</a>.</p>
  <p><strong>Note:</strong> close all web browser tabs to complete the logout process.</p>
</body>
</html>
)rawliteral";

// Variable to store the HTTP request
String header;
// Current time
unsigned long currentTime = millis();

int openDb(const char *filename, sqlite3 **db) {
  int rc = sqlite3_open(filename, db);
  if (rc) {
    Serial.printf("Can't open database: %s\n", sqlite3_errmsg(*db));
    return rc;
  } else {
    Serial.printf("Opened database successfully\n");
  }
  return rc;
}

const char *data = "Callback function called";
static int callback(void *data, int argc, char **argv, char **azColName) {
  int i;
  Serial.printf("%s: ", (const char *)data);
  for (i = 0; i < argc; i++) {
    Serial.printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  }
  Serial.printf("\n");
  return 0;
}

static int callback_getAdmin(void *data, int argc, char **argv, char **azColName) {
  http_username = argv[0];
  http_password = argv[1];
  Serial.println("Username: " + http_username);
  Serial.println("Password: " + http_password);
  return 0;
}

int db_exec(sqlite3 *db, const char *sql) {
  Serial.println(sql);
  long start = micros();
  int rc = sqlite3_exec(db, sql, callback, (void *)data, &zErrMsg);
  if (rc != SQLITE_OK) {
    Serial.printf("SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
  } else {
    Serial.printf("Operation done successfully\n");
  }
  Serial.print(F("Time taken:"));
  Serial.println(micros() - start);
  return rc;
}

int db_exec_getadmin(sqlite3 *db, const char *sql) {
  int rc = sqlite3_exec(db, sql, callback_getAdmin, (void *)data, &zErrMsg);
  if (rc != SQLITE_OK) {
    Serial.printf("SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
  } else {
    Serial.printf("Operation done successfully\n");
  }
  return rc;
}

void setup() {
  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, HIGH);
  Serial.begin(115200);

  while (!Serial)
    ;

  // init MFRC522
  SPI.begin();  // init SPI bus for MFRC522
  rfid.PCD_Init();

  //delay after init
  delay(100);
  Serial.println("");

  // output details for MFRC522 RFID READER / WRITER
  rfid.PCD_DumpVersionToSerial();

  //Initialize File System
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  Serial.println("File System Initialized");

  // start AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(ssid, password);

  // start mDNS
  if (MDNS.begin(dns_name)) {
    Serial.println("DNS gestartet, erreichbar unter: ");
    Serial.println("http://" + String(dns_name));
  }

  Serial.println("Initializing SD card...");

  spiSD.begin(14, 32, 13, 15);  //SCK,MISO,MOSI,SS //HSPI1

  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("Card Mount Failed");
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  //Initialize logfile if not available
  File log = SD.open("/log.csv", FILE_WRITE);
  if (!log) {
    Serial.println("Log konnte nicht geöffnet werden.");
  } else {
    log.println("Keysafe Log - Start oder Reboot");
  }
  log.close();

  sqlite3_initialize();

  // Open user database
  if (openDb("/sd/keysafe.sqlite", &userdb))
    return;

  db_exec_getadmin(userdb, "Select name, password from user where id = 0");

  myservo.attach(SERVO_PIN);
  myservo.write(pos);

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username.c_str(), http_password.c_str()))
      return request->requestAuthentication();
    request->send(SD, "/index.html", "text/html");
  });

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(401);
  });

  server.on("/logged-out", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", logout_html, processor);
  });


  server.on("/showrfiduid", HTTP_GET, [](AsyncWebServerRequest *request) {
    String msg = String(chipID);
    request->send(200, "text/html", msg);  //Send chipID value only to client ajax request)
  });

  server.on(
    "/settime", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, (const char *)data);
      if (error)
        return;

      if (doc.containsKey("day") && doc.containsKey("month") && doc.containsKey("year") && doc.containsKey("hour") && doc.containsKey("minute") && doc.containsKey("second")) {
        String msg = "Zeit erfolgreich synchronisiert von " + String(hour()) + ":" + String(minute()) + ":" + String(second());
        setTime(doc["hour"].as<int>(),
                doc["minute"].as<int>(),
                doc["second"].as<int>(),
                doc["day"].as<int>(),
                doc["month"].as<int>(),
                doc["year"].as<int>());
        msg += " zu " + String(hour()) + ":" + String(minute()) + ":" + String(second());
        writeLog(msg);
        request->send(201, "text/plain", msg);
      } else {
        Serial.println("Falsche Parameter für settime!");
        request->send(400, "text/plain", "Falsche Parameter für settime!");
      }
    });

  server.on(
    "/adduser", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, (const char *)data);
      if (error)
        return;
      if (doc.containsKey("name") && doc.containsKey("mail") && doc.containsKey("rfiduid")) {

        String msg = "Benutzer " + doc["name"].as<String>() + ", " + doc["mail"].as<String>() + ", " + doc["rfiduid"].as<String>();
        String sql = "insert into user(name, mail, role, tag) values ('" + doc["name"].as<String>() + "', '" + doc["mail"].as<String>() + "', '0', '" + doc["rfiduid"].as<String>() + "');";
        rc = db_exec(userdb, sql.c_str());
        if (rc != SQLITE_OK) {
          msg += " nicht erfolgreich hinzugefügt!";
          request->send(501, "text/plain", msg);
          return;
        } else {
          msg += " erfolgreich hinzugefügt!";
          digitalWrite(RED_LED, HIGH);
          delay(500);
          digitalWrite(GREEN_LED, HIGH);
          digitalWrite(RED_LED, LOW);
          delay(500);
          digitalWrite(RED_LED, HIGH);
          digitalWrite(GREEN_LED, LOW);
          delay(500);
          digitalWrite(RED_LED, LOW);
          digitalWrite(GREEN_LED, HIGH);
          delay(500);
          digitalWrite(RED_LED, HIGH);
          digitalWrite(GREEN_LED, LOW);
          delay(500);
          digitalWrite(RED_LED, LOW);
          request->send(201, "text/plain", msg);
        }
        writeLog(msg);
      } else {
        Serial.println("Falsche Parameter!");
        request->send(400, "text/plain", "Benutzer nicht erfolgreich hinzugefügt! Falsche Parameter!");
      }
    });

  server.on(
    "/deleteuser", HTTP_DELETE, [](AsyncWebServerRequest *request) {
      if (request->hasParam("id")) {
        String sql = "delete from user where id = '" + request->getParam("id")->value() + "'";
        String msg = "";
        rc = db_exec(userdb, sql.c_str());
        if (rc != SQLITE_OK) {
          msg += " Nicht erfolgreich gelöscht!";
          request->send(501, "text/plain", msg);
          return;
        } else {
          msg += " Erfolgreich gelöscht!";
          request->send(201, "text/plain", msg);
          writeLog("User mit ID=" + request->getParam("id")->value() + " gelöscht.");
        }
      } else {
        Serial.println("Falsche Parameter!");
        request->send(400, "text/plain", "Benutzer nicht erfolgreich hinzugefügt! Falsche Parameter!");
      }
    });

  server.on("/listusers", HTTP_GET, [](AsyncWebServerRequest *request) {
    String msg = "";
    String sql = "select id, name, mail, tag from user where role = 0;";
    rc = sqlite3_prepare_v2(userdb, sql.c_str(), 1000, &res, &tail);
    if (rc != SQLITE_OK) {
      msg += "Abruf der Userliste nicht erfolgreich!";
      request->send(501, "text/plain", msg);
      return;
    } else {
      String resp = "{\"users\":[";
      int rec_count = 0;
      while (sqlite3_step(res) == SQLITE_ROW) {
        if (rec_count > 0) {
          resp += ",";
        }
        resp += "{\"id\":\"";
        resp += sqlite3_column_int(res, 0);
        resp += "\",\"name\":\"";
        resp += (const char *)sqlite3_column_text(res, 1);
        resp += "\",\"mail\":\"";
        resp += (const char *)sqlite3_column_text(res, 2);
        resp += "\",\"tag\":\"";
        resp += (const char *)sqlite3_column_text(res, 3);
        resp += "\"}";
        rec_count++;
      }
      resp += "]}";
      request->send(200, "text/plain", resp);
    }
    sqlite3_finalize(res);
  });

  server.on("/getlog", HTTP_GET, [](AsyncWebServerRequest *request) {
    File l = SD.open("/log.csv", FILE_READ);
    if (!l) {
      Serial.println("Logfile nicht gefunden!");
      request->send(500, "text/plain", "Kein Logfile gefunden!");
    } else {
      String msg = "";
      while (l.available()) {
        msg += (char)l.read();
      }
      request->send(200, "text/plain", msg);
    }
    l.close();
  });

  server.on("/deletelog", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SD.remove("/log.csv")) {
      Serial.println("File deleted");
      File l = SD.open("/log.csv", FILE_WRITE);
      if (!l) {
        Serial.println("Logfile nicht gefunden!");
        request->send(500, "text/plain", "Kein Logfile gefunden!");
      } else {
        String msg = "";
        l.println("Started new logfile.");
      }
      l.close();
      writeLog("Old log deleted.");

    } else {
      Serial.println("Log delete failed");
      request->send(501, "text/plain", "Logfile konnte nicht gelöscht werden");
    }
  });

  server.begin();
  delay(1000);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, HIGH);
  delay(1000);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  writeLog("System erfolgreich gestartet.");
}

void loop() {
  //RFID-Handling
  // wait for presence of rfid-chip
  if (rfid.PICC_IsNewCardPresent()) {

    //reset CardID
    chipID = "";

    rfid.PICC_ReadCardSerial();

    for (byte i = 0; i < rfid.uid.size; i++) {
      chipID += rfid.uid.uidByte[i] < 0x10 ? " 0" : " ";
      chipID += String(rfid.uid.uidByte[i], HEX);
    }

    //check if access granted and unlock keysafe
    String sql = "select id, name, mail, tag from user where tag = '" + String(chipID) + "';";
    rc = sqlite3_prepare_v2(userdb, sql.c_str(), 1000, &res, &tail);
    if (rc != SQLITE_OK) {

      //Error
      return;
    } else {
      int rec_count = 0;
      while (sqlite3_step(res) == SQLITE_ROW) {
        //Access granted

        //Servotest - Anapssen und löschen
        if (pos == POS_OPEN) {
          pos = POS_CLOSE;
        } else {
          pos = POS_OPEN;
        }
        myservo.write(pos);

        String username = String((const char *)sqlite3_column_text(res, 1));
        String Mail = String((const char *)sqlite3_column_text(res, 2));
        writeLog("Zugang erlaubt: " + chipID + "-Name: " + username + "-e-Mail: " + Mail);
        digitalWrite(GREEN_LED, HIGH);
        delay(1000);
        digitalWrite(GREEN_LED, LOW);
        rec_count++;
      }
      if (rec_count != 1) {
        //Access denied
        writeLog("Zugang verweigert: " + chipID);
        digitalWrite(RED_LED, HIGH);
        delay(1000);
        digitalWrite(RED_LED, LOW);
      }
    }
    delay(1500);
  }
}

// Replaces placeholder with button section in your web page
String processor(const String &var) {
}


void writeLog(String m) {
  //prepare msg with date and time
  String msg = "";
  msg += String(day()) + "/" + String(month()) + "/" + String(year()) + " - " + String(hour()) + ":" + String(minute()) + ":" + String(second()) + " - ";
  msg += m + "<br>";
  //prepare logFile and open in append mode
  File logFile = SD.open("/log.csv", FILE_APPEND);
  if (!logFile) {
    Serial.println("Fehler beim öffnen der log.csv");
    return;
  }
  logFile.println(msg + "\n");
  logFile.close();
}

bool loadFromSpiffs(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) path += "index.htm";

  if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if (path.endsWith(".html")) dataType = "text/html";
  else if (path.endsWith(".htm")) dataType = "text/html";
  else if (path.endsWith(".css")) dataType = "text/css";
  else if (path.endsWith(".js")) dataType = "application/javascript";
  else if (path.endsWith(".png")) dataType = "image/png";
  else if (path.endsWith(".gif")) dataType = "image/gif";
  else if (path.endsWith(".jpg")) dataType = "image/jpeg";
  else if (path.endsWith(".ico")) dataType = "image/x-icon";
  else if (path.endsWith(".xml")) dataType = "text/xml";
  else if (path.endsWith(".pdf")) dataType = "application/pdf";
  else if (path.endsWith(".zip")) dataType = "application/zip";
  File dataFile = SPIFFS.open(path.c_str(), "r");

  dataFile.close();
  return true;
}