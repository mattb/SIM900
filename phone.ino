#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Regexp.h>
#include <YASP.h>
#include <SoftwareSerial.h>
#include <Button.h>
#include <FiniteStateMachine.h>

char nextOKCommand[30];
char nextCommand[30];
char textBuffer[180];

Button button = Button(11,PULLDOWN);

// Declare callbacks for further processing a command after a regexp match
yaspCBReturn_t smsReceived(MatchState *ms);
yaspCBReturn_t smsContent(MatchState *ms);
yaspCBReturn_t ok(MatchState *ms);
yaspCBReturn_t ring(MatchState *ms);
yaspCBReturn_t noCarrier(MatchState *ms);
yaspCBReturn_t atI(MatchState *ms);
yaspCBReturn_t clock(MatchState *ms);
yaspCBReturn_t callerId(MatchState *ms);

State Normal = State(normalEnter, normalUpdate, normalExit);
State Ringing = State(ringingEnter, ringingUpdate, ringingExit);
State Answered = State(answeredEnter, answeredUpdate, answeredExit);

FSM state = FSM(Normal);

void normalEnter() {
  Serial.println(F("Normal mode"));
  screenMessageMedium("READY");
}

void normalUpdate() {
}

void normalExit() {
}

void ringingEnter() {
  Serial.println(F("starting ringing"));
  screenMessageBig("RING");
}

void ringingUpdate() {
  if(button.uniquePress()) {
    strcpy(nextCommand, "ATA");
    state.transitionTo(Answered);
  }
}

void ringingExit() {
  screenClear();
}

void answeredEnter() {
  screenMessageMedium("IN CALL");
  Serial.println(F("starting answering"));
}

void answeredUpdate() {
  if(button.uniquePress()) {
    strcpy(nextCommand, "ATH");
    state.transitionTo(Normal);
  }
}

void answeredExit() {
  screenClear();
}

yaspCommand_t cmds[] = 
{
  { "^OK$", "ok", ok },
  { "^RING$", "ring", ring },
  { "^NO CARRIER$", "NO CARRIER", noCarrier },
  { "^.CMTI: \"SM\",([%d]+)$", "sms", smsReceived },
  { "^.CMGR: (\".+\")$", "sms content", smsContent },
  { "^SIM900 .+$", "ATI", atI },
  { "^.CCLK: \".+,(.+)\"$", "clock", clock },
  //{ "^.CLCC: (.+)$", "callerid", callerId },
  { 0, 0, 0 }
};

yasp cmdParser(&cmds[0], NULL, NULL);
Adafruit_PCD8544 display = Adafruit_PCD8544(6, 5, 4, 3, 2);
SoftwareSerial GPRS(7, 8);

void setup()
{
  memset(nextOKCommand,0,sizeof(nextOKCommand));
  memset(textBuffer,0,sizeof(textBuffer));
  memset(nextCommand,0,sizeof(nextCommand));

  GPRS.begin(19200);               // the GPRS baud rate   
  Serial.begin(19200);             // the Serial port of Arduino baud rate.
  cmdParser.EOL = "\r";
  cmdParser.userPrompt = NULL;
  display.begin();
  display.setContrast(55);
  
  Serial.println(F("BOOT"));

  waitForReady();
  screenMessageMedium("READY");
  //strcpy(nextCommand, "ATI");
}

void loop()
{
  state.update();
  static long runloop = 0;

  char read = 0;
  while(GPRS.available()) {
    read = GPRS.read();
    Serial.write(read);
    cmdParser.addData(read);
  }
  read = 0;

  while(Serial.available()) {
    read = Serial.read();
    GPRS.write(read);
  }

  if(strncmp("", nextCommand, 1) != 0) {
    delay(250);
    Serial.print(F("Sending next command: "));
    Serial.println(nextCommand);
    GPRS.print(nextCommand);
    GPRS.print(F("\r"));
    strcpy(nextCommand, "");
  }

  runloop += 1;

  if(runloop % 20000 == 0) {
    //strcpy(nextCommand, "AT+CCLK?");
  }

#ifdef DEBUG_RUNLOOP
  if(runloop % 50000 == 0) {
    sprintf(textBuffer, "runloop%ld", runloop / 50000);
    screenMessageMedium(textBuffer);
  }
#endif
}

void screenMessage(char *message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0,0);
  display.println(message);
  display.display();
}

void screenMessageMedium(char *message) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(BLACK);
  display.setCursor(0,0);
  display.println(message);
  display.display();
}

void screenMessageBig(char *message) {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(BLACK);
  display.setCursor(0,0);
  display.println(message);
  display.display();
}

void screenClear() {
  display.clearDisplay();
  display.display();
}

void waitForReady() {
  int done = 0;
  int tries = 0;
  while(!done) {
    tries += 1;
    sprintf(textBuffer, "BOOT %d", tries);
    screenMessageMedium(textBuffer);
    
    GPRS.print(F("AT\r"));
    Serial.println(F("AT"));
    int read_idx = GPRS.readBytesUntil('\n',textBuffer,15);
    textBuffer[read_idx] = 0;
    if(strncmp("OK", textBuffer, 2) == 0) {
      done = 1;
    } else {
      if(tries % 6 == 0) {
        screenMessageMedium("POWER");
        powerUpOrDown();
      } else {
        delay(1000);
      }
    }
  }
  // drain any buffered AT/OK strings
  while(GPRS.available()) {
    GPRS.read();
  }
}

void powerUpOrDown()
{
  pinMode(9, OUTPUT); 
  digitalWrite(9,LOW);
  delay(1000);
  digitalWrite(9,HIGH);
  delay(2000);
  digitalWrite(9,LOW);
  delay(3000);
}

yaspCBReturn_t smsReceived(MatchState *ms) {
  screenMessage("Text received");
  ms->GetCapture(&textBuffer[0], 0);
  sprintf(nextCommand,"AT+CMGR=%s", textBuffer);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t smsContent(MatchState *ms) {
  int idx = GPRS.readBytesUntil('\r', textBuffer, 500);
  textBuffer[idx] = 0;
  screenMessage(textBuffer);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t ok(MatchState *ms) {
  if(strncmp("", nextOKCommand, 1) != 0) {
    delay(200);
    Serial.print(F("Sending next command: "));
    Serial.println(nextOKCommand);
    GPRS.print(nextOKCommand);
    GPRS.print(F("\r"));
    strcpy(nextOKCommand, "");
  }
  return YASP_STATUS_NONE;
}

yaspCBReturn_t noCarrier(MatchState *ms) {
  state.transitionTo(Normal);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t ring(MatchState *ms) {
  state.transitionTo(Ringing);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t atI(MatchState *ms) {
  ms->GetMatch(&textBuffer[0]);
  screenMessage(textBuffer);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t clock(MatchState *ms) {
  ms->GetCapture(&textBuffer[0], 0);
  textBuffer[8] = 0; // cut off -28 at the end
  screenMessage(textBuffer);
  return YASP_STATUS_NONE;
}

yaspCBReturn_t callerId(MatchState *ms) {
  int start = 0;
  ms->GetCapture(&textBuffer[0], 0);
  while(textBuffer[start] != '"') {
    start++;
  }
  textBuffer[start] = '+';
  int finish = start + 1;
  while(textBuffer[finish] != '"') {
    finish++;
  }
  textBuffer[finish] = 0;
  screenMessage(&textBuffer[start]);
  return YASP_STATUS_NONE;
}
