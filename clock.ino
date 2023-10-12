/**
 * Seven-segment display clock.
 *
 * This needs:
 * - 7 row drives
 * - 4 column returns
 *
 * The segments: 
 *  ____
 * |    |
 * |    |
 *  ----
 * |    |
 * |    |
 *  ----
 *
 * Going counterclockwise: 0, 1, 2, 3, 4, 5
 * and the sixth is the middle segment.
 */

#include <Arduino.h>
#include <SPI.h>
#include <AceSPI.h>
#include <AceSegment.h>
#include <RTClib.h>

using ace_spi::HardSpiInterface;
using ace_segment::LedModule;
using ace_segment::Max7219Module;
using ace_segment::kDigitRemapArray8Max7219;

/*
 * The segments: 
 *  ____
 * |    |
 * |    |
 *  ----
 * |    |
 * |    |
 *  ----
 *
 * Going clockwise are: 0, 1, 2, 3, 4, 5. Middle segment = 6.
 *
 * Bitpatterns LSB to MSB! 
 */
const uint8_t DIG_TO_BITPATTERN[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111, // 9
};

const int UP_PIN = A1;
const int DOWN_PIN = A2;
const int SET_PIN = A3;

const uint8_t RAM_INITIALIZED_MAGIC[8] = { 'R', 'T', 'C', 'I', 'N', 'I', 'T', 'D' };

using SpiInterface = HardSpiInterface<SPIClass>;
SpiInterface spiInterface(SPI, 10);
Max7219Module<SpiInterface, 4> ledModule(
    spiInterface, kDigitRemapArray8Max7219);

// CE / SCLK / IO
DS1307 rtc;

int g_brightness = 8;

// 0 = current time
// 1 = year
// 2 = month
// 3 = day
int g_current_displaymode = 0;

int  g_serial_selection = 0;
char g_serial_inbox[14];

void setup() {
  Serial.begin(9600);

  Serial.println("init()...");

  SPI.begin();
  spiInterface.begin();
  ledModule.begin();

  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  pinMode(SET_PIN, INPUT_PULLUP);

  ledModule.setBrightness(8);


  Serial.println("starting rtc...");
  rtc.begin();
  Serial.println("starting rtc... done");
  
  // if RTC not set, assume battery died.
  // also check RAM values to make sure magic word is still there

  if (!rtc.isrunning() ||
      rtc.readram(0) != RAM_INITIALIZED_MAGIC[0] ||
      rtc.readram(1) != RAM_INITIALIZED_MAGIC[1] ||
      rtc.readram(2) != RAM_INITIALIZED_MAGIC[2] ||
      rtc.readram(3) != RAM_INITIALIZED_MAGIC[3] ||
      rtc.readram(4) != RAM_INITIALIZED_MAGIC[4] ||
      rtc.readram(5) != RAM_INITIALIZED_MAGIC[5] ||
      rtc.readram(6) != RAM_INITIALIZED_MAGIC[6] ||
      rtc.readram(7) != RAM_INITIALIZED_MAGIC[7]
    ) {
    Serial.println("RTC is NOT running!");
    programMode();

    // max brightness by default
    rtc.writeram(0x10,8);
    rtc.writeram(0, RAM_INITIALIZED_MAGIC[0]);
    rtc.writeram(1, RAM_INITIALIZED_MAGIC[1]);
    rtc.writeram(2, RAM_INITIALIZED_MAGIC[2]);
    rtc.writeram(3, RAM_INITIALIZED_MAGIC[3]);
    rtc.writeram(4, RAM_INITIALIZED_MAGIC[4]);
    rtc.writeram(5, RAM_INITIALIZED_MAGIC[5]);
    rtc.writeram(6, RAM_INITIALIZED_MAGIC[6]);
    rtc.writeram(7, RAM_INITIALIZED_MAGIC[7]);
  }
  
  // successful boot = set RAM values
  g_brightness = rtc.readram(0x10);

  Serial.println("initialization done");
}

void unpackBcd(uint8_t a, uint8_t b, uint8_t* buf) {
  buf[1] = a % 10;
  buf[0] = (a - buf[1]) / 10;
  buf[3] = b % 10;
  buf[2] = (b - buf[3]) / 10;
}

void programMode() {
  // set_state_last must be 1 coming into this loop
  uint8_t up_state_last = 0, down_state_last = 0, set_state_last = 1;
  uint8_t buf[4];
  uint8_t bitpatterns[4];
  
  uint8_t flash_state = 0;

  ledModule.setPatternAt(7, 0b01110111);
  ledModule.setPatternAt(6, 0b01101101);
  ledModule.setPatternAt(5, 0b01101101);
  ledModule.setPatternAt(4, 0);
  ledModule.setBrightness(g_brightness);
  ledModule.flush();

  DateTime dt;
  if (!rtc.isrunning()) {
    dt = DateTime(2022, 1, 1, 0, 0, 0);
  } else {
    do
    {
      dt = rtc.now();;
    } while (!dateTimeValid(dt));
  }

  ledModule.setBrightness(8);

  //============================================================
  // YEAR CODE!!!!!
  //============================================================
  uint16_t year = dt.year();
  while(1) {
    unpackBcd(year / 100, year % 100, buf);
    bitpatterns[0] = DIG_TO_BITPATTERN[buf[0]];
    bitpatterns[1] = DIG_TO_BITPATTERN[buf[1]];
    bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
    bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
    ledModule.setPatternAt(7, bitpatterns[0]);
    ledModule.setPatternAt(6, bitpatterns[1]);
    ledModule.setPatternAt(5, bitpatterns[2]);
    ledModule.setPatternAt(4, bitpatterns[3]);
    ledModule.setBrightness(flash_state ? 2 : 8);
    ledModule.flush();

    int up_state = !digitalRead(UP_PIN);
    int down_state = !digitalRead(DOWN_PIN);
    int set_state = !digitalRead(SET_PIN);

    if (up_state && !up_state_last) {
      year ++;
      if (year > 2099) year = 2022;
      flash_state = 0;
      up_state_last = up_state;
      continue;
    }
    up_state_last = up_state;
    
    if (down_state && !down_state_last) {
      year --;
      if (year < 2022) year = 2099;
      flash_state = 0;
      down_state_last = down_state;
      continue;
    }

    down_state_last = down_state;
    if (set_state && !set_state_last) {
      set_state_last = set_state;
      break;
    }
    set_state_last = set_state;

    flash_state = ~flash_state;
    delay(100);
  }
  flash_state = 0;

  //============================================================
  // MONTH CODE!!!!!
  //============================================================
  uint16_t month = dt.month();
  while(1) {
    unpackBcd(month, 0, buf);
    bitpatterns[0] = 0;
    bitpatterns[1] = 0;
    bitpatterns[2] = DIG_TO_BITPATTERN[buf[0]];;
    bitpatterns[3] = DIG_TO_BITPATTERN[buf[1]];;
    ledModule.setPatternAt(7, bitpatterns[0]);
    ledModule.setPatternAt(6, bitpatterns[1]);
    ledModule.setPatternAt(5, bitpatterns[2]);
    ledModule.setPatternAt(4, bitpatterns[3]);
    ledModule.setBrightness(flash_state ? 2 : 8);
    ledModule.flush();

    int up_state = !digitalRead(UP_PIN);
    int down_state = !digitalRead(DOWN_PIN);
    int set_state = !digitalRead(SET_PIN);

    if (up_state && !up_state_last) {
      month ++;
      if (month > 12) month = 1;
      flash_state = 0;
      up_state_last = up_state;
      continue;
    }
    up_state_last = up_state;
    
    if (down_state && !down_state_last) {
      month --;
      if (month < 1 || month > 12) month = 12;
      flash_state = 0;
      down_state_last = down_state;
      continue;
    }

    down_state_last = down_state;
    if (set_state && !set_state_last) {
      set_state_last = set_state;
      break;
    }
    set_state_last = set_state;

    flash_state = ~flash_state;
    delay(100);
  }
  flash_state = 0;

  //============================================================
  // DAY CODE!!!!!
  //============================================================
  uint16_t day = dt.day();
  while(1) {
    unpackBcd(day, 0, buf);
    bitpatterns[0] = 0;
    bitpatterns[1] = 0;
    bitpatterns[2] = DIG_TO_BITPATTERN[buf[0]];;
    bitpatterns[3] = DIG_TO_BITPATTERN[buf[1]];;
    ledModule.setPatternAt(7, bitpatterns[0]);
    ledModule.setPatternAt(6, bitpatterns[1]);
    ledModule.setPatternAt(5, bitpatterns[2]);
    ledModule.setPatternAt(4, bitpatterns[3]);
    ledModule.setBrightness(flash_state ? 2 : 8);
    ledModule.flush();

    int up_state = !digitalRead(UP_PIN);
    int down_state = !digitalRead(DOWN_PIN);
    int set_state = !digitalRead(SET_PIN);

    if (up_state && !up_state_last) {
      day ++;
      if (day > 31) day = 0;
      flash_state = 0;
      up_state_last = up_state;
      continue;
    }
    up_state_last = up_state;
    
    if (down_state && !down_state_last) {
      day --;
      if (day < 0 || day > 31) day = 31;
      flash_state = 0;
      down_state_last = down_state;
      continue;
    }

    down_state_last = down_state;
    if (set_state && !set_state_last) {
      set_state_last = set_state;
      break;
    }
    set_state_last = set_state;

    flash_state = ~flash_state;
    delay(100);
  }
  flash_state = 0;

  //============================================================
  // HOUR CODE!!!!!
  //============================================================
  uint16_t hour = dt.hour();
  while(1) {
    unpackBcd(hour, 0, buf);
    bitpatterns[0] = DIG_TO_BITPATTERN[buf[0]];
    bitpatterns[1] = DIG_TO_BITPATTERN[buf[1]];
    bitpatterns[2] = 0;
    bitpatterns[3] = 0;
    ledModule.setPatternAt(7, bitpatterns[0]);
    ledModule.setPatternAt(6, bitpatterns[1]);
    ledModule.setPatternAt(5, bitpatterns[2]);
    ledModule.setPatternAt(4, bitpatterns[3]);
    ledModule.setBrightness(flash_state ? 2 : 8);
    ledModule.flush();

    int up_state = !digitalRead(UP_PIN);
    int down_state = !digitalRead(DOWN_PIN);
    int set_state = !digitalRead(SET_PIN);

    if (up_state && !up_state_last) {
      hour ++;
      if (hour > 23) hour = 0;
      flash_state = 0;
      up_state_last = up_state;
      continue;
    }
    up_state_last = up_state;
    
    if (down_state && !down_state_last) {
      hour --;
      if (hour < 0 || hour > 23) hour = 23;
      flash_state = 0;
      down_state_last = down_state;
      continue;
    }

    down_state_last = down_state;
    if (set_state && !set_state_last) {
      set_state_last = set_state;
      break;
    }
    set_state_last = set_state;

    flash_state = ~flash_state;
    delay(100);
  }
  flash_state = 0;

  //============================================================
  // MINUTE CODE!!!!!
  //============================================================
  uint16_t minute = dt.minute();
  while(1) {
    unpackBcd(hour, minute, buf);
    bitpatterns[0] = DIG_TO_BITPATTERN[buf[0]];
    bitpatterns[1] = DIG_TO_BITPATTERN[buf[1]];
    bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
    bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
    ledModule.setPatternAt(7, bitpatterns[0]);
    ledModule.setPatternAt(6, bitpatterns[1]);
    ledModule.setPatternAt(5, bitpatterns[2]);
    ledModule.setPatternAt(4, bitpatterns[3]);
    ledModule.setBrightness(flash_state ? 2 : 8);
    ledModule.flush();

    int up_state = !digitalRead(UP_PIN);
    int down_state = !digitalRead(DOWN_PIN);
    int set_state = !digitalRead(SET_PIN);

    if (up_state && !up_state_last) {
      minute ++;
      if (minute > 59) minute = 0;
      flash_state = 0;
      up_state_last = up_state;
      continue;
    }
    up_state_last = up_state;
    
    if (down_state && !down_state_last) {
      minute --;
      if (minute < 0 || minute > 59) minute = 59;
      flash_state = 0;
      down_state_last = down_state;
      continue;
    }

    down_state_last = down_state;
    if (set_state && !set_state_last) {
      set_state_last = set_state;
      break;
    }
    set_state_last = set_state;

    flash_state = ~flash_state;
    delay(100);
  }
  flash_state = 0;

  //============================================================
  // WRITE SETTINGS BACK!
  //============================================================
  dt.setyear(year);
  dt.setmonth(month);
  dt.setday(day);
  dt.sethour(hour);
  dt.setminute(minute);
  dt.setsecond(0);
  
  rtc.adjust(dt);
}

bool dateTimeValid(DateTime& dt) {
  return (2022 <= dt.year() && dt.year() <= 2099) && (0 <= dt.hour() && dt.hour() <= 23) && (0 <= dt.month() && dt.month() <= 12);
}

void renderDisplay() {
  DateTime now = rtc.now();

  // RTC library occasionally returns garbage data
  // catch it in advance
  if (!dateTimeValid(now)) return;

  uint8_t hr = now.hour();
  uint8_t month = now.month();

  uint8_t buf[4];
  uint8_t bitpatterns[4] = {0, 0, 0, 0 };

  switch( g_current_displaymode ) {
    case 0:
      unpackBcd(hr, now.minute(), buf);
      bitpatterns[0] = DIG_TO_BITPATTERN[buf[0]];
      bitpatterns[1] = DIG_TO_BITPATTERN[buf[1]];
      bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
      bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
      break;
    case 1:
      unpackBcd(now.year() / 100, now.year() % 100, buf);
      bitpatterns[0] = DIG_TO_BITPATTERN[buf[0]];
      bitpatterns[1] = DIG_TO_BITPATTERN[buf[1]];
      bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
      bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
      break;
    case 2:
      unpackBcd(0, now.month(), buf);
      bitpatterns[0] = 0;
      bitpatterns[1] = 0;
      bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
      bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
      break;
    case 3:
      unpackBcd(0, now.day(), buf);
      bitpatterns[0] = 0;
      bitpatterns[1] = 0;
      bitpatterns[2] = DIG_TO_BITPATTERN[buf[2]];
      bitpatterns[3] = DIG_TO_BITPATTERN[buf[3]];
      break;
  }
  
  ledModule.setPatternAt(7, bitpatterns[0]);
  ledModule.setPatternAt(6, bitpatterns[1]);
  ledModule.setPatternAt(5, bitpatterns[2]);
  ledModule.setPatternAt(4, bitpatterns[3]);
  ledModule.setBrightness(g_brightness);
  ledModule.flush();
}

void setDateFromSerial() {
  int checksum = (g_serial_inbox[10] * 100) + (g_serial_inbox[11] * 10) + g_serial_inbox[12];
  if ( ! (0 <= checksum && checksum <= 255)) {
    Serial.println("CRAP CHECKSUM");
    return;
  }
  int computedChecksum = 0;
  for (int i = 0; i < 10; i++) {
    computedChecksum += g_serial_inbox[i];
    computedChecksum &= 0xff;
  }

  if (checksum != computedChecksum) {
    Serial.println("CHECKSUM MISMATCH");
    return;
  }

  int year  = 2000 + (g_serial_inbox[0] * 10) + g_serial_inbox[1];
  if ( ! ( 2022 <= year && year <= 2099) ) {
    Serial.println("CRAP YEAR");
    return;
  }
  int month = (g_serial_inbox[2] * 10) + g_serial_inbox[3];
  if ( ! ( 1 <= month && month <= 12) ) {
    Serial.println("CRAP MONTH");
    return;
  }
  int day = (g_serial_inbox[4] * 10) + g_serial_inbox[5];
  if ( ! ( 1 <= day && day <= 31) ) {
    Serial.println("CRAP DAY");
    return;
  }
  int hour = (g_serial_inbox[6] * 10) + g_serial_inbox[7];
  if ( ! ( 0 <= hour && hour <= 23) ) {
    Serial.println("CRAP HOUR");
    return;
  }
  int minute = (g_serial_inbox[8] * 10) + g_serial_inbox[9];
  if ( ! ( 0 <= hour && hour <= 59) ) {
    Serial.println("CRAP MINUTE");
    return;
  }

  DateTime dt(year, month, day, hour, minute, 0);
  rtc.adjust(dt);
  Serial.println("SET OK");
}

void loop() {
  int ticks_up_held = 0;
  int ticks_down_held = 0;
  int ticks_set_held = 0;
  int ticks_since_datedisplay_pushed = 0; 

  int mode = 0;

  while (1) {
    renderDisplay();

    // poll input
    for (int i = 0; i < 25; i++) {
      // check inbox first
      if (Serial.available()) {
        // serial protocol is ASCII
        // send inbox slot then value 0-9
        // 
        // A B C D E F G H I J K L M
        // y y m m d d H H m m X X X
        //
        // sum of all chars must match K
        // then send S and clock will be set
        int in = Serial.read();

        // selecting inbox
        if ('A' <= in && in <= 'M') {
          g_serial_selection = in - 'A';
        } else if ( '0' <= in && in <= '9') {
          g_serial_inbox[g_serial_selection] = in - '0';
        } else if ( in == 'S' ) {
          setDateFromSerial();
        }
      }

      int up_state = !digitalRead(UP_PIN);
      int down_state = !digitalRead(DOWN_PIN);
      int set_state = !digitalRead(SET_PIN);

      // pressing UP changes brightness
      if ( !up_state && ticks_up_held > 2 ) {
        Serial.write("UP_STATE OFF");
        ticks_up_held = 0;
      } else if ( up_state ) {
        if (ticks_up_held == 0) {
          Serial.write("UP_STATE ON");
          g_brightness --;
          if (g_brightness == 0) g_brightness = 8;
          ledModule.setBrightness(g_brightness);
          ledModule.flush();

          rtc.writeram(0x10, g_brightness);
        }
        ticks_up_held ++;
      }

      // pressing DOWN changes modes
      if ( !down_state && ticks_down_held > 2 ) {
        Serial.write("DOWN_STATE OFF");
        ticks_down_held = 0;
      } else if ( down_state ) {
        if (ticks_down_held == 0) {
          Serial.write("DOWN_STATE ON");
          g_current_displaymode++;
          g_current_displaymode &= 3;
          ticks_since_datedisplay_pushed = 1;
          renderDisplay();
        }

        ticks_down_held ++;
      }
      if (ticks_since_datedisplay_pushed) {
        ticks_since_datedisplay_pushed++;
        if (ticks_since_datedisplay_pushed == 200) {
          g_current_displaymode = 0;
          ticks_since_datedisplay_pushed = 0;
        }
      }

      // holding SET for long enough goes to program mode
      if ( !set_state && ticks_set_held > 2 ) {
        Serial.write("SET_STATE OFF\n");
        ticks_set_held = 0;
      } else if ( set_state ) {
        if (ticks_set_held == 0) {
          Serial.write("SET_STATE ON\n"); 
        }

        ticks_set_held ++;
        if (ticks_set_held == 255) {
          Serial.write("PROGRAM MODE\n"); 
          programMode();
          ticks_set_held = 0;
        }
      }
      delay(10); 
    }
  }
}

