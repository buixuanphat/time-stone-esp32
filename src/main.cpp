
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ChronosESP32.h>

// ==========================================
// 1. HARDWARE
// ==========================================

#define BTN_UP_PIN   2
#define BTN_DOWN_PIN 3

#define LED_PIN 1

#define OLED_SDA 8
#define OLED_SCL 9

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0,
  U8X8_PIN_NONE
);

ChronosESP32 watch("ESP32-C3 Mini");

// ==========================================
// 2. NOTIFICATION STORAGE
// ==========================================

const int MAX_NOTIFY = 10;

struct NotifyItem {
  String app;
  String title;
  String msg;
  unsigned long timestamp;
};

NotifyItem notifyList[MAX_NOTIFY];

int notifyCount = 0;
int notifyHead = 0;
int currentIdx = 0;

bool showNotify = false;

// ==========================================
// 3. BUTTON SETTINGS
// ==========================================

const unsigned long DEBOUNCE_TIME = 40;

bool upLast = HIGH;
bool downLast = HIGH;

bool upStable = HIGH;
bool downStable = HIGH;

unsigned long upTimer = 0;
unsigned long downTimer = 0;

// Long press
const unsigned long LONG_PRESS_TIME = 2000;

bool upLongPressHandled = false;
unsigned long upPressStart = 0;

// Sleep mode
bool sleepMode = false;

// ==========================================
// 4. LED SETTINGS
// ==========================================

const unsigned long LED_BLINK_INTERVAL = 250;
const unsigned long LED_ALERT_DURATION = 10000;

bool ledBlinkActive = false;
bool ledState = false;

unsigned long ledLastToggle = 0;
unsigned long ledAlertStart = 0;

// ==========================================
// 5. MESSAGE SCROLL SETTINGS
// ==========================================

int messagePage = 0;
int totalPages = 1;

unsigned long lastPageTime = 0;

const unsigned long PAGE_INTERVAL = 2500;

// ==========================================
// 6. AUTO DELETE SETTINGS
// ==========================================

const unsigned long NOTIFY_MAX_AGE =
  2UL * 60UL * 60UL * 1000UL;

const unsigned long CLEANUP_INTERVAL = 60000UL;

unsigned long lastCleanupTime = 0;

// ==========================================
// 7. GET NOTIFICATION
// ==========================================

NotifyItem getNotifyAtOffset(int offset) {

  if (notifyCount == 0) {
    return {"", "", "", 0};
  }

  int index =
    (notifyHead - 1 - offset + MAX_NOTIFY) % MAX_NOTIFY;

  return notifyList[index];
}

// ==========================================
// 8. RECEIVE NOTIFICATION
// ==========================================

void notificationCallback(Notification notification) {

  notifyList[notifyHead] = {
    notification.app,
    notification.title,
    notification.message,
    millis()
  };

  notifyHead = (notifyHead + 1) % MAX_NOTIFY;

  if (notifyCount < MAX_NOTIFY) {
    notifyCount++;
  }

  // Mở thông báo mới nhất
  currentIdx = 0;
  messagePage = 0;

  showNotify = true;
  lastPageTime = millis();

  // Kích hoạt LED báo thông báo mới
  ledBlinkActive = true;
  ledAlertStart = millis();
  ledLastToggle = millis();

  ledState = true;
  digitalWrite(LED_PIN, HIGH);
}

// ==========================================
// 9. LED CONTROL
// ==========================================

void updateLED() {

  unsigned long now = millis();

  // Sleep thì tắt LED
  if (sleepMode) {

    digitalWrite(LED_PIN, LOW);

    ledState = false;

    return;
  }

  // Không có thông báo mới
  if (!ledBlinkActive) {

    digitalWrite(LED_PIN, LOW);

    ledState = false;

    return;
  }

  // Hết thời gian báo hiệu
  if (now - ledAlertStart >= LED_ALERT_DURATION) {

    ledBlinkActive = false;

    ledState = false;

    digitalWrite(LED_PIN, LOW);

    return;
  }

  // Chớp LED mỗi 250ms
  if (now - ledLastToggle >= LED_BLINK_INTERVAL) {

    ledLastToggle = now;

    ledState = !ledState;

    digitalWrite(
      LED_PIN,
      ledState ? HIGH : LOW
    );
  }
}

// ==========================================
// 10. DELETE EXPIRED NOTIFICATIONS
// ==========================================

void deleteExpiredNotifications() {

  unsigned long now = millis();

  if (now - lastCleanupTime < CLEANUP_INTERVAL) {
    return;
  }

  lastCleanupTime = now;

  if (notifyCount == 0) return;

  NotifyItem temp[MAX_NOTIFY];

  int newCount = 0;
  int removedBeforeCurrent = 0;

  // Duyệt từ cũ nhất đến mới nhất
  for (int i = notifyCount - 1; i >= 0; i--) {

    NotifyItem item = getNotifyAtOffset(i);

    bool expired =
      (unsigned long)(now - item.timestamp)
      >= NOTIFY_MAX_AGE;

    if (expired) {

      // Đếm thông báo cũ hơn thông báo đang xem
      if (i > currentIdx) {
        removedBeforeCurrent++;
      }

    } else {

      temp[newCount++] = item;
    }
  }

  if (newCount == notifyCount) return;

  // Xây dựng lại danh sách theo thứ tự
  notifyCount = 0;

  for (int i = 0; i < newCount; i++) {

    notifyList[i] = temp[newCount - 1 - i];

    notifyCount++;
  }

  notifyHead = notifyCount % MAX_NOTIFY;

  // Điều chỉnh vị trí thông báo đang xem
  if (notifyCount == 0) {

    currentIdx = 0;
    showNotify = false;
    messagePage = 0;

  } else {

    currentIdx -= removedBeforeCurrent;

    currentIdx = constrain(
      currentIdx,
      0,
      notifyCount - 1
    );

    if (showNotify) {

      messagePage = 0;
      lastPageTime = millis();
    }
  }
}

// ==========================================
// 11. UTF-8 READER
// ==========================================

String getUTF8Char(const String &str, int &pos) {

  if (pos >= str.length()) return "";

  uint8_t c = (uint8_t)str[pos];

  int len = 1;

  if ((c & 0xE0) == 0xC0) {
    len = 2;
  }
  else if ((c & 0xF0) == 0xE0) {
    len = 3;
  }
  else if ((c & 0xF8) == 0xF0) {
    len = 4;
  }

  String result;

  for (int i = 0; i < len && pos < str.length(); i++) {
    result += str[pos++];
  }

  return result;
}

// ==========================================
// 12. TEXT WRAPPING ENGINE
// ==========================================

void printMessageLine(
  const String &line,
  int lineNumber,
  int page,
  int firstY,
  int lineHeight,
  int maxLines
) {

  int firstLine = page * maxLines;

  if (
    lineNumber >= firstLine &&
    lineNumber < firstLine + maxLines
  ) {

    int screenLine = lineNumber - firstLine;

    u8g2.drawUTF8(
      2,
      firstY + screenLine * lineHeight,
      line.c_str()
    );
  }
}

void processWord(
  String word,
  String &line,
  int &lineNumber,
  int page,
  int maxWidth,
  int firstY,
  int lineHeight,
  int maxLines
) {

  if (word.length() == 0) return;

  int pos = 0;

  while (pos < word.length()) {

    String remaining = word.substring(pos);

    String test = line.length() == 0
      ? remaining
      : line + " " + remaining;

    if (u8g2.getUTF8Width(test.c_str()) <= maxWidth) {

      line = test;

      return;
    }

    if (line.length() > 0) {

      printMessageLine(
        line, lineNumber, page,
        firstY, lineHeight, maxLines
      );

      lineNumber++;

      line = "";

      continue;
    }

    String part = "";

    int scan = pos;

    while (scan < word.length()) {

      int oldPos = scan;

      String ch = getUTF8Char(word, scan);

      String candidate = part + ch;

      if (
        u8g2.getUTF8Width(candidate.c_str()) > maxWidth &&
        part.length() > 0
      ) {
        break;
      }

      part = candidate;

      if (scan == oldPos) break;
    }

    if (part.length() == 0) break;

    pos += part.length();

    line = part;

    if (pos < word.length()) {

      printMessageLine(
        line, lineNumber, page,
        firstY, lineHeight, maxLines
      );

      lineNumber++;

      line = "";
    }
  }
}

int drawWrappedMessage(
  String text,
  int page,
  int maxWidth,
  int firstY,
  int lineHeight,
  int maxLines
) {

  String line = "";
  String word = "";

  int lineNumber = 0;
  int pos = 0;

  while (pos < text.length()) {

    String ch = getUTF8Char(text, pos);

    if (
      ch == " " ||
      ch == "\t" ||
      ch == "\n" ||
      ch == "\r"
    ) {

      if (word.length() > 0) {

        processWord(
          word, line, lineNumber, page,
          maxWidth, firstY, lineHeight, maxLines
        );

        word = "";
      }

      if (ch == "\n") {

        if (line.length() > 0) {

          printMessageLine(
            line, lineNumber, page,
            firstY, lineHeight, maxLines
          );

          lineNumber++;

          line = "";
        }
      }

    } else {

      word += ch;
    }
  }

  if (word.length() > 0) {

    processWord(
      word, line, lineNumber, page,
      maxWidth, firstY, lineHeight, maxLines
    );
  }

  if (line.length() > 0) {

    printMessageLine(
      line, lineNumber, page,
      firstY, lineHeight, maxLines
    );

    lineNumber++;
  }

  return lineNumber;
}

// ==========================================
// 13. CLOCK SCREEN
// ==========================================

void drawClock() {

  if (watch.isConnected()) {
    u8g2.drawDisc(5, 6, 3);
  } else {
    u8g2.drawCircle(5, 6, 3);
  }

  if (notifyCount > 0) {

    u8g2.setFont(u8g2_font_6x10_tr);

    char count[4];

    sprintf(count, "%d", notifyCount);

    int width = u8g2.getStrWidth(count);

    u8g2.setCursor(125 - width, 11);

    u8g2.print(count);
  }

  char timeStr[9];

  sprintf(
    timeStr,
    "%02d:%02d:%02d",
    watch.getHourC(),
    watch.getMinute(),
    watch.getSecond()
  );

  u8g2.setFont(u8g2_font_logisoso24_tn);

  int width = u8g2.getStrWidth(timeStr);

  u8g2.setCursor((128 - width) / 2, 45);

  u8g2.print(timeStr);
}

// ==========================================
// 14. NOTIFICATION SCREEN
// ==========================================

void drawNotification() {

  NotifyItem item = getNotifyAtOffset(currentIdx);

  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.setCursor(2, 9);

  u8g2.print(item.app);

  char counter[8];

  sprintf(
    counter,
    "%d/%d",
    currentIdx + 1,
    notifyCount
  );

  int counterWidth = u8g2.getStrWidth(counter);

  u8g2.setCursor(126 - counterWidth, 9);

  u8g2.print(counter);

  u8g2.drawHLine(2, 14, 124);

  u8g2.setFont(u8g2_font_unifont_t_vietnamese1);

  int titleWidth = u8g2.getUTF8Width(
    item.title.c_str()
  );

  if (titleWidth <= 124) {

    int x = (128 - titleWidth) / 2;

    u8g2.drawUTF8(
      x, 30,
      item.title.c_str()
    );

  } else {

    unsigned long offset = (millis() / 35) % (titleWidth + 128);

    int x = 128 - offset;

    u8g2.drawUTF8(
      x, 30,
      item.title.c_str()
    );
  }

  u8g2.drawHLine(2, 33, 124);

  totalPages = 1;

  int totalLines = drawWrappedMessage(
    item.msg,
    messagePage,
    124,
    47,
    16,
    2
  );

  totalPages = max(1, (totalLines + 1) / 2);

  if (totalPages > 1) {

    int barWidth = max(2, 122 / totalPages);

    int barX = 3 + messagePage * barWidth;

    u8g2.drawBox(
      barX, 61,
      min(barWidth, 122 - messagePage * barWidth),
      2
    );
  }
}

// ==========================================
// 15. CHANGE NOTIFICATION
// ==========================================

void moveNotification(int direction) {

  if (notifyCount == 0) return;

  if (!showNotify) {

    showNotify = true;
    currentIdx = 0;
    messagePage = 0;

    lastPageTime = millis();

    return;
  }

  int next = currentIdx + direction;

  if (next < 0) {

    showNotify = false;

    return;
  }

  if (next >= notifyCount) {
    return;
  }

  currentIdx = next;

  messagePage = 0;

  lastPageTime = millis();
}

// ==========================================
// 16. SLEEP MODE
// ==========================================

void enterSleepMode() {

  sleepMode = true;

  // Tắt màn hình OLED
  u8g2.setPowerSave(1);

  // Tắt LED
  digitalWrite(LED_PIN, LOW);

  ledState = false;
}

void exitSleepMode() {

  sleepMode = false;

  // Bật lại màn hình OLED
  u8g2.setPowerSave(0);

  // Làm mới thời gian cuộn
  lastPageTime = millis();
}

// ==========================================
// 17. BUTTON HANDLER
// ==========================================

void handleButtons() {

  unsigned long now = millis();

  // ========================================
  // NÚT UP
  // ========================================

  bool upReading = digitalRead(BTN_UP_PIN);

  if (upReading != upLast) {

    upLast = upReading;

    upTimer = now;
  }

  if (
    now - upTimer > DEBOUNCE_TIME &&
    upStable != upReading
  ) {

    upStable = upReading;

    if (upStable == LOW) {

      upPressStart = now;

      upLongPressHandled = false;
    }
  }

  // Kiểm tra nhấn giữ UP
  if (
    upStable == LOW &&
    !upLongPressHandled &&
    now - upPressStart >= LONG_PRESS_TIME
  ) {

    upLongPressHandled = true;

    if (sleepMode) {
      exitSleepMode();
    } else {
      enterSleepMode();
    }
  }

  // Nhấn thả UP
  if (upStable == HIGH && upLongPressHandled) {

    upLongPressHandled = false;
  }

  // ========================================
  // NÚT DOWN
  // ========================================

  bool downReading = digitalRead(BTN_DOWN_PIN);

  if (downReading != downLast) {

    downLast = downReading;

    downTimer = now;
  }

  if (
    now - downTimer > DEBOUNCE_TIME &&
    downStable != downReading
  ) {

    downStable = downReading;

    if (downStable == LOW) {

      if (!sleepMode) {
        moveNotification(1);
      }
    }
  }
}

// ==========================================
// 18. UP SHORT PRESS HANDLER
// ==========================================

void handleUpShortPress() {

  static bool previousUpState = HIGH;

  if (previousUpState == LOW && upStable == HIGH) {

    if (!upLongPressHandled && !sleepMode) {

      moveNotification(-1);
    }
  }

  previousUpState = upStable;
}

// ==========================================
// 19. AUTO MESSAGE SCROLL
// ==========================================

void updateMessageScroll() {

  if (sleepMode) return;

  if (!showNotify || notifyCount == 0) return;

  unsigned long now = millis();

  if (now - lastPageTime >= PAGE_INTERVAL) {

    lastPageTime = now;

    if (totalPages > 1) {

      messagePage++;

      if (messagePage >= totalPages) {
        messagePage = 0;
      }
    }
  }
}

// ==========================================
// 20. SETUP
// ==========================================

void setup() {

  Serial.begin(115200);

  // Khởi tạo nút bấm
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);

  // Khởi tạo LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Khởi tạo OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  u8g2.begin();

  u8g2.enableUTF8Print();

  u8g2.setContrast(100);

  // Khởi tạo đồng hồ
  watch.setNotificationCallback(notificationCallback);

  watch.begin();

  lastCleanupTime = millis();
}

// ==========================================
// 21. LOOP
// ==========================================

void loop() {

  watch.loop();

  handleButtons();

  handleUpShortPress();

  // Xóa thông báo quá 2 tiếng
  deleteExpiredNotifications();

  // Cập nhật LED thông báo
  updateLED();

  // Cập nhật cuộn tin nhắn
  updateMessageScroll();

  // Đang Sleep thì không cập nhật màn hình
  if (sleepMode) {

    delay(20);

    return;
  }

  u8g2.clearBuffer();

  if (showNotify && notifyCount > 0) {

    drawNotification();

  } else {

    drawClock();
  }

  u8g2.sendBuffer();

  delay(20);
}
