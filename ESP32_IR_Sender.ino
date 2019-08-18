#define IR_OUT 4

#define DEBUG_TEST


hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// 割り込み処理カウンター
volatile byte tick_required = 0;

// 赤外線の定義

// 1Tickの長さ
int tick_us = 562;

// 1bitが0のときと1のときのtickの長さ
// ※最初のtickは点滅tick、それ以外は消灯tick
byte tick_one_off = 4;
byte tick_zero_off = 2;

// 実際に送信する内容
byte ir_buffer[20];
char ir_buffer_size = 0;

// ヘッダのtick数
int tick_header_on = 16;
int tick_header_off = 8;

// 送信箇所のポインタ
byte* ir_buffer_pointer;
// 念の為バイトのサイズを記憶(通常は1だが)
const int sizeofbyte = sizeof(byte);

// ir_buffer_pointerの何ビット目を送信中か
char buffer_bit_pointer = 0;
// 送信中のビットの何tick目を送信中か
char ir_tick_pointer = 0;

// ヘッダの何Tick目を送信中か
byte header_pointer = 0;

#define IR_HEADER 0
#define IR_BODY 1
#define IR_TERMINAL 2
byte send_pointer = 0;

// 赤外線のデューティ比。6bitの分解能にしているので、
volatile byte duty = 0;
// 点滅tick時は31、消灯tickは0をdutyに入れる
#define DUTY_ON 31
#define DUTY_OFF 0

void IRAM_ATTR onTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  tick_required++;
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

void setup() {

#ifdef DEBUG_TEST
  // テスト用のバイト列。
  ir_buffer[0] = 0x80;
  ir_buffer[1] = 0x63;
  ir_buffer[2] = 0x0F;
  ir_buffer[3] = 0xF0;

  ir_buffer_size = 4;

  signalInitialize();
#endif

  pinMode(IR_OUT, OUTPUT);

  // 周波数38*1000(Hz)、1波長を2の6乗(64)個に分割する
  ledcSetup(0,38000,6);
  ledcAttachPin(IR_OUT, 0);
  
  ledcWrite(0, 0);


  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (counted from zero).
  // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
  // info).
  timer = timerBegin(0, 80, true);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer, true);
  
  // Set alarm to call onTimer function every "tick_us" microseconds.
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, tick_us, true);

  // Start an alarm
  timerAlarmEnable(timer);
  
}

byte sendchar = 0;
byte out = 0;

void signalInitialize(){
  ir_buffer_pointer = ir_buffer;
  sendchar = *ir_buffer_pointer;
  out = sendchar & 1;
  ir_tick_pointer = 0;
}

void loop() {

  if(tick_required){
    switch (send_pointer){
      case IR_HEADER:
        duty = getHeaderDuty();
        break;
      case IR_BODY:
        duty = getBodyDuty();
        checkTick();
        break;
      case IR_TERMINAL:
        duty = DUTY_ON;
        send_pointer++;
        break;
      default:
        // 送信するものがなくなったとき
        duty = DUTY_OFF;
        // Start an alarm
        timerENd(timer);
        break;
    }

    // カウンターをデクリメント
    portENTER_CRITICAL_ISR(&timerMux);
    tick_required--;
    portEXIT_CRITICAL_ISR(&timerMux);

    ledcWrite(0, duty);
  }else{
    // 38kHzの周期の約半分。電気を消費しそうなのでloopをフル稼働はさせない。
    delayMicroseconds(13);
  }
}

byte getBodyDuty(){
  
  // データ送信時、最初のtickは点滅tick
  if (!ir_tick_pointer){
    return DUTY_ON;
  }else{
    return DUTY_OFF;
  }
}

void checkTick(){

  ir_tick_pointer++;
  
  // 次のtickから送信するビットが変わるとき
  if ((out && (ir_tick_pointer == tick_one_off)) || (!out && (ir_tick_pointer == tick_zero_off))){
    ir_tick_pointer = 0;
    buffer_bit_pointer++;
    
    if (buffer_bit_pointer >= 8){
      ir_buffer_pointer += sizeofbyte;
      if (&ir_buffer[ir_buffer_size] <= ir_buffer_pointer){
        send_pointer++;
      }
      buffer_bit_pointer = 0;
    }
    
    out = 1 << buffer_bit_pointer & *ir_buffer_pointer;
  }

}

byte getHeaderDuty(){
  header_pointer++;
  
  if (header_pointer >= tick_header_on + tick_header_off){
    send_pointer++;
  }
  
  if (header_pointer <= tick_header_on){
    return DUTY_ON;
  }else{
    return DUTY_OFF;
  }
}
