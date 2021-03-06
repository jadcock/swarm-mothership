#include <SoftwareSerial.h>
#include "protocol.h"

//#include <Wire.h>
extern "C" {
#include "custom_twi.h"
}

#include "SMC.h"
#include <Servo.h>

// some motor limit IDs
#define FORWARD_ACCELERATION 5
#define REVERSE_ACCELERATION 9
#define DECELERATION 2

#define SLAVE_ADDRESS 0x04
#define SERVO_PIN 5

struct ProtocolFSM {
  Message message;
  Message status;
  Message next_status;

  uint8_t message_bytes;
  uint8_t state;

  // flags
  uint8_t message_handled : 1;
  uint8_t next_status_available : 1;
};

enum {
  P_READY,
  P_RECEIVING,
  P_MESSAGE_WAITING
};


struct MothershipFSM {
  Message current;
  uint32_t start;
  uint8_t state;
};

enum {
  M_IDLE,
  M_EXECUTION,
  M_EXECUTION_COMPLETE,
  M_OVERRIDE,
  M_ERROR
};


Servo servo;
ProtocolFSM pfsm;
MothershipFSM mfsm;

void protocolInit() {
  pfsm.message_bytes = 0;
  pfsm.state = P_READY;
  pfsm.message_handled = false;
  pfsm.next_status_available = false;
  messageInit(&pfsm.status, REPORT_NOOP, 0, 0);
}

// interrupt context
extern "C" {
void twi_onSlaveReceive(uint8_t* inBytes, int numBytes) {
  //interrupts(); //enable all interrupts
  //EIMSK &= 0b11111110; //disable external interrupts

  /*
  Serial.print("receive ");
  Serial.println(byteCount);
  */
  volatile uint8_t* rbuf = (uint8_t*)(&pfsm.message);
  for(int ii = 0; ii < numBytes; ++ii) {
    uint8_t next = inBytes[ii];

    // was the message delivered?
    if(pfsm.state == P_MESSAGE_WAITING && pfsm.message_handled) {
      pfsm.state = P_READY;
      pfsm.message_handled = false;
    }

    if(pfsm.state == P_MESSAGE_WAITING) {
      // we can't handle a new message. drop it on the floor
    } else if(pfsm.state == P_READY) {
      // start receiving a new message
      pfsm.message_bytes = 0;
      pfsm.state = P_RECEIVING;
    }

    if(pfsm.state == P_RECEIVING) {
      // make sure that the byte is of the type we expect
      if(pfsm.message_bytes == 0 && !(next & 0x80)) {
        // drop till we get a command
        Serial.println(F("dropping non-command"));
      } else if(pfsm.message_bytes != 0 && (next & 0x80)) {
        // lost sync, restart
        Serial.println(F("lost sync, restarting"));
        pfsm.message_bytes = 0;
        rbuf[pfsm.message_bytes] = next;
        pfsm.message_bytes++;
      } else {
        rbuf[pfsm.message_bytes] = next;
        pfsm.message_bytes++;
        if(pfsm.message_bytes == sizeof(pfsm.message)) {
          pfsm.state = P_MESSAGE_WAITING;
        }
      }
    }

    /*
    Serial.print("next = ");
    Serial.print(next);
    Serial.print(", state = ");
    Serial.println(pfsm.state);
    */
  }


  //EIMSK |= 0b00000001; //enable external interrupts
}

// interrupt context
void twi_onSlaveTransmit() {
  //interrupts(); //enable all interrupts
  //EIMSK &= 0b11111110; //disable external interrupts

  if(pfsm.next_status_available) {
    memcpy((char*)&pfsm.status, (char*)&pfsm.next_status, sizeof(Message));
    pfsm.next_status_available = false;
  }


  twi_transmit((uint8_t*)(&pfsm.status), sizeof(pfsm.status));
  /*
  Serial.print("sent = ");
  Serial.println(sent);
  */

  //EIMSK |= 0b00000001; //enable external interrupts
}
}

bool protocolHasMessage(volatile Message* msg) {
  if(pfsm.state != P_MESSAGE_WAITING) return false;
  if(pfsm.message_handled) return false;

  memcpy((char*)msg, (char*)&pfsm.message, sizeof(Message));
  pfsm.message_handled = true;
  return true;
}

void protocolSetStatus(volatile Message* msg) {
  if(pfsm.next_status_available) return;

  memcpy((char*)&pfsm.next_status, (char*)msg, sizeof(Message));
  pfsm.next_status_available = true;
}

void protocolSetStatus(MessageType type, uint16_t payload, uint8_t id) {
  if(pfsm.next_status_available) return;

  messageInit(&pfsm.next_status, type, payload, id);
  pfsm.next_status_available = true;
}

void mothershipInit() {
  mfsm.state = M_IDLE;
  mfsm.start = millis();
}

void smcInitialize() {
  SMC.reset();

  SMC.setMotorLimit(FORWARD_ACCELERATION, 4);
  SMC.setMotorLimit(REVERSE_ACCELERATION, 4);
  SMC.setMotorLimit(DECELERATION, 4);

  // clear the safe-start violation and let the motor run
  delay(5);
  SMC.exitSafeStart();
}

void setup()
{
  servo.attach(SERVO_PIN);
  servo.write(90);

  // initialize our FSMs
  protocolInit();
  mothershipInit();

  Serial.begin(115200);    // for debugging (optional)

  twi_setAddress(SLAVE_ADDRESS);
  twi_init();

  /*
  Wire.begin(SLAVE_ADDRESS);
  Wire.onReceive(protocolReceive);
  Wire.onRequest(protocolSend);
  */

  SMC.begin();
  // this lets us read the state of the SMC ERR pin (optional)
  pinMode(errPin, INPUT);

  smcInitialize();
}

/* debugging
int8_t mInitState = -1;
int8_t pInitState = -1;
*/

void loop()
{
  delay(1);

  if (digitalRead(errPin) == HIGH && mfsm.state != M_ERROR) {
    // once all other errors have been fixed,
    // this lets the motors run again
    mfsm.state = M_ERROR;
    mfsm.start = millis();
    uint16_t error = SMC.getVariable(ERROR_STATUS);
    Serial.print(F("Error Status: 0x"));
    Serial.println(error, HEX);
    if(error == 0) {
      // this error is recoverable
      smcInitialize();
    }
    protocolSetStatus(REPORT_MOTOR_ERROR, error, 0);
  } else {
    if(mfsm.state == M_IDLE || mfsm.state == M_EXECUTION_COMPLETE) {
      if(protocolHasMessage(&mfsm.current)) {
        if(mfsm.current.type == COMMAND_SET_MOTION) {
          // payload is -30 to 30, scale to -2000 to 2000
          int16_t speed = ((int16_t)messageSignedPayloadLow(&mfsm.current)) * (3000 / 63);
          int16_t angle = ((int16_t)messageSignedPayloadHigh(&mfsm.current)) + 90;

          /*
          Serial.print("speed = ");
          Serial.print(speed);
          Serial.print(",  angle = ");
          Serial.println(angle);

          if(mfsm.state == M_EXECUTION_COMPLETE) {
            Serial.println("chained!");
          }
          */

          protocolSetStatus(&mfsm.current);
          SMC.setMotorSpeed(speed);
          servo.write(angle);
          mfsm.state = M_EXECUTION;
          mfsm.start = millis();
        } else {
          Serial.print(F("ignoring message "));
          Serial.println(mfsm.current.type);
        }
      } else if(mfsm.state == M_EXECUTION_COMPLETE) {
        // close the command chaining windoow
        SMC.setMotorSpeed(0);
        mfsm.state = M_IDLE;
      }
    } else if(mfsm.state == M_EXECUTION) {
      uint32_t now = millis();
      if(now < mfsm.start || (now - mfsm.start >= COMMAND_DURATION_MS)) {
        mfsm.state = M_EXECUTION_COMPLETE; // open command chaining window
      }
    } else if(mfsm.state == M_ERROR) {
      uint32_t now = millis();
      if(now < mfsm.start || (now - mfsm.start >= 100)) {
        SMC.setMotorSpeed(0);
        mfsm.state = M_IDLE;
        protocolSetStatus(REPORT_NOOP, 0, 0);
      }
      SMC.exitSafeStart();
      SMC.reset();
    }
  }

  /* debugging
  if(mInitState != mfsm.state) {
    Serial.print("mothership state = ");
    Serial.println(mfsm.state);
  }

  if(pInitState != pfsm.state) {
    Serial.print("protocol state = ");
    Serial.println(mfsm.state);
  }

  mInitState = mfsm.state;
  pInitState = pfsm.state;
  */
}
