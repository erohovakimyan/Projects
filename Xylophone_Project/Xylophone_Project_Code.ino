#include <Servo.h>

Servo servo1; // Handles P1–P8 (on pin 5)
Servo servo2; // Handles P9–P15 (on pin 10)
Servo servo3; // Sweeps for P1-P8 (on pin 6)
Servo servo4; // Sweeps for P9-P15 (on pin 9)

bool isExecuting = false;
const int buttonPin = 3;

void setup() {
  Serial.begin(9600);
  pinMode(buttonPin, INPUT); // Initialize button pin as input

  servo1.attach(5);   // P1–P8
  servo2.attach(10);  // P9–P15
  servo3.attach(6);   // Sweep for P1-P8
  servo4.attach(9);   // Sweep for P9-P15

  servo1.write(180);
  servo2.write(60);
  servo3.write(0);
  servo4.write(0);
  delay(1000);

  Serial.println("Ready. Send commands via Serial or press button to play music_2");
}

void loop() {
  // Check for button press
  if (digitalRead(buttonPin) == HIGH && !isExecuting) {
    isExecuting = true;
    Serial.println("Button pressed - playing music_2");
    music_2();
    isExecuting = false;
    delay(200); // Small debounce delay
  }
  
  if (Serial.available() && !isExecuting) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    isExecuting = true;
    
    if (command.indexOf('+') != -1) {
      String part1 = command.substring(0, command.indexOf('+'));
      String part2 = command.substring(command.indexOf('+') + 1);
      executeDualCommand(part1, part2);
    } 
  else {
      if (command == "P1")       P1();
      else if (command == "P2")  P2();
      else if (command == "P3")  P3();
      else if (command == "P4")  P4();
      else if (command == "P5")  P5();
      else if (command == "P6")  P6();
      else if (command == "P7")  P7();
      else if (command == "P8")  P8();
      else if (command == "P9")  P9();
      else if (command == "P10") P10();
      else if (command == "P11") P11();
      else if (command == "P12") P12();
      else if (command == "P13") P13();
      else if (command == "P14") P14();
      else if (command == "P15") P15();
      else if (command == "music_1") music_1();
      else if (command == "music_2") music_2();
      else if (command == "music_3") music_3();
      else Serial.println("Invalid command");
    }
    
    isExecuting = false;
  }
}


void executeDualCommand(String cmd1, String cmd2) {
  Serial.print("Executing dual command: ");
  Serial.print(cmd1);
  Serial.print(" + ");
  Serial.println(cmd2);

  int angle1 = 0;
  bool sweep1 = true;
  bool isLeft1 = true;
  
  if (cmd1 == "P1")      { angle1 = 158; }
  else if (cmd1 == "P2") { angle1 = 148; }
  else if (cmd1 == "P3") { angle1 = 140; }
  else if (cmd1 == "P4") { angle1 = 132; }
  else if (cmd1 == "P5") { angle1 = 124; }
  else if (cmd1 == "P6") { angle1 = 116; }
  else if (cmd1 == "P7") { angle1 = 108; }
  else if (cmd1 == "P8") { angle1 = 98; }
  else if (cmd1 == "P9") { angle1 = 84; sweep1 = false; }
  else if (cmd1 == "P10") { angle1 = 126; isLeft1 = false; sweep1 = false; }
  else if (cmd1 == "P11") { angle1 = 111; isLeft1 = false; }
  else if (cmd1 == "P12") { angle1 = 98; isLeft1 = false; }
  else if (cmd1 == "P13") { angle1 = 85; isLeft1 = false; }
  else if (cmd1 == "P14") { angle1 = 73; isLeft1 = false; }
  else if (cmd1 == "P15") { angle1 = 60; isLeft1 = false; }

  int angle2 = 0;
  bool sweep2 = true;
  bool isLeft2 = true;
  
  if (cmd2 == "P1")      { angle2 = 158; }
  else if (cmd2 == "P2") { angle2 = 148; }
  else if (cmd2 == "P3") { angle2 = 140; }
  else if (cmd2 == "P4") { angle2 = 132; }
  else if (cmd2 == "P5") { angle2 = 124; }
  else if (cmd2 == "P6") { angle2 = 116; }
  else if (cmd2 == "P7") { angle2 = 108; }
  else if (cmd2 == "P8") { angle2 = 98; }
  else if (cmd2 == "P9") { angle2 = 84; sweep2 = false; }
  else if (cmd2 == "P10") { angle2 = 126; isLeft2 = false; sweep2 = false; }
  else if (cmd2 == "P11") { angle2 = 111; isLeft2 = false; }
  else if (cmd2 == "P12") { angle2 = 98; isLeft2 = false; }
  else if (cmd2 == "P13") { angle2 = 85; isLeft2 = false; }
  else if (cmd2 == "P14") { angle2 = 73; isLeft2 = false; }
  else if (cmd2 == "P15") { angle2 = 60; isLeft2 = false; }

  if (isLeft1) {
    servo1.write(angle1);
  } else {
    servo2.write(angle1);
  }
  
  if (isLeft2) {
    servo1.write(angle2);
  } else {
    servo2.write(angle2);
  }

  delay(500);

  if (sweep1) {
    if (isLeft1) {
      servo3.write(58);
    } else {
      servo4.write(40);
    }
  }
  
  if (sweep2) {
    if (isLeft2) {
      servo3.write(58);
    } else {
      servo4.write(40);
    }
  }
  
  delay(140);
  
  // Reset sweep servos
  servo3.write(0);
  servo4.write(0);
  delay(140);

  // Handle special cases (P9 and P10)
  if (cmd1 == "P9") {
    servo1.write(124); // Return to P7 position
    delay(100);
  }
  if (cmd1 == "P10") {
    servo2.write(85); // Return to P7 position
    delay(100);
  }
  if (cmd2 == "P9") {
    servo1.write(124); // Return to P7 position
    delay(100);
  }
  if (cmd2 == "P10") {
    servo2.write(85); // Return to P7 position
    delay(100);
  }
}

// Individual position functions
void P1()  { executeCommand(158, true, true); }
void P2()  { executeCommand(148, true, true); }
void P3()  { executeCommand(140, true, true); }
void P4()  { executeCommand(132, true, true); }
void P5()  { executeCommand(124, true, true); }
void P6()  { executeCommand(116, true, true); }
void P7()  { executeCommand(108, true, true); }
void P8()  { executeCommand(98, true, true); }

void P9()  { 
  executeCommand(84, true, true);
  delay(50);
  executeCommand(108, true, false);
}

void P10()  { 
  executeCommand(126, false, true);
  delay(50);
  executeCommand(98, false, false);
}

void P11() { executeCommand(111, false, true); }
void P12() { executeCommand(98, false, true); }
void P13() { executeCommand(85, false, true); }
void P14() { executeCommand(73, false, true); }
void P15() { executeCommand(60, false, true); }

void executeCommand(int angle, bool isP1P9, bool shouldSweep) {
  if (isP1P9) {
    servo1.write(angle);
    Serial.print("Moved servo1 to angle: ");
  } else {
    servo2.write(angle);
    Serial.print("Moved servo2 to angle: ");
  }
  Serial.println(angle);
  
  delay(500);
  
  if (shouldSweep) {
    if (isP1P9) {
      servo3.write(58);
      delay(140);
      servo3.write(0);
      delay(100);
    } else {
      servo4.write(40);
      delay(140);
      servo4.write(0);
      delay(100);
    }
  }
}

void music_1() {
  Serial.println("Playing music_1 sequence:");
  
  String sequence[] = {"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "P10", "P11", "P12", "P13", "P14", "P15"};
  
  for (int i = 0; i < 15; i++) {
    if (sequence[i] == "P1")       P1();
    else if (sequence[i] == "P2")  P2();
    else if (sequence[i] == "P3")  P3();
    else if (sequence[i] == "P4")  P4();
    else if (sequence[i] == "P5")  P5();
    else if (sequence[i] == "P6")  P6();
    else if (sequence[i] == "P7")  P7();
    else if (sequence[i] == "P8")  P8();
    else if (sequence[i] == "P9")  P9();
    else if (sequence[i] == "P10") P10();
    else if (sequence[i] == "P11") P11();
    else if (sequence[i] == "P12") P12();
    else if (sequence[i] == "P13") P13();
    else if (sequence[i] == "P14") P14();
    else if (sequence[i] == "P15") P15();
    
    delay(100); // Short delay between notes
  }
  Serial.println("Sequence complete");
}

void music_2() {
  Serial.println("Playing music_2 sequence:");
  
  // First iteration
  P4(); delay(10);
  P3(); delay(10);
  P2(); delay(10);
  P1(); delay(10);
  P12(); delay(30);
  P12(); delay(30);
  
  // Second iteration
  P4(); delay(10);
  P3(); delay(10);
  P2(); delay(10);
  P1(); delay(10);
  P12(); delay(30);
  P12(); delay(30);
  
  P4(); delay(10);
  P13(); delay(10);
  P13(); delay(10);
  P4(); delay(10);
  P3(); delay(10);
  P12(); delay(10);
  P12(); delay(10);
  P3(); delay(10);
  P2(); delay(10);
  P3(); delay(10);
  P4(); delay(10);
  P2(); delay(10);
  P1(); delay(10);
  P1(); delay(10);
  P1(); delay(10);
  
  Serial.println("Sequence complete");
}

void music_3() {
  Serial.println("Playing music_3 sequence:");
  
  // First iteration
  P3(); delay(40);
  P3(); delay(40);
  P4(); delay(40);
  P3(); delay(40);
  P6(); delay(40);
  P5(); delay(40);
  P3(); delay(40);
  P3(); delay(40);
  P4(); delay(40);
  P3(); delay(40);
  P7(); delay(40);
  P6(); delay(40);
  
  // Second iteration
  P3(); delay(40);
  P3(); delay(40);
  P11();delay(40);
  P9(); delay(40);
  P7(); delay(40);
  P7(); delay(40);
  P6(); delay(40);
  P5(); delay(40);
  P10();delay(20);
  P10();delay(20);
  P9(); delay(40);
  P7(); delay(40);
  P8(); delay(40);
  P7(); delay(40);
  
  Serial.println("Sequence complete");
}