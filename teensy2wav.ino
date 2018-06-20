#include <SPI.h>
#include <SD.h>
#include <SD_t3.h>
#include <SerialFlash.h>
#include <EEPROM.h>
// for buttons
#include <Bounce.h>

#include <Audio.h>
#include <Wire.h>

// for RTC
#include <TimeLib.h>



//write wav
unsigned long ChunkSize = 0L;
unsigned long Subchunk1Size = 16;
unsigned int AudioFormat = 1;
unsigned int numChannels = 1;
unsigned long sampleRate = 44100;
unsigned int bitsPerSample = 16;
unsigned long byteRate = sampleRate * numChannels * (bitsPerSample / 8); // samplerate x channels x (bitspersample / 8)
unsigned int blockAlign = numChannels * bitsPerSample / 8;
unsigned long Subchunk2Size = 0L;
unsigned long recByteSaved = 0L;
unsigned long NumSamples = 0L;
byte byte1, byte2, byte3, byte4;

//const int myInput = AUDIO_INPUT_LINEIN;
const int myInput = AUDIO_INPUT_MIC;

AudioPlaySdWav           audioSD;
AudioInputI2S            audioInput;
AudioOutputI2S           audioOutput;
AudioRecordQueue         queue1;

//recod from mic
AudioConnection          patchCord1(audioInput, 0, queue1, 0);
AudioConnection          patchCord2(audioSD, 0, audioOutput, 0);
AudioConnection          patchCord3(audioSD, 0, audioOutput, 1);

AudioControlSGTL5000     audioShield;


// Bounce objects to easily and reliably read the buttons
// example: Bounce <alias> = Bounce(<pin number>, delay-in-ms)
Bounce confirm = Bounce(32, 100);
Bounce left =   Bounce(31, 100);  // 100 = 100 ms debounce time
Bounce right =   Bounce(30, 100);

int mode = 0;  // 0=stopped, 1=recording, 2=playing
File frec;
elapsedMillis  msecs;

// Use these with the Teensy Audio Shield
#define SDCARD_CS_PIN    10
#define SDCARD_MOSI_PIN  7
#define SDCARD_SCK_PIN   14

// Define LED to indicate when recording
//#define LED 13

// declare variables used to hold the sleep-time length, and recording length
// TODO: use these addresses to store updated cycle parameters
unsigned int gainaddr = 2;
unsigned int sleepaddr = 1;
unsigned int recordaddr = 0;


//////////////////// For setting up display //////////////////////////////
// Include NewLiquidCrystal Library for I2C
#include <LiquidCrystal_I2C.h>

// Define LCD pinout
const int  en = 2, rw = 1, rs = 0, d4 = 4, d5 = 5, d6 = 6, d7 = 7, bl = 3;

// Define I2C Address - change if reqiuired
const int i2c_addr = 0x3F;

LiquidCrystal_I2C lcd(i2c_addr, en, rw, rs, d4, d5, d6, d7, bl, POSITIVE);

bool screen_is_here = false;

/////////////////////////////////////////////////////////////////////////

//character array to store changing filename
char fname[11];


void setup() {


  //check if screen is plugged in
  Wire.begin();
  {
    Wire.beginTransmission(i2c_addr);
    if (Wire.endTransmission() == 0) {

      // Set the screen_is_here flag to 'true'
      screen_is_here = true;

      // Set display type as 16 char, 2 rows
      lcd.begin(16, 2);

      // Print on first row
      lcd.setCursor(0, 0);
      lcd.print("Recording Test");

      // Wait 1 second
      //delay(1000);

      // Print on second row
      lcd.setCursor(0, 1);
      lcd.print("Press any button.");

      // Wait 8 seconds
      //delay(8000);

      // Clear the display
      lcd.clear();
    }
  }

  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);

  // Setup name for new recording
  {

    int monthv = month();
    int dayv = day();
    int hourv = hour();
    int minutev = minute();
    //sprintf( fname, "%s.WAV",temp );
    sprintf(fname,"%02d%02d%02d%02d.wav",monthv,dayv,hourv,minutev);
    

  }


  // Configure the pushbutton pins
  pinMode(32, INPUT_PULLUP);
  pinMode(31, INPUT_PULLUP);
  pinMode(30, INPUT_PULLUP);
  //pinMode(13, OUTPUT);

  Serial.begin(9600);
  AudioMemory(60);
  audioShield.enable();
  audioShield.inputSelect(myInput);
  audioShield.micGain(40);  //0-63
  audioShield.volume(0.5);  //0-1

  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
}


/*
  TODO: In the main loop, there will be main functions:
  1. sleep
  2. record
  3. save

  TODO: In addition, there will be the functions for the menu, which listen to button input for changing
  the recording and sleep length.

*/


void loop() {
  asm volatile(" WFI"); //Tell cpu to wait for interrupt... saves battery.

  if (screen_is_here) {
  mainMenu();
    
  }
  else
  { /* What to do when the screen isn't here */
    Serial.println("No screen detected. Just going into regular loop.");
    recordingloop();
  }
}

void startRecording() {

  frec = SD.open(fname, FILE_WRITE);
  Serial.print(frec);
  Serial.println();
  Serial.println(fname);
  // if the dated initial file on SD is successfully created, then start recording.
  if (frec) {
    queue1.begin();
    Serial.println("Supposedly started recording to SD..."); //Execution never enters this block :-(...
    mode = 1;
    recByteSaved = 0L;
  }
}

void continueRecording() {

  if (queue1.available() >= 2) {
    byte buffer[512];
    memcpy(buffer, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    memcpy(buffer + 256, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    // write all 512 bytes to the SD card
    frec.write(buffer, 512);
    recByteSaved += 512;
    elapsedMicros usec = 0;
    Serial.print("SD write, us=");
    Serial.println(usec);
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  queue1.end();
  if (mode == 1) {
    while (queue1.available() > 0) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
      recByteSaved += 256;
    }
    writeOutHeader();
    frec.close();
  }
  Serial.println("Setting mode to 0");
  mode = 0;
  Serial.println(mode);
}


void startPlaying() { //Not certain that we need this later.
  Serial.println("startPlaying");
  Serial.println(fname);
  Serial.println();
  //audioSD.play("BOB.WAV");
  audioSD.play(fname);
  mode = 2;

}


void stopPlaying() { //Not certain that we need this later.
  Serial.println("stopPlaying");
  if (mode == 2) audioSD.stop();
  mode = 0;
}



// This function is what turns the RAW audio into a WAV, by adding a header to the file.
void writeOutHeader() { // update WAV header with final filesize/datasize

  //  NumSamples = (recByteSaved*8)/bitsPerSample/numChannels;
  //  Subchunk2Size = NumSamples*numChannels*bitsPerSample/8; // number of samples x number of channels x number of bytes per sample
  Subchunk2Size = recByteSaved;
  ChunkSize = Subchunk2Size + 36;
  frec.seek(0);
  frec.write("RIFF");
  byte1 = ChunkSize & 0xff;
  byte2 = (ChunkSize >> 8) & 0xff;
  byte3 = (ChunkSize >> 16) & 0xff;
  byte4 = (ChunkSize >> 24) & 0xff;
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  frec.write("WAVE");
  frec.write("fmt ");
  byte1 = Subchunk1Size & 0xff;
  byte2 = (Subchunk1Size >> 8) & 0xff;
  byte3 = (Subchunk1Size >> 16) & 0xff;
  byte4 = (Subchunk1Size >> 24) & 0xff;
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = AudioFormat & 0xff;
  byte2 = (AudioFormat >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2);
  byte1 = numChannels & 0xff;
  byte2 = (numChannels >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2);
  byte1 = sampleRate & 0xff;
  byte2 = (sampleRate >> 8) & 0xff;
  byte3 = (sampleRate >> 16) & 0xff;
  byte4 = (sampleRate >> 24) & 0xff;
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = byteRate & 0xff;
  byte2 = (byteRate >> 8) & 0xff;
  byte3 = (byteRate >> 16) & 0xff;
  byte4 = (byteRate >> 24) & 0xff;
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  byte1 = blockAlign & 0xff;
  byte2 = (blockAlign >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2);
  byte1 = bitsPerSample & 0xff;
  byte2 = (bitsPerSample >> 8) & 0xff;
  frec.write(byte1);  frec.write(byte2);
  frec.write("data");
  byte1 = Subchunk2Size & 0xff;
  byte2 = (Subchunk2Size >> 8) & 0xff;
  byte3 = (Subchunk2Size >> 16) & 0xff;
  byte4 = (Subchunk2Size >> 24) & 0xff;
  frec.write(byte1);  frec.write(byte2);  frec.write(byte3);  frec.write(byte4);
  frec.close();

  Serial.println("header written");
  Serial.print("Subchunk2: ");
  Serial.println(Subchunk2Size);

}






//////////// Below code the controls the overall system sleep  ////////////////
// https://forum.pjrc.com/threads/33243-RTC-alarm-to-wake-up-from-deep-sleep //
///////////////////////////////////////////////////////////////////////////////

/******************* Seting Alarm **************************/
#define RTC_IER_TAIE_MASK       0x4u
#define RTC_SR_TAF_MASK         0x4u

void rtcSetup(void)
{
  SIM_SCGC6 |= SIM_SCGC6_RTC;// enable RTC clock
  RTC_CR |= RTC_CR_OSCE;// enable RTC
}

void rtcSetAlarm(uint32_t nsec)
{
  RTC_TAR = RTC_TSR + nsec;
  RTC_IER |= RTC_IER_TAIE_MASK;
}

/********************LLWU**********************************/
#define WMUF5_MASK      0x20u

static void llwuISR(void)
{
#if defined(HAS_KINETIS_LLWU_32CH)
  LLWU_MF5 |= WMUF5_MASK;
#elif defined(HAS_KINETIS_LLWU_16CH)
  LLWU_F3 |= WMUF5_MASK; // clear source in LLWU Flag register
#endif
  RTC_IER = 0;// clear RTC interrupts
}

void llwuSetup(void)
{
  attachInterruptVector( IRQ_LLWU, llwuISR );
  NVIC_SET_PRIORITY( IRQ_LLWU, 2 * 16 );

  NVIC_CLEAR_PENDING( IRQ_LLWU );
  NVIC_ENABLE_IRQ( IRQ_LLWU );

  LLWU_PE1 = 0;
  LLWU_PE2 = 0;
  LLWU_PE3 = 0;
  LLWU_PE4 = 0;
  LLWU_ME  = LLWU_ME_WUME5; //rtc alarm
}

/********************* go to deep sleep *********************/
// These are defined in the core now
#define SMC_PMPROT_AVLLS_MASK   0x2u
#define SMC_PMCTRL_STOPM_MASK   0x7u
#define SCB_SCR_SLEEPDEEP_MASK  0x4u

void goSleep(void) {
  //volatile unsigned int dummyread;
  /* Make sure clock monitor is off so we don't get spurious reset */
  // currently not set by anything I know, so the clock monitor is not set from reset
  MCG_C6 &= ~MCG_C6_CME0;
  //
  /* Write to PMPROT to allow all possible power modes */
  SMC_PMPROT = SMC_PMPROT_AVLLS_MASK;// Not needed already taken care of in here.
  /* Set the STOPM field to 0b100 for VLLSx mode */
  SMC_PMCTRL &= ~SMC_PMCTRL_STOPM_MASK;
  SMC_PMCTRL |= SMC_PMCTRL_STOPM(0x4); // VLLSx

  SMC_VLLSCTRL =  SMC_VLLSCTRL_VLLSM(0x3); // VLLS3
  /*wait for write to complete to SMC before stopping core */
  (void) SMC_PMCTRL;

  SYST_CSR &= ~SYST_CSR_TICKINT;      // disable systick timer interrupt
  SCB_SCR |= SCB_SCR_SLEEPDEEP_MASK;  // Set SLEEPDEEP bit to enable deep sleep mode (STOP)
  asm volatile( "wfi" );  // WFI instruction will start entry into STOP mode
  // will never return, but generates system reset
}

/*  From previously used example for how to activate the sleep/alarm
  void setup() {
   //
   // put your setup code here, to run once:
   flashLed(100);
   //
   rtcSetup();
   llwuSetup();

   rtcSetAlarm(5); // This value is seconds
   goSleep();
  }
*/

//This is used by setup loop to specify where to get time from, pointing to RTC
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}





///////////////// Recording logic /////////////////////
int recordinglength = EEPROM.read(recordaddr)*1000;// milliseconds
int sleeplength = EEPROM.read(sleepaddr)*60; //seconds



void recordingloop(){
  elapsedMillis waiting;     // "waiting" starts at zero
  while (waiting <= (recordinglength + 1000)) {
      if (mode == 0) {
        startRecording();
      }
    if (mode == 1) {
      continueRecording();
    }
  }
  // Time has elapsed, run this code to wrap up the recording
  if (mode == 1) stopRecording();
  rtcSetup();
  llwuSetup();

  rtcSetAlarm(sleeplength); // This value is seconds
  goSleep();
}

///////////////////// For changing defaults ////////////////////////////

String menuItems[3] = {"Sleep Length--->", " <--Mic Gain--> ", "<--Record Length"};
int MenuSelection = 3;
int MenuMode = 0;//0, not selected.  1, selected.

/* vvv For Reference Only. Don't uncomment. vvv */
//unsigned int gainaddr = 2;
//unsigned int sleepaddr = 1;
//unsigned int recordaddr = 0;
//int recordinglength = 10000;// milliseconds
//int sleeplength = 600; //seconds
/* ^^^ For Reference Only. Don't uncomment. ^^^^ */

void SleepMenu(){
  //Get eeprom values
  int sleeplength = EEPROM.read(sleepaddr);
  // We're limited to between 0 and 255, as an eeprom addr only lets us store a byte.
  const int sleepuplimit = 255;
  const int sleepbottomlimit = 0;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sleep --> mins: ");
  lcd.setCursor(0,1);
  lcd.print(sleeplength);
  do{
  
  int buttonv = buttonChecker();
  if(buttonv == 0){ //select button
    //TODO: write editval to eeprom
    EEPROM.write(sleepaddr, sleeplength);
    MenuMode = 0;
    }
  else if(buttonv == 1){ //left button
      if(sleeplength > sleepbottomlimit){
          sleeplength = sleeplength - 1;
        lcd.setCursor(0,1);
        lcd.print("                ");//to clear the bottom line before re-writing it
        lcd.setCursor(0,1);
        lcd.print(sleeplength);
      }
    }
  else if(buttonv == 2){ //right button
      if(sleeplength < sleepuplimit ){
          sleeplength++;
          lcd.setCursor(0,1);
          lcd.print("                ");//to clear the bottom line before re-writing it
          lcd.setCursor(0,1);
          lcd.print(sleeplength);
      }
    }
  }while(MenuMode);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(menuItems[MenuSelection]);
}
  
void GainMenu(){
  //Get eeprom values
  int gainval = EEPROM.read(gainaddr);
  // We're limited to between 0 and 255, as an eeprom addr only lets us store a byte.
  // Gain seems to be limited with 0-63
  const int gainuplimit = 63;
  const int gainbottomlimit = 0;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Gain ---> DBs:  ");
  lcd.setCursor(0,1);
  lcd.print(gainval);
  do{
  
  int buttonv = buttonChecker();
  if(buttonv == 0){ //select button
    //TODO: write editval to eeprom
    EEPROM.write(gainaddr, gainval);
    MenuMode = 0;
    }
  else if(buttonv == 1){ //left button
      if(gainval > gainbottomlimit){
          gainval = gainval - 1;
        lcd.setCursor(0,1);
        lcd.print("                ");//to clear the bottom line before re-writing it
        lcd.setCursor(0,1);
        lcd.print(gainval);
      }
    }
  else if(buttonv == 2){ //right button
      if(gainval < gainuplimit ){
          gainval++;
          lcd.setCursor(0,1);
          lcd.print("                ");//to clear the bottom line before re-writing it
          lcd.setCursor(0,1);
          lcd.print(gainval);
      }
    }
  }while(MenuMode);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(menuItems[MenuSelection]);
}
  
void RecordMenu(){
  //Get eeprom values
  int recordval = EEPROM.read(recordaddr);
  // We're limited to between 0 and 255, as an eeprom addr only lets us store a byte.
  // Gain seems to be limited with 0-63
  const int recorduplimit = 63;
  const int recordbottomlimit = 0;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Record --> secs:");
  lcd.setCursor(0,1);
  lcd.print(recordval);
  do{
  
  int buttonv = buttonChecker();
  if(buttonv == 0){ //select button
    //TODO: write editval to eeprom
    EEPROM.write(recordaddr, recordval);
    MenuMode = 0;
    }
  else if(buttonv == 1){ //left button
      if(recordval > recordbottomlimit){
          recordval = recordval - 1;
        lcd.setCursor(0,1);
        lcd.print("                ");//to clear the bottom line before re-writing it
        lcd.setCursor(0,1);
        lcd.print(recordval);
      }
    }
  else if(buttonv == 2){ //right button
      if(recordval < recorduplimit ){
          recordval++;
          lcd.setCursor(0,1);
          lcd.print("                ");//to clear the bottom line before re-writing it
          lcd.setCursor(0,1);
          lcd.print(recordval);
      }
    }
  }while(MenuMode);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(menuItems[MenuSelection]);
}

void mainMenu(){
   int menuUpLimit = 2;
   int menuBotLimit = 0;

   lcd.setCursor(0,0);
   lcd.print("<--- Choose --->");
   MenuSelection = 1; // drop us in the middle of the list
   do {
   int buttonv = buttonChecker();
   if (buttonv == 3){
    /* No button was pressed. Do nothing different. */
    }
   else if(buttonv == 0){ //confirmation button
    if (MenuMode == 0){
      Serial.println("item selected");
        MenuMode=1;
        if(MenuSelection == 0){ //Sleep Menu
          SleepMenu();
          }
        if(MenuSelection == 1){ //Gain Menu
          GainMenu();
          }
        if(MenuSelection == 2){ //Recording length Menu
          RecordMenu();
        }
      }
    }
   else if(buttonv == 1){//left button
    Serial.println("left button");
      if(MenuSelection >menuBotLimit){MenuSelection = MenuSelection - 1;
         lcd.clear();
         lcd.setCursor(0,0);
   lcd.print(menuItems[MenuSelection]);
   Serial.println(menuItems[MenuSelection]);
   delay(200);
   }
    }
   else if(buttonv == 2){//right button
    Serial.println("right button");
      if(MenuSelection < menuUpLimit){MenuSelection++;
         lcd.clear();
         lcd.setCursor(0,0);
   lcd.print(menuItems[MenuSelection]);
   Serial.println(menuItems[MenuSelection]);
   delay(200);
   }
    }
   }while(true);
}

int buttonChecker(){
// First, read the buttons
    confirm.update(); //confirm
    left.update(); //left
    right.update(); //right

    // Respond to button presses
    if (confirm.fallingEdge()) {
    return 0; 
    }
    if (  left.fallingEdge())  {//incomingByte == '2'  ||
    return 1;
    }
    if ( right.fallingEdge())  {// incomingByte == '3'  ||
    return 2;
    }
    else {return 3;}//No button pressed
}

