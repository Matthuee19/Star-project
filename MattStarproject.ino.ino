#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Preferences.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define DHTPIN 15
#define DHTTYPE DHT22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MPU6050 mpu;
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences;

const uint8_t PROGMEM PET_SPRITE[] = {
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x3C,0x3C,0x00,
  0x00,0x7E,0x7E,0x00,
  0x00,0xFF,0xFF,0x00,
  0x01,0xE7,0xE7,0x80,
  0x03,0xC3,0xC3,0xC0,
  0x07,0x81,0x81,0xE0,
  0x0F,0x00,0x00,0xF0,
  0x1E,0x18,0x18,0x78,
  0x3C,0x3C,0x3C,0x3C,
  0x78,0x7E,0x7E,0x1E,
  0x70,0xE7,0xE7,0x0E,
  0xF1,0xC3,0xC3,0x8F,
  0xF1,0x81,0x81,0x8F,
  0xF1,0x00,0x00,0x8F,
  0xF1,0x81,0x81,0x8F,
  0xF1,0xC3,0xC3,0x8F,
  0x70,0xE7,0xE7,0x0E,
  0x78,0x7E,0x7E,0x1E,
  0x3C,0x3C,0x3C,0x3C,
  0x1E,0x18,0x18,0x78,
  0x0F,0x00,0x00,0xF0,
  0x07,0x81,0x81,0xE0,
  0x03,0xC3,0xC3,0xC0,
  0x01,0xE7,0xE7,0x80,
  0x00,0xFF,0xFF,0x00,
  0x00,0x7E,0x7E,0x00,
  0x00,0x3C,0x3C,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00
};

struct PetState {
  int joy;
  int energy;
  int fullness;
};

PetState pet;

enum ViewMode {
  PET_VIEW,
  MENU_VIEW,
  STATS_VIEW
};

ViewMode currentView = PET_VIEW;

float accelerationX = 0;
float accelerationY = 0;
float accelerationZ = 0;
float humidity = NAN;
float temperatureC = NAN;

bool mpuFound = false;
bool dhtFound = false;

uint32_t lastMpuReadAt = 0;
uint32_t lastDhtReadAt = 0;
uint32_t shakeAnimationEndsAt = 0;
uint32_t heartAnimationEndsAt = 0;
uint32_t petJumpStartedAt = 0;
uint32_t playRunStartedAt = 0;
uint32_t nappingUntil = 0;
int nappingPetX = 0;

float menuBallX = 64;
float menuBallY = 32;
float menuCenterAccelerationX = 0;
float menuCenterAccelerationY = 0;
int selectedMenuItem = -1;

struct MenuItem {
  const char *label;
  int joyChange;
  int energyChange;
  int fullnessChange;
  int reaction;
};

#define HEART_REACTION 0
#define NAP_REACTION 1
#define RUN_REACTION 2

const MenuItem MENU_ITEMS[] = {
  {"FEED", 5, 0, 20, HEART_REACTION},
  {"PLAY", 10, -5, 0, RUN_REACTION},
  {"NAP", 0, 20, 0, NAP_REACTION},
  {"DANCE", 15, -10, 0, HEART_REACTION}
};

const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MenuItem);

const int MENU_ITEM_X[] = {32, 96, 32, 96};
const int MENU_ITEM_Y[] = {20, 20, 44, 44};

#define STARTING_JOY 50
#define STARTING_ENERGY 50
#define STARTING_FULLNESS 50

#define BUTTON_DEBOUNCE_MS 30
#define MPU_READ_INTERVAL_MS 80
#define DHT_READ_INTERVAL_MS 2000
#define SHAKE_THRESHOLD 3.0f
#define SHAKE_COOLDOWN_MS 800
#define SHAKE_JOY_CHANGE 5
#define SHAKE_ENERGY_CHANGE -3
#define SHAKE_FULLNESS_CHANGE 0
#define STANDARD_GRAVITY 9.81f

#define PET_SPRITE_WIDTH 32
#define PET_SPRITE_HEIGHT 32
#define PET_WALK_PIXEL_MS 60
#define PET_PRE_JUMP_MS 200
#define PET_JUMP_MS 350
#define PET_JUMP_HEIGHT 18
#define HEARTS_DURATION_MS 1600
#define NAP_DURATION_MS 6000
#define PLAY_LAP_MS 900
#define PLAY_LAP_COUNT 3

#define MENU_CENTER_X 64
#define MENU_CENTER_Y 32
#define MENU_TILT_LIMIT 4.0f
#define MENU_CENTER_DEADZONE 0.5f
#define MENU_BALL_X_RANGE 18
#define MENU_BALL_Y_RANGE 12
#define MENU_X_DIRECTION 1.0f
#define MENU_Y_DIRECTION 1.0f
#define SWAP_MPU_AXES false
#define RESET_SAVED_PET_ON_BOOT false

struct ButtonState {
  int pin;
  bool stableState;
  bool lastRawState;
  uint32_t lastChangedAt;
};

ButtonState buttonOne = {12, HIGH, HIGH, 0};
ButtonState buttonTwo = {14, HIGH, HIGH, 0};
bool readButton(ButtonState &btn) {
  bool rawState = digitalRead(btn.pin);
  uint32_t now = millis();

  if (rawState != btn.lastRawState) {
    btn.lastChangedAt = now;
    btn.lastRawState = rawState;
  }

  if ((now - btn.lastChangedAt) > BUTTON_DEBOUNCE_MS) {
    btn.stableState = rawState;
  }

  return btn.stableState == LOW;
}

void loadPetState() {
  preferences.begin("pet", false);

  if (RESET_SAVED_PET_ON_BOOT) {
    pet.joy = STARTING_JOY;
    pet.energy = STARTING_ENERGY;
    pet.fullness = STARTING_FULLNESS;
  } else {
    pet.joy = preferences.getInt("joy", STARTING_JOY);
    pet.energy = preferences.getInt("energy", STARTING_ENERGY);
    pet.fullness = preferences.getInt("full", STARTING_FULLNESS);
  }

  preferences.end();
}

void savePetState() {
  preferences.begin("pet", false);
  preferences.putInt("joy", pet.joy);
  preferences.putInt("energy", pet.energy);
  preferences.putInt("full", pet.fullness);
  preferences.end();
}

void clampPetStats() {
  pet.joy = constrain(pet.joy, 0, 100);
  pet.energy = constrain(pet.energy, 0, 100);
  pet.fullness = constrain(pet.fullness, 0, 100);
}

void applyMenuItem(const MenuItem &item) {
  pet.joy += item.joyChange;
  pet.energy += item.energyChange;
  pet.fullness += item.fullnessChange;
  clampPetStats();

  uint32_t now = millis();

  if (item.reaction == HEART_REACTION) {
    heartAnimationEndsAt = now + HEARTS_DURATION_MS;
  } else if (item.reaction == NAP_REACTION) {
    nappingUntil = now + NAP_DURATION_MS;
    nappingPetX = random(0, SCREEN_WIDTH - PET_SPRITE_WIDTH);
  } else if (item.reaction == RUN_REACTION) {
    playRunStartedAt = now;
  }

  savePetState();
}

bool isNapping() {
  return millis() < nappingUntil;
}

bool isPlaying() {
  uint32_t now = millis();
  return (playRunStartedAt != 0 &&
          now < playRunStartedAt + PLAY_LAP_MS * PLAY_LAP_COUNT);
}

int petWalkingX(uint32_t now) {
  return (now / PET_WALK_PIXEL_MS) % (SCREEN_WIDTH - PET_SPRITE_WIDTH);
}

int playRunX(uint32_t now) {
  uint32_t elapsed = now - playRunStartedAt;
  int lap = elapsed / PLAY_LAP_MS;
  int lapProgress = elapsed % PLAY_LAP_MS;
  int x = map(lapProgress, 0, PLAY_LAP_MS, 0, SCREEN_WIDTH - PET_SPRITE_WIDTH);

  if (lap % 2 == 1) {
    x = (SCREEN_WIDTH - PET_SPRITE_WIDTH) - x;
  }

  if (lap >= PLAY_LAP_COUNT) {
    playRunStartedAt = 0;
  }

  return x;
}

void readMpu() {
  uint32_t now = millis();
  if (now - lastMpuReadAt < MPU_READ_INTERVAL_MS) {
    return;
  }
  lastMpuReadAt = now;

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  accelerationX = accel.acceleration.x;
  accelerationY = accel.acceleration.y;
  accelerationZ = accel.acceleration.z;

  if (SWAP_MPU_AXES) {
    float tempX = accelerationX;
    accelerationX = accelerationY;
    accelerationY = tempX;
  }

  float totalAcceleration =
      sqrt(accelerationX * accelerationX + accelerationY * accelerationY +
           accelerationZ * accelerationZ);

  float delta = fabs(totalAcceleration - STANDARD_GRAVITY);

  if (delta > SHAKE_THRESHOLD) {
    uint32_t now = millis();
    if (now > shakeAnimationEndsAt) {
      shakeAnimationEndsAt = now + SHAKE_COOLDOWN_MS;
      pet.joy += SHAKE_JOY_CHANGE;
      pet.energy += SHAKE_ENERGY_CHANGE;
      pet.fullness += SHAKE_FULLNESS_CHANGE;
      clampPetStats();
      savePetState();
    }
  }
}

void readDht() {
  uint32_t now = millis();
  if (now - lastDhtReadAt < DHT_READ_INTERVAL_MS) {
    return;
  }
  lastDhtReadAt = now;

  humidity = dht.readHumidity();
  temperatureC = dht.readTemperature();
}

void updateMenuBall() {
  float ax = accelerationX;
  float ay = accelerationY;

  if (fabs(ax) < MENU_CENTER_DEADZONE) {
    ax = 0;
  }
  if (fabs(ay) < MENU_CENTER_DEADZONE) {
    ay = 0;
  }

  ax = constrain(ax, -MENU_TILT_LIMIT, MENU_TILT_LIMIT);
  ay = constrain(ay, -MENU_TILT_LIMIT, MENU_TILT_LIMIT);

  menuBallX = MENU_CENTER_X + ax * MENU_BALL_X_RANGE * MENU_X_DIRECTION;
  menuBallY = MENU_CENTER_Y + ay * MENU_BALL_Y_RANGE * MENU_Y_DIRECTION;

  selectedMenuItem = -1;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int dx = menuBallX - MENU_ITEM_X[i];
    int dy = menuBallY - MENU_ITEM_Y[i];
    if (abs(dx) < 14 && abs(dy) < 10) {
      selectedMenuItem = i;
      break;
    }
  }
}
void drawPet(int x, int y) {
  display.drawBitmap(x, y, PET_SPRITE, PET_SPRITE_WIDTH, PET_SPRITE_HEIGHT, 1);
}

void drawHearts() {
  uint32_t now = millis();
  if (now > heartAnimationEndsAt) return;

  int elapsed = heartAnimationEndsAt - now;
  int offset = map(elapsed, 0, HEARTS_DURATION_MS, 0, 12);

  display.setTextSize(1);
  display.setTextColor(1);

  display.setCursor(20, 10 - offset);
  display.print("<3");

  display.setCursor(90, 10 - offset);
  display.print("<3");
}

void drawNap() {
  display.setTextSize(2);
  display.setTextColor(1);
  display.setCursor(nappingPetX, 20);
  display.print("Z");
  display.setCursor(nappingPetX + 12, 12);
  display.print("Z");
  display.setCursor(nappingPetX + 24, 4);
  display.print("Z");
}

void drawPlayRun() {
  uint32_t now = millis();
  int x = playRunX(now);
  drawPet(x, 16);
}

void drawStats() {
  display.setTextSize(1);
  display.setTextColor(1);

  display.setCursor(0, 0);
  display.print("JOY: ");
  display.print(pet.joy);

  display.setCursor(0, 12);
  display.print("ENG: ");
  display.print(pet.energy);

  display.setCursor(0, 24);
  display.print("FUL: ");
  display.print(pet.fullness);

  display.setCursor(0, 36);
  display.print("TEMP: ");
  if (isnan(temperatureC)) display.print("--");
  else display.print(temperatureC);

  display.setCursor(0, 48);
  display.print("HUM: ");
  if (isnan(humidity)) display.print("--");
  else display.print(humidity);
}

void drawMenu() {
  display.setTextSize(1);
  display.setTextColor(1);

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int x = MENU_ITEM_X[i] - 12;
    int y = MENU_ITEM_Y[i] - 6;

    if (selectedMenuItem == i) {
      display.drawRect(x - 2, y - 2, 40, 16, 1);
    }

    display.setCursor(x, y);
    display.print(MENU_ITEMS[i].label);
  }

  display.fillCircle(menuBallX, menuBallY, 4, 1);
}

void drawPetView() {
  uint32_t now = millis();

  if (isNapping()) {
    drawNap();
    return;
  }

  if (isPlaying()) {
    drawPlayRun();
    return;
  }

  int x = petWalkingX(now);
  drawPet(x, 16);

  drawHearts();
}

void setup() {
  pinMode(buttonOne.pin, INPUT_PULLUP);
  pinMode(buttonTwo.pin, INPUT_PULLUP);

  Wire.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  dht.begin();
  delay(100);

  if (mpu.begin()) {
    mpuFound = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  loadPetState();
}

void loop() {
  readMpu();
  readDht();

  bool b1 = readButton(buttonOne);
  bool b2 = readButton(buttonTwo);

  if (b1 && b2) {
    currentView = STATS_VIEW;
  } else if (b1) {
    currentView = PET_VIEW;
  } else if (b2) {
    currentView = MENU_VIEW;
  }

  if (currentView == MENU_VIEW) {
    updateMenuBall();
    if (selectedMenuItem >= 0 && b1) {
      applyMenuItem(MENU_ITEMS[selectedMenuItem]);
    }
  }

  display.clearDisplay();

  if (currentView == PET_VIEW) {
    drawPetView();
  } else if (currentView == MENU_VIEW) {
    drawMenu();
  } else if (currentView == STATS_VIEW) {
    drawStats();
  }

  display.display();
}