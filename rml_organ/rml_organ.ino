/*
Project: Recorder Pipe Organ for R Mono Lab.
Author : Takao Watase

This file is a main part of RP-103 controller.

Copyright 2015 Takao Watase

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
Sorry, some Japanese comments remains.

MIDI入力に応じてソレノイドを制御するプログラムです．

ソレノイドに流す電流はPWMで制御しています．13chのPWMを扱うため，TLC5940を
使っています．

This sketch uses following liblaries.
- MIDI    
- TLC5940 
- Metro

You should copy these libraries into your library folder and 
you should make small change in tlc_config.h of TLC5940 library
as follows.

   #define TLC_PWM_PERIOD   256  // original is 8192 

This change makes PWM frequency higher in order to prevent 
hum noise from the solenoids. PWM frequency will be around 31kHz.
*/

#include <Metro.h>
#include <MIDI.h>
#include "Tlc5940.h"
#include "solenoid.h"


//#define PRINT_DEBUG
#define TESTING
#define FORCE_TRANSPOSE         // 音域外のnoteはトランスポーズして発音

// Tables of solenoid's parameter 

/* ----- note numbers
specify note number of each solenoids.
ソレノイドに対応するnote number を設定します
note numberの連続，不連続は問わないので，実際のリコーダーの並びの通りに設定してください．
*/
              //       0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12
uint8_t note[] =     { 53, 60, 55, 62, 64, 67, 65, 69, 71, 74, 72, 57, 59 };
              //       F3, C4, G3, D4, E4, G4, F4, A4, B4, D5, C5, A3, B3 };

/* ----- PWM duty value
ソレノイドを制御するPWMの値(duty)は，0-127です．127で約100%です．
ソレノイドがOFF -> ONに変化するときは，多めの電流を，ON状態を保持する
間(HOLD)は，少し小さめの電流を流すことで，消費電流と発熱を抑えるように
しています．
ソレノイドのばらつきも考慮して，ON, HOLDそれぞれのduty値をテーブルに
しています．
また，複数のソレノイドが同時にONになると，電流が下がるので，delta[]
テーブルで，duty値を補正します．
使用しているソレノイドに応じて調整してください．
*/
              //       0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12
uint8_t dutyOn[]   = { 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 };
//uint8_t dutyOn[]   = { 43, 40, 40, 43, 40, 40, 40, 40, 45, 40, 40, 43, 43 };
uint8_t dutyHold[] = { 33, 30, 30, 33, 30, 30, 30, 30, 33, 30, 30, 33, 33 };

// compensasion for number of solenoid.  同時駆動しているソレノイドの数に応じたDuty値の補正値
              //       0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
uint8_t delta[]    = { 0, 0, 1, 2, 2, 3, 4, 4, 5, 6,  6,  7,  8,  8 };

// Bb3とB3のリコーダーは差し替え．Bb3もB3で鳴らす
#define NOTE_Bb3 58
#define NOTE_B3  59

#define NmSol 13                //  number of solenoids
#define MaxSol 12               //  max index of solenoid
#define InitialDuty OFF_DUTY    //  initial duty value

#define MAX_POLY 12   // maximum polyphony. 最大同時発音数
/*
同時に鳴らせるパイプの数を制限します．
実際に鳴っている数は，変数nm_polyが示します．
制限以上の数のnote onを受けたときは，無視して発音しません．
同時発音数が増えると，電流と空気の消費量が比例して増加します．
電源や送風装置の能力に応じて設定してください．
*/
// ----- variables ------

// range of available note.
uint8_t minNote = 0;      // min note number of the playable key range
uint8_t maxNote = 127;    // max note number of the playable key range.
int8_t nm_poly = 0;       // number of the current polyphony

// ----- variables for testing ------

#ifdef TESTING

Metro play0(250);

int x=0;

#endif

// ----- macros -----

#define GetTlcIndex(idx) (idx+1)
#define GetDutyOn(idx) (dutyOn[idx])
#define GetDutyHold(idx) (dutyHold[idx])
#define GetDutyOff(idx) (OFF_DUTY)

// ----- instances ------

Solenoid voice[NmSol];        // status of each solenoids.

// ----- MIDI callback ------

MIDI_CREATE_DEFAULT_INSTANCE();

void handleNoteOn(byte channel, byte pitch, byte velocity)
{
    // routine when a note is pressed.
    // note number(pitch)のチェックと，対応するvoice配列のカウントをアップします．
    // 実際の発音処理は，loop内で実行します．

#ifdef PRINT_DEBUG
   Serial.print(" Rx Note On : "); Serial.println(pitch);
#endif

    if (pitch == NOTE_Bb3) pitch = NOTE_B3;    // Bb3は，B3で鳴らす

#ifdef FORCE_TRANSPOSE
    pitch = ForceTranspose(pitch);             // 音域外の音もオクターブシフトして鳴らす
#else     
                                              // 音域外の音は無視
    if (pitch < minNote) return;              // 最低音より低いキーは無視
    if (pitch > maxNote) return;              // 最高音より高いキーも無視
#endif

    // 最大同時発音数に達していたら，処理しない．（先着優先）
    if (nm_poly >= MAX_POLY) {
      return;
    }

    // 他のケースはON処理をする
    for (int i = 0; i < NmSol; i++) {  // キーが一致するボイスのカウントを+1する
      if (pitch == note[i]) {
        voice[i].IncNoteCount();
        if (voice[i].GetNoteCount() == 1) nm_poly++;
                                        // 最初のnoteなら同時発音数をinc
#ifdef PRINT_DEBUG
        Serial.print(" Poly : "); Serial.println(nm_poly);
        Serial.print(" Inc Note Count : "); Serial.println(pitch);
#endif
        return;
      }
    }

    // Try to keep your callbacks short (no delays ect)
    // otherwise it would slow down the loop() and have a bad impact
    // on real-time performance.
}

void handleNoteOff(byte channel, byte pitch, byte velocity)
{
    // routine when a note is turned off.
    // 対応するvoice配列のカウントを減らします．
    // 実際の消音処理は，loop内で実行します．

#ifdef PRINT_DEBUG
   Serial.print(" Rx Note Off : "); Serial.println(pitch);
#endif

    if (pitch == NOTE_Bb3) pitch = NOTE_B3;  // Bb3はB3として処理

#ifdef FORCE_TRANSPOSE
    pitch = ForceTranspose(pitch);            // 音域外の音もオクターブシフトして鳴らす
#else     
                                              // 音域外の音は無視
    if (pitch < minNote) return;              // 最低音より低いキーは無視
    if (pitch > maxNote) return;              // 最高音より高いキーも無視
#endif

    for (int i = 0; i < NmSol; i++) {  // キーが一致するボイスのカウントを-1する
      if (pitch == note[i]) {
        voice[i].DecNoteCount();
        if (voice[i].GetNoteCount() == 0) {
          // カウント0なら，同時発音数をdec
          nm_poly--;
          if (nm_poly < 0) nm_poly = 0;
        }
#ifdef PRINT_DEBUG
        Serial.print(" Poly : "); Serial.println(nm_poly);
        Serial.print(" Dec Note Count : "); Serial.println(pitch);
#endif
        return;
      }
    }

    // Do something when the note is released.
    // Note that NoteOn messages with 0 velocity are interpreted as NoteOffs.
}


// ------ Setup ------

void setup()
{
  /* Call Tlc.init() to setup the tlc.
     You can optionally pass an initial PWM value (0 - 4095) for all channels.*/
  Tlc.init(InitialDuty);
  
  // Initialize voices and assigner.
  for (int i = 0; i < NmSol; i++) {
    voice[i].init(i);
  }
  
  // Store range of note number
  GetMinMaxNote();
  
  // Connect the handleNoteOn function to the library,
  // so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(handleNoteOn);  // Put only the name of the function

  // Do the same for NoteOffs
  MIDI.setHandleNoteOff(handleNoteOff);

  // Initiate MIDI communications, listen to all channels
  MIDI.begin(MIDI_CHANNEL_OMNI);

#ifdef PRINT_DEBUG
  Serial.begin(9600);

  unsigned long t = millis();
  Serial.print("Time = "); Serial.println(t);
  delay(1000);
  t = millis();
  Serial.print("Time = "); Serial.println(t);
#endif

  play0.reset();  
}


// ------ Loop ------


void loop()
{
#ifdef PRINT_DEBUG
  unsigned long t = millis();
//  Serial.print("Time = "); Serial.println(t);
#endif

  for (int i = 0; i < NmSol; i++) {
    if (voice[i].isNewOn()) {
      SetNoteOn(i);

#ifdef PRINT_DEBUG
      Serial.print("Note On: "); Serial.println(i);
#endif

    }
    else if (voice[i].isNewOff()) {
      SetNoteOff(i);
#ifdef PRINT_DEBUG
      Serial.print("Note Off: "); Serial.println(i);
#endif
    }

    else if (voice[i].isNewHold()) {
      SetNoteHold(i);
#ifdef PRINT_DEBUG
      Serial.print("Note Hold: "); Serial.println(i);
#endif
    }
  }
  // send data to TLC5940
  Tlc.update();

  // Call MIDI.read the fastest you can for real-time performance.
  MIDI.read();

  // Update duty for HOLD.
  ServHoldDuty();
  
/*
  // TEST  test all the note[].
  if (play0.check()) {
    handleNoteOff(0, note[x % 13], 0);
//    handleNoteOff(0, note[(x+5) % 13], 0);
//    handleNoteOff(0, note[(x+9) % 13], 0);
    x++;
    handleNoteOn(0, note[x % 13], 64);
//    handleNoteOn(0, note[(x+5) % 13], 64);
//    handleNoteOn(0, note[(x+9) % 13], 64);
    play0.reset();
  }
*/
/*
  // TEST  repeating note[1]
  if (play0.check()) {
    if (x % 2) {
      handleNoteOff(0, note[1], 0);
    } else {
      handleNoteOn(0, note[1], 64);
    }
    play0.reset();
    x++;
  }
*/

}


// ------ Sub routines ------

void SetNoteOn(int idx) {
  voice[idx].SetOn();
  uint8_t d = delta[nm_poly];
  Tlc.set(GetTlcIndex(idx), GetDutyOn(idx) + d);  // set duty for ON
}

void SetNoteOff(int idx) {
  voice[idx].SetOff();
  Tlc.set(GetTlcIndex(idx), GetDutyOff(idx));  // set duty for OFF
}

void SetNoteHold(int idx) {
  voice[idx].SetHold();                       // notes: This function just change the status into "HOLD".
                                              //        Duty value is set in the ServHoldDuty function.
}

// Update duty for HOLD status
void ServHoldDuty() {
  uint8_t d = delta[nm_poly];                // compensation of polyphony.
  for (int i = 0; i < NmSol; i++) {
    if (voice[i].GetStat() == sHOLD) {
      Tlc.set(GetTlcIndex(i), GetDutyHold(i)+d);   // set duty for HOLD
    }
  }
}

// Get available range of note number from the table of note number.
void GetMinMaxNote() {
  // note[] をスキャンして最小値を最大値を求める
  minNote = 127;  // set maximum note number
  maxNote = 0;    // set minimum note number
  for (int i = 0; i < NmSol; i++) {
    if (note[i] < minNote) minNote = note[i];
    if (note[i] > maxNote) maxNote = note[i];
  }
}

// transpose a note which is out of the range to inside the range.
int ForceTranspose(byte key) {
  while (key < minNote) { key += 12; }
  while (key > maxNote) { key -= 12; }
  return key;
}  


