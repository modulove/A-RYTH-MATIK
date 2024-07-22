#include <EncoderButton.h>
#include "src/libmodulove/arythmatik.h"

using namespace modulove;
using namespace arythmatik;

// Define pins for the encoder
#define ENCODER_A 2
#define ENCODER_B 3
#define ENCODER_BTN 4

// Declare A-RYTH-MATIK hardware variable.
Arythmatik hw;

// reverse encoder
#ifdef ENCODER_REVERSED
EncoderButton encoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_SW_PIN);
#else
EncoderButton encoder(ENCODER_PIN2, ENCODER_PIN1, ENCODER_SW_PIN);
#endif


// Define paddle and ball movement rates
const unsigned long PADDLE_RATE = 33;
const unsigned long BALL_RATE = 16;
const uint8_t PADDLE_HEIGHT = 24;

// Define screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Initialize display with I2C (libModulove uses I2C)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Define initial ball and paddle positions
uint8_t ball_x = 64, ball_y = 32;
uint8_t ball_dir_x = 1, ball_dir_y = 1;
unsigned long ball_update;
unsigned long paddle_update;
const uint8_t CPU_X = 12;
uint8_t cpu_y = 16;
const uint8_t PLAYER_X = 115;
uint8_t player_y = 16;


void drawCourt() {
  hw.display.drawRect(0, 0, 128, 64, WHITE);
}

void onEncoderRotation(EncoderButton &eb) {
  int increment = encoder.increment();  // Get the incremental change
  player_y = constrain(player_y + increment, 1, SCREEN_HEIGHT - PADDLE_HEIGHT);
}

void onEncoderClicked(EncoderButton &eb) {
  // Start the game or release the ball
  ball_dir_x = (random(0, 2) * 2) - 1;  // Random direction (-1 or 1)
  ball_dir_y = (random(0, 2) * 2) - 1;
}

void setup() {
  // Initialize A-RYTH-MATIK hardware
  hw.Init();

  // Initialize display
  hw.display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  hw.display.clearDisplay();
  drawCourt();
  hw.display.display();

  // Set up encoder parameters
  encoder.setDebounceInterval(5);
  encoder.setMultiClickInterval(70);
  encoder.setLongClickDuration(400);
  encoder.setRateLimit(10);
  encoder.setIdleTimeout(1000);
  encoder.setEncoderHandler(onEncoderRotation);
  encoder.setClickHandler(onEncoderClicked);

  // Wait for 2 seconds before starting
  delay(2000);

  // Initialize ball and paddle update times
  ball_update = millis();
  paddle_update = ball_update;
}

void loop() {
  // Process A-RYTH-MATIK inputs
  hw.ProcessInputs();

  // Update encoder state
  encoder.update();

  bool update = false;
  unsigned long time = millis();

  if (time > ball_update) {
    uint8_t new_x = ball_x + ball_dir_x;
    uint8_t new_y = ball_y + ball_dir_y;

    // Check for wall collisions
    if (new_x == 0) {  // Player 2 (right side) scores
      ball_dir_x = -ball_dir_x;
      new_x += ball_dir_x + ball_dir_x;
      hw.outputs[0].High();  // Trigger channel 1
      hw.outputs[5].High();  // Trigger channel 6
      delay(12);             // Short delay to ensure the trigger is registered
      hw.outputs[0].Low();
      hw.outputs[5].Low();
    }
    if (new_x == 127) {  // Player 1 (left side) scores
      ball_dir_x = -ball_dir_x;
      new_x += ball_dir_x + ball_dir_x;
      hw.outputs[1].High();  // Trigger channel 2
      hw.outputs[5].High();  // Trigger channel 6
      delay(12);             // Short delay to ensure the trigger is registered
      hw.outputs[1].Low();
      hw.outputs[5].Low();
    }

    // Check if we hit the horizontal walls.
    if (new_y == 0 || new_y == 63) {  // Ball hits top or bottom wall
      ball_dir_y = -ball_dir_y;
      new_y += ball_dir_y + ball_dir_y;
      hw.outputs[2].High();  // Trigger channel 3
      hw.outputs[5].High();  // Trigger channel 6
      delay(12);             // Short delay to ensure the trigger is registered
      hw.outputs[2].Low();
      hw.outputs[5].Low();
    }

    // Check for paddle collisions
    if (new_x == CPU_X && new_y >= cpu_y && new_y <= cpu_y + PADDLE_HEIGHT) {
      ball_dir_x = -ball_dir_x;
      new_x += ball_dir_x + ball_dir_x;
      hw.outputs[3].High();  // Trigger channel 4
      hw.outputs[5].High();  // Trigger channel 6
      delay(12);             // Short delay to ensure the trigger is registered
      hw.outputs[3].Low();
      hw.outputs[5].Low();
    }
    if (new_x == PLAYER_X && new_y >= player_y && new_y <= player_y + PADDLE_HEIGHT) {
      ball_dir_x = -ball_dir_x;
      new_x += ball_dir_x + ball_dir_x;
      hw.outputs[4].High();  // Trigger channel 5
      hw.outputs[5].High();  // Trigger channel 6
      delay(12);             // Short delay to ensure the trigger is registered
      hw.outputs[4].Low();
      hw.outputs[5].Low();
    }

    // Update ball position
    hw.display.drawPixel(ball_x, ball_y, BLACK);
    hw.display.drawPixel(new_x, new_y, WHITE);
    //hw.display.drawCircle(ball_x, ball_y, 3, BLACK);
    //hw.display.drawCircle(new_x, new_y, 3, WHITE);
    ball_x = new_x;
    ball_y = new_y;

    ball_update += BALL_RATE;
    update = true;
  }

  if (time > paddle_update) {
    paddle_update += PADDLE_RATE;

    // Update CPU paddle position
    hw.display.drawFastVLine(CPU_X, cpu_y, PADDLE_HEIGHT, BLACK);
    const uint8_t half_paddle = PADDLE_HEIGHT >> 1;
    if (cpu_y + half_paddle > ball_y) {
      cpu_y -= 1;
    }
    if (cpu_y + half_paddle < ball_y) {
      cpu_y += 1;
    }
    if (cpu_y < 1) cpu_y = 1;
    if (cpu_y + PADDLE_HEIGHT > 63) cpu_y = 63 - PADDLE_HEIGHT;
    hw.display.drawFastVLine(CPU_X, cpu_y, PADDLE_HEIGHT, WHITE);

    // Update player paddle position
    hw.display.drawFastVLine(PLAYER_X, player_y, PADDLE_HEIGHT, BLACK);
    if (player_y < 1) player_y = 1;
    if (player_y + PADDLE_HEIGHT > 63) player_y = 63 - PADDLE_HEIGHT;
    hw.display.drawFastVLine(PLAYER_X, player_y, PADDLE_HEIGHT, WHITE);

    update = true;
  }

  if (update) {
    hw.display.display();
  }
}
