//
// SerialLines: collect typed bytes into a line, then hand the line on
//
// Open the Serial Monitor at 115200 baud and type some text. Each byte goes into
// a 32-byte RngBuffer. A newline, or a full buffer, ends the line: the sketch
// takes the bytes out, oldest first, and prints them.
//
// Everything runs in loop(), one thread of execution, so the built-in lock hooks
// (which do nothing) are enough. EdgeTimestamps shows an interrupt and loop()
// sharing a buffer.
//

#include <RngBuffer.h>

uint8_t storage[32];
RngBuffer line;

static void printLine(const char *reason) {
  Serial.print(reason);
  Serial.print(F(", "));
  Serial.print((unsigned long) rng_buffer_count(&line));
  Serial.print(F(" bytes: "));

  uint8_t c;
  while ( rng_buffer_get(&line, &c) ) Serial.write(c);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  if ( !rng_buffer_init(&line, storage, sizeof(storage)) ) {
    Serial.println(F("init failed"));
  }
  Serial.print(F("Type a line of up to "));
  Serial.print((unsigned long) rng_buffer_capacity(&line));
  Serial.println(F(" characters"));
}

void loop() {
  while ( Serial.available() > 0 ) {
    int c = Serial.read();
    if ( c == '\r' ) continue;                  // CR of a CR LF line ending

    if ( c == '\n' ) {
      if ( !rng_buffer_is_empty(&line) ) printLine("line");
      continue;
    }

    rng_buffer_put(&line, (uint8_t) c);
    if ( rng_buffer_is_full(&line) ) printLine("buffer full");
  }
}
