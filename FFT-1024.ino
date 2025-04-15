// full system that records and filters raw sound data
// and finds FFT and rms

// load library for usb host
#include "USBHost_t36.h"

// button libraries
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>

#include <Bounce.h>


#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// GUItool: begin automatically generated code
AudioInputI2S            i2s2;           //xy=1374.2857666015625,857.1428833007812
AudioPlaySdRaw           playSdRaw1;     //xy=1491.2857666015625,780.1428833007812
AudioRecordQueue         queue1;         //xy=1500.2857666015625,972.1428833007812
AudioMixer4              mixer1;         //xy=1594.2857666015625,862.1428833007812
AudioFilterStateVariable filter1;        //xy=1742.2857666015625,868.1428833007812
AudioFilterStateVariable filter2;        //xy=1854.2857666015625,958.1428833007812
AudioAnalyzeRMS          rms1;           //xy=2005.2857666015625,1036.1428833007812
AudioAnalyzePeak         peak1;          //xy=2007.2857666015625,1085.1428833007812
AudioOutputI2S           i2s1;           //xy=2010.2857666015625,966.1428833007812
AudioAnalyzeFFT1024      fft1024_1;      //xy=2019.0357971191406,896.3928966522217
AudioConnection          patchCord1(i2s2, 0, queue1, 0);
AudioConnection          patchCord2(i2s2, 0, mixer1, 1);
AudioConnection          patchCord3(playSdRaw1, 0, mixer1, 0);
AudioConnection          patchCord4(mixer1, 0, filter1, 0);
AudioConnection          patchCord5(filter1, 0, filter2, 0);
AudioConnection          patchCord6(filter2, 2, rms1, 0);
AudioConnection          patchCord7(filter2, 2, fft1024_1, 0);
AudioConnection          patchCord8(filter2, 2, peak1, 0);
AudioConnection          patchCord9(filter2, 2, i2s1, 0);
AudioConnection          patchCord10(filter2, 2, i2s1, 1);
AudioControlSGTL5000     sgtl5000_1;     //xy=1682.2857666015625,1069.1428833007812
// GUItool: end automatically generated code


// button constants
#define  DEFAULT_I2C_ADDR 0x3A
#define  SWITCH1  18  
#define  SWITCH2  19 
#define  PWM1  12  
#define  PWM2  13 

Adafruit_seesaw ss;

// const int myInput = AUDIO_INPUT_MIC;
// const int myInput = AUDIO_INPUT_LINEIN;
int myInput;

// features for Teensy Audio Shield
#define SDCARD_CS_PIN   10
#define SDCARD_MOSI_PIN 7
#define SDCARD_SCK_PIN  14

// intialize usb host as usb hub and enable MIDI host on it
// usb hub: other usb devices (casio keyboard) can plug into it
// MIDI host: other MIDI devices can send it MIDI messages
USBHost myusb;
USBHub hub1(myusb);
MIDIDevice midi1(myusb);

// file for recorded data
File frec;

void setup() {
  // setup for switch
  // pcb uses pin 28 for switch
  // set pinmode to INPUT_PULLUP bc switch connects pin to ground
  Serial.begin(38400);
  pinMode(28, INPUT_PULLUP);
  
  // set amount of memory in audio board
  AudioMemory(14);
  Serial.begin(115200);
  while (!Serial) delay(10); // wait until serial port is opened

  // button board (seesaw) set up code 
  if (!ss.begin(DEFAULT_I2C_ADDR)) {
    Serial.println(F("seesaw not found!"));
    while(1) delay(10);
  }

  Serial.println(F("seesaw started!"));
  ss.pinMode(SWITCH1, INPUT_PULLUP);
  ss.pinMode(SWITCH2, INPUT_PULLUP);
  ss.analogWrite(PWM1, 1);
  ss.analogWrite(PWM2, 1);

  // enable the audio shield, select input, and output volume
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(myInput);
  sgtl5000_1.micGain(48);
  sgtl5000_1.volume(0.6);

  //mixer1.gain(0, 1.0);
  //mixer1.gain(1, 1.0);
  
  // initialize the sd card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("unable to access the SD card");
      delay(500);
    }
  }


  // start usb MIDI host
  myusb.begin();

  // start highpass filter at 130 hz and lowpass at 2080 hz
  // middle C on piano is 260 hz (filters cover 4 octave range)
  // range is 1 below middle C and 3 octaves above
  filter1.frequency(2080);
  filter2.frequency(130);

  delay(500);
}

elapsedMillis msecs;

int isNotePlaying = 0;
int noteNumber = 0;
int prevNoteNumber = 0;
int prevNoteTurnedOn = 0;
int waitToSettle = 0;

int rms = 0;
int prevRms = 0;

float avgFreq = 0;
float prevAvgFreq = 0;

float fftSum = 0;
float fftAvg = 0;
float FFT_bins[16];
float FFT_bins_minus_avg[16];
float freqFFT;

int parabola_index = 0;
int parabola_x_index_values[3];
int parabola_x_values[3];
float parabola_y_values[3];
int parabola_x_index_to_value[16] = {129, 172, 215, 258, 301, 345, 388, 431, 474, 517, 560, 603, 646, 689, 732, 775};

// button variables
uint8_t incr =0;
uint8_t turnOff = 1;
uint8_t turnOn = 240;

// 1 = up, 0 = down
int switch1Up = 1;
int switch2Up = 1;

// recording modes
int mode12 = 0;  // 0=stopped, 1=recording, 2=playing

elapsedMillis timerButtonSwitch = 0; 
void loop() {
  if (timerButtonSwitch > 100) {
    timerButtonSwitch = 0;
    //switch for line in vs. mic
    if (digitalRead(28) == HIGH) {
      myInput = AUDIO_INPUT_MIC;
      // Serial.println("MIC");
      sgtl5000_1.inputSelect(myInput);
    } else {
      myInput = AUDIO_INPUT_LINEIN;
      // Serial.println("line in");
      sgtl5000_1.inputSelect(myInput);
    }
    
    // digitalRead is 0 when pressed (false)
    if (! ss.digitalRead(SWITCH1)) { 
      //Serial.println("Record 1 Button Press");
      // mode - 0=stopped, 1=recording, 2=playing
      if ((mode12 == 0 || mode12 == 2) && switch1Up == 1) {
        // prevents pitch from recording and playing from mixing
        mixer1.gain(0,0);
        mixer1.gain(1,1);
        Serial.println("Record 1 is ON");
        if (mode12 == 2) stopPlaying();
        if (mode12 == 0) startRecording();
        ss.analogWrite(PWM1, turnOn);
      } else if (mode12 == 1 && switch1Up == 1) {
        mixer1.gain(0,0);
        mixer1.gain(1,1);
        Serial.println("Record 1 is OFF");
        stopRecording();
        ss.analogWrite(PWM1, turnOff);
      }  
      switch1Up = 0;    
    } else { 
      switch1Up = 1;
    }
  }
  
  if (! ss.digitalRead(SWITCH2)) {
    //Serial.println("Play 1 Button Press");
    // mode - 0=stopped, 1=recording, 2=playing
    if ((mode12 == 0 || mode12 == 1) && switch2Up == 1) {
      mixer1.gain(0,1);
      mixer1.gain(1,0);
      Serial.println("Play 1 is ON");
      if (mode12 == 1) stopRecording();
      if (mode12 == 0) startPlaying();
      ss.analogWrite(PWM2, turnOn);
    } else if (mode12 == 2 && switch2Up == 1) {
      mixer1.gain(0,0);
      mixer1.gain(1,1);
      Serial.println("Play 1 is OFF");
      stopPlaying();
      ss.analogWrite(PWM2, turnOff);
    }
    switch2Up = 0; 
  } else { 
    switch2Up = 1;
  }

  // if playing or recording, keep going...
  if (mode12 == 1) {
    continueRecording();
  }
  if (mode12 == 2) {
    continuePlaying();
  }
  
  if (msecs > 25) {
    msecs = 0;
    
    if (rms1.available()) {
      rms = int(100 * rms1.read()) + prevRms / 2;
      prevRms = rms;
      //Serial.println(rms);
    }
        
    if (fft1024_1.available()) {
      // FFT with 1024 bins at a 43 Hz resolution - ViVo looks at 129 Hz to 775 Hz
      fftSum = 0;
      for (int i = 0; i < 16; i++) {
        FFT_bins[i] = fft1024_1.read(i+3) * 300;
        fftSum += FFT_bins[i];
      }
      fftAvg = fftSum / 16;

      for (int i = 0; i < 16; i++) {
        if (FFT_bins[i] > fftAvg) {
          FFT_bins_minus_avg[i] = FFT_bins[i] - fftAvg;
        } else {
          FFT_bins_minus_avg[i] = 0;
        }
        if (fftAvg < 1) {
          FFT_bins_minus_avg[i] = 0;
        }

        //Serial.print(FFT_bins_minus_avg[i]);
        //Serial.print("   ");

        // if the average FFT is strong enough to find the center frequency
        // find the first three FFT bins that are greater than the average FFT bin value
        if (fftAvg > 1) {
          if (FFT_bins_minus_avg[i] > 0 && parabola_index < 3) {
            parabola_x_index_values[parabola_index] = i;
            parabola_y_values[parabola_index] = FFT_bins[i];
            parabola_index++;
          }
        }
      }
      parabola_index = 0;

      //Serial.println(" ");

      if (fftAvg > 1) {
        for (int i=0; i < 3; i++) {
          parabola_x_values[i] = parabola_x_index_to_value[parabola_x_index_values[i]];
        }
        int x1 = parabola_x_values[0];
        int x2 = parabola_x_values[1];
        int x3 = parabola_x_values[2];

        int y1 = parabola_y_values[0];
        int y2 = parabola_y_values[1];
        int y3 = parabola_y_values[2];
    
        double denom = (x1 - x2) * (x1 - x3) * (x2 - x3);
        double A     = (x3 * (y2 - y1) + x2 * (y1 - y3) + x1 * (y3 - y2)) / denom;
        double B     = (x3*x3 * (y1 - y2) + x2*x2 * (y3 - y1) + x1*x1 * (y2 - y3)) / denom;
        //double C     = (x2 * x3 * (x2 - x3) * y1 + x3 * x1 * (x3 - x1) * y2 + x1 * x2 * (x1 - x2) * y3) / denom;
        double xv = -B / (2*A);
        //double yv = C - B*B / (4*A);

        avgFreq = (float) xv;
        //Serial.println(freqFFT);
      } else {
        //freqFFT = 0;
      }

      if (avgFreq >= 130 && avgFreq < 254) { 
        if (avgFreq >= 130 && avgFreq < 138) noteNumber = 48; 
        if (avgFreq >= 138 && avgFreq < 146) noteNumber = 49; 
        if (avgFreq >= 146 && avgFreq < 155) noteNumber = 50; 
        if (avgFreq >= 155 && avgFreq < 164) noteNumber = 51; 
        if (avgFreq >= 164 && avgFreq < 174) noteNumber = 52; 
        if (avgFreq >= 174 && avgFreq < 185) noteNumber = 53; 
        if (avgFreq >= 185 && avgFreq < 196) noteNumber = 54; 
        if (avgFreq >= 196 && avgFreq < 207) noteNumber = 55; 
        if (avgFreq >= 207 && avgFreq < 220) noteNumber = 56; 
        if (avgFreq >= 220 && avgFreq < 233) noteNumber = 57; 
        if (avgFreq >= 233 && avgFreq < 246) noteNumber = 58; 
        if (avgFreq >= 246 && avgFreq < 254) noteNumber = 59; 
      }
      if (avgFreq >= 254 && avgFreq < 509) {
        if (avgFreq >= 254 && avgFreq < 269) noteNumber = 60;
        if (avgFreq >= 269 && avgFreq < 285) noteNumber = 61;
        if (avgFreq >= 285 && avgFreq < 302) noteNumber = 62;
        if (avgFreq >= 302 && avgFreq < 320) noteNumber = 63;
        if (avgFreq >= 320 && avgFreq < 339) noteNumber = 64;
        if (avgFreq >= 339 && avgFreq < 360) noteNumber = 65;
        if (avgFreq >= 360 && avgFreq < 381) noteNumber = 66;
        if (avgFreq >= 381 && avgFreq < 404) noteNumber = 67;
        if (avgFreq >= 404 && avgFreq < 428) noteNumber = 68;
        if (avgFreq >= 428 && avgFreq < 453) noteNumber = 69;
        if (avgFreq >= 453 && avgFreq < 480) noteNumber = 70;
        if (avgFreq >= 480 && avgFreq < 509) noteNumber = 71;
      }
      if (avgFreq >= 509 && avgFreq < 1017) {
        if (avgFreq >= 509 && avgFreq < 539) noteNumber = 72;
        if (avgFreq >= 539 && avgFreq < 571) noteNumber = 73;
        if (avgFreq >= 571 && avgFreq < 605) noteNumber = 74;
        if (avgFreq >= 605 && avgFreq < 641) noteNumber = 75;
        if (avgFreq >= 641 && avgFreq < 679) noteNumber = 76;
        if (avgFreq >= 679 && avgFreq < 719) noteNumber = 77;
        if (avgFreq >= 719 && avgFreq < 762) noteNumber = 78;
        if (avgFreq >= 762 && avgFreq < 807) noteNumber = 79;
        if (avgFreq >= 807 && avgFreq < 855) noteNumber = 80;
        if (avgFreq >= 855 && avgFreq < 906) noteNumber = 81;
        if (avgFreq >= 906 && avgFreq < 960) noteNumber = 82;
        if (avgFreq >= 960 && avgFreq < 1017) noteNumber = 83;
      }
      
      if (rms > 20) {
        if (130 < avgFreq && avgFreq < 800) {
          prevAvgFreq = avgFreq;
          Serial.println(avgFreq);
        }
      }
    }

    //Serial.println(AudioMemoryUsageMax());
  }
    
  // if signal is strong and in the right frequency range
  // 48 is low C, 60 is middle C, 72 is C5, and 84 is C6
  if (rms > 20 && noteNumber >= 48 && noteNumber <= 84) {
    if (isNotePlaying == 0) {
      waitToSettle++;
      if (waitToSettle > 7) {
        midi1.sendNoteOn(noteNumber, 30, 4);
        //Serial.println("Note on");
        //midi1.sendNoteOn(noteNumber + 12, 30, 4);
        isNotePlaying = 1;
        prevNoteTurnedOn = noteNumber;
        waitToSettle = 0;
      }
    }

    if (isNotePlaying == 1 && (noteNumber != prevNoteTurnedOn)) {
      waitToSettle++;
      if (waitToSettle > 7) {
        midi1.sendNoteOff(prevNoteNumber, 0, 4);
        midi1.sendNoteOn(noteNumber, 30, 4);
        prevNoteTurnedOn = noteNumber;
      }
    }

    if (isNotePlaying == 1 && (noteNumber == prevNoteTurnedOn)) {
      waitToSettle = 0;
    }
  } 

  // if signal is weak and note if playing, turn off note
  if (rms < 15 && isNotePlaying == 1) {
    midi1.sendNoteOff(prevNoteNumber, 0, 4);
    isNotePlaying = 0;
    waitToSettle = 0;
  }
}

void startRecording() {
  Serial.println("start Recording");
  if (SD.exists("RECORD.RAW")) {
    // The SD library writes new data to the end of the
    // file so to start a new recording, the old file
    // must be deleted
    SD.remove("RECORD.RAW");
  }
  frec = SD.open("RECORD.RAW", FILE_WRITE);
  if (frec) {
    queue1.begin();
    mode12 = 1;
  }
}

void continueRecording() {
  if (queue1.available() >= 2) {
    byte buffer[512];
    memcpy(buffer, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    memcpy(buffer+256, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    // write all 512 bytes to the SD card
    //elapsedMicros usec = 0;
    frec.write(buffer, 512);
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  queue1.end();
  if (mode12 == 1) {
    while (queue1.available() > 0) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
    }
    frec.close();
  }
  mode12 = 0;
}


void startPlaying() {
  Serial.println("startPlaying");
  playSdRaw1.play("RECORD.RAW");
  mode12 = 2;
}

void continuePlaying() {
  if (!playSdRaw1.isPlaying()) {
    Serial.println("stopPlaying - end of file");   
    playSdRaw1.stop();
    ss.analogWrite(PWM2, turnOff);
    mode12 = 0;
    mixer1.gain(0,0);
    mixer1.gain(1,1);
  }
}

void stopPlaying() {
  Serial.println("stopPlaying");
  if (mode12 == 2) playSdRaw1.stop();
  mode12 = 0;
}
