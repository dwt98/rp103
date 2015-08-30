/*
Project: Recorder Pipe Organ for R Mono Lab.
Author : Takao Watase

This file is a part of RP-103 controller.

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

#ifndef solenoid_h
#define solenoid_h
/* Solenoid class 
    ソレノイド(note)の状態を保持するクラス
    フラグではなく，ONのカウントで管理するのは，多重ONでも鳴るようにするため．．
    音域外のnoteもオクターブシフトして鳴らす構造なので，多重ONが起こりうる．

    This code is a part of RP-103 Organ designed by R-MONO Lab.
    (C) 2015 by T.Watase, R-MONO Lab.
    The first issued : Aug. 10, 2015
*/

#include <stdint.h>
#define MAX_DUTY 128   // means maximum of duty ratio (100%).
#define OFF_DUTY 0     // means minimum of duty ratio (0%).
#define IntvalOn 50    // Interval time holding "on" value of duty. on時の電流は50ms保持

                // status of solenoid controll．
enum {  sOFF = 0,   // solenoid OFF, duty is 0%. オフの状態
        sON,        // solenoid turns OFF to ON, use "on" duty.　オフ→オンの変化時
        sHOLD };    // solenoid holding status ON. use "hold" duty, is less than "on" duty.　オンを保持する状態．ON時よりも少ないdutyでよい．

// Solenoid class stores the values of each solenoid.
class Solenoid {
  // member
  private:
  uint8_t id;        // index number. maybe not in use.
  uint8_t stat;      // status
  uint8_t count;     // note count 多重ONを数える
  Metro   timer;     // timer      ONの時間をはかる
  
  // function 
  public:
  Solenoid() { init(0); }
  void init(int index) {
//    id = index; stat = sOFF; count = 0; enable = true; 
    id = index; stat = sOFF; count = 0;
    timer.interval(IntvalOn);
  }

  void SetStat(uint8_t s) { stat = s; }
  uint8_t GetStat() { return stat; }
  uint8_t GetNoteCount() { return count; }

  // note countを+1する．
  // countが 0 -> 1 のときだけtrueを返す．
  bool IncNoteCount() {
    return (count++ == 0);
  }

  // note countを-1する．
  // 1 -> 0 のときだけtrueを返す．
  bool DecNoteCount() {
    if (count == 0) return false;  // 既に0のときはfalseだけ返す．
    return (--count == 0);
  }

  // 状態をONに変更
  void SetOn() {
    SetStat(sON);
    timer.reset();    // タイマーをスタート
  }    

  // 状態をOFFに変更
  void SetOff() {
    SetStat(sOFF);
  }

  // 状態をHOLD変更
  void SetHold() {
    SetStat(sHOLD);
  }

  // 最初のONならtrueを返す．多重ON時はfalse.
  bool isNewOn() {
    return (GetNoteCount() > 0) && (stat == sOFF);
  }
  
  // 最後のOFFならtrueを返す.多重OFFはfalse
  bool isNewOff() {
    return (GetNoteCount() == 0) && (stat != sOFF);
  }
  
  // HOLDに変化させるかどうかを判断．
  bool isNewHold() { 
    if (GetStat() != sON) return false;
    return timer.check();
  }
};

#endif

