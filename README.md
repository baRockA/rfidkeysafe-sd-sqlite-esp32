# rfidkeysafe-sd-sqlite-esp32
 Keysafe based on a esp32 with mfrc522 rfid-reader and spi card reader. Controll interface via ESPAsyncWebserver and tried to use a RESTful interface.

# Where I bought things
ESP32 Dev Board: https://www.az-delivery.de/products/esp32-developmentboard
RFID Kit MFRC522: https://www.az-delivery.de/products/rfid-set
SPI Cardreader: https://www.az-delivery.de/en/products/copy-of-spi-reader-micro-speicherkartenmodul-fur-arduino
And some other things like LEDs and resistors laying aroung at home.

# Helpful sources of inspiration
For the Webserver and the login: https://randomnerdtutorials.com/esp32-esp8266-web-server-http-authentication/
Also used: https://github.com/me-no-dev/ESPAsyncWebServer

For the RFID-Reader: https://esp32io.com/tutorials/esp32-rfid-nfc

To use sqlite3: https://github.com/siara-cc/esp32_arduino_sqlite3_lib

And for using the Cardreader with the VSPI: https://randomnerdtutorials.com/esp32-microsd-card-arduino/

Of courde I used the very helpful examples that are downloaded if you install libraries within the Arduino IDE.
