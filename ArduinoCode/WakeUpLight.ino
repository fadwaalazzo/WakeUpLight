/* Includes */
#include <Encoder.h>
#include <DS3231.h>
#include <Wire.h>
#include <TimerOne.h>  
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* debug info */
//#define DEBUG   
unsigned long startLoopTime = 0;
unsigned long loopTime = 0;

/* display */
Adafruit_SSD1306 myDisp;
bool displayOff = false;
unsigned long displayOffTimer = 0;
const int displayOffTime = 20000;   //shut off display after this time

/* encoder */
Encoder myEnc(3,5);
const byte encSpeed = 1;
long oldEnc  = 0;
int internEnc = 0;
const byte ENCODER_BUTTON = 0b01000000;   //PortD6
unsigned long buttonStartTime = 0;
bool buttonFlag = false;
bool buttonEnabled = true;

/* real time clock */
DS3231 myRtc(8,9);
Time t_now;
Time t_now_local;
Time t_alarm;
uint8_t min_old = 0;
int duration = -1;
int8_t number;
bool shutOffFlag = false;
unsigned long shutOfftimer = 0;

/* buzzer */
const byte BUZZER_PIN = 0b00000100;   //PortB2
bool startBuzzer = false;
unsigned long buzzerTimer = 0;
int buzzerState = 3;

/* dimmer */
const byte AC_LOAD_PIN = 0b00010000;   //PortD4
const byte zeroCrossPin = 2;
int timerExpireTime = 100;    //unit: us/brightness-step (10ms and 100 steps)
volatile int timerCounter = 0;
volatile int dimLevel = 100; //0 to 80 useful for dimming. 100 means the lamp is off
byte dimArray1[] = {80,76,72,68,64,56,48,40,32,24,0}; //11 elements
byte dimArray2[] = {80,78,76,74,72,68,64,60,56,52,48,44,40,36,32,28,24,20,16,12,0}; //21 elements
byte dimArray3[] = {80,78,76,74,72,70,68,66,64,62,60,58,56,54,52,50,48,46,44,42,39,36,33,30,27,24,21,18,15,12,0}; //31 elements
bool startDimming = false;
byte dimIndex = 0;
bool startLamp = false;

/* state machine */
enum State
{
  CLOCK_VIEW,
  ALARM_VIEW,
  DURATION_VIEW,
  CLOCK_SETTINGS,
  ALARM_SETTINGS,
  DURATION_SETTINGS
};
State state = CLOCK_VIEW;
State state_old = CLOCK_VIEW;

enum Event
{
  NOTHING,
  ENC_LEFT,
  ENC_RIGHT,
  BUTTON_LONG,
  BUTTON_SHORT,
  TIMER_ALARM_EXPIRED,
  TIMER_MINUTE_EXPIRED
};
Event event = NOTHING;



void setup() 
{
  /* dimmer */
  DDRD |= AC_LOAD_PIN;    //Output
  PORTD &= ~AC_LOAD_PIN;  //Low
  attachInterrupt(digitalPinToInterrupt(zeroCrossPin), IsrPin2HasChanged, RISING); 
  Timer1.initialize(timerExpireTime);                      
  Timer1.attachInterrupt(IsrTimerHasExpired, timerExpireTime); 
  
  /* real time clock */
  myRtc.begin();  
  delay(2000);
  
  /* display */
  myDisp.begin(SSD1306_SWITCHCAPVCC, 0x3C); //display size: 128x64. textsize 1 is 8px and 2 is 16px high. -> 24 is mid point
  myDisp.setTextColor(WHITE);
  myDisp.clearDisplay();
  t_now = myRtc.getTime();
  displayContent();
  myDisp.display();
  myDisp.dim(true);

  /* Encoderbutton */
  DDRD &= ~ENCODER_BUTTON;  //Input
  PORTD |= ENCODER_BUTTON;  //High

  /* Buzzer */
  DDRB |= BUZZER_PIN;    //Output
  PORTB &= ~BUZZER_PIN;  //Low
  
  /* debug info */
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("End of setup function.");
#endif

}

void loop(){   
  /* when something has changed */
  if(CheckTimer() | CheckButton() | CheckEncoder())
  {      

    // when the alarm has triggered:
    if(event == TIMER_ALARM_EXPIRED)
    {
      event = NOTHING;
      dimIndex = 0;
      startDimming = true;
    }
    
    if(event == TIMER_MINUTE_EXPIRED && startDimming == true)
    {
      if(duration == -1)  // alarm clock is off
      {
        startDimming = false; 
        dimLevel = 100;       
        startBuzzer = false;  
      }
      if(duration == 0)    // when 0 minutes are set and the buzzer should go off immediately
      {
          startDimming = false; 
          startBuzzer = true;
          shutOffFlag = false;
          dimLevel = 0;      
      }
      if(duration == 10)    // when 10 minutes are configured
      {
        if(dimIndex >= 10)    
        {
          startDimming = false; 
          startBuzzer = true;
          shutOffFlag = false;
        }
        else
        {
          dimLevel = dimArray1[dimIndex];   //11 elements in the array 0..10
        }
      }
      
      if(duration == 20)
      {
        if(dimIndex >= 20)
        {
          startDimming = false;
          startBuzzer = true;
          shutOffFlag = false;
        }
        else
        {
          dimLevel = dimArray2[dimIndex]; //21 elements in the array 0..20
        }
      }
      
      if(duration == 30)
      {
        if(dimIndex >= 30)
        {
          startDimming = false;
          startBuzzer = true;
          shutOffFlag = false;
        }
        else
        {
          dimLevel = dimArray3[dimIndex]; //31 elements in the array 0..30
        }
      }

      dimIndex ++;
    }

    
    /*--------------*/
    /* statemachine */
    /*--------------*/
    switch(state)
    {
    /*---------------------------------------------------------------------------------*/  
      case CLOCK_VIEW:
      {
        if(event==ENC_RIGHT) {state = ALARM_VIEW; if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;}if(startLamp == true){state = CLOCK_VIEW; dimLevel = dimLevel - 2; if(dimLevel<0)dimLevel = 0;}} 
        if(event==ENC_LEFT) {if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;}if(startLamp == true){dimLevel = dimLevel + 2; if(dimLevel>80)dimLevel = 80;}}
        if(event==BUTTON_LONG) {state = CLOCK_SETTINGS; number = 1; t_now_local = t_now;}
        if(event==BUTTON_SHORT) {startBuzzer = false; startDimming = false; if(dimLevel == 100){dimLevel = 80; startLamp = true;}else{dimLevel = 100; startLamp = false;} } 
        break;
      }
    /*---------------------------------------------------------------------------------*/
      case ALARM_VIEW:
      {
        if(event==ENC_RIGHT) {state = DURATION_VIEW; if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;} if(startLamp == true){state = ALARM_VIEW; dimLevel = dimLevel - 2; if(dimLevel<0)dimLevel = 0;}}
        if(event==ENC_LEFT) {state = CLOCK_VIEW; if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;} if(startLamp == true){state = ALARM_VIEW; dimLevel = dimLevel + 2; if(dimLevel>80)dimLevel = 80;}}
        if(event==BUTTON_LONG) {state = ALARM_SETTINGS; number = 1;}
        if(event==BUTTON_SHORT) {startBuzzer = false; startDimming = false; if(dimLevel == 100){dimLevel = 80; startLamp = true;}else{dimLevel = 100; startLamp = false;} } 
        break;  
      }
    /*---------------------------------------------------------------------------------*/  
      case DURATION_VIEW:
      {
        if(event==ENC_RIGHT) {if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;}if(startLamp == true){dimLevel = dimLevel - 2; if(dimLevel<0)dimLevel = 0;}}
        if(event==ENC_LEFT) {state = ALARM_VIEW; if(startBuzzer == true){startBuzzer = false; startDimming = false; dimLevel = 100;}if(startLamp == true){state = DURATION_VIEW; dimLevel = dimLevel + 2; if(dimLevel>80)dimLevel = 80;}}
        if(event==BUTTON_LONG) {state = DURATION_SETTINGS;}
        if(event==BUTTON_SHORT) {startBuzzer = false; startDimming = false; if(dimLevel == 100){dimLevel = 80; startLamp = true;}else{dimLevel = 100; startLamp = false;}}
        break;
      }
    /*---------------------------------------------------------------------------------*/
      case CLOCK_SETTINGS:
      {          
        //change to next number
        if(event==BUTTON_SHORT) 
        {
          number = number + 1;
          if(number > 2)
          {
            //change state
            state = CLOCK_VIEW;
            //write time to real time clock
            myRtc.setTime(t_now_local.hour, t_now_local.min, t_now_local.sec);
          }
          break;
        }
        
        //when encoder turned right
        if(event==ENC_RIGHT) 
        {
          switch (number)
          {
            case 1:
            {
              t_now_local.hour = t_now_local.hour + 1;
              if(t_now_local.hour>23){t_now_local.hour = 0;}
              break;
            }
            case 2:
            {
              t_now_local.min = t_now_local.min + 1;
              if(t_now_local.min>59){t_now_local.min = 0;}
              break;
            }
            default:
            {break;}
                       
          }
           break;
        }
        
        //when encoder turned left
        if(event==ENC_LEFT) 
        {
          if(number == 1)
          {
            t_now_local.hour = t_now_local.hour -1;
            if(t_now_local.hour>23){t_now_local.hour = 23;}  
            break;
          }
          else  //so it has to be the second number
          {
            t_now_local.min = t_now_local.min -1;
            if(t_now_local.min>59){t_now_local.min = 59;} 
            break;
          }
           break;
        }
        break;
      }
    /*---------------------------------------------------------------------------------*/
      case ALARM_SETTINGS:
      {
        //change to next number
        if(event==BUTTON_SHORT) 
        {
          number = number + 1;
          if(number > 2)
          {
            state = ALARM_VIEW; 
          }
          break;
        }
        
        //when encoder turned right
        if(event==ENC_RIGHT) 
        {
          if(number == 1)
          {
              t_alarm.hour = t_alarm.hour + 1;
              if(t_alarm.hour>23){t_alarm.hour = 0;}
              break;
          }
          else  //so it has to be the second number
          {
              t_alarm.min = t_alarm.min + 1;
              if(t_alarm.min>59){t_alarm.min = 0;}
              break;
          }
          break;
        }
        
        //when encoder turned left
        if(event==ENC_LEFT) 
        {
          if(number == 1)
          {
            t_alarm.hour = t_alarm.hour -1;
            if(t_alarm.hour>23){t_alarm.hour = 23;} 
            break;
          }
          else
          {
            t_alarm.min = t_alarm.min -1;
            if(t_alarm.min>59){t_alarm.min = 59;}
            break;
          }
          break;
        }
        break;
      }
    /*---------------------------------------------------------------------------------*/
      case DURATION_SETTINGS:
      {
       //change to next number
        if(event==BUTTON_SHORT) 
        {
          number = number + 1;
          if(number > 1)
          {
            state = DURATION_VIEW; 
          }
          break;
        }
        
        //when encoder turned right
        if(event==ENC_RIGHT) 
        {
           if(duration==-1){duration = 0;}
           else
           {
            duration = duration + 10;
            if(duration>30){duration = -1;}
           }
           break;
        }
        
        //when encoder turned left
        if(event==ENC_LEFT) 
        {
          if(duration==0){duration = -1;}
          else
          {
          duration = duration - 10;
          if(duration<-1){duration = 30;} 
          }
          break;
        }
        break;
      }
      
      default:
      {break;}
    }

    if( (event == ENC_LEFT || event == ENC_RIGHT || event == BUTTON_LONG || event == BUTTON_SHORT )&& displayOff == true) //turn on the display again
    {
      displayOffTimer = millis();   //reset display off timer
      state = CLOCK_VIEW;   //always wake up in clock view
      displayOff = false;   
    }
    
    if(event == ENC_LEFT || event == ENC_RIGHT || event == BUTTON_LONG || event == BUTTON_SHORT ) //prevent display from going dark
    {
      displayOffTimer = millis();
    }

    if(displayOff == false)
    {
      /* update display */
      myDisp.clearDisplay();
      displayContent();
      myDisp.display();  
    }
  }
  
  if(millis()-displayOffTimer>displayOffTime && displayOff == false)  //turn off display after xy seconds
  {
    displayOff = true;
    state = CLOCK_VIEW;
    myDisp.clearDisplay();
    myDisp.display();  
  }

  /* generate buzzer noise */
  if(startBuzzer == true)
  {
    playMelody();
    
    /* if not turned off after 5min then stop the alarm */
    if(shutOffFlag == false)
    {
      shutOffFlag = true;
      shutOfftimer = millis();
    }
    
    if(millis()-shutOfftimer>300000)
    {
      shutOffFlag = false;
      startDimming = false; 
      dimLevel = 100;       
      startBuzzer = false; 
    } 
  }
  else
  {
    PORTB &= ~BUZZER_PIN; //shut up
  }
  

  #ifdef DEBUG
    loopTime = millis()-startLoopTime;
    Serial.print("Looptime in ms: ");
    Serial.println(loopTime);
    startLoopTime = millis();
  #endif
   
}



/*-----------*/
/* functions */
/*-----------*/

void displayContent(){
  //@brief: this function displays the menues on the screen
  switch (state)
  {
    case CLOCK_VIEW:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("Time:");
      myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);
      if(t_now.hour<10 && t_now.min>9) displayln("0%d:%d", t_now.hour, t_now.min );
      if(t_now.min<10 && t_now.hour>9) displayln("%d:0%d", t_now.hour, t_now.min );
      if(t_now.hour<10 && t_now.min<10) displayln("0%d:0%d", t_now.hour, t_now.min );
      if(t_now.hour>9 && t_now.min>9) displayln("%d:%d", t_now.hour, t_now.min );
      myDisp.setCursor(3, 55);
      myDisp.setTextSize(1);
      if(duration == -1)
      {displayln("alarm state: OFF");}
      else{displayln("alarm state: ON");}
      break;
    }
    case ALARM_VIEW:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("alarm time:");
      myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);
      if(t_alarm.hour<10 && t_alarm.min>9) displayln("0%d:%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.min<10 && t_alarm.hour>9) displayln("%d:0%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.hour<10 && t_alarm.min<10) displayln("0%d:0%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.hour>9 && t_alarm.min>9) displayln("%d:%d", t_alarm.hour, t_alarm.min );
      
      myDisp.setCursor(3, 55);
      myDisp.setTextSize(1);
      displayln("delta:");
      
      myDisp.setCursor(43, 55);
      /* calculate delta between alarm time and current time */
      int del_m = t_alarm.min - t_now.min;
      int del_h;
      if(del_m<0)
      {
        del_m = 60+del_m;
        del_h = t_alarm.hour - t_now.hour-1;
      }
      else
      {
        del_h = t_alarm.hour - t_now.hour;
      }
      if(del_h<0)
      {
        del_h = 24+del_h;
      }
      if(duration==-1)
      {
        displayln("--");
      }
      else
      {
      if(del_h<10 && del_m>9) displayln("0%d:%d", del_h, del_m );
      if(del_h>9 && del_m<10) displayln("%d:0%d", del_h, del_m );
      if(del_h<10 && del_m<10) displayln("0%d:0%d", del_h, del_m );
      if(del_h>9 && del_m>9) displayln("%d:%d", del_h, del_m );
      }
      break;
    }
    case DURATION_VIEW:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("alarm duration:");
      //myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);
      if(duration==0||duration==-1)
      {
        myDisp.setTextSize(1);
        if(duration==0){displayln("instant ON");}
        else{displayln("alarm OFF");}
      }
      else
      {
        displayln("%d", duration );
        myDisp.setTextSize(1);
        myDisp.setCursor(45, 24); 
        displayln(" min.");
      }
      break;
    }
    case CLOCK_SETTINGS:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("set clock:");
      myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);
      if(t_now_local.hour<10 && t_now_local.min>9) displayln("0%d:%d", t_now_local.hour, t_now_local.min );
      if(t_now_local.min<10 && t_now_local.hour>9) displayln("%d:0%d", t_now_local.hour, t_now_local.min );
      if(t_now_local.hour<10 && t_now_local.min<10) displayln("0%d:0%d", t_now_local.hour, t_now_local.min );
      if(t_now_local.hour>9 && t_now_local.min>9) displayln("%d:%d", t_now_local.hour, t_now_local.min );
      
      if(number == 1) {myDisp.setCursor(35, 30); displayln("__");}
      else {myDisp.setCursor(70, 30); displayln("__");} 
      
      break;
    }
    case ALARM_SETTINGS:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("set alarm:");
      myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);
      if(t_alarm.hour<10 && t_alarm.min>9) displayln("0%d:%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.min<10 && t_alarm.hour>9) displayln("%d:0%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.hour<10 && t_alarm.min<10) displayln("0%d:0%d", t_alarm.hour, t_alarm.min );
      if(t_alarm.hour>9 && t_alarm.min>9) displayln("%d:%d", t_alarm.hour, t_alarm.min );

      if(number == 1) {myDisp.setCursor(35, 30); displayln("__");}
      else {myDisp.setCursor(70, 30); displayln("__");} 
      
      break;
    }
    case DURATION_SETTINGS:
    {
      myDisp.setTextSize(1);
      myDisp.setCursor(3, 0);
      displayln("set duration:");
      //myDisp.setTextSize(2);
      myDisp.setCursor(35, 24);

      if(duration==0||duration==-1)
      {
        myDisp.setTextSize(1);
        if(duration==0){
          displayln("instant ON");
          myDisp.setCursor(35, 30); 
          displayln("__________");
          }
        else{
          displayln("alarm OFF");
          myDisp.setCursor(35, 30); 
          displayln("_________");
          }
      }
      else
      {
        displayln("%d", duration );
        myDisp.setCursor(35, 30); 
        displayln("__");
        myDisp.setTextSize(1);
        myDisp.setCursor(45, 24); 
        displayln(" min.");
      }
      
      break;
    }
    default:
    {
      break;
    }
  }
}

inline bool CheckButton()
{//@brief: this function checks the button state and reports back if it has been pressed and for how long 
  if(~PIND & ENCODER_BUTTON)  //button is LOW
  {
    if(buttonFlag == false)
    {
      buttonFlag = true;
      buttonStartTime = millis();
    }
    else
    {
      if(millis()-buttonStartTime>1700) //more than 1.7 sec.
      {
        event = BUTTON_LONG;
        buttonFlag = false;
        buttonEnabled = false;
        return true;
      }
    }  
  }
  else  //button is high
  {
    if(buttonEnabled == false)
    {
      if(millis()-buttonStartTime>1000)
      {
        buttonEnabled = true;
        buttonFlag = false;
      }
    }
    
    if(buttonFlag == true &&  buttonEnabled == true)   //that means its been low meanwhile
    {
        event = BUTTON_SHORT;
        buttonFlag = false;
        return true;
    }
  }
  
  return false;
}

inline bool CheckEncoder(){
  //@brief: this function will make sure the encoder register only counts up or down one step at a time
  long enc = myEnc.read();
  if( enc%4 == 0)    // when 4 steps are passed
  { 
    if ( enc != oldEnc ) 
    {
      int delta = 0.25*(enc - oldEnc); 
      internEnc = internEnc - delta;
      oldEnc = enc;
      if (internEnc<0){internEnc = 0;}
      if (delta<0){event = ENC_LEFT;}else{event = ENC_RIGHT;}
      return true;
    }
  }
  return false;
}

inline bool CheckTimer(){
  //@brief: this function will check the timer and generate timer events 
  t_now = myRtc.getTime();

  if(t_now.hour == t_alarm.hour && t_now.min == t_alarm.min && t_now.sec == t_alarm.sec)
  {
    event = TIMER_ALARM_EXPIRED;
    return true;
  }
  if(t_now.min != min_old)    
  {                           //necessary to update the dim level and the display every minute
    min_old = t_now.min;
    event = TIMER_MINUTE_EXPIRED;
    return true;
  }
  return false;
}

void playMelody()
{//@brief: this function controlls the buzzer 
  if(millis() - buzzerTimer>1000 && buzzerState == 3)
  {
    PORTB |= BUZZER_PIN;  //High
    buzzerState = 0;
    buzzerTimer = millis();
  }
  if(millis() - buzzerTimer>70 && buzzerState == 0)
  {
    PORTB &= ~BUZZER_PIN; //Low
    buzzerState = 1;
    buzzerTimer = millis();
  }
  if(millis()-buzzerTimer>100 && buzzerState == 1)
  {
    PORTB |= BUZZER_PIN;  //High
    buzzerState = 2;
    buzzerTimer = millis();
  }
  if(millis()-buzzerTimer>70 && buzzerState == 2)
  {
    PORTB &= ~BUZZER_PIN; //Low
    buzzerState = 3;
    buzzerTimer = millis();
  }
}


void displayln(const char* format, ...){
  //@brief: Draws a printf style string at the current cursor position
  char buffer[32];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  
  int len = strlen(buffer);
  for (uint8_t i = 0; i < len; i++) {
    myDisp.write(buffer[i]);
  }
}


void IsrPin2HasChanged() { 
  //@brief: Interrupt service routine to detect the zero crossing of the mains voltage
  Timer1.restart();     
  timerCounter = 0;
  if(dimLevel == 100)   //lamp off
  {
    PORTD &= ~AC_LOAD_PIN;
  }
}                                 


void IsrTimerHasExpired() { 
  //@brief: if timer1 has expired this is called              
  if(timerCounter == dimLevel && dimLevel != 100)   // lamp off?
  {                                  
    PORTD |= AC_LOAD_PIN;
  } 
  if(timerCounter == dimLevel + 5 )   //take away the triac control signal 100us later. The triac will shut off on its own after the zero crossing happend.
  {
    PORTD &= ~AC_LOAD_PIN;            
  }
       
  timerCounter ++;                                                           
}  

  
