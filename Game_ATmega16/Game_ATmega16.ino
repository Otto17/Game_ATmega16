/*
*
* ========================================
* ИГРОВАЯ КОНСОЛЬ на ATmega16
* ========================================
* 12 игр с сохранением рекордов в EEPROM и необычным управлением: тактовая кнопка и ИК обнаружения препятствий YL-63 (FC-51)
*
* Релиз от 22.04.2026г, автор Otto
*
* DODGE — уклонение: препятствия летят навстречу по двум линиям, нужно вовремя перестраиваться (кнопка или датчик YL-63 = сменить линию).
*
* FLAPPY — аркада: управляем птичкой, летящей через узкие щели в столбах (кнопка или датчик YL-63 = прыжок).
*
* SNAKE — классическая змейка: собираем еду и растем, избегая стен и своего хвоста (датчик YL-63 = поворот налево, кнопка = поворот направо относительно головы змеи).
*
* RACE — гонки: едем по трем полосам и уворачиваемся от встречных машин (датчик YL-63 = влево, кнопка = вправо).
*
* PONG — одиночный пинг-понг: отбиваем ускоряющийся мячик от стены (датчик YL-63 = ракетка вверх, кнопка = ракетка вниз, можно удерживать).
*
* BRICK — арканоид: отбиваем мяч платформой, чтобы разбить все блоки сверху экрана (датчик YL-63 = влево, кнопка = вправо, можно удерживать).
*
* INVADER — космический тир: нужно сбить как можно больше пришельцев за 60 секунд (датчик YL-63 = двигать пушку влево по кругу, кнопка = выстрел вверх).
*
* JUMPER — платформер: бежим вперед и перепрыгиваем генерирующиеся ямы (кнопка или датчик YL-63 = прыжок / двойной прыжок в воздухе, долгое нажатие = прыгнуть выше).
*
* TAPPER — реакция: падают капли сверху в 4 колонках, нужно успевать подставлять стакан (датчик YL-63 = влево, кнопка = вправо).
*
* AVOIDER — выживание: уворачиваемся от летящих сбоку снарядов, вертикальные снаряды только отвлекают (кнопка или датчик YL-63 = сменить позицию игрока верх/низ).
*
* REFLECT — пинг-понг наоборот: шар летает от края к краю, нужно включать защитную стенку с той стороны, куда он летит в данный момент (кнопка или датчик YL-63 = переключить стенку слева/справа).
*
* MORSE — память: запоминаем и повторяем последовательность световых сигналов, которая увеличивается с каждым раундом (кнопка или датчик YL-63: короткое нажатие = точка, удержание = тире).
*
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <stdlib.h>

// ================================================================
// EEPROM
// ================================================================

// records — массив рекордов для каждой игры
uint16_t records[12];

// load_records загружает все рекорды из EEPROM в оперативную память
void load_records() {
  for (uint8_t i = 0; i < 12; i++) {
    records[i] = eeprom_read_word((uint16_t*)(i * 2));
    if (records[i] == 0xFFFF) records[i] = 0;  // Неинициализированная ячейка трактуется как ноль
  }
}

// save_record сохраняет рекорд игры в EEPROM, если новый счёт превышает текущий
void save_record(uint8_t game, uint16_t score) {
  if (score > records[game]) {
    records[game] = score;
    eeprom_write_word((uint16_t*)(game * 2), score);
  }
}

// ================================================================
// СИСТЕМНЫЙ ТАЙМЕР
// ================================================================

// _ms — счётчик миллисекунд, инкрементируется по прерыванию таймера
volatile uint32_t _ms = 0;

// ISR обработчик прерывания по совпадению Timer1 — инкрементирует счётчик миллисекунд
ISR(TIMER1_COMPA_vect) {
  _ms++;
}

// timer_init настраивает Timer1 в режиме CTC для генерации прерываний каждую миллисекунду
void timer_init() {
  TCCR1A = 0;
  TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);  // CTC, делитель 64
  OCR1A = 124;                                        // 16 МГц / 64 / 125 = 2 кГц
  TIMSK |= (1 << OCIE1A);
}

// millis возвращает количество миллисекунд с момента запуска таймера
uint32_t millis() {
  uint32_t m;
  cli();  // Атомарное чтение 32-битного значения
  m = _ms;
  sei();
  return m;
}

// wait_ms блокирует выполнение на заданное количество миллисекунд
void wait_ms(uint32_t ms) {
  uint32_t t = millis();
  while (millis() - t < ms)
    ;
}

// ================================================================
// I2C
// ================================================================

#define OLED_ADDR 0x3C  // Адрес OLED-дисплея на шине I2C

// i2c_init инициализирует аппаратный модуль TWI
void i2c_init() {
  TWBR = 24;  // Частота SCL при 16 МГц: ~250 кГц
  TWSR = 0;
  TWCR = (1 << TWEN);
}

// i2c_wait ожидает завершения текущей операции TWI
void i2c_wait() {
  while (!(TWCR & (1 << TWINT)))
    ;
}

// i2c_start формирует условие START и отправляет адрес устройства
void i2c_start(uint8_t addr) {
  TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
  i2c_wait();
  TWDR = addr;
  TWCR = (1 << TWINT) | (1 << TWEN);
  i2c_wait();
}

// i2c_write отправляет один байт данных по шине I2C
void i2c_write(uint8_t d) {
  TWDR = d;
  TWCR = (1 << TWINT) | (1 << TWEN);
  i2c_wait();
}

// i2c_stop формирует условие STOP на шине I2C
void i2c_stop() {
  TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
  for (uint8_t i = 0; i < 10; i++) asm("nop");  // Задержка для завершения STOP-условия
}

// ================================================================
// OLED SSD1306
// ================================================================

// oled_buf — видеобуфер 128x32 (512 байт, 1 бит на пиксель)
uint8_t oled_buf[512];

// oled_cmd отправляет одну команду контроллеру SSD1306
void oled_cmd(uint8_t c) {
  i2c_start(OLED_ADDR << 1);
  i2c_write(0x00);  // Байт управления: команда
  i2c_write(c);
  i2c_stop();
}

// oled_init инициализирует OLED-дисплей SSD1306 128x32
void oled_init() {
  for (uint8_t i = 0; i < 100; i++) asm("nop");  // Задержка для стабилизации питания
  oled_cmd(0xAE);                                // Выключает дисплей
  oled_cmd(0xD5);                                // Устанавливает частоту обновления
  oled_cmd(0x80);
  oled_cmd(0xA8);  // Устанавливает мультиплексирование (32 строки)
  oled_cmd(0x1F);
  oled_cmd(0xD3);  // Смещение дисплея
  oled_cmd(0x00);
  oled_cmd(0x40);  // Начальная строка
  oled_cmd(0x8D);  // Включает встроенный DC-DC преобразователь
  oled_cmd(0x14);
  oled_cmd(0x20);  // Режим горизонтальной адресации
  oled_cmd(0x00);
  oled_cmd(0xA1);  // Зеркалирование по горизонтали
  oled_cmd(0xC8);  // Зеркалирование по вертикали
  oled_cmd(0xDA);  // Конфигурация COM-пинов для 128x32
  oled_cmd(0x02);
  oled_cmd(0x81);  // Контрастность
  oled_cmd(0xCF);
  oled_cmd(0xD9);  // Период предзаряда
  oled_cmd(0xF1);
  oled_cmd(0xDB);  // Уровень VCOMH
  oled_cmd(0x40);
  oled_cmd(0xA4);  // Вывод из RAM
  oled_cmd(0xA6);  // Нормальный режим (не инвертированный)
  oled_cmd(0x21);  // Диапазон столбцов
  oled_cmd(0);
  oled_cmd(127);
  oled_cmd(0x22);  // Диапазон страниц
  oled_cmd(0);
  oled_cmd(3);
  oled_cmd(0xAF);  // Включает дисплей
}

// oled_update передаёт содержимое видеобуфера на дисплей
void oled_update() {
  oled_cmd(0x21);  // Сбрасывает указатель адреса перед передачей
  oled_cmd(0);
  oled_cmd(127);
  oled_cmd(0x22);
  oled_cmd(0);
  oled_cmd(3);
  i2c_start(OLED_ADDR << 1);
  i2c_write(0x40);  // Байт управления: данные
  for (uint16_t i = 0; i < 512; i++) i2c_write(oled_buf[i]);
  i2c_stop();
}

// oled_clear очищает видеобуфер (заполняет нулями)
void oled_clear() {
  for (uint16_t i = 0; i < 512; i++) oled_buf[i] = 0;
}

// oled_pixel устанавливает или сбрасывает пиксель в видеобуфере
void oled_pixel(uint8_t x, uint8_t y, uint8_t col) {
  if (x > 127 || y > 31) return;  // Пиксель за пределами экрана игнорируется
  uint16_t idx = (y >> 3) * 128 + x;
  if (col) oled_buf[idx] |= (1 << (y & 7));
  else oled_buf[idx] &= ~(1 << (y & 7));
}

// oled_hline рисует горизонтальную линию от x0 до x1 на строке y
void oled_hline(uint8_t y, uint8_t x0, uint8_t x1) {
  if (x0 > x1) {
    uint8_t t = x0;
    x0 = x1;
    x1 = t;
  }
  for (uint8_t x = x0; x <= x1; x++) oled_pixel(x, y, 1);
}

// oled_vline рисует вертикальную линию от y0 до y1 на столбце x
void oled_vline(uint8_t x, uint8_t y0, uint8_t y1) {
  if (y0 > y1) {
    uint8_t t = y0;
    y0 = y1;
    y1 = t;
  }
  for (uint8_t y = y0; y <= y1; y++) oled_pixel(x, y, 1);
}

// oled_rect рисует прямоугольник: залитый при fill=1, контурный при fill=0
void oled_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t fill) {
  if (fill) {
    for (uint8_t x = x0; x <= x1; x++)
      for (uint8_t y = y0; y <= y1; y++)
        oled_pixel(x, y, 1);
  } else {
    oled_hline(y0, x0, x1);
    oled_hline(y1, x0, x1);
    oled_vline(x0, y0, y1);
    oled_vline(x1, y0, y1);
  }
}

// ================================================================
// ШРИФТ 5x7
// ================================================================

// font5x7 — растровый шрифт 5x7 для символов ASCII 32..90, хранится в PROGMEM
const uint8_t font5x7[][5] PROGMEM = {
  { 0x00, 0x00, 0x00, 0x00, 0x00 },  // ' ' 32
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x1C, 0x22, 0x41, 0x00 },  // '(' 40
  { 0x00, 0x41, 0x22, 0x1C, 0x00 },  // ')' 41
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x08, 0x08, 0x08, 0x08, 0x08 },  // '-' 45
  { 0x00, 0x60, 0x60, 0x00, 0x00 },  // '.' 46
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x3E, 0x51, 0x49, 0x45, 0x3E },  // '0' 48
  { 0x00, 0x42, 0x7F, 0x40, 0x00 },
  { 0x42, 0x61, 0x51, 0x49, 0x46 },
  { 0x21, 0x41, 0x45, 0x4B, 0x31 },
  { 0x18, 0x14, 0x12, 0x7F, 0x10 },
  { 0x27, 0x45, 0x45, 0x45, 0x39 },
  { 0x3C, 0x4A, 0x49, 0x49, 0x30 },
  { 0x01, 0x71, 0x09, 0x05, 0x03 },
  { 0x36, 0x49, 0x49, 0x49, 0x36 },
  { 0x06, 0x49, 0x49, 0x29, 0x1E },  // '9' 57
  { 0x00, 0x36, 0x36, 0x00, 0x00 },  // ':' 58
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x14, 0x14, 0x14, 0x14, 0x14 },  // '=' 61
  { 0x00, 0x41, 0x22, 0x14, 0x08 },  // '>' 62
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x7E, 0x09, 0x09, 0x09, 0x7E },  // 'A' 65
  { 0x7F, 0x49, 0x49, 0x49, 0x36 },
  { 0x3E, 0x41, 0x41, 0x41, 0x22 },
  { 0x7F, 0x41, 0x41, 0x41, 0x3E },
  { 0x7F, 0x49, 0x49, 0x49, 0x41 },
  { 0x7F, 0x09, 0x09, 0x09, 0x01 },
  { 0x3E, 0x41, 0x49, 0x49, 0x7A },
  { 0x7F, 0x08, 0x08, 0x08, 0x7F },
  { 0x00, 0x41, 0x7F, 0x41, 0x00 },
  { 0x20, 0x40, 0x41, 0x3F, 0x01 },
  { 0x7F, 0x08, 0x14, 0x22, 0x41 },
  { 0x7F, 0x40, 0x40, 0x40, 0x40 },
  { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
  { 0x7F, 0x04, 0x08, 0x10, 0x7F },
  { 0x3E, 0x41, 0x41, 0x41, 0x3E },
  { 0x7F, 0x09, 0x09, 0x09, 0x06 },
  { 0x3E, 0x41, 0x51, 0x21, 0x5E },
  { 0x7F, 0x09, 0x19, 0x29, 0x46 },
  { 0x46, 0x49, 0x49, 0x49, 0x31 },
  { 0x01, 0x01, 0x7F, 0x01, 0x01 },
  { 0x3F, 0x40, 0x40, 0x40, 0x3F },
  { 0x0F, 0x30, 0x40, 0x30, 0x0F },
  { 0x3F, 0x40, 0x30, 0x40, 0x3F },
  { 0x63, 0x14, 0x08, 0x14, 0x63 },
  { 0x07, 0x08, 0x70, 0x08, 0x07 },
  { 0x61, 0x51, 0x49, 0x45, 0x43 },  // 'Z' 90
};

// oled_char_clipped рисует символ с обрезкой по горизонтальным границам clip_l..clip_r
void oled_char_clipped(int16_t sx, uint8_t y, char c, uint8_t clip_l, uint8_t clip_r) {
  if (c < 32 || c > 90) c = ' ';  // Символы вне диапазона заменяются пробелом
  uint8_t idx = c - 32;
  for (uint8_t col = 0; col < 5; col++) {
    int16_t px = sx + col;
    if (px < (int16_t)clip_l) continue;
    if (px > (int16_t)clip_r) break;
    uint8_t bits = pgm_read_byte(&font5x7[idx][col]);
    for (uint8_t row = 0; row < 7; row++)
      if (bits & (1 << row))
        oled_pixel((uint8_t)px, y + row, 1);
  }
}

// oled_str_clipped рисует строку символов с обрезкой по горизонтальным границам
void oled_str_clipped(int16_t sx, uint8_t y, const char* s, uint8_t clip_l, uint8_t clip_r) {
  int16_t x = sx;
  while (*s) {
    if (x > (int16_t)clip_r + 5) break;  // Остальные символы за правой границей — пропускаются
    if (x + 5 >= (int16_t)clip_l)
      oled_char_clipped(x, y, *s, clip_l, clip_r);
    x += 6;
    s++;
  }
}

// oled_char рисует один символ без обрезки
void oled_char(uint8_t x, uint8_t y, char c) {
  oled_char_clipped((int16_t)x, y, c, 0, 127);
}

// oled_text рисует строку из RAM на экране
void oled_text(uint8_t x, uint8_t y, const char* s) {
  int16_t cx = (int16_t)x;
  while (*s) {
    if (cx + 5 > 127) break;
    if (cx >= 0) oled_char((uint8_t)cx, y, *s);
    s++;
    cx += 6;
  }
}

// oled_num рисует беззнаковое целое число на экране
void oled_num(uint8_t x, uint8_t y, uint16_t n) {
  char buf[6];
  uint8_t i = 0;
  if (n == 0) {
    buf[i++] = '0';
  } else {
    char tmp[6];
    uint8_t j = 0;
    while (n) {
      tmp[j++] = '0' + (n % 10);
      n /= 10;
    }
    while (j) buf[i++] = tmp[--j];  // Разворачивает цифры в правильный порядок
  }
  buf[i] = 0;
  oled_text(x, y, buf);
}

// oled_text_center рисует строку из RAM по центру экрана по горизонтали
void oled_text_center(uint8_t y, const char* s) {
  uint8_t len = 0;
  const char* p = s;
  while (*p++) len++;
  uint8_t x = (128 - len * 6) / 2;
  oled_text(x, y, s);
}

// oled_text_P рисует строку из PROGMEM на экране
void oled_text_P(uint8_t x, uint8_t y, const char* s) {
  int16_t cx = (int16_t)x;
  char c;
  while ((c = pgm_read_byte(s++))) {
    if (cx + 5 > 127) break;
    if (cx >= 0) oled_char((uint8_t)cx, y, c);
    cx += 6;
  }
}

// oled_text_center_P рисует строку из PROGMEM по центру экрана по горизонтали
void oled_text_center_P(uint8_t y, const char* s) {
  uint8_t len = 0;
  const char* p = s;
  while (pgm_read_byte(p++)) len++;
  int16_t cx = (128 - len * 6) / 2;
  oled_text_P((uint8_t)cx, y, s);
}

// ================================================================
// КНОПКИ И ДАТЧИК
// ================================================================

bool s1_e = false, s1_l = true, s1_s = true;  // Флаг события / предыдущее / стабильное состояние кнопки 1
uint32_t s1_t = 0;                            // Метка времени последнего изменения кнопки 1
bool b2_e = false, b2_l = true, b2_s = true;  // Флаг события / предыдущее / стабильное состояние кнопки 2
uint32_t b2_t = 0;                            // Метка времени последнего изменения кнопки 2

// btn_init настраивает пины PD2 и PD3 как входы с подтяжкой
void btn_init() {
  DDRD &= ~((1 << 2) | (1 << 3));
  PORTD |= ((1 << 2) | (1 << 3));
}

// btn_upd опрашивает состояние кнопок с программным антидребезгом (30 мс)
void btn_upd() {
  uint32_t now = millis();
  bool r1 = (PIND & (1 << 2)) != 0;
  if (r1 != s1_l) {
    s1_t = now;
    s1_l = r1;
  }
  if ((now - s1_t) > 30 && r1 != s1_s) {
    s1_s = r1;
    if (!s1_s) s1_e = true;  // Фиксирует нажатие при переходе в LOW
  }
  bool r2 = (PIND & (1 << 3)) != 0;
  if (r2 != b2_l) {
    b2_t = now;
    b2_l = r2;
  }
  if ((now - b2_t) > 30 && r2 != b2_s) {
    b2_s = r2;
    if (!b2_s) b2_e = true;
  }
}

// btn1 возвращает true однократно при нажатии кнопки 1, сбрасывая флаг события
bool btn1() {
  if (s1_e) {
    s1_e = false;
    return true;
  }
  return false;
}

// btn1_held возвращает true, пока кнопка 1 удерживается
bool btn1_held() {
  return !s1_s;
}

// btn2 возвращает true однократно при нажатии кнопки 2, сбрасывая флаг события
bool btn2() {
  if (b2_e) {
    b2_e = false;
    return true;
  }
  return false;
}

// btn2_held возвращает true, пока кнопка 2 удерживается
bool btn2_held() {
  return !b2_s;
}

// btns_clear сбрасывает накопленные события обеих кнопок
void btns_clear() {
  s1_e = false;
  b2_e = false;
}

// ================================================================
// ЭКРАН КОНЦА ИГРЫ
// ================================================================

// end_screen отображает экран окончания игры с выбором «RETRY» или «MENU»
bool end_screen(uint8_t game_idx, uint16_t sc) {
  save_record(game_idx, sc);
  btns_clear();

  uint8_t cursor = 0;
  char score_buf[8];
  uint8_t si = 0;
  uint16_t tmp = sc;
  if (tmp == 0) score_buf[si++] = '0';
  else {
    char t[6];
    uint8_t j = 0;
    while (tmp) {
      t[j++] = '0' + (tmp % 10);
      tmp /= 10;
    }
    while (j) score_buf[si++] = t[--j];
  }
  score_buf[si] = 0;

  bool new_rec = (sc > 0 && sc >= records[game_idx]);

  while (1) {
    oled_clear();
    oled_text_center_P(0, PSTR("GAME OVER"));
    if (new_rec) oled_text_center_P(9, PSTR("NEW RECORD"));
    else oled_text_center(9, score_buf);
    oled_char(28, 22, cursor ? ' ' : '>');
    oled_text_P(34, 22, PSTR("RETRY"));
    oled_char(70, 22, cursor ? '>' : ' ');
    oled_text_P(76, 22, PSTR("MENU"));
    oled_update();

    while (1) {
      btn_upd();
      if (btn1()) {
        cursor ^= 1;  // Переключает курсор между пунктами
        break;
      }
      if (btn2()) {
        btns_clear();
        return !cursor;  // true = RETRY, false = MENU
      }
    }
  }
}

// msg_P отображает одно- или двухстрочное сообщение из PROGMEM на заданное время
void msg_P(const char* s1, const char* s2, uint16_t ms) {
  oled_clear();
  oled_text_center_P(s2 ? 8 : 12, s1);
  if (s2 && pgm_read_byte(s2)) oled_text_center_P(20, s2);
  oled_update();
  wait_ms(ms);
}

// ================================================================
// ИГРА 1: DODGE
// ================================================================

// game_dodge — игра уклонения: игрок переключается между двумя линиями, избегая препятствий
bool game_dodge() {
  msg_P(PSTR("DODGE"), PSTR("BTN=SWITCH"), 2000);

  uint8_t pl = 0;                // Текущая линия игрока (0 = верх, 1 = низ)
  uint8_t ox[2] = { 118, 80 };   // X-координаты препятствий
  uint8_t ol[2] = { 0, 1 };      // Линии препятствий
  bool oa[2] = { true, false };  // Активность препятствий
  uint16_t sc = 0;
  uint8_t sp = 2, sp_frac = 0;  // Скорость и дробная часть для плавного ускорения
  uint32_t lt = millis();
  uint32_t spawn_t = millis() + 800 + (uint32_t)random(0, 1200);

  while (1) {
    btn_upd();
    if (btn1() || btn2()) pl ^= 1;  // Переключает линию игрока

    if (millis() - lt >= 40) {
      lt = millis();

      for (uint8_t i = 0; i < 2; i++) {
        if (!oa[i]) continue;
        if (ox[i] >= sp) ox[i] -= sp;
        else ox[i] = 0;
        if (ol[i] == pl && ox[i] < 14 && ox[i] + 9 > 4)  // Столкновение с игроком
          return end_screen(0, sc);
        if (ox[i] == 0) {
          sc++;
          ox[i] = 118;
          ol[i] = (uint8_t)random(2);
          if (sc % 3 == 0) {
            sp_frac += 2;
            if (sp_frac >= 10 && sp < 7) {  // Накопление дробной части до повышения скорости
              sp++;
              sp_frac = 0;
            }
          }
          if (i == 1) oa[1] = false;
        }
      }

      if (!oa[1] && millis() >= spawn_t) {  // Второе препятствие появляется по таймеру
        oa[1] = true;
        ox[1] = 118;
        ol[1] = (uint8_t)random(2);
        spawn_t = millis() + 600 + (uint32_t)random(0, 1400);
      }

      oled_clear();
      oled_hline(15, 0, 127);  // Разделительная линия между дорожками
      oled_rect(4, pl ? 19 : 4, 12, pl ? 27 : 12, 1);
      for (uint8_t i = 0; i < 2; i++)
        if (oa[i])
          oled_rect(ox[i], ol[i] ? 19 : 4, ox[i] + 8, ol[i] ? 27 : 12, 0);
      oled_num(96, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 2: FLAPPY
// ================================================================

// game_flappy — игра с прыгающей птицей: игрок прыгает через вертикальные щели
bool game_flappy() {
  msg_P(PSTR("FLAPPY"), PSTR("BTN=JUMP"), 2000);

  int16_t by16 = 14 * 16, bv16 = 0;  // Позиция и скорость птицы в единицах 1/16 пикселя
  const int16_t MAX_VEL = 10, JUMP_V = -12, FLOOR16 = 28 * 16;
  const uint8_t GRAV_PERIOD = 2;  // Гравитация применяется каждый второй кадр
  uint8_t grav_counter = 0;
  int16_t px = 180;  // X-координата столба
  uint8_t gt = 10;   // Верхняя граница щели
  uint16_t sc = 0;
  uint8_t sp = 1;  // Скорость горизонтального движения столбов
  uint32_t lt = millis();
  uint32_t hover_end = millis() + 2000;  // Птица зависает в начале для ориентации игрока
  bool hovering = true;

  while (1) {
    btn_upd();
    if (btn1() || btn2()) {
      bv16 = JUMP_V;
      hovering = false;
    }

    if (millis() - lt >= 40) {
      lt = millis();
      if (hovering) {
        if (millis() > hover_end) hovering = false;
        bv16 = 0;
        by16 = 14 * 16;
        grav_counter = 0;
      } else {
        grav_counter++;
        if (grav_counter >= GRAV_PERIOD) {
          grav_counter = 0;
          bv16++;
        }
        if (bv16 > MAX_VEL) bv16 = MAX_VEL;
        by16 += bv16;
      }
      if (by16 < 0) {
        by16 = 0;
        bv16 = 0;
      }  // Ограничение сверху
      if (by16 > FLOOR16) {
        by16 = FLOOR16;
        bv16 = 0;
      }  // Ограничение снизу

      px -= sp;
      if (px < -7) {  // Столб ушёл за экран — генерирует новый
        px = 127;
        gt = (uint8_t)random(3, 13);
        sc++;
        if (sc % 12 == 0 && sp < 3) sp++;
      }

      uint8_t iby = (uint8_t)(by16 >> 4);
      if (!hovering && px >= 0 && px < 128) {
        bool x_hit = (px < 18) && (px + 6 > 14);  // Проверка перекрытия по X с птицей
        if (x_hit) {
          bool in_top = ((int8_t)(iby + 1) < (int8_t)gt - 1);
          bool in_bot = ((int8_t)(iby + 2) > (int8_t)(gt + 14) + 1);
          if (in_top || in_bot) return end_screen(1, sc);  // Попадание в столб
        }
      }

      oled_clear();
      oled_rect(14, iby, 17, iby + 3, 1);  // Птица
      if (px >= 0 && px < 128) {
        uint8_t upx = (uint8_t)px;
        if (gt > 0) oled_rect(upx, 0, upx + 6, gt - 1, 1);          // Верхний столб
        if (gt + 14 < 32) oled_rect(upx, gt + 14, upx + 6, 31, 1);  // Нижний столб
      }
      oled_num(0, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 3: SNAKE
// ================================================================

// game_snake — классическая змейка на поле 32x8 клеток
bool game_snake() {
  msg_P(PSTR("SNAKE"), PSTR("B1=L B2=R"), 2000);

  uint8_t sx[48], sy[48], sl = 3;  // Координаты сегментов и длина змейки
  sx[0] = 16;
  sy[0] = 4;
  sx[1] = 15;
  sy[1] = 4;
  sx[2] = 14;
  sy[2] = 4;
  int8_t dx = 1, dy = 0;                                      // Направление движения
  uint8_t fx = (uint8_t)random(32), fy = (uint8_t)random(8);  // Позиция еды
  uint16_t sc = 0, ms = 300;                                  // Счёт и интервал движения
  uint32_t lt = millis();

  while (1) {
    btn_upd();
    if (btn1()) {  // Поворот влево относительно текущего направления
      int8_t t = dx;
      dx = dy;
      dy = -t;
    }
    if (btn2()) {  // Поворот вправо относительно текущего направления
      int8_t t = dx;
      dx = -dy;
      dy = t;
    }

    if (millis() - lt >= ms) {
      lt = millis();
      int8_t nx = sx[0] + dx, ny = sy[0] + dy;
      if (nx < 0) nx = 31;  // Тороидальное поле по горизонтали
      if (nx > 31) nx = 0;
      if (ny < 0) ny = 7;  // Тороидальное поле по вертикали
      if (ny > 7) ny = 0;

      for (uint8_t i = 0; i < sl; i++)  // Столкновение с собственным телом
        if (sx[i] == (uint8_t)nx && sy[i] == (uint8_t)ny)
          return end_screen(2, sc);

      if ((uint8_t)nx == fx && (uint8_t)ny == fy) {  // Поедание еды
        sc++;
        if (sl < 48) sl++;
        if (sc % 5 == 0 && ms > 160) ms -= 15;  // Ускорение каждые 5 очков
        if (sc > records[2]) {
          records[2] = sc;
          eeprom_write_word((uint16_t*)(2 * 2), sc);
        }
        bool ok;
        do {  // Еда не должна появляться на теле змейки
          fx = (uint8_t)random(32);
          fy = (uint8_t)random(8);
          ok = true;
          for (uint8_t i = 0; i < sl; i++)
            if (sx[i] == fx && sy[i] == fy) ok = false;
        } while (!ok);
      }
      for (int8_t i = sl - 1; i > 0; i--) {  // Сдвиг тела вслед за головой
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
      }
      sx[0] = (uint8_t)nx;
      sy[0] = (uint8_t)ny;
    }

    oled_clear();
    oled_num(0, 0, sc);
    oled_text_P(40, 0, PSTR("R"));
    oled_num(46, 0, records[2]);
    for (uint8_t i = 0; i < sl; i++)
      oled_rect(sx[i] * 4, sy[i] * 4, sx[i] * 4 + 2, sy[i] * 4 + 2, 1);
    oled_rect(fx * 4, fy * 4, fx * 4 + 2, fy * 4 + 2, 0);  // Еда рисуется контуром
    oled_update();
  }
}

// ================================================================
// ИГРА 4: RACE
// ================================================================

// game_race — гоночная игра на 3 полосах с уклонением от встречных машин
bool game_race() {
  msg_P(PSTR("RACE"), PSTR("B1=L B2=R"), 2000);

  uint8_t pl = 1;  // Текущая полоса игрока (0..2)
  int8_t oy[3];    // Y-координаты встречных машин
  uint8_t ol[3];   // Полосы встречных машин
  bool oa[3];      // Активность встречных машин
  for (uint8_t i = 0; i < 3; i++) oa[i] = false;
  uint16_t sc = 0, sms_base = 2000, speed_ms = 80;
  uint32_t lt = millis(), ls = millis();
  uint16_t next_spawn = sms_base + (uint16_t)random(0, 800);

  while (1) {
    btn_upd();
    if (btn1() && pl > 0) pl--;
    if (btn2() && pl < 2) pl++;
    uint32_t now = millis();

    if (now - ls >= next_spawn) {  // Спавн новой встречной машины
      ls = now;
      next_spawn = sms_base + (uint16_t)random(0, sms_base / 2);
      for (uint8_t i = 0; i < 3; i++) {
        if (!oa[i]) {
          oy[i] = -8;
          ol[i] = (sc == 0) ? (random(0, 2) == 0 ? 0 : 2) : (uint8_t)random(3);  // На первом ходу машина не появляется в центре
          oa[i] = true;
          break;
        }
      }
    }

    if (now - lt >= speed_ms) {
      lt = now;
      for (uint8_t i = 0; i < 3; i++) {
        if (!oa[i]) continue;
        oy[i]++;
        if (ol[i] == pl && oy[i] >= 22 && oy[i] <= 30) return end_screen(3, sc);  // Столкновение
        if (oy[i] > 31) {                                                         // Машина ушла за экран
          oa[i] = false;
          sc++;
          if (sc % 8 == 0) {
            if (speed_ms > 25) speed_ms -= 8;     // Ускорение падения
            if (sms_base > 900) sms_base -= 150;  // Учащение спавна
          }
        }
      }
      uint8_t lx[] = { 8, 52, 96 };  // X-координаты центров полос
      oled_clear();
      for (uint8_t y = 0; y < 32; y += 4) {  // Пунктирные разделители полос
        oled_pixel(37, y, 1);
        oled_pixel(80, y, 1);
      }
      oled_rect(lx[pl], 22, lx[pl] + 13, 29, 1);  // Машина игрока
      for (uint8_t i = 0; i < 3; i++)
        if (oa[i] && oy[i] >= 0)
          oled_rect(lx[ol[i]], (uint8_t)oy[i], lx[ol[i]] + 13, (uint8_t)(oy[i] + 7), 0);
      oled_num(0, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 5: PONG
// ================================================================

// game_pong — одиночный понг: игрок отбивает мяч левой ракеткой
bool game_pong() {
  msg_P(PSTR("PONG"), PSTR("B1=UP B2=DOWN"), 2000);

  uint8_t py = 12;                                 // Y-координата ракетки
  float bx = 64, by = 16, bvx = 0.8f, bvy = 0.5f;  // Позиция и скорость мяча
  uint16_t sc = 0;
  uint32_t lt = millis();

  while (1) {
    btn_upd();
    if (btn1_held() && py > 0) py--;
    if (btn2_held() && py < 24) py++;

    if (millis() - lt >= 20) {
      lt = millis();
      bx += bvx;
      by += bvy;
      if (by <= 0 || by >= 30) bvy = -bvy;        // Отражение от верхней и нижней стенок
      if (bx >= 125) bvx = -bvx;                  // Отражение от правой стенки
      if (bx <= 3 && by >= py && by <= py + 8) {  // Отбивание ракеткой
        bvx = -bvx;
        bx = 3;
        sc++;
        if (bvx > 0) bvx += 0.07f;  // Лёгкое ускорение мяча при каждом отбивании
        else bvx -= 0.07f;
      }
      if (bx < 0) return end_screen(4, sc);  // Мяч пропущен
      oled_clear();
      oled_vline(0, py, py + 8);                                                 // Ракетка
      oled_rect((uint8_t)bx, (uint8_t)by, (uint8_t)bx + 2, (uint8_t)by + 2, 1);  // Мяч
      oled_num(50, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 6: BRICK
// ================================================================

// game_brick — арканоид: разбивает блоки мячом, отражая его платформой
bool game_brick() {
  msg_P(PSTR("BRICK"), PSTR("B1=L B2=R"), 2000);

  uint8_t bricks[2];  // Битовые маски блоков (2 ряда по 8 блоков)
  bricks[0] = 0xFF;
  bricks[1] = 0xFF;
  uint8_t px = 56;                                  // X-координата платформы
  float bx = 64, by = 20, bvx = 0.5f, bvy = -0.7f;  // Позиция и скорость мяча
  uint16_t sc = 0;
  uint32_t lt = millis();

  while (1) {
    btn_upd();
    if (btn1_held() && px > 0) px -= 3;
    if (btn2_held() && px < 112) px += 3;

    if (millis() - lt >= 25) {
      lt = millis();
      bx += bvx;
      by += bvy;
      if (bx <= 0 || bx >= 126) bvx = -bvx;                     // Отражение от боковых стенок
      if (by <= 0) bvy = -bvy;                                  // Отражение от верхней стенки
      if (by >= 28 && by <= 30 && bx >= px && bx <= px + 16) {  // Отражение от платформы
        bvy = -bvy;
        by = 28;
      }
      if (by > 31) return end_screen(5, sc);  // Мяч упал за платформу

      if (by >= 2 && by <= 10) {  // Зона блоков
        uint8_t row = (by < 6) ? 0 : 1;
        uint8_t col = ((uint8_t)bx) / 16;
        if (col < 8 && (bricks[row] & (1 << col))) {
          bricks[row] &= ~(1 << col);  // Разрушает блок
          bvy = -bvy;
          sc++;
        }
      }
      if (bricks[0] == 0 && bricks[1] == 0) {  // Все блоки разрушены
        msg_P(PSTR("YOU WIN"), PSTR(""), 2000);
        return end_screen(5, sc);
      }
      oled_clear();
      for (uint8_t r = 0; r < 2; r++)
        for (uint8_t c = 0; c < 8; c++)
          if (bricks[r] & (1 << c))
            oled_rect(c * 16, r * 4 + 2, c * 16 + 14, r * 4 + 5, 1);
      oled_hline(30, px, px + 16);                                               // Платформа
      oled_rect((uint8_t)bx, (uint8_t)by, (uint8_t)bx + 1, (uint8_t)by + 1, 1);  // Мяч
      oled_num(0, 24, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 7: INVADER
// ================================================================

// game_invader — стрельба по пришельцам за 60 секунд
bool game_invader() {
  msg_P(PSTR("INVADER"), PSTR("60 SEC"), 2000);

  uint8_t aliens[3];  // Битовые маски пришельцев (3 ряда по 8)
  for (uint8_t i = 0; i < 3; i++) aliens[i] = (uint8_t)random(1, 255);
  uint8_t px = 60;         // X-координата пушки
  int8_t bx = -1, by = 0;  // Координаты снаряда, -1 = нет снаряда
  uint16_t sc = 0;
  uint32_t lt = millis(), lm = millis();
  uint32_t next_sec = millis() + 2000UL;
  uint8_t remain = 60;  // Оставшееся время в секундах

  while (1) {
    btn_upd();
    if (btn1_held()) {
      if (px > 0) px--;
      else px = 121;  // Циклический переход через край экрана
    }
    if (btn2() && bx < 0) {  // Выстрел возможен только если нет активного снаряда
      bx = px + 3;
      by = 28;
    }
    uint32_t now_ms = millis();

    if (now_ms >= next_sec) {  // Отсчёт таймера (2000 мс = 1 игровая секунда)
      next_sec += 2000UL;
      if (remain == 0) return end_screen(6, sc);
      remain--;
    }

    if (now_ms - lt >= 30) {
      lt = now_ms;
      if (bx >= 0) {  // Движение снаряда вверх
        by -= 2;
        if (by < 0) {
          bx = -1;                         // Снаряд ушёл за верх экрана
        } else if (by >= 2 && by <= 10) {  // Проверка попадания в зону пришельцев
          uint8_t row = (by - 2) / 3, col = (uint8_t)bx / 16;
          if (row < 3 && col < 8 && (aliens[row] & (1 << col))) {
            aliens[row] &= ~(1 << col);
            bx = -1;
            sc++;
          }
        }
      }
      if (now_ms - lm >= 600) {  // Периодическое перемещение пришельцев
        lm = now_ms;
        for (uint8_t r = 0; r < 3; r++) {
          if (random(2)) aliens[r] <<= 1;
          else aliens[r] >>= 1;
          if (aliens[r] == 0) aliens[r] = (uint8_t)random(1, 255);  // Восстанавливает ряд, если все уничтожены
        }
      }
      oled_clear();
      for (uint8_t r = 0; r < 3; r++)
        for (uint8_t c = 0; c < 8; c++)
          if (aliens[r] & (1 << c))
            oled_rect(c * 16, r * 3 + 2, c * 16 + 2, r * 3 + 4, 1);
      oled_rect(px, 28, px + 6, 30, 1);                                                  // Пушка
      if (bx >= 0 && by <= 31) oled_vline((uint8_t)bx, (uint8_t)by, (uint8_t)(by + 2));  // Снаряд
      oled_num(0, 24, sc);
      oled_num(108, 24, remain);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 8: JUMPER
// ================================================================

// game_jumper — прыжки через ямы с поддержкой двойного прыжка и удержания
bool game_jumper() {
  msg_P(PSTR("JUMPER"), PSTR("BTN=JUMP"), 2000);

  int16_t py8 = 22 * 8, pvy8 = 0;  // Позиция и скорость игрока в единицах 1/8 пикселя
  const int16_t GROUND8 = 22 * 8, CEIL8 = -10 * 8, GRAV8 = 2, MAX_VEL8 = 20;
  const int16_t JUMP_S8 = -28, JUMP_L8 = -64;  // Короткий и длинный импульс прыжка
  int16_t gx = 127;                            // X-координата начала ямы
  uint8_t gw = (uint8_t)random(14, 28);        // Ширина ямы
  uint16_t sc = 0;
  uint8_t sp = 1;  // Скорость прокрутки
  bool ground = true, used_djump = false;
  uint32_t lt = millis(), press_start = 0;
  bool was_pressed = false;

  while (1) {
    btn_upd();
    bool held_now = btn1_held() || btn2_held();

    if (held_now && !was_pressed) {  // Момент нажатия
      press_start = millis();
      if (ground) {
        pvy8 = JUMP_S8;
        ground = false;
        used_djump = false;
      } else if (!used_djump) {  // Двойной прыжок
        pvy8 = JUMP_S8;
        used_djump = true;
      }
    }
    if (held_now && !ground && !used_djump && (millis() - press_start) > 150 && (millis() - press_start) < 1024 && pvy8 > JUMP_L8) {
      pvy8 -= 4;  // Удержание усиливает прыжок только первые 300 мс
      if (pvy8 < JUMP_L8) pvy8 = JUMP_L8;
    }
    was_pressed = held_now;

    if (millis() - lt >= 40) {
      lt = millis();
      if (!ground) {
        pvy8 += GRAV8;
        if (pvy8 > MAX_VEL8) pvy8 = MAX_VEL8;
        py8 += pvy8;
        if (py8 >= GROUND8) {  // Приземление
          py8 = GROUND8;
          pvy8 = 0;
          ground = true;
          used_djump = false;
        }
        if (py8 < CEIL8) {  // Потолок
          py8 = CEIL8;
          pvy8 = 0;
        }
      }
      gx -= sp;
      if (gx <= -(int16_t)gw) {             // Яма ушла за экран — генерирует новую
        gx = 127 + (int16_t)random(0, 40);  // Случайная задержка перед появлением
        gw = (uint8_t)random(10, 32);       // Более разнообразная ширина ям
        sc++;
        if (sc % 5 == 0 && sp < 5) sp++;
      }
      if (ground) {  // Проверка попадания в яму при нахождении на земле
        int16_t p_l = 4, p_r = 10, g_l = gx, g_r = gx + (int16_t)gw;
        int16_t ov_l = (p_l > g_l) ? p_l : g_l, ov_r = (p_r < g_r) ? p_r : g_r;
        if (ov_r - ov_l >= 2) return end_screen(7, sc);
      }
      int16_t py_real = py8 >> 3;
      oled_clear();
      if (gx > 0) oled_hline(30, 0, (uint8_t)gx);                           // Земля слева от ямы
      if (gx + (int16_t)gw < 128) oled_hline(30, (uint8_t)(gx + gw), 127);  // Земля справа от ямы
      if (py_real < 31 && py_real + 6 >= 0) {
        int16_t y0 = py_real, y1 = py_real + 6;
        if (y0 < 0) y0 = 0;
        if (y1 > 31) y1 = 31;
        oled_rect(4, (uint8_t)y0, 10, (uint8_t)y1, 1);  // Игрок
      }
      oled_num(96, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 9: TAPPER
// ================================================================

// game_tapper — ловля падающих капель стаканом в 4 колонках
bool game_tapper() {
  msg_P(PSTR("TAPPER"), PSTR("B1=L B2=R"), 2000);

  const uint8_t COL_X[4] = { 8, 40, 72, 104 };  // X-координаты колонок
  const uint8_t CUP_W = 14;
  const uint8_t CUP_Y = 25;

  uint8_t cup_col = 1;  // Текущая колонка стакана (0..3)

  int8_t drop_y[4];     // Y-позиция капли, -1 = неактивна
  uint8_t drop_col[4];  // Колонка каждой капли
  uint8_t drop_cnt = 0;

  for (uint8_t i = 0; i < 4; i++) drop_y[i] = -1;

  uint16_t sc = 0;
  uint8_t lives = 3;
  uint32_t lt = millis();
  uint32_t ls = millis();
  uint16_t drop_ms = 60;     // Интервал падения капель (мс/шаг)
  uint16_t spawn_ms = 1400;  // Интервал появления новых капель

  uint8_t col_busy = 0;  // Битовая маска колонок с активными каплями

  while (1) {
    btn_upd();

    if (btn1() && cup_col > 0) cup_col--;
    if (btn2() && cup_col < 3) cup_col++;

    uint32_t now = millis();

    if (now - ls >= spawn_ms) {  // Спавн новой капли в свободной колонке
      ls = now;
      if (col_busy != 0x0F) {  // Хотя бы одна колонка свободна
        uint8_t try_col;
        uint8_t tries = 0;
        do {
          try_col = (uint8_t)random(4);
          tries++;
        } while ((col_busy & (1 << try_col)) && tries < 8);

        if (!(col_busy & (1 << try_col))) {
          for (uint8_t i = 0; i < 4; i++) {
            if (drop_y[i] == -1) {
              drop_y[i] = 0;
              drop_col[i] = try_col;
              col_busy |= (1 << try_col);
              break;
            }
          }
        }
      }
    }

    if (now - lt >= drop_ms) {  // Обновление физики капель
      lt = now;

      for (uint8_t i = 0; i < 4; i++) {
        if (drop_y[i] < 0) continue;

        drop_y[i]++;

        if (drop_y[i] >= (int8_t)CUP_Y) {  // Капля достигла линии стакана
          col_busy &= ~(1 << drop_col[i]);

          if (drop_col[i] == cup_col) {  // Капля поймана
            sc++;
            if (sc % 5 == 0 && drop_ms > 25) drop_ms -= 5;
            if (sc % 8 == 0 && spawn_ms > 600) spawn_ms -= 100;
          } else {  // Капля пропущена
            if (lives > 0) lives--;
            if (lives == 0) {
              drop_y[i] = -1;
              return end_screen(8, sc);
            }
          }
          drop_y[i] = -1;
        }
      }

      oled_clear();

      for (uint8_t y = 0; y < 32; y += 3) {  // Пунктирные разделители колонок
        oled_pixel(31, y, 1);
        oled_pixel(63, y, 1);
        oled_pixel(95, y, 1);
      }

      // Стакан
      uint8_t cx = COL_X[cup_col];
      oled_hline(CUP_Y, cx, cx + CUP_W - 1);
      oled_hline(CUP_Y + 4, cx, cx + CUP_W - 1);
      oled_vline(cx, CUP_Y, CUP_Y + 4);
      oled_vline(cx + CUP_W - 1, CUP_Y, CUP_Y + 4);

      for (uint8_t i = 0; i < 4; i++) {  // Капли (3 пикселя в форме треугольника)
        if (drop_y[i] < 0) continue;
        uint8_t dx = COL_X[drop_col[i]] + 6;
        uint8_t dy = (uint8_t)drop_y[i];
        oled_pixel(dx, dy, 1);
        oled_pixel(dx - 1, dy + 1, 1);
        oled_pixel(dx, dy + 1, 1);
        oled_pixel(dx + 1, dy + 1, 1);
      }

      for (uint8_t l = 0; l < lives; l++)  // Индикаторы жизней
        oled_pixel(118 + l * 3, 0, 1);

      oled_num(0, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 10: AVOIDER
// ================================================================

// game_avoider — уклонение от снарядов, летящих со всех сторон; счёт = секунды выживания
bool game_avoider() {
  msg_P(PSTR("AVOIDER"), PSTR("B1=UP B2=DN"), 2000);

  uint8_t pl_pos = 0;                 // Позиция игрока: 0 = верх, 1 = низ
  const uint8_t PL_Y[2] = { 8, 23 };  // Y-координаты двух позиций
  const uint8_t PL_X = 60;
  const uint8_t PL_R = 3;  // Полуразмер игрока

  const uint8_t MAX_PROJ = 6;
  int16_t proj_x[MAX_PROJ];
  int8_t proj_y[MAX_PROJ];
  int8_t proj_vx[MAX_PROJ];
  int8_t proj_vy[MAX_PROJ];
  bool proj_a[MAX_PROJ];
  for (uint8_t i = 0; i < MAX_PROJ; i++) proj_a[i] = false;

  uint16_t sc = 0;
  uint32_t lt = millis();
  uint32_t ls = millis();
  uint32_t sec_t = millis();
  uint16_t spawn_ms = 1200;
  uint16_t frame_ms = 50;

  while (1) {
    btn_upd();

    if (btn1() || btn2()) pl_pos ^= 1;  // Переключает позицию игрока

    uint32_t now = millis();

    if (now - sec_t >= 2000UL) {  // Отсчёт игровых секунд
      sec_t += 2000UL;
      sc++;
      if (spawn_ms > 500) spawn_ms -= 50;  // Учащение спавна снарядов
      if (frame_ms > 25) frame_ms -= 2;    // Ускорение физики
    }

    if (now - ls >= spawn_ms) {  // Спавн снаряда с произвольного направления
      ls = now;
      for (uint8_t i = 0; i < MAX_PROJ; i++) {
        if (!proj_a[i]) {
          uint8_t dir = (uint8_t)random(4);

          if (dir == 0) {  // Слева — летит горизонтально на Y игрока
            proj_x[i] = -3;
            proj_y[i] = (int8_t)PL_Y[pl_pos];
            proj_vx[i] = 2;
            proj_vy[i] = 0;
            proj_a[i] = true;
          } else if (dir == 1) {  // Справа — летит горизонтально на Y игрока
            proj_x[i] = 130;
            proj_y[i] = (int8_t)PL_Y[pl_pos];
            proj_vx[i] = -2;
            proj_vy[i] = 0;
            proj_a[i] = true;
          } else if (dir == 2) {  // Сверху — вертикальный, вне зоны игрока по X
            int16_t rx;
            if (random(2) == 0)
              rx = (int16_t)random(2, 55);
            else
              rx = (int16_t)random(66, 125);
            proj_x[i] = rx;
            proj_y[i] = -3;
            proj_vx[i] = 0;
            proj_vy[i] = 2;
            proj_a[i] = true;
          } else {  // Снизу — вертикальный, вне зоны игрока по X
            int16_t rx;
            if (random(2) == 0)
              rx = (int16_t)random(2, 55);
            else
              rx = (int16_t)random(66, 125);
            proj_x[i] = rx;
            proj_y[i] = 34;
            proj_vx[i] = 0;
            proj_vy[i] = -2;
            proj_a[i] = true;
          }
          break;
        }
      }
    }

    if (now - lt >= frame_ms) {  // Обновление физики и отрисовка кадра
      lt = now;

      uint8_t pl_y = PL_Y[pl_pos];

      oled_clear();

      oled_rect(PL_X - PL_R, pl_y - PL_R,
                PL_X + PL_R, pl_y + PL_R, 1);  // Игрок

      for (uint8_t i = 0; i < MAX_PROJ; i++) {
        if (!proj_a[i]) continue;

        proj_x[i] += proj_vx[i];
        proj_y[i] += proj_vy[i];

        if (proj_x[i] < -5 || proj_x[i] > 132 || proj_y[i] < -5 || proj_y[i] > 37) {
          proj_a[i] = false;  // Снаряд вышел за пределы экрана
          continue;
        }

        // Только горизонтальные снаряды убивают; вертикальные — визуальная помеха
        if (proj_vy[i] == 0) {
          bool hx = (proj_x[i] + 2 >= (int16_t)(PL_X - PL_R)) && (proj_x[i] - 2 <= (int16_t)(PL_X + PL_R));
          bool hy = (proj_y[i] + 2 >= (int8_t)(pl_y - PL_R)) && (proj_y[i] - 2 <= (int8_t)(pl_y + PL_R));
          if (hx && hy) {
            return end_screen(9, sc);
          }
        }

        // Снаряд рисуется крестиком
        int16_t dx16 = proj_x[i];
        int8_t dy8 = proj_y[i];
        if (dx16 >= 1 && dx16 <= 126 && dy8 >= 1 && dy8 <= 30) {
          uint8_t dx = (uint8_t)dx16;
          uint8_t dy = (uint8_t)dy8;
          oled_pixel(dx, dy, 1);
          oled_pixel(dx - 1, dy, 1);
          oled_pixel(dx + 1, dy, 1);
          oled_pixel(dx, dy - 1, 1);
          oled_pixel(dx, dy + 1, 1);
        }
      }

      oled_num(0, 0, sc);
      oled_update();
    }
  }
}

// ================================================================
// ИГРА 11: REFLECT
// ================================================================

// game_reflect — шар отскакивает между двумя стенками; игрок переключает активную стенку
bool game_reflect() {
  msg_P(PSTR("REFLECT"), PSTR("BTN=SWITCH"), 2000);

  float bx = 64, by = 16;      // Позиция шара
  float vx = 1.5f, vy = 1.0f;  // Скорость шара

  uint8_t wall = 0;  // Активная стенка: 0 = левая, 1 = правая
  const uint8_t WALL_L = 2;
  const uint8_t WALL_R = 125;
  const uint8_t WALL_H = 12;

  uint16_t sc = 0;
  uint32_t lt = millis();

  vx = 1.5f;  // Начальное направление — вправо от центра

  while (1) {
    btn_upd();

    if (btn1() || btn2()) wall ^= 1;  // Переключает активную стенку

    if (millis() - lt >= 20) {
      lt = millis();

      bx += vx;
      by += vy;

      if (by <= 1) {
        by = 1;
        vy = -vy;
      }  // Отражение от верха
      if (by >= 30) {
        by = 30;
        vy = -vy;
      }  // Отражение от низа

      if (bx >= WALL_R - 1) {  // Шар достиг правого края
        if (wall == 1) {       // Стенка активна — отражает
          bx = WALL_R - 1;
          vx = -vx;
          sc++;
          if (vx < 0) vx -= 0.1f;  // Ускорение по X
          else vx += 0.1f;
          if (vy < 0) vy -= 0.05f;  // Ускорение по Y
          else vy += 0.05f;
          if (vx > -1.0f && vx < 0) vx = -1.0f;
          if (vx < 4.0f && vx > 0) {}
          if (vx > 4.0f) vx = 4.0f;  // Ограничение максимальной скорости
          if (vx < -4.0f) vx = -4.0f;
        } else {
          return end_screen(10, sc);  // Стенки нет — шар потерян
        }
      }

      if (bx <= WALL_L + 1) {  // Шар достиг левого края
        if (wall == 0) {
          bx = WALL_L + 1;
          vx = -vx;
          sc++;
          if (vx < 0) vx -= 0.1f;
          else vx += 0.1f;
          if (vy < 0) vy -= 0.05f;
          else vy += 0.05f;
          if (vx > 4.0f) vx = 4.0f;
          if (vx < -4.0f) vx = -4.0f;
        } else {
          return end_screen(10, sc);
        }
      }

      oled_clear();

      if (wall == 0) {  // Активная левая стенка — сплошная линия
        oled_vline(WALL_L, 0, 31);
      } else {  // Неактивная — пунктир
        for (uint8_t y = 0; y < 32; y += 3)
          oled_pixel(WALL_L, y, 1);
      }

      if (wall == 1) {  // Активная правая стенка — сплошная линия
        oled_vline(WALL_R, 0, 31);
      } else {
        for (uint8_t y = 0; y < 32; y += 3)
          oled_pixel(WALL_R, y, 1);
      }

      uint8_t ibx = (uint8_t)bx;
      uint8_t iby = (uint8_t)by;
      oled_rect(ibx, iby, ibx + 2, iby + 2, 1);  // Шар

      oled_num(56, 0, sc);

      if (vx > 0) {  // Индикатор направления: стрелка вправо
        oled_char(116, 0, '>');
      } else {  // Индикатор направления: точечная стрелка влево (символ '<' отсутствует в шрифте)
        oled_pixel(8, 3, 1);
        oled_pixel(9, 3, 1);
        oled_pixel(10, 3, 1);
        oled_pixel(9, 2, 1);
        oled_pixel(9, 4, 1);
      }

      oled_update();
    }
  }
}

// ================================================================
// ИГРА 12: MORSE
// ================================================================

// game_morse — запоминание и воспроизведение последовательности точек и тире
bool game_morse() {
  msg_P(PSTR("MORSE"), PSTR("S=DOT L=DASH"), 2000);

  const uint8_t MAX_SEQ = 8;
  uint8_t seq[MAX_SEQ];  // Последовательность: 0 = точка, 1 = тире
  uint8_t seq_len = 0;
  uint16_t sc = 0;

  while (1) {
    if (seq_len < MAX_SEQ) {  // Добавляет новый элемент в последовательность
      seq[seq_len] = (uint8_t)random(2);
      seq_len++;
    }

    // Фаза показа последовательности
    for (uint8_t i = 0; i < seq_len; i++) {
      oled_clear();
      oled_num(108, 0, sc);

      uint8_t tw = 0;  // Расчёт ширины всей последовательности для центрирования
      for (uint8_t k = 0; k < seq_len; k++) {
        tw += seq[k] ? 10 : 4;
        if (k < seq_len - 1) tw += 3;
      }
      uint8_t sx = (128 - tw) / 2;

      for (uint8_t k = 0; k < seq_len; k++) {
        uint8_t w = seq[k] ? 10 : 4;
        if (k == i) oled_rect(sx, 12, sx + w - 1, 18, 1);  // Текущий элемент — залитый
        else oled_rect(sx, 12, sx + w - 1, 18, 0);         // Остальные — контурные
        sx += w + 3;
      }
      oled_text_center_P(24, PSTR("REMEMBER"));
      oled_update();
      wait_ms(seq[i] ? 700 : 350);  // Тире показывается дольше точки

      oled_clear();  // Пауза между символами
      oled_num(108, 0, sc);
      oled_text_center_P(24, PSTR("REMEMBER"));
      oled_update();
      wait_ms(200);
    }

    oled_clear();  // Переход к фазе ввода
    oled_num(108, 0, sc);
    oled_text_center_P(14, PSTR("YOUR TURN"));
    oled_update();
    wait_ms(800);
    btns_clear();

    // Фаза ввода
    for (uint8_t i = 0; i < seq_len; i++) {
      char buf[4];
      buf[0] = '0' + (i + 1);
      buf[1] = '/';
      buf[2] = '0' + seq_len;
      buf[3] = 0;

      while (btn1_held() || btn2_held()) {  // Ожидает полного отпускания перед замером
        btn_upd();
      }
      wait_ms(50);
      btns_clear();

      uint32_t ws = millis();
      bool got = false;
      uint32_t last_draw = 0;

      while (millis() - ws < 12000UL) {  // Таймаут 12 секунд на ввод символа
        btn_upd();

        uint32_t now = millis();
        if (now - last_draw >= 200) {  // Обновление экрана ожидания каждые 200 мс
          last_draw = now;
          uint32_t elapsed = now - ws;
          uint8_t rem = (uint8_t)((12000UL - elapsed) / 2000UL);
          oled_clear();
          oled_num(108, 0, sc);
          oled_text(0, 0, buf);
          oled_num(0, 24, rem);
          oled_text_center_P(14, PSTR("PRESS"));
          oled_update();
        }

        if (btn1_held() || btn2_held()) {
          got = true;
          break;
        }
      }

      if (!got) {
        msg_P(PSTR("TIMEOUT"), PSTR(""), 1500);
        return end_screen(11, sc);
      }

      uint32_t ps = millis();
      while (btn1_held() || btn2_held()) {  // Замер длительности удержания
        btn_upd();
        uint32_t hd = millis() - ps;
        oled_clear();
        oled_num(108, 0, sc);
        oled_text(0, 0, buf);
        uint8_t bw = (hd > 120) ? 120 : (uint8_t)hd;
        if (bw > 0) oled_hline(16, 4, 4 + bw);  // Полоса индикации длительности нажатия
        oled_text_center_P(24, PSTR("HOLD"));
        oled_update();
      }
      uint32_t dur = millis() - ps;
      uint8_t inp = (dur >= 1200UL) ? 1 : 0;  // Порог: >= 1200 мс = тире, иначе точка

      oled_clear();
      oled_num(108, 0, sc);
      oled_text(0, 0, buf);
      uint8_t iw = inp ? 10 : 4;
      uint8_t ix = (128 - iw) / 2;
      oled_rect(ix, 14, ix + iw - 1, 20, 1);  // Показывает введённый символ
      oled_update();
      wait_ms(300);

      if (inp != seq[i]) {  // Неверный ввод
        msg_P(PSTR("WRONG"), PSTR(""), 1500);
        return end_screen(11, sc);
      }

      wait_ms(400);
      btns_clear();
    }

    sc++;  // Раунд пройден
    oled_clear();
    oled_text_center_P(8, PSTR("OK"));
    oled_num(60, 20, sc);
    oled_update();
    wait_ms(1200);

    if (seq_len >= MAX_SEQ) seq_len = 0;  // Сброс длины при достижении максимума
  }
}

// ================================================================
// МЕНЮ со скроллингом (12 игр, 3 страницы: 4+4+4)
// ================================================================

const char GM0[] PROGMEM = "DODGE";
const char GM1[] PROGMEM = "FLAPPY";
const char GM2[] PROGMEM = "SNAKE";
const char GM3[] PROGMEM = "RACE";
const char GM4[] PROGMEM = "PONG";
const char GM5[] PROGMEM = "BRICK";
const char GM6[] PROGMEM = "INVADER";
const char GM7[] PROGMEM = "JUMPER";
const char GM8[] PROGMEM = "TAPPER";
const char GM9[] PROGMEM = "AVOIDER";
const char GM10[] PROGMEM = "REFLECT";
const char GM11[] PROGMEM = "MORSE";

// GM — массив указателей на названия игр в PROGMEM
const char* const GM[12] PROGMEM = {
  GM0, GM1, GM2, GM3, GM4, GM5, GM6, GM7, GM8, GM9, GM10, GM11
};

// build_entry формирует строку вида «ИМЯ(РЕКОРД)» для пункта меню, возвращает длину
uint8_t build_entry(uint8_t idx, char* buf) {
  uint8_t i = 0;
  const char* p = (const char*)pgm_read_word(&GM[idx]);
  char c;
  while ((c = pgm_read_byte(p++))) buf[i++] = c;
  buf[i++] = '(';
  uint16_t rec = records[idx];
  if (rec == 0) buf[i++] = '0';
  else {
    char tmp[6];
    uint8_t j = 0;
    while (rec) {
      tmp[j++] = '0' + (rec % 10);
      rec /= 10;
    }
    while (j) buf[i++] = tmp[--j];
  }
  buf[i++] = ')';
  buf[i] = 0;
  return i;
}

#define DIV_X 65         // X-координата вертикального разделителя меню
#define L_CONTENT_X 6    // Левая граница контента левой колонки
#define L_CONTENT_R 64   // Правая граница контента левой колонки
#define R_CONTENT_X 72   // Левая граница контента правой колонки
#define R_CONTENT_R 127  // Правая граница контента правой колонки
#define L_CONTENT_W (L_CONTENT_R - L_CONTENT_X + 1)
#define R_CONTENT_W (R_CONTENT_R - R_CONTENT_X + 1)

// Scroller — состояние горизонтального скроллинга для одного пункта меню
struct Scroller {
  uint8_t offset;    // Текущее смещение текста
  uint8_t max_off;   // Максимальное смещение
  int8_t dir;        // Направление скроллинга (+1 / -1)
  uint8_t pause;     // Счётчик паузы на краях
  bool need_scroll;  // Требуется ли скроллинг для данного текста
};

Scroller scr[4];       // Скроллеры для 4 видимых пунктов меню
char entry[4][14];     // Буферы строк пунктов меню
uint8_t entry_len[4];  // Длины строк пунктов меню

// games_on_page возвращает количество игр на указанной странице меню
uint8_t games_on_page(uint8_t page) {
  uint8_t base = page * 4;
  uint8_t cnt = 12 - base;
  return (cnt > 4) ? 4 : cnt;
}

// init_scrollers инициализирует скроллеры и строки для текущей страницы меню
void init_scrollers(uint8_t page) {
  uint8_t base = page * 4;
  uint8_t count = games_on_page(page);

  for (uint8_t i = 0; i < 4; i++) {
    if (i < count) {
      uint8_t idx = base + i;
      entry_len[i] = build_entry(idx, entry[i]);
      uint8_t col_w = (i / 2 == 0) ? L_CONTENT_W : R_CONTENT_W;
      uint16_t text_w = (uint16_t)entry_len[i] * 6;
      scr[i].offset = 0;
      scr[i].dir = 1;
      scr[i].pause = 40;
      if (text_w > col_w) {  // Текст шире колонки — скроллинг необходим
        scr[i].need_scroll = true;
        uint16_t ms = text_w - col_w;
        scr[i].max_off = (ms > 127) ? 127 : (uint8_t)ms;
      } else {
        scr[i].need_scroll = false;
        scr[i].max_off = 0;
      }
    } else {  // Пустая ячейка на неполной странице
      entry[i][0] = 0;
      entry_len[i] = 0;
      scr[i].need_scroll = false;
      scr[i].max_off = 0;
      scr[i].offset = 0;
    }
  }
}

// draw_menu отрисовывает текущую страницу меню с выделением выбранного пункта
void draw_menu(uint8_t s) {
  uint8_t page = s / 4;
  uint8_t base = page * 4;
  uint8_t count = games_on_page(page);

  oled_clear();
  oled_text_center_P(0, PSTR("RETRO ARCADE"));
  oled_hline(9, 0, 127);
  oled_vline(DIV_X, 10, 31);

  for (uint8_t i = 0; i < 4; i++) {
    if (i >= count) continue;

    uint8_t idx = base + i;
    uint8_t col = i / 2;  // 0 = левая колонка, 1 = правая
    uint8_t row = i % 2;  // 0 = верхняя строка, 1 = нижняя
    uint8_t y = 11 + row * 11;

    uint8_t arrow_x = (col == 0) ? 0 : (DIV_X + 1);
    uint8_t content_x = (col == 0) ? L_CONTENT_X : R_CONTENT_X;
    uint8_t content_r = (col == 0) ? L_CONTENT_R : R_CONTENT_R;

    oled_char(arrow_x, y, (idx == s) ? '>' : ' ');

    int16_t tx = (int16_t)content_x - (int16_t)scr[i].offset;
    oled_str_clipped(tx, y, entry[i], content_x, content_r);
  }

  oled_update();
}

// menu отображает главное меню и запускает выбранные игры
void menu() {
  uint8_t s = 0;
  uint8_t page = 0;

  init_scrollers(page);
  draw_menu(s);

  uint32_t last_scroll = millis();

  while (1) {
    btn_upd();

    if (btn1()) {  // Переход к следующему пункту с цикличностью
      s = (s + 1) % 12;
      uint8_t new_page = s / 4;
      if (new_page != page) {
        page = new_page;
        init_scrollers(page);
      }
      draw_menu(s);
    }

    if (btn2()) {  // Запуск выбранной игры с поддержкой повтора
      bool retry = true;
      while (retry) {
        switch (s) {
          case 0: retry = game_dodge(); break;
          case 1: retry = game_flappy(); break;
          case 2: retry = game_snake(); break;
          case 3: retry = game_race(); break;
          case 4: retry = game_pong(); break;
          case 5: retry = game_brick(); break;
          case 6: retry = game_invader(); break;
          case 7: retry = game_jumper(); break;
          case 8: retry = game_tapper(); break;
          case 9: retry = game_avoider(); break;
          case 10: retry = game_reflect(); break;
          case 11: retry = game_morse(); break;
        }
      }
      init_scrollers(page);
      draw_menu(s);
      last_scroll = millis();
    }

    if (millis() - last_scroll >= 80) {  // Анимация скроллинга длинных названий
      last_scroll = millis();
      bool need_redraw = false;

      for (uint8_t i = 0; i < 4; i++) {
        if (!scr[i].need_scroll) continue;
        need_redraw = true;
        if (scr[i].pause > 0) {  // Пауза на краях перед сменой направления
          scr[i].pause--;
          continue;
        }

        scr[i].offset = (uint8_t)((int16_t)scr[i].offset + scr[i].dir);

        if (scr[i].offset >= scr[i].max_off) {
          scr[i].offset = scr[i].max_off;
          scr[i].dir = -1;
          scr[i].pause = 40;
        } else if (scr[i].offset == 0) {
          scr[i].dir = 1;
          scr[i].pause = 40;
        }
      }

      if (need_redraw) draw_menu(s);
    }
  }
}

// ================================================================
// MAIN
// ================================================================

// main — точка входа: инициализирует периферию, показывает заставку и запускает меню
int main() {
  btn_init();
  i2c_init();
  timer_init();
  sei();
  oled_init();

  // Стирание EEPROM с рекордами при зажатой кнопке и подачи питания
  if (!(PIND & (1 << 3))) {  // Прямая проверка пина PD3 (LOW = кнопка нажата)
    for (uint8_t i = 0; i < 24; i++)
      eeprom_write_byte((uint8_t*)i, 0);
  }

  load_records();

  oled_clear();
  oled_text_center_P(0, PSTR("RETRO ARCADE"));
  oled_text_center_P(12, PSTR("12 GAMES V9.2"));
  oled_text_center_P(24, PSTR("BY OTTO"));
  oled_update();
  wait_ms(3500);

  menu();
  return 0;
}