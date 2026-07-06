/*
Author of base clock:Vincent, Hardware:2.0, Date 2023/4/17
improved clock: Nicu FLORICA (niq_ro/tehniq3)
v.1.0 - added digital clock
v.1.a - flashing seconds
v.1.b - added test touch to control hour (DST on/off)
v.1.b1 - changed DST (1 or 0) = summer or winter time
v.1.c - added 4 buttons (2 for adjust hours, 2 for minutes)
v.1.c1 - show virtual witch button was activated + increased numbers
v.2.0 - added NTP sync at power-on
v.2.a - removed 3 butons, remains one for activate the manual NTP sync
v.2.b - cut-off the backlight in time of NTP sync, added also automatic NTP sync at 5:00 
v.2b1 - changed the seconds increment and also "jump" for seconds 
v.2c - real NTP clock (sync every seconds), changed color for digital clock at every push
*/

#include <Arduino_GFX_Library.h>
#include "TAMC_GT911.h"

#include <WiFi.h>
#include <time.h>

// --- Configurare Retea WiFi ---
const char* ssid     = "bbk2";
const char* password = "internet2";

// --- Configurare NTP si Fus Orar Romania ---
const char* ntpServer = "pool.ntp.org";
// Folosim standardul POSIX pentru a seta automat ora de iarna/vara (GMT+2 / GMT+3)
const char* timeZone = "EET-2EEST,M3.5.0/3,M10.5.0/4";

#define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin
#define TFT_BL 2
/* More dev device declaration: https://github.com/moononournation/Arduino_GFX/wiki/Dev-Device-Declaration */
#if defined(DISPLAY_DEV_KIT)
Arduino_GFX *gfx = create_default_Arduino_GFX();
#else /* !defined(DISPLAY_DEV_KIT) */

/* More data bus class: https://github.com/moononournation/Arduino_GFX/wiki/Data-Bus-Class */
//Arduino_DataBus *bus = create_default_Arduino_DataBus();

/* More display class: https://github.com/moononournation/Arduino_GFX/wiki/Display-Class */
//Arduino_GFX *gfx = new Arduino_ILI9341(bus, DF_GFX_RST, 0 /* rotation */, false /* IPS */);

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    GFX_NOT_DEFINED /* CS */, GFX_NOT_DEFINED /* SCK */, GFX_NOT_DEFINED /* SDA */,
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
    5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
    8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */
);
// option 1:
// ST7262 IPS LCD 800x480
 Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(
   bus,
   800 /* width */, 0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
   480 /* height */, 0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
   1 /* pclk_active_neg */, 16000000 /* prefer_speed */, true /* auto_flush */);

#endif /* !defined(DISPLAY_DEV_KIT) */
/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

#define BACKGROUND BLACK
#define MARK_COLOR WHITE
#define SUBMARK_COLOR DARKGREY // LIGHTGREY
#define HOUR_COLOR WHITE
#define MINUTE_COLOR BLUE // LIGHTGREY
#define SECOND_COLOR RED
uint16_t clkcolor = 0x001F;
byte color = 7;
byte color1 = 9;

#define SIXTIETH 0.016666667
#define TWELFTH 0.08333333
#define SIXTIETH_RADIAN 0.10471976
#define TWELFTH_RADIAN 0.52359878
#define RIGHT_ANGLE_RADIAN 1.5707963

#define TOUCH_SDA  19
#define TOUCH_SCL  20
#define TOUCH_INT -1
#define TOUCH_RST 38
#define TOUCH_WIDTH  480
#define TOUCH_HEIGHT 272
//#define TOUCH_WIDTH 800
//#define TOUCH_HEIGHT 480

TAMC_GT911 tp = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

static uint8_t conv2d(const char *p)
{
    uint8_t v = 0;
    return (10 * (*p - '0')) + (*++p - '0');
}

static int16_t w, h, center;
static int16_t hHandLen, mHandLen, sHandLen, markLen;
static float sdeg, mdeg, hdeg;
static int16_t osx = 0, osy = 0, omx = 0, omy = 0, ohx = 0, ohy = 0; // Saved H, M, S x & y coords
static int16_t nsx, nsy, nmx, nmy, nhx, nhy;                         // H, M, S x & y coords
static int16_t xMin, yMin, xMax, yMax;                               // redraw range
static int16_t hh0, hh, mm, ss;
static unsigned long targetTime; // next action time

static int16_t *cached_points;
static uint16_t cached_points_idx = 0;
static int16_t *last_cached_point;

int hh1 = 60;
int mm1 = 60;
int ss1 = 60;

int apasari = 0;
int apasat = 0;
int dst = 0;
int dst0 = 0;
int dh = 0;
int dm = 0;

unsigned long tpapasare = 300; //100;
unsigned long tpapasat;

int bx = 500;
int by = 200;
int bdx = 100;
int bdy = 100;
int bds = 50;
int br = 20;


void setup(void)
{
  Serial.begin(115200);
  Serial.println(" ");
  Serial.println("NTP Clock with ESP32 and TAMC_GT911 touch");

  // ==========================================
  // INCEPUT: Cod adăugat pentru sincronizare NTP
  // ==========================================
  Serial.printf("Conectare la WiFi %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK!");

  // Configurare NTP si Fus orar Romania
  configTime(0, 0, ntpServer);
  setenv("TZ", timeZone, 1);
  tzset();

  // Preluare ora exacta
  Serial.print("Astept ora de la NTP...");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println(" Primita!");

  // Scriem ora primita in variabilele tale pe care le folosește ceasul
  hh   = timeinfo.tm_hour;
  mm = timeinfo.tm_min;
  ss = timeinfo.tm_sec;
  
  // Oprim WiFi-ul ca să nu mai consume curent și resurse, nu mai avem nevoie de el
  //WiFi.mode(WIFI_OFF);
  // ==========================================
  // SFARSIT: Cod adăugat pentru sincronizare NTP
  // ==========================================
    delay(1000);
    gfx->begin();
    gfx->fillScreen(BACKGROUND);

#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#endif

    // init LCD constant
    w = gfx->width();
    h = gfx->height();
    if (w < h)
    {
        center = w / 2;
    }
    else
    {
        center = h / 2;
    }
    hHandLen = center * 3 / 8;
    mHandLen = center * 2 / 3;
    sHandLen = center * 5 / 6;
    markLen = sHandLen / 6;
    cached_points = (int16_t *)malloc((hHandLen + 1 + mHandLen + 1 + sHandLen + 1) * 2 * 2);

    // Draw 60 clock marks
    draw_round_clock_mark(
    // draw_square_clock_mark(
        center - markLen, center,
        center - (markLen * 2 / 3), center,
        center - (markLen / 2), center);

hh0 = hh;
  //  hh0 = conv2d(__TIME__);
  //  mm = conv2d(__TIME__ + 3);
  //  ss = conv2d(__TIME__ + 6);

  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(100);
  digitalWrite(TOUCH_RST, HIGH);
  delay(100);

  tp.begin();
  tp.setRotation(ROTATION_INVERTED);
 // tp.setRotation(ROTATION_NORMAL);

 //  targetTime = ((millis() / 1000) + 1) * 1000;
   targetTime = millis();
        gfx->fillRect(500, 50, 800, 25, BLACK);
        gfx->setCursor(500, 50);
        gfx->setTextColor(RED,BLACK);
        gfx->setTextSize(3 /* x scale */, 3 /* y scale */, 0 /* pixel_margin */);
}

void loop()
{
  tp.read();
  if (millis() - tpapasat > tpapasare)
  {
  if (tp.isTouched) 
  {
    for (int i=0; i<tp.touches; i++)
    {
      Serial.print("Touch ");Serial.print(i+1);Serial.print(": ");;
      Serial.print("  x: ");Serial.print(1.66*tp.points[i].x);
      Serial.print("  y: ");Serial.print(1.66*tp.points[i].y);
      Serial.print("  size: ");Serial.println(tp.points[i].size);
      Serial.println(' ');

      if ((1.66*tp.points[i].x > bx+bdx+bds) and (1.66*tp.points[i].x < bx+bdx+bds+bdx))
      {
      if ((1.66*tp.points[i].y > by+bdy+bds) and (1.66*tp.points[i].y < by+bdy+bds+bdy))
      {
       gfx->fillRoundRect(bx+bdx+bds, by+bdy+bds, bdx, bdy, br, DARKGREY); 
       apasat = 4;
       Serial.println("Color changed");
      }
      }
    }
    
      if (apasat == 4)
      {
/*
// Color definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0 
#define WHITE    0xFFFF
*/
color++;
if (color > 7)
  color = 1;

if (color == 1) 
   clkcolor = BLUE; 
else
if (color == 2) 
   clkcolor = RED; 
else
if (color == 3) 
   clkcolor = GREEN; 
else
if (color == 4) 
   clkcolor = CYAN; 
else
if (color == 5) 
   clkcolor = MAGENTA; 
else
if (color == 6) 
   clkcolor = YELLOW; 
else
if (color == 7) 
   clkcolor = DARKGREY; 
      apasat = 0;
      }
    }
  tpapasat = millis();
  apasat = 0;
  }

   
    unsigned long cur_millis = millis();
 //   if (cur_millis >= targetTime)
    if (cur_millis - targetTime  >= 1000)
    {
    NTPsync();
    Serial.println(ss);
    targetTime = millis();
    }

    // Pre-compute hand degrees, x & y coords for a fast screen update
   // sdeg = SIXTIETH_RADIAN * ((0.001 * (cur_millis % 1000)) + ss); // 0-59 (includes millis)
    sdeg = SIXTIETH_RADIAN * (ss); // 0-59 (includes millis)
    nsx = cos(sdeg - RIGHT_ANGLE_RADIAN) * sHandLen + center;
    nsy = sin(sdeg - RIGHT_ANGLE_RADIAN) * sHandLen + center;
    if ((nsx != osx) || (nsy != osy))
    {
        mdeg = (SIXTIETH * sdeg) + (SIXTIETH_RADIAN * mm); // 0-59 (includes seconds)
        hdeg = (TWELFTH * mdeg) + (TWELFTH_RADIAN * hh);   // 0-11 (includes minutes)
        mdeg -= RIGHT_ANGLE_RADIAN;
        hdeg -= RIGHT_ANGLE_RADIAN;
        nmx = cos(mdeg) * mHandLen + center;
        nmy = sin(mdeg) * mHandLen + center;
        nhx = cos(hdeg) * hHandLen + center;
        nhy = sin(hdeg) * hHandLen + center;

        // redraw hands
        redraw_hands_cached_draw_and_erase();

        ohx = nhx;
        ohy = nhy;
        omx = nmx;
        omy = nmy;
        osx = nsx;
        osy = nsy;

        delay(1);
    }

    // numerical clock by niq_ro (tehniq3)
    gfx->setTextSize(12 /* x scale */, 18 /* y scale */, 0 /* pixel_margin */);
    if ((hh1 != hh) or (color1 != color))
    {
     gfx->setTextColor(BACKGROUND);
     gfx->setCursor(480, 20);
     gfx->print (hh1/10);
     gfx->print (hh1%10);
     gfx->setTextColor(clkcolor);  // DARKGREY);
     gfx->setCursor(480, 20);
     gfx->print (hh/10);
     gfx->print (hh%10);
     hh1 = hh;
    }

    gfx->setCursor(600, 20);
    if (ss%2 == 0)
      gfx->setTextColor(clkcolor);  //DARKGREY);
      else
      gfx->setTextColor(BACKGROUND);
      gfx->print(":");

    if ((mm1 != mm) or (color1 != color))
    {
     gfx->setTextColor(BACKGROUND);
     gfx->setCursor(650, 20);
     gfx->print (mm1/10);
     gfx->print (mm1%10);
     gfx->setTextColor(clkcolor);  //DARKGREY);
     gfx->setCursor(650, 20);
     gfx->print (mm/10);
     gfx->print (mm%10);
     mm1 = mm;
    }
/*
gfx->fillRoundRect(bx, by, bdx, bdy, br, RED);
gfx->fillRoundRect(bx, by+bdy+bds, bdx, bdy, br, GREEN);
gfx->fillRoundRect(bx+bdx+bds, by, bdx, bdy, br, YELLOW);
*/
gfx->fillRoundRect(bx+bdx+bds, by+bdy+bds, bdx, bdy, br, clkcolor);  //BLUE);

   gfx->setTextSize(3, 3, 0);
   gfx->setTextColor(WHITE);
   /*
   gfx->setCursor(bx, by-bds/2);
      gfx->print("H+");
   gfx->setCursor(bx+bdx+bds, by-bds/2);
      gfx->print("M+");
   gfx->setCursor(bx, by+bdy+bds-bds/2);
      gfx->print("H-");
      */
   gfx->setCursor(bx+bdx+bds, by+bdy+bds-bds/2);
      gfx->print("Color");

 color1 = color;    
}  // end main loop


void draw_round_clock_mark(int16_t innerR1, int16_t outerR1, int16_t innerR2, int16_t outerR2, int16_t innerR3, int16_t outerR3)
{
  float x, y;
  int16_t x0, x1, y0, y1, innerR, outerR;
  uint16_t c;

  for (uint8_t i = 0; i < 60; i++)
  {
    if ((i % 15) == 0)
    {
      innerR = innerR1;
      outerR = outerR1;
      c = MARK_COLOR;
    }
    else if ((i % 5) == 0)
    {
      innerR = innerR2;
      outerR = outerR2;
      c = MARK_COLOR;
    }
    else
    {
      innerR = innerR3;
      outerR = outerR3;
      c = SUBMARK_COLOR;
    }

    mdeg = (SIXTIETH_RADIAN * i) - RIGHT_ANGLE_RADIAN;
    x = cos(mdeg);
    y = sin(mdeg);
    x0 = x * outerR + center;
    y0 = y * outerR + center;
    x1 = x * innerR + center;
    y1 = y * innerR + center;

    gfx->drawLine(x0, y0, x1, y1, c);
  }
}

void draw_square_clock_mark(int16_t innerR1, int16_t outerR1, int16_t innerR2, int16_t outerR2, int16_t innerR3, int16_t outerR3)
{
    float x, y;
    int16_t x0, x1, y0, y1, innerR, outerR;
    uint16_t c;

    for (uint8_t i = 0; i < 60; i++)
    {
        if ((i % 15) == 0)
        {
            innerR = innerR1;
            outerR = outerR1;
            c = MARK_COLOR;
        }
        else if ((i % 5) == 0)
        {
            innerR = innerR2;
            outerR = outerR2;
            c = MARK_COLOR;
        }
        else
        {
            innerR = innerR3;
            outerR = outerR3;
            c = SUBMARK_COLOR;
        }

        if ((i >= 53) || (i < 8))
        {
            x = tan(SIXTIETH_RADIAN * i);
            x0 = center + (x * outerR);
            y0 = center + (1 - outerR);
            x1 = center + (x * innerR);
            y1 = center + (1 - innerR);
        }
        else if (i < 23)
        {
            y = tan((SIXTIETH_RADIAN * i) - RIGHT_ANGLE_RADIAN);
            x0 = center + (outerR);
            y0 = center + (y * outerR);
            x1 = center + (innerR);
            y1 = center + (y * innerR);
        }
        else if (i < 38)
        {
            x = tan(SIXTIETH_RADIAN * i);
            x0 = center - (x * outerR);
            y0 = center + (outerR);
            x1 = center - (x * innerR);
            y1 = center + (innerR);
        }
        else if (i < 53)
        {
            y = tan((SIXTIETH_RADIAN * i) - RIGHT_ANGLE_RADIAN);
            x0 = center + (1 - outerR);
            y0 = center - (y * outerR);
            x1 = center + (1 - innerR);
            y1 = center - (y * innerR);
        }
        gfx->drawLine(x0, y0, x1, y1, c);
    }
}

void redraw_hands_cached_draw_and_erase()
{
    gfx->startWrite();
    draw_and_erase_cached_line(center, center, nsx, nsy, SECOND_COLOR, cached_points, sHandLen + 1, false, false);
    draw_and_erase_cached_line(center, center, nhx, nhy, HOUR_COLOR, cached_points + ((sHandLen + 1) * 2), hHandLen + 1, true, false);
    draw_and_erase_cached_line(center, center, nmx, nmy, MINUTE_COLOR, cached_points + ((sHandLen + 1 + hHandLen + 1) * 2), mHandLen + 1, true, true);
    gfx->endWrite();
}

void draw_and_erase_cached_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t color, int16_t *cache, int16_t cache_len, bool cross_check_second, bool cross_check_hour)
{
#if defined(ESP8266)
    yield();
#endif
    bool steep = _diff(y1, y0) > _diff(x1, x0);
    if (steep)
    {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    int16_t dx, dy;
    dx = _diff(x1, x0);
    dy = _diff(y1, y0);

    int16_t err = dx / 2;
    int8_t xstep = (x0 < x1) ? 1 : -1;
    int8_t ystep = (y0 < y1) ? 1 : -1;
    x1 += xstep;
    int16_t x, y, ox, oy;
    for (uint16_t i = 0; i <= dx; i++)
    {
        if (steep)
        {
            x = y0;
            y = x0;
        }
        else
        {
            x = x0;
            y = y0;
        }
        ox = *(cache + (i * 2));
        oy = *(cache + (i * 2) + 1);
        if ((x == ox) && (y == oy))
        {
            if (cross_check_second || cross_check_hour)
            {
                write_cache_pixel(x, y, color, cross_check_second, cross_check_hour);
            }
        }
        else
        {
            write_cache_pixel(x, y, color, cross_check_second, cross_check_hour);
            if ((ox > 0) || (oy > 0))
            {
                write_cache_pixel(ox, oy, BACKGROUND, cross_check_second, cross_check_hour);
            }
            *(cache + (i * 2)) = x;
            *(cache + (i * 2) + 1) = y;
        }
        if (err < dy)
        {
            y0 += ystep;
            err += dx;
        }
        err -= dy;
        x0 += xstep;
    }
    for (uint16_t i = dx + 1; i < cache_len; i++)
    {
        ox = *(cache + (i * 2));
        oy = *(cache + (i * 2) + 1);
        if ((ox > 0) || (oy > 0))
        {
            write_cache_pixel(ox, oy, BACKGROUND, cross_check_second, cross_check_hour);
        }
        *(cache + (i * 2)) = 0;
        *(cache + (i * 2) + 1) = 0;
    }
}

void write_cache_pixel(int16_t x, int16_t y, int16_t color, bool cross_check_second, bool cross_check_hour)
{
    int16_t *cache = cached_points;
    if (cross_check_second)
    {
        for (uint16_t i = 0; i <= sHandLen; i++)
        {
            if ((x == *(cache++)) && (y == *(cache)))
            {
                return;
            }
            cache++;
        }
    }
    if (cross_check_hour)
    {
        cache = cached_points + ((sHandLen + 1) * 2);
        for (uint16_t i = 0; i <= hHandLen; i++)
        {
            if ((x == *(cache++)) && (y == *(cache)))
            {
                return;
            }
            cache++;
        }
    }
    gfx->writePixel(x, y, color);
}

void NTPsync() {
 // Serial.println("\n--- Incepe sincronizarea manuala ---");
/*  
  // 1. Pornim modulul WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectare WiFi...");
 */ 
  // Așteptăm conectarea, dar punem o limită de 10 secunde ca să nu se blocheze la nesfârșit
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Conectat!");
   /* 
    // 2. Forțăm o nouă cerere NTP
    configTime(0, 0, ntpServer);
    setenv("TZ", timeZone, 1);
    tzset();
    */
    // 3. Preluăm ora nouă
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // Suprascriem variabilele tale cu ora exactă de pe internet
      hh   = timeinfo.tm_hour;
      mm = timeinfo.tm_min;
      ss = timeinfo.tm_sec;
      Serial.printf("Ora actualizata cu succes: %02d:%02d:%02d\n", hh, mm, ss);
    } else {
      Serial.println("Eroare la obtinerea orei NTP!");
    }
  } else {
    Serial.println(" Esuat conexiunea la WiFi!");
  }
/*
  // 4. Închidem WiFi-ul pentru a economisi curent până mâine la 02:00
  WiFi.mode(WIFI_OFF);
  Serial.println("--- Sincronizare terminata, WiFi oprit ---\n");
 // targetTime = ((millis() / 1000) + 1) * 1000;
  targetTime = millis();
*/
}
