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

    ANALOG/DIGITAL-WANDLUNG:
    - kleinster digitaler Sprung 1.06 V/1024 = 1.035 mV - eigentlich 1.0V/1024
    - Hinweis zur Abweichung: https://github.com/esp8266/Arduino/issues/2672
    -> ADC-Messspannung = Digitalwert * 1.035 mV
    - Spannungsteiler (47k / 10k) am ADC-Eingang zur 
    - Transformation von BattMin - BattMax in den Messbereich von 0 - 1.06V 
    -> Batteriespannung = ADC-Messspannung * (47+10)/10 
    -> Transformationsvariable Digitalwert-to-Batteriespannung: Battdiv = 1.035 mV * 5.7
    
    HISTORY: Please refer Github History
    
 ****************************************************/


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Sensors
byte set_sensor() {

  // Piepser
  pinMode(MOSI, OUTPUT);
  analogWriteFreq(4000);

  
  if (sys.typk && sys.hwversion == 1) {
    // CS notwendig, da nur bei CS HIGH neue Werte im Chip gelesen werden
    pinMode(THERMOCOUPLE_CS, OUTPUT);
    digitalWrite(THERMOCOUPLE_CS, HIGH);
  }

  // MAX1161x
  byte reg = 0xA0;    // A0 = 10100000
  // page 14
  // 1: setup mode
  // SEL2:0 = Reference (Table 6)
  // external(1)/internal(0) clock
  // unipolar(0)/bipolar(1)
  // 0: reset the configuration register to default
  // 0: dont't care
 
  Wire.beginTransmission(MAX1161x_ADDRESS);
  Wire.write(reg);
  byte error = Wire.endTransmission();
  return error;
  
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Reading ADC-Channel Average
int get_adc_average (byte ch) {  
  // Get the average value for the ADC channel (ch) selected. 
  // MAX11613/15 samples the channel 8 times and returns the average.  
  // Setup byte required: 0xA0 
  
  byte config = 0x21 + (ch << 1);   //00100001 + ch  // page 15 
  // 0: config mode
  // 01: SCAN = Converts the ch eight times
  // 0000: placeholder ch
  // 1: single-ended

  Wire.beginTransmission(MAX1161x_ADDRESS);
  Wire.write(config);  
  Wire.endTransmission();
  
  Wire.requestFrom(MAX1161x_ADDRESS, 2);
  word regdata = (Wire.read() << 8) | Wire.read();

  return regdata & 4095;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Charge Detection
void set_batdetect(boolean stat) {

  if (!stat)  pinMode(CHARGEDETECTION, INPUT_PULLDOWN_16);
  else pinMode(CHARGEDETECTION, INPUT);
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Reading Battery Voltage
void get_Vbat() {
  
  // Digitalwert transformiert in Batteriespannung in mV
  int voltage = analogRead(ANALOGREADBATTPIN);

  // CHARGE DETECTION
  //                LOAD        COMPLETE        SHUTDOWN
  // MCP:           LOW           HIGH           HIGH-Z 
  // curStateNone:  LOW           HIGH           HIGH
  // curStatePull:  LOW           HIGH           LOW
  
  // Messung bei INPUT_PULLDOWN
  set_batdetect(LOW);
  byte curStatePull = digitalRead(CHARGEDETECTION);
  // Messung bei INPUT
  set_batdetect(HIGH);
  byte curStateNone = digitalRead(CHARGEDETECTION);
  // Ladeanzeige
  battery.charge = !curStateNone;
  
  // Standby erkennen
  if (voltage < 10) {
    sys.stby = true;
    //return;
  }
  else sys.stby = false;

  // Transformation Digitalwert in Batteriespannung
  voltage = voltage * BATTDIV; 
  
  // Referenzwert bei COMPLETE neu setzen
  if ((curStateNone && curStatePull) && battery.setreference) {     // COMPLETE
    if (battery.voltage > 0 && !sys.stby) {
      if (battery.voltage < battery.max) {
        battery.max = battery.voltage-10;      
        // Grenze etwas nach unten versetzen, um die Ladespannung zu kompensieren
        // alternativ die Speicherung um 5 min verschieben, Gefahr: das dann schon abgeschaltet
        //setconfig(eSYSTEM,{});                                      // SPEICHERN
        IPRINTF("New battery voltage reference: %umV\r\n", battery.max); 
      }
    }
    battery.setreference = false;
    //battery_set_full(1);
    
  } else if (!curStateNone && !curStatePull) {                      // LOAD
    battery.setreference = true;
  }

  // Batteriespannung wird in einen Buffer geschrieben da die gemessene
  // Spannung leicht schwankt, aufgrund des aktuellen Energieverbrauchs
  // wird die Batteriespannung als Mittel aus mehreren Messungen ausgegeben

  vol_sum += voltage;
  vol_count++;
  
}

#define THRESHOLD 3700

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Calculate SOC
void cal_soc() {

  // mittlere Batteriespannung aus dem Buffer lesen und in Prozent umrechnen
  int voltage;
  
  if (vol_count > 0) voltage = vol_sum / vol_count;
  else voltage = 0;
  median_add(voltage);

  int average = median_average(); 
  int rate = 0; 
  int drift = 0;
  /*
  if (battery.full == 0)   {               // wenn geladen wird, oder bei Systemstart
    
    if (battery.charge && battery.startload != 0) {   // Systemstart ausschließen
      rate = (30*battery.sincefull/600000);   //1000*60 *10  // 3.0 mV/min  // > 400 mA/h
      drift = (average - battery.voltage)/20;

      voltage = battery.startload + rate + drift;
      
    } else voltage = average;     // Systemstart   
  
  } else {                                  // Battery Simulation
      
    if (battery.sincefull != 0)  {         // Kein Laden, keine Quelle
      rate = (4*battery.sincefull/600000);  // 1000*60 *10  // 0.4 mV/min

      if (battery.voltage > battery.min) {  // Start abfangen, da ist voltage = 0
        if (average < THRESHOLD || battery.voltage < THRESHOLD)    // Soll/Ist Drift
          battery.drift += (average - battery.voltage)/10;          // beschleunigte Anpassung
        else  battery.drift += (average - battery.voltage)/20;    // normale Anpassung, Aenderung bei Abstand > 20
      }
    } else  {   // Am Laden, Quelle verhanden
      rate = 0;
      drift = 0;
    }
    
    voltage = battery.full - rate + battery.drift;

  }
*/
  //battery.voltage = voltage;
  battery.voltage = average;
  
  battery.percentage = ((battery.voltage - battery.min)*100)/(battery.max - battery.min);
  
  // Schwankungen verschiedener Batterien ausgleichen
  if (battery.percentage > 100) battery.percentage = 100;
  
  IPRINTF("Battery voltage: %umV,\tcharge: %u%%\r\n", battery.voltage, battery.percentage); 

  // Abschaltung des Systems bei <0% Akkuleistung
  if (battery.percentage < 0) {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
    display.drawString(DISPLAY_WIDTH/2, DISPLAY_HEIGHT/3, "LOW BATTERY");
    display.drawString(DISPLAY_WIDTH/2, 2*DISPLAY_HEIGHT/3, "PLEASE SWITCH OFF");
    display.display();
    ESP.deepSleep(0);
    delay(100); // notwendig um Prozesse zu beenden
  }

  vol_sum = 0;
  vol_count = 0;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Initialize Hardware Alarm
void set_piepser() {

  // Hardware-Alarm bereit machen
  pinMode(MOSI, OUTPUT);
  analogWriteFreq(4000);
  //sys.hwalarm = false;
  
}

void piepserON() {
  analogWrite(MOSI,512);
}

void piepserOFF() {
  analogWrite(MOSI,0);
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Control Hardware Alarm
void controlAlarm(bool action){                // action dient zur Pulsung des Signals

  bool setalarm = false;

  for (int i=0; i < CHANNELS; i++) {
    if (ch[i].alarm) {
          
      // Check limits
      if ((ch[i].temp <= ch[i].max && ch[i].temp >= ch[i].min) || ch[i].temp == INACTIVEVALUE) {
        // everything is ok
        ch[i].isalarm = false;
        ch[i].showalarm = false;

      // Alarm anzeigen
      } else if (ch[i].isalarm && ch[i].showalarm) {
        // do alarm
        setalarm = true;  // Alarm noch nicht quittiert

        // Show Alarm
        if (ch[i].show && !displayblocked) {
          ch[i].show = false;
          question.typ = HARDWAREALARM;
          question.con = i;
          drawQuestion(i);
        }
      
      } else if (!ch[i].isalarm && ch[i].temp != INACTIVEVALUE) {
        // first rising limits

        bool sendM = true;
        //if (wifi.mode == 1 && iot.TS_httpKey != "") {
        if (wifi.mode == 1) {            // was ist wenn kein Wifi?  
          notification.ch = i+1;
          if (ch[i].temp > ch[i].max) notification.limit = 1;
          else if (ch[i].temp < ch[i].min) notification.limit = 0;
            
          // Sender frei? Falls fehlerhaftes Senden, wird der Client selbst wieder frei
          if (iot.TS_httpKey != "" && iot.TS_on) {
            if (sendNote(0)) sendNote(1);  // Notification per Thingspeak
            else sendM = false;       // kann noch nicht gesendet werden, also warten
          } else if (iot.TG_on > 0) {
            if (sendNote(0)) sendNote(2);           // Notification per Server
            else sendM = false;       // kann noch nicht gesendet werden, also warten
          } // keine Notification Daten, also weiter
        } // kein Internet, also weiter       

        if (sendM) {
          ch[i].isalarm = true;
          ch[i].showalarm = true;
          ch[i].show = true;
          setalarm = true;
        } 
      }
    } else {
      ch[i].isalarm = false;
      ch[i].showalarm = false;
    }   
  }

  // Hardware-Alarm-Variable: sys.hwalarm
  if (sys.hwalarm && setalarm && action) {
    piepserON();
  }
  else {
    piepserOFF();
  }  
}

unsigned long ampere_sum = 0;
unsigned long ampere_con = 0;
float ampere = 0;
unsigned long ampere_time;

void ampere_control() {
    
    ampere_sum += ((get_adc_average(5) * 2.048 )/ 4096.0)*1000.0;
    ampere_con++;

    if (millis()-ampere_time > 10*60*1000) {
      ampere_time = millis();
      ampere = ampere_sum/ampere_con;
      ampere_con = 0;
      ampere_sum = 0;
    }
}

/*
// Sobald geladen wird, Referenzen neu setzen / zurücksetzen
void battery_reset_reference() {

  if (battery.charge) {
  
    if (battery.full != 0) {        // Full Referenz zurücksetzen
      battery.full = 0;
      battery.sincefull = 0;          // Entladezeit zurücksetzen
      setconfig(eSYSTEM,{});
      Serial.println("Battery Start Loading");
    }
  
    if (battery.startload == 0 && !sys.stby) {   // Start Load Reference zu Beginn des Ladens setzen
      battery.startload = battery.voltage;
      // bei Systemstart überhöht, gepeichert wäre besser
      Serial.println("Battery Load Start Reference");
    }
  }
}

// Battery Full Reference (Ladeendpunkt) setzen
void battery_set_full(bool full) {
  if (full) battery.full = battery.max;     
  else  battery.full = constrain(median_average(),0,BATTMAX);  
  battery.sincefull = 0;
  battery.startload = 0;
  setconfig(eSYSTEM,{});
  Serial.println("Battery Full Reference");
}


#define INTERVALBATTERYSIMULATION 1000*60*5
void battery_simulation() {

  // Full Referenz noch nicht gesetzt, da nicht vollständig aufgeladen
  // aber nicht direkt nach Systemstart
  if (battery.full == 0 && !battery.charge && millis() > INTERVALCOMMUNICATION) {
    battery_set_full(0);
  }

  
  if ((millis() - lastUpdateBattery > INTERVALBATTERYSIMULATION)) {

    // Batterie-Entladezeit, darf nur laufen, wenn keine Quelle anhängt
    if (!battery.charge && (median_average() < battery.max)) {   // kein Laden, Quelle entfernt
      battery.sincefull += INTERVALBATTERYSIMULATION;
      setconfig(eSYSTEM,{});
         
    } else if (battery.charge) {            // Ladezeit
      battery.sincefull += INTERVALBATTERYSIMULATION;
    }
    
    lastUpdateBattery = millis();
    Serial.print("Battery Time: ");
    Serial.println(battery.sincefull);
  }
}

*/

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Reading Temperature KTYPE
double get_thermocouple(bool internal) {

  long dd = 0;
  
  // Communication per I2C Pins but with CS
  digitalWrite(THERMOCOUPLE_CS, LOW);                    // START
  for (uint8_t i=32; i; i--){
    dd = dd <<1;
    if (twi_read_bit())  dd |= 0x01;
  }
  digitalWrite(THERMOCOUPLE_CS, HIGH);                   // END

  // Invalid Measurement
  if (dd & 0x7) {             // Abfrage von D0/D1/D2 (Fault)
    return INACTIVEVALUE; 
  }

  if (internal) {
    // Internal Reference
    double ii = (dd >> 4) & 0x7FF;     // Abfrage D4-D14
    ii *= 0.0625;
    if ((dd >> 4) & 0x800) ii *= -1;  // Abfrage D15 (Vorzeichen)
    return ii;
  }
  

  // Temperatur
  if (dd & 0x80000000) {    // Abfrage D31 (Vorzeichen)
    // Negative value, drop the lower 18 bits and explicitly extend sign bits.
    dd = 0xFFFFC000 | ((dd >> 18) & 0x00003FFFF);
  }
  else {
    // Positive value, just drop the lower 18 bits.
    dd >>= 18;
  }

  // Temperature in Celsius
  double vv = dd;
  vv *= 0.25;
  return vv;
}










