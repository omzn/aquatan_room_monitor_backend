/*
 * Copyright (c) 2016 RedBear
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#define GID_TO_DETECT  1000
#define MY_UID        10000

#define NANOV1

#ifdef NANOV1
#include <BLE_API.h> // Nano v1
#else
#include <nRF5x_BLE_API.h> // for Nano v2
#endif

BLEDevice ble;

uint16_t minorNumber = MY_UID;          // Change minor for identity
uint16_t majorNumber =  GID_TO_DETECT; 
uint8_t txPower = 0xC5;   // Power
uint16_t interval = 8000; // 5 secs in multiples of 0.625ms
// 20 secs (32000) does not work.

static uint8_t beaconPayload[] = {
  0x4C,0x00,                                                                       // Company Identifier Code = Apple
  0x02,                                                                            // Type = iBeacon
  0x15,                                                                            // Following data length
  0xb7,0x65,0x40,0xe0,0xef,0x3c,0x11,0xe4,0x95,0xc9,0x00,0x02,0xa5,0xd5,0xc5,0x1b,
  0x00,0x00,                                                                       // Major
  0x00,0x00,                                                                       // Minor
  0xC5                                                                             // Measure Power
};
// 05 DC 3A 99
void setup() {
  // put your setup code here, to run once
  // close peripheral power
  NRF_POWER->DCDCEN = 0x00000001;
  NRF_UART0->TASKS_STOPTX = 1;
  NRF_UART0->TASKS_STOPRX = 1;
  NRF_UART0->ENABLE = 0;

  pinMode(LED, OUTPUT);

  for (int i = 0; i < minorNumber % 10 + 1; i++) {
    digitalWrite(LED, HIGH);
    delay(500);  
    digitalWrite(LED, LOW);
    delay(500);  
  }

#ifdef NANOV1
  digitalWrite(LED, HIGH); // HIGH v1
#else
  digitalWrite(LED, LOW); // LOW v2
#endif

  beaconPayload[20] = (uint8_t)((majorNumber >> 8) & 0x00ff);
  beaconPayload[21] = (uint8_t)((majorNumber) & 0x00ff);
  beaconPayload[22] = (uint8_t)((minorNumber >> 8) & 0x00ff);
  beaconPayload[23] = (uint8_t)((minorNumber) & 0x00ff);
  beaconPayload[24] = txPower;
  
  ble.init(); 
  // set advertisement
  ble.accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED | GapAdvertisingData::LE_GENERAL_DISCOVERABLE);
  ble.accumulateAdvertisingPayload(GapAdvertisingData::MANUFACTURER_SPECIFIC_DATA, beaconPayload, sizeof(beaconPayload));
  // set advertise type  
  //  ADV_CONNECTABLE_UNDIRECTED
  //  ADV_CONNECTABLE_DIRECTED
  //  ADV_SCANNABLE_UNDIRECTED
  //  ADV_NON_CONNECTABLE_UNDIRECTED
  ble.setAdvertisingType(GapAdvertisingParams::ADV_NON_CONNECTABLE_UNDIRECTED);
  ble.setAdvertisingInterval(interval); 
  // set adv_timeout, in seconds
  ble.setAdvertisingTimeout(0);
  // start advertising
  ble.startAdvertising();
}

void loop() {
  // put your main code here, to run repeatedly:
  ble.waitForEvent();
}
