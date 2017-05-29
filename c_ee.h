 /*************************************************** 
    Copyright (C) 2016  Steffen Ochs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    HISTORY: Please refer Github History
    
 ****************************************************/

 // HELP: https://github.com/bblanchon/ArduinoJson

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initalize EEPROM
void setEE() {
  
  // EEPROM Sector: 0xFB, ab Sector 0xFC liegen System Parameter
  
  DPRINT("[INFO]\tInitalize EEPROM at Sector: 0x"); // letzter Sector von APP2
  DPRINT((((uint32_t)&_SPIFFS_end - 0x40200000) / SPI_FLASH_SEC_SIZE),HEX);
  DPRINT(" (");
  DPRINT(EEPROM_SIZE, DEC);  // ESP.getFreeSketchSpace()
  DPRINTLN("B)");
  
  EEPROM.begin(EEPROM_SIZE);

  // WIFI SETTINGS:         0    - 300
  // THINGSPEAK SETTINGS:   300  - 400
  // CHANNEL SETTINGS:      400  - 900
  // PITMASTER SETTINGS:    900  - 1500
  // SYSTEM SETTINGS:       1500 - 1750
  
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Write to EEPROM
void writeEE(const char* json, int len, int startP) {
  
  DPRINT("[INFO]\tWriting to EE: (");
  DPRINT(len);
  DPRINT(") ");
  DPRINTLN(json);
  
  for (int i = startP; i < (startP+len); ++i) {
    EEPROM.write(i, json[i-startP]);
  } 
  EEPROM.commit();
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Read from EEPROM
void readEE(char *buffer, int len, int startP) {
  
  for (int i = startP; i < (startP+len); ++i) {
    buffer[i-startP] = char(EEPROM.read(i));
  }
  
  DPRINT("[INFO]\tReading from EE: ");
  DPRINTLN(buffer);
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Clear EEPROM
void clearEE(int len, int startP) {  
  
  DPRINTF("[INFO]\tClear EEPROM from: %u to: %u\r\n", startP, startP+len-1); 

  for (int i = startP; i < (startP+len); ++i) {
    EEPROM.write(i, 0);
  } 
  EEPROM.commit();
}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Check Memory Sectors
void check_sector() {

  // https://github.com/esp8266/Arduino/blob/master/cores/esp8266/Esp.cpp
  freeSpaceStart = (ESP.getSketchSize() + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1));
  freeSpaceEnd = (uint32_t)&_SPIFFS_start - 0x40200000 - FLASH_SECTOR_SIZE;
  log_sector = freeSpaceStart/SPI_FLASH_SEC_SIZE;
  
  DPRINT("[INFO]\tInitalize SKETCH at Sector: 0x01 (");
  DPRINT((ESP.getSketchSize() + FLASH_SECTOR_SIZE - 1)/1024);
  DPRINTLN("K)");
  DPRINT("[INFO]\tInitalize DATALG at Sector: 0x");
  DPRINT(log_sector,HEX);
  DPRINT(" (");
  DPRINT((freeSpaceEnd - freeSpaceStart)/1024, DEC);  // ESP.getFreeSketchSpace()
  DPRINTLN("K)");
    
  //DPRINTLN(ESP.getFlashChipRealSize()/1024);
  //DPRINTLN(ESP.getFlashChipSize() - 0x4000);
    
}




