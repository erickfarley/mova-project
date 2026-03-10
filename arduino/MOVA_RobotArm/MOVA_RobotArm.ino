#include <Servo.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// ==========================
// Pin mapping (adjust as needed)
// ==========================
static const uint8_t PIN_BASE = 3;       // 180 deg servo
static const uint8_t PIN_COTOVELO = 5;   // 180 deg servo
static const uint8_t PIN_ANTEBRACO = 6;  // continuous rotation servo
static const uint8_t PIN_PUNHO = 9;      // continuous rotation servo
static const uint8_t PIN_GARRA = 10;     // continuous rotation servo

// ==========================
// Serial and parser
// ==========================
static const unsigned long SERIAL_BAUD = 9600;
static const size_t LINE_BUF_SIZE = 120;
char g_lineBuf[LINE_BUF_SIZE];
size_t g_lineLen = 0;

struct Joint180 {
  Servo servo;
  uint8_t pin;
  int currentAngle;   // logical angle 0..180
  int homeAngle;      // logical home angle
  bool inverted;
};

struct Axis360 {
  Servo servo;
  uint8_t pin;
  bool inverted;
  int neutralUs;      // stop pulse
  int powerPct;       // 0..100
  int spanUs;         // max delta from neutral when power is 100
  int logicalDir;     // -1 left/close, 0 stop, +1 right/open
  unsigned long runUntilMs; // 0 = infinite until stop command
};

Joint180 g_base = {Servo(), PIN_BASE, 90, 90, false};
Joint180 g_cotovelo = {Servo(), PIN_COTOVELO, 90, 90, false};

Axis360 g_antebraco = {Servo(), PIN_ANTEBRACO, false, 1500, 65, 350, 0, 0};
Axis360 g_punho = {Servo(), PIN_PUNHO, false, 1500, 65, 350, 0, 0};
Axis360 g_garra = {Servo(), PIN_GARRA, false, 1500, 75, 350, 0, 0};

int g_speedPct = 45;  // 1..100

bool g_demoOn = false;
bool g_cumprimentoOn = false;
unsigned long g_nextDemoMs = 0;
uint8_t g_demoStep = 0;
unsigned long g_nextCumprimentoMs = 0;

// ==========================
// Helpers
// ==========================
int clampInt(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

void toUpperInPlace(char* s) {
  while (*s) {
    *s = (char)toupper((unsigned char)*s);
    s++;
  }
}

bool parseLong(const char* token, long& outValue) {
  if (token == NULL || *token == '\0') return false;
  char* endPtr = NULL;
  long value = strtol(token, &endPtr, 10);
  if (endPtr == token || *endPtr != '\0') return false;
  outValue = value;
  return true;
}

int logicalToPhysical(const Joint180& joint, int logicalAngle) {
  logicalAngle = clampInt(logicalAngle, 0, 180);
  if (joint.inverted) return 180 - logicalAngle;
  return logicalAngle;
}

void writeJointLogical(Joint180& joint, int logicalAngle) {
  int angle = clampInt(logicalAngle, 0, 180);
  joint.currentAngle = angle;
  joint.servo.write(logicalToPhysical(joint, angle));
}

void moveJointSmooth(Joint180& joint, int targetAngle, int speedPct) {
  targetAngle = clampInt(targetAngle, 0, 180);
  speedPct = clampInt(speedPct, 1, 100);

  int delayMs = map(speedPct, 1, 100, 25, 2);
  int current = joint.currentAngle;
  if (current == targetAngle) return;

  int step = (targetAngle > current) ? 1 : -1;
  while (current != targetAngle) {
    current += step;
    writeJointLogical(joint, current);
    delay(delayMs);
    // keep timed axis stop responsive even during smooth moves
    unsigned long nowMs = millis();
    if (g_antebraco.logicalDir != 0 && g_antebraco.runUntilMs > 0 && nowMs >= g_antebraco.runUntilMs) {
      g_antebraco.servo.writeMicroseconds(g_antebraco.neutralUs);
      g_antebraco.logicalDir = 0;
      g_antebraco.runUntilMs = 0;
    }
    if (g_punho.logicalDir != 0 && g_punho.runUntilMs > 0 && nowMs >= g_punho.runUntilMs) {
      g_punho.servo.writeMicroseconds(g_punho.neutralUs);
      g_punho.logicalDir = 0;
      g_punho.runUntilMs = 0;
    }
    if (g_garra.logicalDir != 0 && g_garra.runUntilMs > 0 && nowMs >= g_garra.runUntilMs) {
      g_garra.servo.writeMicroseconds(g_garra.neutralUs);
      g_garra.logicalDir = 0;
      g_garra.runUntilMs = 0;
    }
  }
}

void movePoseSmooth(int baseAngle, int cotoveloAngle, int speedPct) {
  baseAngle = clampInt(baseAngle, 0, 180);
  cotoveloAngle = clampInt(cotoveloAngle, 0, 180);
  speedPct = clampInt(speedPct, 1, 100);
  int delayMs = map(speedPct, 1, 100, 25, 2);

  int b = g_base.currentAngle;
  int c = g_cotovelo.currentAngle;

  while (b != baseAngle || c != cotoveloAngle) {
    if (b < baseAngle) b++;
    else if (b > baseAngle) b--;

    if (c < cotoveloAngle) c++;
    else if (c > cotoveloAngle) c--;

    writeJointLogical(g_base, b);
    writeJointLogical(g_cotovelo, c);
    delay(delayMs);
  }
}

int axisPulseForDirection(const Axis360& axis, int logicalDir) {
  int dir = clampInt(logicalDir, -1, 1);
  if (dir == 0) return axis.neutralUs;

  int physicalDir = axis.inverted ? -dir : dir;
  int span = map(clampInt(axis.powerPct, 0, 100), 0, 100, 0, axis.spanUs);
  int pulse = axis.neutralUs + (physicalDir * span);
  return clampInt(pulse, 700, 2300);
}

void axisStop(Axis360& axis) {
  axis.servo.writeMicroseconds(axis.neutralUs);
  axis.logicalDir = 0;
  axis.runUntilMs = 0;
}

void axisRun(Axis360& axis, int logicalDir, unsigned long durationMs) {
  axis.logicalDir = clampInt(logicalDir, -1, 1);
  axis.servo.writeMicroseconds(axisPulseForDirection(axis, axis.logicalDir));
  if (durationMs > 0) axis.runUntilMs = millis() + durationMs;
  else axis.runUntilMs = 0;
}

void stopAll360() {
  axisStop(g_antebraco);
  axisStop(g_punho);
  axisStop(g_garra);
}

void homeAll() {
  movePoseSmooth(g_base.homeAngle, g_cotovelo.homeAngle, g_speedPct);
  stopAll360();
}

Axis360* axisByName(const char* token) {
  if (strcmp(token, "ANTEBRACO") == 0) return &g_antebraco;
  if (strcmp(token, "PUNHO") == 0) return &g_punho;
  if (strcmp(token, "GARRA") == 0) return &g_garra;
  return NULL;
}

void printHelp() {
  Serial.println(F("OK HELP"));
  Serial.println(F("CMD: PING | HELP | HOME"));
  Serial.println(F("CMD: SPEED <1..100>"));
  Serial.println(F("CMD: BASE <0..180> | COTOVELO <0..180>"));
  Serial.println(F("CMD: POSE <base> <cotovelo> [vel]"));
  Serial.println(F("CMD: ANTEBRACO GIRAR <DIR|ESQ> [ms] | ANTEBRACO PARAR"));
  Serial.println(F("CMD: PUNHO GIRAR <DIR|ESQ> [ms] | PUNHO PARAR"));
  Serial.println(F("CMD: GARRA ABRIR [ms] | GARRA FECHAR [ms] | GARRA PARAR"));
  Serial.println(F("CMD: DIR <BASE|COTOVELO> <INV|NORM>"));
  Serial.println(F("CMD: <ANTEBRACO|PUNHO|GARRA> DIR <INV|NORM>"));
  Serial.println(F("CMD: <ANTEBRACO|PUNHO|GARRA> POTENCIA <0..100>"));
  Serial.println(F("CMD: <ANTEBRACO|PUNHO|GARRA> NEUTRO <us>"));
  Serial.println(F("CMD: <ANTEBRACO|PUNHO|GARRA> RAW <us> [ms]"));
  Serial.println(F("CMD: DEMO ON|OFF | CUMPRIMENTO ON|OFF | CUMPRIMENTAR | PEGA <base> <cotovelo> [ms]"));
}

void doCumprimentar() {
  axisRun(g_punho, +1, 220);
  delay(260);
  axisRun(g_punho, -1, 220);
  delay(260);
  axisRun(g_punho, +1, 220);
  delay(260);
  axisRun(g_punho, -1, 220);
  delay(260);
  axisStop(g_punho);
}

void runPega(int baseAngle, int cotoveloAngle, unsigned long closeMs) {
  axisRun(g_garra, +1, 250); // abrir
  delay(280);
  movePoseSmooth(baseAngle, cotoveloAngle, g_speedPct);
  axisRun(g_garra, -1, closeMs); // fechar
}

// ==========================
// Command handling
// ==========================
void handleAxisCommand(Axis360& axis, const char* axisName, char* tokens[], int count) {
  if (count < 2) {
    Serial.println(F("ERR missing axis command"));
    return;
  }

  if (strcmp(tokens[1], "PARAR") == 0) {
    axisStop(axis);
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.println(F(" PARAR"));
    return;
  }

  if (strcmp(tokens[1], "GIRAR") == 0) {
    if (count < 3) {
      Serial.println(F("ERR GIRAR needs DIR|ESQ"));
      return;
    }
    int logicalDir = 0;
    if (strcmp(tokens[2], "DIR") == 0) logicalDir = +1;
    else if (strcmp(tokens[2], "ESQ") == 0) logicalDir = -1;
    else {
      Serial.println(F("ERR GIRAR dir invalid"));
      return;
    }
    unsigned long ms = 0;
    if (count >= 4) {
      long parsed = 0;
      if (parseLong(tokens[3], parsed)) ms = (unsigned long)clampInt((int)parsed, 0, 60000);
    }
    axisRun(axis, logicalDir, ms);
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.print(F(" GIRAR "));
    Serial.print(tokens[2]);
    if (ms > 0) {
      Serial.print(F(" "));
      Serial.print(ms);
    }
    Serial.println();
    return;
  }

  if (strcmp(tokens[1], "ABRIR") == 0 && strcmp(axisName, "GARRA") == 0) {
    unsigned long ms = 300;
    if (count >= 3) {
      long parsed = 0;
      if (parseLong(tokens[2], parsed)) ms = (unsigned long)clampInt((int)parsed, 0, 60000);
    }
    axisRun(axis, +1, ms);
    Serial.print(F("OK GARRA ABRIR "));
    Serial.println(ms);
    return;
  }

  if (strcmp(tokens[1], "FECHAR") == 0 && strcmp(axisName, "GARRA") == 0) {
    unsigned long ms = 400;
    if (count >= 3) {
      long parsed = 0;
      if (parseLong(tokens[2], parsed)) ms = (unsigned long)clampInt((int)parsed, 0, 60000);
    }
    axisRun(axis, -1, ms);
    Serial.print(F("OK GARRA FECHAR "));
    Serial.println(ms);
    return;
  }

  if (strcmp(tokens[1], "DIR") == 0) {
    if (count < 3) {
      Serial.println(F("ERR DIR needs INV|NORM"));
      return;
    }
    if (strcmp(tokens[2], "INV") == 0) axis.inverted = true;
    else if (strcmp(tokens[2], "NORM") == 0) axis.inverted = false;
    else {
      Serial.println(F("ERR DIR invalid"));
      return;
    }
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.print(F(" DIR "));
    Serial.println(axis.inverted ? F("INV") : F("NORM"));
    return;
  }

  if (strcmp(tokens[1], "POTENCIA") == 0) {
    if (count < 3) {
      Serial.println(F("ERR POTENCIA needs 0..100"));
      return;
    }
    long parsed = 0;
    if (!parseLong(tokens[2], parsed)) {
      Serial.println(F("ERR POTENCIA invalid"));
      return;
    }
    axis.powerPct = clampInt((int)parsed, 0, 100);
    if (axis.logicalDir != 0) axis.servo.writeMicroseconds(axisPulseForDirection(axis, axis.logicalDir));
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.print(F(" POTENCIA "));
    Serial.println(axis.powerPct);
    return;
  }

  if (strcmp(tokens[1], "NEUTRO") == 0) {
    if (count < 3) {
      Serial.println(F("ERR NEUTRO needs pulse"));
      return;
    }
    long parsed = 0;
    if (!parseLong(tokens[2], parsed)) {
      Serial.println(F("ERR NEUTRO invalid"));
      return;
    }
    axis.neutralUs = clampInt((int)parsed, 1000, 2000);
    if (axis.logicalDir == 0) axis.servo.writeMicroseconds(axis.neutralUs);
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.print(F(" NEUTRO "));
    Serial.println(axis.neutralUs);
    return;
  }

  if (strcmp(tokens[1], "RAW") == 0) {
    if (count < 3) {
      Serial.println(F("ERR RAW needs pulse"));
      return;
    }
    long pulse = 0;
    if (!parseLong(tokens[2], pulse)) {
      Serial.println(F("ERR RAW invalid"));
      return;
    }
    int us = clampInt((int)pulse, 700, 2300);
    axis.servo.writeMicroseconds(us);
    axis.logicalDir = 0;
    axis.runUntilMs = 0;
    if (count >= 4) {
      long ms = 0;
      if (parseLong(tokens[3], ms) && ms > 0) {
        delay((unsigned long)clampInt((int)ms, 1, 60000));
        axisStop(axis);
      }
    }
    Serial.print(F("OK "));
    Serial.print(axisName);
    Serial.print(F(" RAW "));
    Serial.println(us);
    return;
  }

  Serial.println(F("ERR axis command unknown"));
}

void handleCommand(char* line) {
  // normalize separators
  for (size_t i = 0; line[i] != '\0'; i++) {
    if (line[i] == ',' || line[i] == ';') line[i] = ' ';
  }

  char* tokens[8];
  int count = 0;
  char* tok = strtok(line, " \t\r\n");
  while (tok != NULL && count < 8) {
    toUpperInPlace(tok);
    tokens[count++] = tok;
    tok = strtok(NULL, " \t\r\n");
  }
  if (count == 0) return;

  if (strcmp(tokens[0], "PING") == 0) {
    Serial.println(F("PONG"));
    return;
  }

  if (strcmp(tokens[0], "HELP") == 0) {
    printHelp();
    return;
  }

  if (strcmp(tokens[0], "HOME") == 0) {
    homeAll();
    Serial.println(F("OK HOME"));
    return;
  }

  if (strcmp(tokens[0], "SPEED") == 0) {
    if (count < 2) {
      Serial.println(F("ERR SPEED needs value"));
      return;
    }
    long value = 0;
    if (!parseLong(tokens[1], value)) {
      Serial.println(F("ERR SPEED invalid"));
      return;
    }
    g_speedPct = clampInt((int)value, 1, 100);
    Serial.print(F("OK SPEED "));
    Serial.println(g_speedPct);
    return;
  }

  if (strcmp(tokens[0], "BASE") == 0) {
    if (count < 2) {
      Serial.println(F("ERR BASE needs angle"));
      return;
    }
    long value = 0;
    if (!parseLong(tokens[1], value)) {
      Serial.println(F("ERR BASE invalid"));
      return;
    }
    int target = clampInt((int)value, 0, 180);
    moveJointSmooth(g_base, target, g_speedPct);
    Serial.print(F("OK BASE "));
    Serial.println(target);
    return;
  }

  if (strcmp(tokens[0], "COTOVELO") == 0) {
    if (count < 2) {
      Serial.println(F("ERR COTOVELO needs angle"));
      return;
    }
    long value = 0;
    if (!parseLong(tokens[1], value)) {
      Serial.println(F("ERR COTOVELO invalid"));
      return;
    }
    int target = clampInt((int)value, 0, 180);
    moveJointSmooth(g_cotovelo, target, g_speedPct);
    Serial.print(F("OK COTOVELO "));
    Serial.println(target);
    return;
  }

  if (strcmp(tokens[0], "POSE") == 0) {
    if (count < 3) {
      Serial.println(F("ERR POSE needs base and cotovelo"));
      return;
    }
    long baseVal = 0, cotVal = 0;
    if (!parseLong(tokens[1], baseVal) || !parseLong(tokens[2], cotVal)) {
      Serial.println(F("ERR POSE invalid"));
      return;
    }
    int speed = g_speedPct;
    if (count >= 4) {
      long speedVal = 0;
      if (parseLong(tokens[3], speedVal)) speed = clampInt((int)speedVal, 1, 100);
    }
    int baseAngle = clampInt((int)baseVal, 0, 180);
    int cotAngle = clampInt((int)cotVal, 0, 180);
    movePoseSmooth(baseAngle, cotAngle, speed);
    Serial.print(F("OK POSE "));
    Serial.print(baseAngle);
    Serial.print(F(" "));
    Serial.print(cotAngle);
    Serial.print(F(" "));
    Serial.println(speed);
    return;
  }

  if (strcmp(tokens[0], "DIR") == 0) {
    if (count < 3) {
      Serial.println(F("ERR DIR needs axis and mode"));
      return;
    }
    bool inv;
    if (strcmp(tokens[2], "INV") == 0) inv = true;
    else if (strcmp(tokens[2], "NORM") == 0) inv = false;
    else {
      Serial.println(F("ERR DIR mode invalid"));
      return;
    }

    if (strcmp(tokens[1], "BASE") == 0) {
      g_base.inverted = inv;
      writeJointLogical(g_base, g_base.currentAngle);
      Serial.print(F("OK DIR BASE "));
      Serial.println(inv ? F("INV") : F("NORM"));
      return;
    }
    if (strcmp(tokens[1], "COTOVELO") == 0) {
      g_cotovelo.inverted = inv;
      writeJointLogical(g_cotovelo, g_cotovelo.currentAngle);
      Serial.print(F("OK DIR COTOVELO "));
      Serial.println(inv ? F("INV") : F("NORM"));
      return;
    }
    Serial.println(F("ERR DIR axis invalid"));
    return;
  }

  if (strcmp(tokens[0], "DEMO") == 0) {
    if (count < 2) {
      Serial.println(F("ERR DEMO needs ON|OFF"));
      return;
    }
    if (strcmp(tokens[1], "ON") == 0) {
      g_demoOn = true;
      g_demoStep = 0;
      g_nextDemoMs = millis();
      Serial.println(F("OK DEMO ON"));
      return;
    }
    if (strcmp(tokens[1], "OFF") == 0) {
      g_demoOn = false;
      stopAll360();
      Serial.println(F("OK DEMO OFF"));
      return;
    }
    Serial.println(F("ERR DEMO mode invalid"));
    return;
  }

  if (strcmp(tokens[0], "CUMPRIMENTO") == 0) {
    if (count < 2) {
      Serial.println(F("ERR CUMPRIMENTO needs ON|OFF"));
      return;
    }
    if (strcmp(tokens[1], "ON") == 0) {
      g_cumprimentoOn = true;
      g_nextCumprimentoMs = millis();
      Serial.println(F("OK CUMPRIMENTO ON"));
      return;
    }
    if (strcmp(tokens[1], "OFF") == 0) {
      g_cumprimentoOn = false;
      axisStop(g_punho);
      Serial.println(F("OK CUMPRIMENTO OFF"));
      return;
    }
    Serial.println(F("ERR CUMPRIMENTO mode invalid"));
    return;
  }

  if (strcmp(tokens[0], "CUMPRIMENTAR") == 0) {
    doCumprimentar();
    Serial.println(F("OK CUMPRIMENTAR"));
    return;
  }

  if (strcmp(tokens[0], "PEGA") == 0) {
    int baseAngle = 50;
    int cotAngle = 120;
    unsigned long ms = 400;
    long tmp = 0;
    if (count >= 2 && parseLong(tokens[1], tmp)) baseAngle = clampInt((int)tmp, 0, 180);
    if (count >= 3 && parseLong(tokens[2], tmp)) cotAngle = clampInt((int)tmp, 0, 180);
    if (count >= 4 && parseLong(tokens[3], tmp)) ms = (unsigned long)clampInt((int)tmp, 0, 60000);
    runPega(baseAngle, cotAngle, ms);
    Serial.print(F("OK PEGA "));
    Serial.print(baseAngle);
    Serial.print(F(" "));
    Serial.print(cotAngle);
    Serial.print(F(" "));
    Serial.println(ms);
    return;
  }

  Axis360* axis = axisByName(tokens[0]);
  if (axis != NULL) {
    handleAxisCommand(*axis, tokens[0], tokens, count);
    return;
  }

  Serial.println(F("ERR unknown command"));
}

void processSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      g_lineBuf[g_lineLen] = '\0';
      handleCommand(g_lineBuf);
      g_lineLen = 0;
      continue;
    }

    if (g_lineLen < (LINE_BUF_SIZE - 1)) {
      g_lineBuf[g_lineLen++] = c;
    } else {
      // overflow: reset current line
      g_lineLen = 0;
      Serial.println(F("ERR line too long"));
    }
  }
}

void updateTimedAxes() {
  unsigned long nowMs = millis();
  if (g_antebraco.logicalDir != 0 && g_antebraco.runUntilMs > 0 && nowMs >= g_antebraco.runUntilMs) {
    axisStop(g_antebraco);
    Serial.println(F("OK ANTEBRACO PARAR (timer)"));
  }
  if (g_punho.logicalDir != 0 && g_punho.runUntilMs > 0 && nowMs >= g_punho.runUntilMs) {
    axisStop(g_punho);
    Serial.println(F("OK PUNHO PARAR (timer)"));
  }
  if (g_garra.logicalDir != 0 && g_garra.runUntilMs > 0 && nowMs >= g_garra.runUntilMs) {
    axisStop(g_garra);
    Serial.println(F("OK GARRA PARAR (timer)"));
  }
}

void updateModes() {
  unsigned long nowMs = millis();

  if (g_demoOn && nowMs >= g_nextDemoMs) {
    switch (g_demoStep) {
      case 0:
        homeAll();
        g_nextDemoMs = nowMs + 200;
        break;
      case 1:
        axisRun(g_antebraco, +1, 350);
        g_nextDemoMs = nowMs + 450;
        break;
      case 2:
        axisRun(g_antebraco, -1, 350);
        g_nextDemoMs = nowMs + 450;
        break;
      case 3:
        axisRun(g_garra, +1, 300);
        g_nextDemoMs = nowMs + 420;
        break;
      case 4:
        axisRun(g_garra, -1, 400);
        g_nextDemoMs = nowMs + 520;
        break;
      case 5:
        movePoseSmooth(120, 65, 55);
        g_nextDemoMs = nowMs + 300;
        break;
      case 6:
        movePoseSmooth(70, 120, 55);
        g_nextDemoMs = nowMs + 300;
        break;
      default:
        g_nextDemoMs = nowMs + 250;
        break;
    }
    g_demoStep = (uint8_t)((g_demoStep + 1) % 7);
  }

  if (g_cumprimentoOn && nowMs >= g_nextCumprimentoMs) {
    doCumprimentar();
    g_nextCumprimentoMs = millis() + 1800;
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  g_base.servo.attach(g_base.pin);
  g_cotovelo.servo.attach(g_cotovelo.pin);
  g_antebraco.servo.attach(g_antebraco.pin);
  g_punho.servo.attach(g_punho.pin);
  g_garra.servo.attach(g_garra.pin);

  writeJointLogical(g_base, g_base.homeAngle);
  writeJointLogical(g_cotovelo, g_cotovelo.homeAngle);
  stopAll360();

  Serial.println(F("MOVA ARM READY"));
  Serial.print(F("BAUD "));
  Serial.println(SERIAL_BAUD);
  printHelp();
}

void loop() {
  processSerial();
  updateTimedAxes();
  updateModes();
}
