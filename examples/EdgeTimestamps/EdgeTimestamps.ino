//
// EdgeTimestamps: an interrupt fills the buffer, loop() empties it
//
// Connect a button between EDGE_PIN and GND (the pin uses its pull-up). Every
// change on the pin runs onEdge() in interrupt context, which stores an Edge
// record. loop() takes the records out and prints them. A button bounces, so
// one press often shows several edges a few microseconds apart.
//
// An interrupt and loop() share the buffer, so this sketch defines the lock
// hooks rng_buffer_lock() and rng_buffer_unlock(). Without them the built-in
// hooks do nothing, and a put in the interrupt can corrupt a get in loop().
// Rules for the hooks (see RngBuffer.h):
//   - exactly these signatures, defined after #include <RngBuffer.h> so that
//     they get C linkage in this C++ file
//   - they must work inside an interrupt as well as in loop()
//   - in the sketch or the application's own sources, not in a separate library
//

#include <RngBuffer.h>

#if defined(ARDUINO_ARCH_SAMD)
const int EDGE_PIN = 5;       // has an interrupt on the Zero and on MKR boards (pin 2 does not on MKR)
#else
const int EDGE_PIN = 2;       // Uno: 2 or 3. ESP32 and nRF52: most GPIOs
#endif

struct Edge {
  uint32_t us;                // micros() at the change
  uint8_t  level;             // pin level after the change
};

const size_t CAPACITY = 32;
Edge storage[CAPACITY];
RngBuffer edges;
volatile unsigned long lost = 0;      // edges that found the buffer full


// ---- Lock hooks: a critical section that also works inside an interrupt ----
// The library never nests the lock, and nothing else runs while it is held,
// so one saved state is enough. A critical section cannot time out, so the
// timeout is ignored and the lock always succeeds.

#if defined(ARDUINO_ARCH_ESP32)
// On ESP32 noInterrupts() masks only the calling core. A spinlock critical
// section keeps out the other core as well, and the _SAFE form can be used
// both in a task and in an interrupt.
static portMUX_TYPE lockMux = portMUX_INITIALIZER_UNLOCKED;

bool rng_buffer_lock(unsigned int timeout) {
  (void) timeout;
  portENTER_CRITICAL_SAFE(&lockMux);
  return true;
}

void rng_buffer_unlock(void) {
  portEXIT_CRITICAL_SAFE(&lockMux);
}

#elif defined(__AVR__)
// Save the status register, which holds the global interrupt flag, then disable
// interrupts. Unlock restores the saved flag, so inside an interrupt they stay off.
static uint8_t savedSreg;

bool rng_buffer_lock(unsigned int timeout) {
  (void) timeout;
  uint8_t sreg = SREG;
  cli();
  savedSreg = sreg;
  return true;
}

void rng_buffer_unlock(void) {
  SREG = savedSreg;
}

#elif defined(__arm__)
// Single-core ARM Cortex-M (SAMD, nRF52, STM32 and others): the same with PRIMASK.
// On a dual-core chip such as the RP2040 this does not keep out the other core.
// Some radio stacks restrict disabling interrupts: check your core's rules.
static uint32_t savedPrimask;

bool rng_buffer_lock(unsigned int timeout) {
  (void) timeout;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  savedPrimask = primask;
  return true;
}

void rng_buffer_unlock(void) {
  __set_PRIMASK(savedPrimask);
}

#else
#error "Define rng_buffer_lock() and rng_buffer_unlock() for this platform"
#endif


// Interrupt context: store one record, count it as lost if there is no room
void onEdge() {
  Edge e;
  e.us = micros();
  e.level = (uint8_t) digitalRead(EDGE_PIN);
  if ( rng_buffer_put_element(&edges, &e) != RNG_BUFFER_OK ) lost = lost + 1;
}

// lost is shared with the interrupt and wider than one byte, so read it under
// the same lock: on an 8-bit MCU a plain read could see it half-updated
static unsigned long lostSoFar() {
  rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT);
  unsigned long n = lost;
  rng_buffer_unlock();
  return n;
}

void setup() {
  Serial.begin(115200);

  // Initialize before the interrupt is attached: init takes no lock
  if ( rng_buffer_init_elements(&edges, storage, sizeof(Edge), CAPACITY) != RNG_BUFFER_OK ) {
    Serial.println(F("init failed"));
  }
  pinMode(EDGE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(EDGE_PIN), onEdge, CHANGE);

  Serial.print(F("Press a button on pin "));
  Serial.println(EDGE_PIN);
}

void loop() {
  static uint32_t previousUs = 0;
  static bool havePrevious = false;
  static unsigned long reportedLost = 0;

  Edge e;
  while ( rng_buffer_get_element(&edges, &e) == RNG_BUFFER_OK ) {
    Serial.print(e.level ? F("HIGH at ") : F("LOW at "));
    Serial.print((unsigned long) e.us);
    Serial.print(F(" us"));
    if ( havePrevious ) {
      Serial.print(F(", +"));
      Serial.print((unsigned long) (e.us - previousUs));
      Serial.print(F(" us"));
    }
    Serial.println();
    previousUs = e.us;
    havePrevious = true;
  }

  unsigned long n = lostSoFar();
  if ( n != reportedLost ) {
    Serial.print(F("lost (buffer full): "));
    Serial.println(n - reportedLost);
    reportedLost = n;
  }
}
