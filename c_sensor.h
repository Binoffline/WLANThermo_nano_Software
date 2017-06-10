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

  // THERMOCOUPLE
  #ifdef KTYPE
    // CS notwendig, da nur bei CS HIGH neue Werte im Chip gelesen werden
    pinMode(THERMOCOUPLE_CS, OUTPUT);
    digitalWrite(THERMOCOUPLE_CS, HIGH);
  #endif

  // Piepser
  pinMode(MOSI, OUTPUT);
  analogWriteFreq(4000);

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


#ifdef KTYPE

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Reading Temperature KTYPE
double get_thermocouple(void) {

  long dd = 0;
  
  // Communication per I2C Pins but with CS
  digitalWrite(THERMOCOUPLE_CS, LOW);                    // START
  for (uint8_t i=32; i; i--){
    dd = dd <<1;
    if (twi_read_bit())  dd |= 0x01;
  }
  digitalWrite(THERMOCOUPLE_CS, HIGH);                   // END

  // Invalid Measurement
  if (dd & 0x7) {
    //DPRINTPLN("No thermocouple!");
    return INACTIVEVALUE; 
  }

  if (dd & 0x80000000) {
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

#endif


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
  battery.charge = curStateNone;
  
  // Standby erkennen
  if (voltage < 10) {
    stby = true;
    return;
  }
  else stby = false;

  // Transformation Digitalwert in Batteriespannung
  voltage = voltage * BATTDIV; 

  // Referenzwert bei COMPLETE neu setzen
  if ((curStateNone && curStatePull) && battery.setreference) {     // COMPLETE
    if (battery.voltage > 0) {
      if (battery.voltage < battery.max) {
        battery.max = battery.voltage-10;      
        // Grenze etwas nach unten versetzen, um die Ladespannung zu kompensieren
        // alternativ die Speicherung um 5 min verschieben, Gefahr: das dann schon abgeschaltet
        modifyconfig(eSYSTEM,{});                                      // SPEICHERN
        DPRINTF("[INFO]\tNew battery voltage reference: %umV\r\n", battery.max); 
      }
    }
    battery.setreference = false;
  } else if (!curStateNone && !curStatePull) {                      // LOAD
    battery.setreference = true;
  }

  // Batteriespannung wird in einen Buffer geschrieben da die gemessene
  // Spannung leicht schwankt, aufgrund des aktuellen Energieverbrauchs
  // wird die Batteriespannung als Mittel aus mehreren Messungen ausgegeben

  vol_sum += voltage;
  vol_count++;
  
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Calculate SOC
void cal_soc() {

  // mittlere Batteriespannung aus dem Buffer lesen und in Prozent umrechnen
  int voltage;
  
  if (vol_count > 0) voltage = vol_sum / vol_count;
  else voltage = 0;
  median_add(voltage);
  battery.voltage = median_average();
  
  battery.percentage = ((battery.voltage - battery.min)*100)/(battery.max - battery.min);
  
  // Schwankungen verschiedener Batterien ausgleichen
  if (battery.percentage > 100) battery.percentage = 100;
  
  DPRINTF("[INFO]\tBattery voltage: %umV,\tcharge: %u%%\r\n", battery.voltage, battery.percentage); 

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
        setalarm = true;

        // Show Alarm
        if (ch[i].show && !displayblocked) {
          displayblocked = true;
          ch[i].show = false;
          question.typ = HARDWAREALARM;
          question.con = i;
          drawQuestion(i);
        }
      
      } else if (!ch[i].isalarm && ch[i].temp != INACTIVEVALUE) {
        // first rising limits
        ch[i].isalarm = true;
        ch[i].showalarm = true;
        ch[i].show = true;
        setalarm = true;

        if (!isAP) {
        #ifdef THINGSPEAK
          if (charts.TShttpKey != "") {
            if (ch[i].temp > ch[i].max) sendMessage(i+1,1);
            else if (ch[i].temp < ch[i].min) sendMessage(i+1,0);
          }
        #endif
        }
      }
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








