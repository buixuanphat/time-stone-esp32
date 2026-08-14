#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int POT_PIN = 4;
const int BUZZER_PIN = 26;
const int BTN_PIN = 25;

// các chế độ chính
enum AppMode
{
  MODE_MENU,
  MODE_COUNTDOWN,
  MODE_STOPWATCH,
  MODE_FOOD_PICKER
};

AppMode currentMode = MODE_MENU;
int menuIndex = 0;

// Trạng thái cho Đếm ngược & Bấm giờ
enum SubState
{
  IDLE,
  RUNNING,
  PAUSED,
  FINISHED
};

SubState subState = IDLE;

// --- BIẾN CHO ĐẾM NGƯỢC ---
int cdTotalSeconds = 0;
int cdRemainingSeconds = 0;
unsigned long cdPreviousMillis = 0;

// --- BIẾN CHO BẤM GIỜ ---
unsigned long swStartTime = 0;
unsigned long swElapsedTime = 0;

// --- BIẾN CHO RANDOM MÓN ĂN ---
const char *foodList[] = {
    "Com Tam", "Pho Bo", "Bun Cha", "Bun Dau Mam Tom",
    "Hu Tieu", "Mi Quang", "Ga Ran", "Lau Thai",
    "Banh Mi", "Tra Sua", "Bun Bo Hue", "Com Ga"};
const int totalFoods = sizeof(foodList) / sizeof(foodList[0]);
int currentFoodIndex = 0;
bool isSpinning = false;

// --- XỬ LÝ NÚT NHẤN SMART ---
bool lastBtnState = LOW;
unsigned long btnPressTime = 0;
const unsigned long DEBOUNCE_TIME = 50;
const unsigned long LONG_PRESS_TIME = 1200; // Nhấn giữ > 1.2s để thoát về Menu
bool isPressing = false;
bool longPressTriggered = false;

void playBeep(int duration = 50)
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void setup()
{
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  randomSeed(analogRead(0)); // Khởi tạo seed cho random

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("Khong tim thay man hinh OLED!"));
    for (;;)
      ;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

// -------------------------------------------------------------------
// XỬ LÝ NÚT NHẤN SỰ KIỆN
// -------------------------------------------------------------------
void handleButton()
{
  bool currentBtnState = digitalRead(BTN_PIN);

  if (currentBtnState == HIGH && lastBtnState == LOW && !isPressing)
  {
    btnPressTime = millis();
    isPressing = true;
    longPressTriggered = false;
  }

  // Khi đang GIỮ NÚT (Nhấn giữ long press)
  if (isPressing && currentBtnState == HIGH)
  {
    if ((millis() - btnPressTime >= LONG_PRESS_TIME) && !longPressTriggered)
    {
      longPressTriggered = true;

      if (currentMode != MODE_MENU)
      {
        // Thoát từ bất kỳ chức năng nào về Menu chính
        currentMode = MODE_MENU;
        subState = IDLE;
        playBeep(100);
      }
    }
  }

  // Khi NHẢ NÚT (Short Press)
  if (currentBtnState == LOW && lastBtnState == HIGH && isPressing)
  {
    unsigned long pressDuration = millis() - btnPressTime;
    isPressing = false;

    if (!longPressTriggered && pressDuration > DEBOUNCE_TIME)
    {
      playBeep(30);

      // 1. Ở MENU CHÍNH -> Chọn ứng dụng
      if (currentMode == MODE_MENU)
      {
        if (menuIndex == 0)
          currentMode = MODE_COUNTDOWN;
        else if (menuIndex == 1)
          currentMode = MODE_STOPWATCH;
        else if (menuIndex == 2)
          currentMode = MODE_FOOD_PICKER;

        subState = IDLE;
        swElapsedTime = 0;
      }
      // 2. Ở CHẾ ĐỘ ĐẾM NGƯỢC
      else if (currentMode == MODE_COUNTDOWN)
      {
        if (subState == IDLE || subState == PAUSED)
        {
          if (cdRemainingSeconds > 0)
          {
            subState = RUNNING;
            cdPreviousMillis = millis();
          }
        }
        else if (subState == RUNNING)
        {
          subState = PAUSED;
        }
        else if (subState == FINISHED)
        {
          subState = IDLE;
        }
      }
      // 3. Ở CHẾ ĐỘ BẤM GIỜ
      else if (currentMode == MODE_STOPWATCH)
      {
        if (subState == IDLE || subState == PAUSED)
        {
          subState = RUNNING;
          swStartTime = millis() - swElapsedTime;
        }
        else if (subState == RUNNING)
        {
          subState = PAUSED;
          swElapsedTime = millis() - swStartTime;
        }
      }
      // 4. Ở CHẾ ĐỘ RANDOM MÓN ĂN
      else if (currentMode == MODE_FOOD_PICKER)
      {
        isSpinning = true; // Kích hoạt hiệu ứng quay ngẫu nhiên
      }
    }
  }

  lastBtnState = currentBtnState;
}

// -------------------------------------------------------------------
// CÁC HÀM HIỂN THỊ OLED
// -------------------------------------------------------------------
void drawMenu()
{
  int potVal = analogRead(POT_PIN);
  menuIndex = map(potVal, 0, 4095, 0, 2); // Divide into 3 items

  display.setTextSize(1);
  display.setCursor(20, 0);
  display.println("=== MAIN MENU ===");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  const char *items[] = {"1. Dem nguoc", "2. Bam gio", "3. Random mon an"};

  for (int i = 0; i < 3; i++)
  {
    int y = 18 + i * 15;
    if (i == menuIndex)
    {
      display.fillRect(0, y - 2, 128, 13, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    }
    else
    {
      display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    }
    display.setCursor(5, y);
    display.print(items[i]);
  }
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
}

void drawCountdown()
{
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (subState == IDLE)
  {
    int potValue = analogRead(POT_PIN);
    int minutes = map(potValue, 0, 4095, 0, 60);
    cdTotalSeconds = minutes * 60;
    cdRemainingSeconds = cdTotalSeconds;
    display.print("SET TIME (Hold=Menu)");
  }
  else if (subState == RUNNING)
  {
    display.print("COUNTING...");
  }
  else if (subState == PAUSED)
  {
    display.print("PAUSED");
  }
  else if (subState == FINISHED)
  {
    display.print("TIME'S UP!");
  }

  int mins = cdRemainingSeconds / 60;
  int secs = cdRemainingSeconds % 60;
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", mins, secs);

  display.setTextSize(3);
  display.setCursor(18, 25);
  display.print(timeStr);
}

void drawStopwatch()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("STOPWATCH (Hold=Menu)");

  unsigned long currentMillis = millis();
  unsigned long elapsed = swElapsedTime;

  if (subState == RUNNING)
  {
    elapsed = currentMillis - swStartTime;
  }

  int mins = (elapsed / 60000) % 60;
  int secs = (elapsed / 1000) % 60;
  int ms = (elapsed % 1000) / 10; // Lấy 2 chữ số miligiây

  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", mins, secs, ms);

  display.setTextSize(2);
  display.setCursor(16, 28);
  display.print(timeStr);
}

void drawFoodPicker()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HOM NAY AN GI?");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Nếu bấm nút -> chạy hiệu ứng quay slot ngẫu nhiên
  if (isSpinning)
  {
    for (int i = 0; i < 15; i++)
    {
      currentFoodIndex = random(0, totalFoods);
      display.fillRect(0, 20, 128, 44, SSD1306_BLACK);

      display.setTextSize(2);
      display.setCursor(10, 30);
      display.print(foodList[currentFoodIndex]);
      display.display();

      digitalWrite(BUZZER_PIN, HIGH);
      delay(30 + i * 15); // Quay chậm dần
      digitalWrite(BUZZER_PIN, LOW);
    }
    isSpinning = false;
  }
  else
  {
    // Nếu không bấm quay, cho phép duyệt danh sách bằng biến trở
    if (subState == IDLE)
    {
      int potVal = analogRead(POT_PIN);
      currentFoodIndex = map(potVal, 0, 4095, 0, totalFoods - 1);
    }

    display.setTextSize(2);
    display.setCursor(5, 28);
    display.print(foodList[currentFoodIndex]);

    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print("Bam nut: Random ngay!");
  }
}

// -------------------------------------------------------------------
// LOOP CHÍNH
// -------------------------------------------------------------------
void loop()
{
  handleButton();

  // Logic Đếm ngược
  if (currentMode == MODE_COUNTDOWN && subState == RUNNING)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - cdPreviousMillis >= 1000)
    {
      cdPreviousMillis = currentMillis;
      if (cdRemainingSeconds > 0)
      {
        cdRemainingSeconds--;
      }
      else
      {
        subState = FINISHED;
      }
    }
  }

  // Còi báo khi đếm ngược hoàn tất
  if (currentMode == MODE_COUNTDOWN && subState == FINISHED)
  {
    if ((millis() / 250) % 2 == 0)
    {
      digitalWrite(BUZZER_PIN, HIGH);
    }
    else
    {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
  else
  {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Cập nhật màn hình OLED theo Chế độ hiện tại
  display.clearDisplay();
  switch (currentMode)
  {
  case MODE_MENU:
    drawMenu();
    break;
  case MODE_COUNTDOWN:
    drawCountdown();
    break;
  case MODE_STOPWATCH:
    drawStopwatch();
    break;
  case MODE_FOOD_PICKER:
    drawFoodPicker();
    break;
  }
  display.display();
}