//
// SensorRecords: queue fixed-size records and process them in batches
//
// Every 50 ms the sketch reads A0 into a Reading record and puts it into a
// RngBuffer of 16 records. Once a second it takes the records out in batches
// of up to 8 and prints the average of each batch.
//
// 20 readings arrive per second but only 16 fit, so the buffer fills up. The
// sketch then drops the oldest record to make room: the buffer always holds
// the newest readings, and about 4 per second are dropped.
//
// Everything runs in loop(), so the built-in lock hooks are enough.
//

#include <RngBuffer.h>

struct Reading {
  uint32_t ms;        // millis() when read
  int16_t  raw;       // analogRead() result
};

const int SENSOR_PIN = A0;
const unsigned long SAMPLE_MS = 50;
const unsigned long REPORT_MS = 1000;
const size_t CAPACITY = 16;
const size_t BATCH = 8;

Reading storage[CAPACITY];
RngBuffer readings;
unsigned long lastSample = 0;
unsigned long lastReport = 0;
unsigned long dropped = 0;

static const char *statusName(RngBufferStatus s) {
  switch ( s ) {
    case RNG_BUFFER_OK:          return "OK";
    case RNG_BUFFER_FULL:        return "FULL";
    case RNG_BUFFER_EMPTY:       return "EMPTY";
    case RNG_BUFFER_LOCK_FAILED: return "LOCK_FAILED";
    case RNG_BUFFER_INVALID:     return "INVALID";
  }
  return "unknown";
}

static void sample() {
  Reading r;
  r.ms = millis();
  r.raw = (int16_t) analogRead(SENSOR_PIN);

  RngBufferStatus s = rng_buffer_put_element(&readings, &r);
  if ( s == RNG_BUFFER_FULL ) {
    Reading oldest;
    rng_buffer_get_element(&readings, &oldest);     // make room for the newest
    dropped++;
    s = rng_buffer_put_element(&readings, &r);
  }
  if ( s != RNG_BUFFER_OK ) {
    Serial.print(F("put failed: "));
    Serial.println(statusName(s));
  }
}

static void report() {
  Reading batch[BATCH];
  size_t n;
  RngBufferStatus s;

  // OK means a whole batch came out and there may be more.
  // EMPTY means the buffer ran out: n holds how many came out (possibly 0).
  do {
    s = rng_buffer_get_elements(&readings, batch, BATCH, &n);
    if ( n == 0 ) break;

    long sum = 0;
    for ( size_t i = 0; i < n; i++ ) sum += batch[i].raw;

    Serial.print(F("batch of "));
    Serial.print((unsigned long) n);
    Serial.print(F(" from "));
    Serial.print((unsigned long) batch[0].ms);
    Serial.print(F(" ms: average "));
    Serial.println(sum / (long) n);
  } while ( s == RNG_BUFFER_OK );

  if ( s != RNG_BUFFER_OK && s != RNG_BUFFER_EMPTY ) {
    Serial.print(F("get failed: "));
    Serial.println(statusName(s));
  }
  Serial.print(F("dropped so far: "));
  Serial.println(dropped);
}

void setup() {
  Serial.begin(115200);
  RngBufferStatus s = rng_buffer_init_elements(&readings, storage, sizeof(Reading), CAPACITY);
  if ( s != RNG_BUFFER_OK ) {
    Serial.print(F("init failed: "));
    Serial.println(statusName(s));
  }
}

void loop() {
  unsigned long now = millis();
  if ( now - lastSample >= SAMPLE_MS ) {
    lastSample = now;
    sample();
  }
  if ( now - lastReport >= REPORT_MS ) {
    lastReport = now;
    report();
  }
}
