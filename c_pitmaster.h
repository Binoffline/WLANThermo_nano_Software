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

    The AutotunePID elements of this program based on the work of 
    AUTHOR: Repetier
    PURPOSE: Repetier-Firmware/extruder.cpp

    HISTORY: Please refer Github History
    
 ****************************************************/

int pidMax = 100;      // Maximum (PWM) value, the heater should be set


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Set Pitmaster Pin
void set_pitmaster(bool init) {
  
  pinMode(PITMASTER1, OUTPUT);
  digitalWrite(PITMASTER1, LOW);

  pinMode(PITMASTER2, OUTPUT);
  digitalWrite(PITMASTER2, LOW);

  if (init) {
    pitmaster.pid = 0;
    pitmaster.channel = 0;
    pitmaster.set = PITMASTERSETMIN;
    pitmaster.active = false;
    //pitmaster.resume = 0;
  }

  pitmaster.resume = 1;   // später wieder raus
  if (!pitmaster.resume) pitmaster.active = false; 

  pitmaster.value = 0;
  pitmaster.manual = false;
  pitmaster.event = false;
  pitmaster.msec = 0;
  pitmaster.pause = 1000;

}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Set Default PID-Settings
void set_pid() {
  
  pidsize = 3; //3;
  pid[0] = {"SSR SousVide", 0, 0, 165, 0.591, 1000, 100, 0.08, 5, 0, 95, 0.9, 0, 0, 100, 0, 0, 0, 0};
  pid[1] = {"TITAN 50x50", 1, 1, 3.8, 0.01, 128, 6.2, 0.001, 5, 0, 95, 0.9, 0, 25, 100, 0, 0, 0, 0};
  pid[2] = {"Kamado 50x50", 2, 1, 7.0, 0.019, 630, 6.2, 0.001, 5, 0, 95, 0.9, 0, 25, 100, 0, 0, 0, 0};

}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PID
float PID_Regler(){ 

  // see: http://rn-wissen.de/wiki/index.php/Regelungstechnik

  float x = ch[pitmaster.channel].temp;         // IST
  float w = pitmaster.set;                      // SOLL
  byte ii = pitmaster.pid;
  
  // PID Parameter
  float kp, ki, kd;
  if (x > (pid[ii].pswitch * w)) {
    kp = pid[ii].Kp;
    ki = pid[ii].Ki;
    kd = pid[ii].Kd;
  } else {
    kp = pid[ii].Kp_a;
    ki = pid[ii].Ki_a;
    kd = pid[ii].Kd_a;
  }

  // Abweichung bestimmen
  //float e = w - x;                          
  int diff = (w -x)*10;
  float e = diff/10.0;              // nur Temperaturunterschiede von >0.1°C beachten
  
  // Proportional-Anteil
  float p_out = kp * e;                     
  
  // Differential-Anteil
  float edif = (e - pid[ii].elast)/(pitmaster.pause/1000.0);   
  pid[ii].elast = e;
  float d_out = kd * edif;                  

  // Integral-Anteil
  // Anteil nur erweitert, falls Bregrenzung nicht bereits erreicht
  if ((p_out + d_out) < PITMAX) {
    pid[ii].esum += e * (pitmaster.pause/1000.0);             
  }
  // ANTI-WIND-UP (sonst Verzögerung)
  // Limits an Ki anpassen: Ki*limit muss y_limit ergeben können
  if (pid[ii].esum * ki > pid[ii].Ki_max) pid[ii].esum = pid[ii].Ki_max/ki;
  else if (pid[ii].esum * ki < pid[ii].Ki_min) pid[ii].esum = pid[ii].Ki_min/ki;
  float i_out = ki * pid[ii].esum;
                    
  // PID-Regler berechnen
  float y = p_out + i_out + d_out;  
  y = constrain(y,PITMIN,PITMAX);           // Auflösung am Ausgang ist begrenzt            
  
  DPRINTLN("{INFO]\tPID:" + String(y,1) + "\tp:" + String(p_out,1) + "\ti:" + String(i_out,2) + "\td:" + String(d_out,1));
  
  return y;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
void disableAllHeater() {
  // Anschlüsse ausschalten
  digitalWrite(PITMASTER1, LOW);
  pitmaster.active = 0;
  pitmaster.value = 0;
  pitmaster.event = false;
  pitmaster.msec = 0;
  
}

static inline float min(float a,float b)  {
        if(a < b) return a;
        return b;
}

static inline float max(float a,float b)  {
        if(a < b) return b;
        return a;
}

void stopautotune() {
  autotune.value = 0;
  autotune.initialized = false;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
void startautotunePID(int maxCycles, bool storeValues)  {

    if (!autotune.initialized) {
  
      autotune.cycles = 0;           // Durchläufe
      autotune.heating = true;      // Flag
      autotune.temp = pitmaster.set;
      autotune.maxCycles = constrain(maxCycles, 5, 20);
      autotune.storeValues = storeValues;
  
      uint32_t temp_millis = millis();
      autotune.t0 = temp_millis;    // Zeitpunkt t0
      autotune.t1 = temp_millis;    // Zeitpunkt t1
      autotune.t2 = temp_millis;    // Zeitpunkt t2
      autotune.t_high = 0;
      autotune.bias = pidMax/2;         // Startwert = halber Wert
      autotune.d = pidMax/2;          // Startwert = halber Wert

      float tem = ch[pitmaster.channel].temp; 
      autotune.Kp = 0;
      autotune.Ki = 0; 
      autotune.Kd = 0;
      autotune.maxTemp = tem; 
      autotune.minTemp = tem;
      autotune.previousTemp = tem;  // Startwert
      autotune.maxTP = 0.0;
      autotune.tWP = temp_millis;
      autotune.TWP = tem;
  
      DPRINTPLN("[AUTOTUNE]\t Start!");
 
      disableAllHeater();         // switch off all heaters.
      
      autotune.value = pidMax;   // Aktor einschalten
    }
    
    autotune.initialized = true;
    pitmaster.active = true;
    
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
float autotunePID() {

  /*
   Relay Feedback Test [Astrom & Hagglund, 1984
   Autotune Variation (ATV)
   see: https://www.google.de/?gws_rd=ssl#q=Relay+Feedback+PID
   
         
             t_high = t1-t2*
     +d       ____      ____      _
     bias--__|    |____|    |____|
     -d     t2*   t1   t2
   /                         t_low = t2-t1  
   
   */

  float currentTemp = ch[pitmaster.channel].temp;
  unsigned long time = millis();

  if (autotune.cycles == 0) { 
    float TP = (currentTemp - autotune.previousTemp)/ (float)pitmaster.pause;
    if (autotune.maxTP < TP) {
      autotune.maxTP = TP;
      autotune.tWP = time;
      autotune.TWP = (currentTemp + autotune.previousTemp)/2.0;
      DPRINTP("[AUTOTUNE]\tWendepunktbestimmung: ");
      DPRINTLN(TP*1000);
    }
    autotune.previousTemp = currentTemp;
  }
  
  autotune.maxTemp = max(autotune.maxTemp,currentTemp);
  autotune.minTemp = min(autotune.minTemp,currentTemp);
  
  // Soll während aufheizen ueberschritten --> switch heating off  
  if (autotune.heating == true && currentTemp > autotune.temp)  {
    if(time - autotune.t2 > 3000)  {    // warum Wartezeit und wieviel
                
      autotune.heating = false;
      autotune.value = (autotune.bias - autotune.d);     // Aktorwert
                
      autotune.t1 = time;
      autotune.t_high = autotune.t1 - autotune.t2;
      autotune.maxTemp = autotune.temp;
      DPRINTPLN("[AUTOTUNE]\tTemperature overrun!");
    }
  }
  
  // Soll während abkühlen unterschritten --> switch heating on
  else if (autotune.heating == false && currentTemp < autotune.temp) {
    if(time - autotune.t1 > 3000)  {
                
      autotune.heating = true;
      autotune.t2 = time;
      autotune.t_low = autotune.t2 - autotune.t1; // half wave length
      DPRINTPLN("[AUTOTUNE]\tTemperature fall below!");

      if (autotune.cycles == 0) {

        // Bestimmung von Kp_a, Ki_a, Kd_a
        // Approximation der Strecke durch PT1Tt-Glied aus Sprungantwort

        uint32_t tWP1 = (autotune.TWP-autotune.minTemp)/autotune.maxTP;  // Zeitabschn. (ms) unter Wendetangente bis Ausgangstemperatur
        uint32_t tWP2 = (autotune.temp-autotune.TWP)/autotune.maxTP;     // Zeitabschn. (ms) oberhalb Wendetangente bis Zieltemperatur
        
        uint32_t Tt = (autotune.tWP - autotune.t0 - tWP1)/1000.0;      // Totzeit in sec
        uint32_t Tg = (tWP1 + tWP2)/1000.0;                            // Ausgleichszeit in sec

        float Ks = (autotune.temp - autotune.minTemp)/(autotune.bias + autotune.d);
        float Tn = 2 * Tt;            // Tn = 3.33 * Tt;
        float Tv = 0.5 * Tt;

        DPRINTP("[AUTOTUNE]\tTt: ");
        DPRINT(Tt);
        DPRINTP(" s, Tg: ");
        DPRINT(Tg);
        DPRINTPLN(" s");

        autotune.Kp_a = 1.2/Ks * Tg/Tt;  // Kp_a = 0.9/Ks * Tg/Tt
        autotune.Ki_a = autotune.Kp_a/Tn;
        autotune.Kd_a = autotune.Kp_a*Tv;

        DPRINTP("[AUTOTUNE]\tKp_a: ");
        DPRINT(autotune.Kp_a);
        DPRINTP(" Ki_a: ");
        DPRINT(autotune.Ki_a);
        DPRINTP(" Kd_a: ");
        DPRINTLN(autotune.Kd_a);
        
        
      } else {

        // Anpassung Bias
        autotune.bias += (autotune.d*(autotune.t_high - autotune.t_low))/(autotune.t_low + autotune.t_high);
        autotune.bias = constrain(autotune.bias, 5 ,pidMax - 5);  // Arbeitsbereich zwischen 5% und 95%
                    
        if (autotune.bias > pidMax/2)  autotune.d = pidMax - autotune.bias;
        else autotune.d = autotune.bias;
        
        DPRINTP("[AUTOTUNE]\tbias: ");
        DPRINT(autotune.bias);
        DPRINTP(" d: ");
        DPRINTLN(autotune.d);

        if(autotune.cycles > 2)  {

        // Parameter according Ziegler & Nichols method: http://en.wikipedia.org/wiki/Ziegler%E2%80%93Nichols_method
        // Ku = kritische Verstaerkung = 4*d / (pi*A)
        // Tu = kritische Periodendauer = t_low + t_high = (t2 - t1) + (t1 - t2*) = t2 - t2*
        // A = Amplitude der Schwingung = maxTemp-minTemp/2 // Division fehlt in der Quelle

          float A = (autotune.maxTemp - autotune.minTemp)/2.0;
          float Ku = (4.0 * autotune.d) / (3.14159 * A);
          float Tu = ((float)(autotune.t_low + autotune.t_high)/1000.0);
          
          DPRINTP("[AUTOTUNE]\tKu: ");
          DPRINT(Ku);
          DPRINTP(" Tu: ");
          DPRINT(Tu);
          DPRINTP(" A: ");
          DPRINT(A);
          DPRINTP(" t_max: ");
          DPRINT(autotune.maxTemp);
          DPRINTP(" t_min: ");
          DPRINTLN(autotune.minTemp);

          float Tn = 0.5*Tu;
          float Tv = 0.125*Tu;
          
          autotune.Kp = 0.6*Ku;
          autotune.Ki = autotune.Kp/Tn;      // Ki = 1.2*Ku/Tu = Kp/Tn = Kp/(0.5*Tu) = 2*Kp/Tu
          autotune.Kd = autotune.Kp*Tv;      // Kd = 0.075*Ku*Tu = Kp*Tv = Kp*0.125*Tu
                        
          DPRINTP("[AUTOTUNE]\tKp: ");
          DPRINT(autotune.Kp);
          DPRINTP(" Ki: ");
          DPRINT(autotune.Ki);
          DPRINTP(" Kd: ");
          DPRINTLN(autotune.Kd);
            
        }
      }
      autotune.value = (autotune.bias + autotune.d);
      DPRINTPLN("[AUTOTUNE]\tNext cycle");
      autotune.cycles++;
      autotune.minTemp = autotune.temp;
    }
  }
    
  if (currentTemp > (autotune.temp + 40))  {   // FEHLER
    DPRINTPLN("[ERROR]\tAutotune failure: Overtemperature");
    disableAllHeater();
    stopautotune();
    return 0;
  }
    
  if (((time - autotune.t1) + (time - autotune.t2)) > (10L*60L*1000L*2L)) {   // 20 Minutes
        
    DPRINTPLN("[ERROR]\tAutotune failure: TIMEOUT");
    disableAllHeater();
    stopautotune();
    return 0;
  }
    
  if (autotune.cycles > autotune.maxCycles) {       // FINISH
            
    DPRINTPLN("[AUTOTUNE]\tFinished!");
    disableAllHeater();
    stopautotune();
            
    if (autotune.storeValues)  {
      
      pid[pitmaster.pid].Kp = autotune.Kp;
      pid[pitmaster.pid].Ki = autotune.Ki;
      pid[pitmaster.pid].Kd = autotune.Kd;

      
      pid[pitmaster.pid].Kp_a = autotune.Kp_a;
      pid[pitmaster.pid].Ki_a = autotune.Ki_a;
      pid[pitmaster.pid].Kd_a = autotune.Kd_a;
      DPRINTLN("[AUTOTUNE]\tParameters saved!");
    }
    return 0;
  }
  DPRINTLN("{INFO]\tAUTOTUNE: " + String(autotune.value,1) + " %");
  return autotune.value;       
}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Control - Manuell PWM
void pitmaster_control() {
  // ESP PWM funktioniert nur bis 10 Hz Trägerfrequenz stabil, daher eigene Taktung
  if (pitmaster.active == 1) {
  
    // Ende eines HIGH-Intervalls, wird durch pit_event nur einmal pro Intervall durchlaufen
    if ((millis() - pitmaster.last > pitmaster.msec) && pitmaster.event) {
      digitalWrite(PITMASTER1, LOW);
      pitmaster.event = false;
    }

    // neuen Stellwert bestimmen und ggf HIGH-Intervall einleiten
    if (millis() - pitmaster.last > pitmaster.pause) {
  
      float y;

      if (autotune.initialized)       pitmaster.value = autotunePID();
      else if (!pitmaster.manual)     pitmaster.value = PID_Regler();
      // falls manuel wird value vorgegeben
      
      if (pid[pitmaster.pid].aktor == 1)                // FAN
        analogWrite(PITMASTER1,map(pitmaster.value,0,100,0,1024));
      else if (pid[pitmaster.pid].aktor == 0){          // SSR
        pitmaster.msec = map(pitmaster.value,0,100,0,pitmaster.pause); 
        if (pitmaster.msec > 0) digitalWrite(PITMASTER1, HIGH);
        if (pitmaster.msec < pitmaster.pause) pitmaster.event = true;  // außer bei 100%
      }
    
      pitmaster.last = millis();
    }
  } else {
    digitalWrite(PITMASTER1, LOW);
    pitmaster.value = 0;
    pitmaster.event = false;
    pitmaster.msec = 0;
  }
  
}










