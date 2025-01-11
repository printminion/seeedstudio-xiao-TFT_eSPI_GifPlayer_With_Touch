/**
 * 1. Follow the instructions to setup the libraries in https://wiki.seeedstudio.com/get_start_round_display/
 * 2. Create "data" folder in SD-Card
 * 3. Put multiple gif images with size 240x240 (use https://ezgif.com/ to resize)
 * 4. Print nice 3d case https://cults3d.com/en/design-collections/printminion/seeed-studio-round-display-for-xiao-1-28-inch-round-touch-screen-240x240
 * 5. Support me: Buy Me A Coffee https://www.buymeacoffee.com/printminion
 * 6. Follow me: https://twitter.com/printminion
 */
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"

#include <vector>
#include <SD.h>
#include "AnimatedGIF.h"
#include <Preferences.h>

static const unsigned char PROGMEM image_micro_sd_no_card_bits[] = {0x3f,0xc0,0xa0,0x20,0x40,0x20,0x60,0x20,0x53,0x30,0x48,0x90,0x4c,0x88,0x42,0x08,0x49,0x10,0x4a,0x90,0x44,0xc8,0x40,0x28,0x47,0x90,0x58,0x68,0x37,0xd4,0x00,0x00};
static const unsigned char PROGMEM image_file_search_bits[] = {0x01,0xf0,0x02,0x08,0x04,0x04,0x08,0x02,0x08,0x02,0x08,0x0a,0x08,0x0a,0x08,0x12,0x04,0x64,0x0a,0x08,0x15,0xf0,0x28,0x00,0x50,0x00,0xa0,0x00,0xc0,0x00,0x00,0x00};

Preferences preferences;
AnimatedGIF gif;

// rule: loop GIF at least during 3s, maximum 5 times, and don't loop/animate longer than 30s per GIF
const int maxLoopIterations =     1; // stop after this amount of loops
const int maxLoopsDuration  =  3000; // ms, max cumulated time after the GIF will break loop
const int maxGifDuration    = 240000; // ms, max GIF duration
const int maxGifDurationMs  = 60 * 1000; // ms, max GIF duration

// used to center image based on GIF dimensions
static int xOffset = 0;
static int yOffset = 0;

static int totalFiles = 0; // GIF files count
static int currentFile = 0;
static int lastFile = -1;

char GifComment[256];

static File FSGifFile; // temp gif file holder
static File GifRootFolder; // directory listing
std::vector<std::string> GifFiles; // GIF files path

const int DO_PREVIOUS = 1;
const int DO_NEXT = 2;
const int DO_NOTHING = 3;

const int DO_CHANGE_MODE = 4;

const int PREF_MODE_STANDBY = 0; // wait for user to change the image
const int PREF_MODE_PLAYER = 1; // automatically change the picture after it played

// previous button position
int iPreviousButtonMarginLeft = 50;
int iPreviousButtonMarginTop = 120;

// next button position
int iNextButtonMarginLeft = 190;
int iNextButtonMarginTop = 120;

// mode button position
int iModeButtonMarginLeft = 120;
int iModeButtonMarginTop = 200;

int currentGifPlayedTime = 0;
int currentGifPlayedLoops = 0;

// foler path variable
const char *folderPath = "/data/";

uint16_t* tft_buffer;

unsigned int prefMode = PREF_MODE_STANDBY;
unsigned int prefCurrentFileIndex = 0;
unsigned long startTimeMs = 0;

bool isUiDemoSeen = false;
bool isUiDemoDisplayed = false;

static void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  log_d("GIFOpenFile( %s )\n", fname );
  FSGifFile = SD.open(fname);
  if (FSGifFile) {
    *pSize = FSGifFile.size();
    return (void *)&FSGifFile;
  }
  return NULL;
}

static void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
}

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  int32_t iBytesRead;
  iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  // Note: If you read a file all the way to the last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
      iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
  if (iBytesRead <= 0)
      return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
  // log_d("Seek time = %d us\n", i);
  return pFile->iPos;
}

static void TFTDraw(int x, int y, int w, int h, uint16_t* lBuf )
{
  tft.pushRect( x+xOffset, y+yOffset, w, h, lBuf );
}

// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > SCREEN_WIDTH)
      iWidth = SCREEN_WIDTH;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {// restore to background color
    for (x=0; x<iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
          s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) { // if transparency used
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while(x < iWidth) {
      c = ucTransparent-1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) { // done, stop
          s--; // back up to treat it like transparent
        } else { // opaque
            *d++ = usPalette[c];
            iCount++;
        }
      } // while looking for opaque pixels
      if (iCount) { // any opaque pixels?
        TFTDraw( pDraw->iX+x, y, iCount, 1, (uint16_t*)usTemp );
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
            iCount++;
        else
            s--;
      }
      if (iCount) {
        x += iCount; // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x=0; x<iWidth; x++)
      usTemp[x] = usPalette[*s++];
    TFTDraw( pDraw->iX, y, iWidth, 1, (uint16_t*)usTemp );
  }
} /* GIFDraw() */

int gifPlay( char* gifPath ) { // 0=infinite
  gif.begin(BIG_ENDIAN_PIXELS);
  if( ! gif.open( gifPath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw ) ) {
    log_n("Could not open gif %s", gifPath );

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor( TFT_WHITE, TFT_BLACK );
    tft.drawString( "Could not open gif", 20, tft.height()/2 );
    tft.drawString(String(gifPath), 20, tft.height()/2 + 40 );

    delay(300);

    return maxLoopsDuration;
  }

  int frameDelay = 0; // store delay for the last frame
  int then = 0; // store overall delay
  bool showcomment = false;
  currentGifPlayedLoops = 0;

  // center the GIF !!
  int w = gif.getCanvasWidth();
  int h = gif.getCanvasHeight();
  xOffset = ( tft.width()  - w )  /2;
  yOffset = ( tft.height() - h ) /2;

  if( lastFile != currentFile ) {
    log_n("Playing %s [%d,%d] with offset [%d,%d]", gifPath, w, h, xOffset, yOffset );
    lastFile = currentFile;
    showcomment = true;
  }

  while (gif.playFrame(true, &frameDelay)) {
    // if( showcomment )
    //   if (gif.getComment(GifComment))
    //     log_n("GIF Comment: %s", GifComment);

    then += frameDelay;

    unsigned long currentTimeMs = millis();
    unsigned long elapsedTimeMs = currentTimeMs - startTimeMs;

    // check if screen tapped
    //if(chsc6x_is_pressed()){
    int result = loopUI();

    // log_n("GIF played [prefMode: %d] for %d ms, result: %d, maxGifDurationMs: %d", prefMode, elapsedTimeMs, result, maxGifDurationMs);
    if (prefMode == PREF_MODE_PLAYER && elapsedTimeMs >= maxGifDurationMs) {
      log_n("Play next image");
      result = DO_NEXT;
    }

    if (result == DO_NOTHING) {
      then = 0;
    } else if (result == DO_NEXT) {
      log_d("LoopUI result: %d", result);
      then = maxGifDuration + DO_NEXT;
      gif.close();
      return then;
    } else if (result == DO_PREVIOUS) {
      log_d("LoopUI result: %d", result);
      then = maxGifDuration + DO_PREVIOUS;
      gif.close();
      return then;
    }

    //   // Serial.println("The display is touched.");
    //   // tft.fillScreen(TFT_RED);
    //   log_w("Broke the GIF loop, max duration exceeded");
    //   //then = maxGifDuration;
    //   //then = 0;
    //   break;
    // } else {
    //   then = 0;
    // }
  }

  gif.close();
  return then;
}

int getGifInventory(const char *basePath) {
  int amount = 0;
  GifRootFolder = SD.open(basePath);
  if (!GifRootFolder) {
    log_n("Failed to open directory");
    return 0;
  }

  if (!GifRootFolder.isDirectory()) {
    log_n("Not a directory");
    return 0;
  }

  File file = GifRootFolder.openNextFile();

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  int textPosX = tft.width() / 2 - 16;
  int textPosY = tft.height() / 2 - 10;

  tft.drawString("by", textPosX, textPosY - 90);
  tft.drawString("@printminion", textPosX - 60, textPosY - 60);

  tft.drawBitmap(42, textPosY + 20, image_file_search_bits, 15, 16, 0xFFFF);
  tft.drawString("GIF files:", textPosX - 40, textPosY + 20);

  //tft.drawString(folderPath, textPosX - 40, textPosY + 20);

  while (file) {
    if (!file.isDirectory()) {
      GifFiles.push_back(file.name());

      log_n("Found file: %s", file.name());

      amount++;
      tft.drawString(String(amount), textPosX, textPosY + 40);
      file.close();
    }
    file = GifRootFolder.openNextFile();
  }
  GifRootFolder.close();
  log_n("Found %d GIF files", amount);
  return amount;
}

void setup() {
  //Serial.begin(115200);

  // Open Preferences with my-app namespace. Each application module, library, etc
  // has to use a namespace name to prevent key name collisions. We will open storage in
  // RW-mode (second parameter has to be false).
  // Note: Namespace name is limited to 15 chars.
  preferences.begin("gif-player", false);

  // Get the mode value, if the key does not exist, return a default value of 0
  // Note: Key name is limited to 15 chars.
  prefMode = preferences.getUInt("mode", PREF_MODE_STANDBY);
  //Serial.printf("Current Mode: %u\n", prefMode);
  log_n("Current Mode: %u\n", prefMode);
  if (prefMode == PREF_MODE_STANDBY) {
    //Serial.printf("Current Mode: standby\n");
    log_n("Current Mode: standby\n");
  } else if (prefMode == PREF_MODE_PLAYER) {
    //Serial.printf("Current Mode: player\n");
    log_n("Current Mode: player\n");
  } else {
    //Serial.printf("Current Mode: unknown\n");
    log_n("Current Mode: unknown\n");
    prefMode = PREF_MODE_STANDBY;
    //Serial.printf("Current Mode: standby (fallback)\n");
    log_n("Current Mode: standby (fallback)\n");
  }

  prefCurrentFileIndex = preferences.getUInt("file_index", 0);
  isUiDemoSeen = preferences.getBool("intro_seen", false);

  //Serial.printf("CurrentFileIndex: %u\n", prefCurrentFileIndex);
  log_n("CurrentFileIndex: %u\n", prefCurrentFileIndex);
  currentFile = prefCurrentFileIndex;

  screen_rotation = 3;
  xiao_disp_init();

  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin();  // Turn on the IIC bus for touch driver

  prepareUI();

  // while (!Serial) ;
  // pinMode(D6, OUTPUT);
  // digitalWrite(D6, HIGH);

  int attempts = 0;
  int maxAttempts = 50;
  int delayBetweenAttempts = 300;
  bool isblinked = false;

  tft.setTextSize(2);

  pinMode(D2, OUTPUT);
  while (!SD.begin(D2)) {
//    Serial.print("SD Card mount failed! (attempt ");
//    Serial.print(attempts);
//    Serial.print(" of ");
//    Serial.print(maxAttempts);
//    Serial.println(")");
    log_n("SD Card mount failed! (attempt %d of %d)", attempts, maxAttempts);
    // log_n("SD Card mount failed! (attempt %d of %d)", attempts, maxAttempts );
    isblinked = !isblinked;
    attempts++;
    if (isblinked) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_BLACK, TFT_WHITE);
    }

    tft.drawBitmap(36, tft.height() / 2, image_micro_sd_no_card_bits, 14, 16, 0xFFFF);
    tft.drawString("INSERT SD", tft.width() / 2 - 50, tft.height() / 2);

    if (attempts > maxAttempts) {
      //Serial.println("Giving up");
      log_n("Giving up");
    }
    delay(delayBetweenAttempts);
  }

  // log_n("SD Card mounted!");
  //Serial.println("SD Card mounted!");
  log_n("SD Card mounted!");

  tft.begin();
  tft.fillScreen(TFT_BLACK);

  // scan the SD card GIF folder
  totalFiles = getGifInventory(folderPath);

  if (currentFile >= totalFiles) {
    currentFile = 0;
  }

  showUIDemo(true);
  delay(5000);
}

void showUIDemo(bool showControls) {
    tft.fillScreen(TFT_BLACK);

    drawBackButton(false);
    drawNextButton(false);
    drawModeSwitchButton(false);

    int marginTop = (tft.height() / 2) + 30;
    tft.drawString("prev", 40, marginTop);
    tft.drawString("next", 165, marginTop);

    if (prefMode == PREF_MODE_STANDBY) {
      tft.drawString("still", 90, 200);
    } else {
      tft.drawString("player", 90, 200);
    }

    tft.drawString("mode", 95, marginTop + 10);

    // dummy button
    int left = 90;
    int top = 90;

    if (showControls) {
      tft.drawString("controls", 75, 40);
      // tft.drawRoundRect(left, top, 60, 40, 5, TFT_WHITE);
      // tft.drawString("ok", left + 20, top + 10);
    }

 // int halfX = tft.width() / 2;
 // int halfY = tft.height() / 2;

  // tft.drawLine(0, halfX, tft.width(), halfX, TFT_RED);
  //tft.drawLine(halfY, 0, halfY, tft.height(), TFT_RED);
}

void loop() {

  if (!isUiDemoSeen) {
    if (!isUiDemoDisplayed) {
      showUIDemo(true);
      isUiDemoDisplayed = true;
      // isUiDemoSeen = true;
      delay(60);
    }

    if (chsc6x_is_pressed()) {
      isUiDemoSeen = true;
      preferences.putBool("intro_seen", isUiDemoSeen);
    }

    delay(60);
    return;
  }

  tft.fillScreen(TFT_BLACK);

  preferences.putUInt("file_index", currentFile);
  currentFile++;

  const char *fileName = GifFiles[currentFile % totalFiles].c_str();
  const char *fileDir = "/data/";
  char *filePath = (char *)malloc(strlen(fileName) + strlen(fileDir) + 1);
  strcpy(filePath, fileDir);
  strcat(filePath, fileName);

  int loops = maxLoopIterations;           // max loops
  int durationControl = maxLoopsDuration;  // force break loop after xxx ms
  currentGifPlayedTime = 0;

  startTimeMs = millis();

  //while(loops-->0 && durationControl > 0 ) {
  while (durationControl > 0) {
    int result = gifPlay((char *)filePath);

    if (result == maxGifDuration + DO_PREVIOUS) {
      durationControl = 0;
      currentFile = currentFile - 2;
      if (currentFile < 0) {
        currentFile = totalFiles;
      }

      preferences.putUInt("file_index", currentFile);

    } else if (result == maxGifDuration + DO_NEXT) {
      durationControl = 0;
    } else {
      durationControl -= result;
    }

    gif.reset();
  }
  free(filePath);
}

//////////////////////////////////
TFT_eSprite sprButtonPrevious = TFT_eSprite(&tft);       // Sprite object
TFT_eSprite sprButtonNext = TFT_eSprite(&tft);  // Sprite object for right arrow
TFT_eSprite sprButtonModeSelect = TFT_eSprite(&tft);  // Sprite object for mode button

int xw;
int yh;

lv_coord_t touchX, touchY;

bool isNextButtonTouched = false;
bool isPreviousButtonTouched = false;
bool isModeButtonTouched = false;

int triangleSideLength = 60;
int triangleSideLengthHalf = triangleSideLength / 2;

int lastTouchedX = 0;
int lastTouchedY = 0;
bool repaint = true;
bool isPressedLastState = false;

void prepareUI() {

  xw = tft.width() / 2;  // xw, yh is middle of screen
  yh = tft.height() / 2;

  int x1 = 0;
  int y1 = triangleSideLength;

  tft.setPivot(xw, yh);  // Set pivot to middle of TFT screen

  // Create the Sprite
  sprButtonPrevious.createSprite(triangleSideLength, triangleSideLength);
  sprButtonPrevious.fillSprite(TFT_TRANSPARENT);
  // sprButtonPrevious.fillTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_GREEN);

  sprButtonPrevious.fillTriangle(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, TFT_WHITE);
  sprButtonPrevious.drawTriangle(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, TFT_BLACK);

  sprButtonPrevious.drawWideLine(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 5, TFT_BLACK);
  sprButtonPrevious.drawWideLine(triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, 5, TFT_BLACK);
  sprButtonPrevious.drawWideLine(triangleSideLength - 1, 0, 0, triangleSideLengthHalf, 5, TFT_BLACK);

  //sprButtonNext.setColorDepth(8);
  sprButtonNext.createSprite(triangleSideLength, triangleSideLength);
  sprButtonNext.fillSprite(TFT_TRANSPARENT);
  sprButtonNext.fillTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_WHITE);
  sprButtonNext.drawTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_BLACK);

  sprButtonNext.drawWideLine(0, 0, 0, triangleSideLength, 5, TFT_BLACK);
  sprButtonNext.drawWideLine(0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, 5, TFT_BLACK);
  sprButtonNext.drawWideLine(0, 0, triangleSideLength, triangleSideLengthHalf, 5, TFT_BLACK);

  // create mode button
  sprButtonModeSelect.createSprite(triangleSideLength, triangleSideLength);
  sprButtonModeSelect.fillSprite(TFT_TRANSPARENT);

  // Calculate the size and allocate the buffer for the grabbed TFT area
  tft_buffer =  (uint16_t*) malloc( (triangleSideLength + 2) * (triangleSideLength + 2) * 2 );
}

void drawBackButton(bool selected) {
    int x1 = iPreviousButtonMarginLeft; // 60;
    int y1 = iPreviousButtonMarginTop; //120;

    // drawX(x1, y1);

    tft.setPivot(x1, y1);
    //sprButtonPrevious.pushRotated(180);
    sprButtonPrevious.pushSprite(x1 - triangleSideLengthHalf, y1 - triangleSideLengthHalf, TFT_TRANSPARENT);

    if (selected) {
      tft.drawRoundRect(
          x1 - triangleSideLengthHalf,
          y1 - triangleSideLengthHalf,
          triangleSideLength,
          triangleSideLength,
          3,
          TFT_WHITE
          );
    }
}

void drawNextButton(bool selected) {
    int x1 = iNextButtonMarginLeft; // 190;
    int y1 = iNextButtonMarginTop;// 120;
    // drawX(x1, y1);

    tft.setPivot(x1, y1);
    // sprButtonNext.pushRotated(0);
    sprButtonNext.pushSprite(x1 - triangleSideLengthHalf, y1 - triangleSideLengthHalf, TFT_TRANSPARENT);

    if (selected) {
      tft.drawRoundRect(
          x1 - triangleSideLengthHalf,
          y1 - triangleSideLengthHalf,
          triangleSideLength,
          triangleSideLength,
          3,
          TFT_WHITE
          );
    }
}

void drawModeSwitchButton(bool selected) {
    int x1 = iModeButtonMarginLeft;
    int y1 = iModeButtonMarginTop;

    int halfHeight = sprButtonModeSelect.height() / 2;
    int halfWidth = sprButtonModeSelect.width() / 2;

    // drawX(x1, y1);

    sprButtonModeSelect.fillScreen(TFT_TRANSPARENT);
    sprButtonModeSelect.fillRoundRect(0, 0, 60, 60, 3, TFT_BLACK);

    int topX = 0;
    int topY = 0;
    int btnHeight = triangleSideLength;
    int btnWidth = triangleSideLength;
    int borderThickness = 4;

    if (prefMode == PREF_MODE_STANDBY) {
      // draw pause icon
      sprButtonModeSelect.fillRoundRect(
          topX,
          topY,
          20,
          60,
          3,
          TFT_BLACK
          );
      sprButtonModeSelect.fillRoundRect(
          topX + borderThickness,
          topY + borderThickness,
          12,
          52,
          3,
          TFT_WHITE
          );

      sprButtonModeSelect.fillRoundRect(40, 0, 20, 60, 3, TFT_BLACK);
      sprButtonModeSelect.fillRoundRect(40 + borderThickness, 0 + borderThickness, 12, 52, 3, TFT_WHITE);
    } else {
      // draw play icon
      // sprButtonModeSelect.fillTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_BLACK);
      // sprButtonModeSelect.fillTriangle(4, 4, 0, triangleSideLength - 8, triangleSideLength - 8, triangleSideLengthHalf - 4, TFT_WHITE);

      sprButtonModeSelect.fillTriangle(
        topX,
        topY,
        topX,
        btnHeight + topY,
        btnWidth + topX,
        btnHeight /2 + topY,
        TFT_BLACK
      );

      sprButtonModeSelect.fillTriangle(
        topX + borderThickness,
        topY + borderThickness * 2,
        topX + borderThickness,
        btnHeight + topY - borderThickness * 2,
        btnWidth + topX - borderThickness * 2,
        btnHeight /2 + topY,
        TFT_WHITE
      );

    }

    tft.setPivot(x1, y1);
    sprButtonModeSelect.pushSprite(x1 - halfWidth, y1 - halfHeight, TFT_TRANSPARENT);

    if (selected) {
      tft.drawRoundRect(
          x1 - halfWidth,
          y1 - halfHeight,
          btnHeight,
          btnWidth,
          3,
          TFT_WHITE
          );
    }
}

/**
* Switch between player and standby mode
*/
void switchMode() {
  if (prefMode == PREF_MODE_STANDBY) {
    prefMode = PREF_MODE_PLAYER;
    preferences.putUInt("mode", PREF_MODE_PLAYER);
  } else {
    prefMode = PREF_MODE_STANDBY;
    preferences.putUInt("mode", PREF_MODE_STANDBY);
  }
  log_n("Switched mode to %d", prefMode);
}

bool isPointInRect(int touchX, int touchY, int topX, int topY, int rectWidth, int rectHeight) {
  log_n("isPointInRect: touchX: %d, touchY: %d, topX: %d, topY: %d, rectWidth: %d, rectHeight: %d", touchX, touchY, topX, topY, rectWidth, rectHeight);
    // Check if the touch point is within the x-range of the rectangle
    if (touchX >= topX && touchX <= topX + rectWidth) {
        // Check if the touch point is within the y-range of the rectangle
        if (touchY >= topY && touchY <= topY + rectHeight) {
            return true; // Point is inside the rectangle
        }
    }
    return false; // Point is outside the rectangle
}

bool isPointInCenteredRect(int touchX, int touchY, int topX, int topY, int rectWidth, int rectHeight) {
  // move coordinates to test centered rectngle
  topX = topX - rectWidth / 2;
  topY = topY - rectHeight / 2;

  log_n("isPointInCenteredRect: touchX: %d, touchY: %d, topX: %d, topY: %d, rectWidth: %d, rectHeight: %d", touchX, touchY, topX, topY, rectWidth, rectHeight);
    // Check if the touch point is within the x-range of the rectangle
    if (touchX >= topX && touchX <= topX + rectWidth) {
        // Check if the touch point is within the y-range of the rectangle
        if (touchY >= topY && touchY <= topY + rectHeight) {
            return true; // Point is inside the rectangle
        }
    }
    return false; // Point is outside the rectangle
}

int loopUI() {
  int result = DO_NOTHING;

  repaint = false;

  if (!chsc6x_is_pressed()) {
    if (isPressedLastState) {
      isPressedLastState = false;

      //tft.fillScreen(TFT_SKYBLUE);
      //tft.drawLine(0, xw, tft.width(), xw, TFT_RED);
      //tft.drawLine(xw, 0, xw, tft.width(), TFT_BLUE);
    }

    delay(60);
    // yield();
    return result;
  }

  // Serial.println("The display is touched.");
  log_d("The display is touched.");
  // tft.fillScreen(TFT_RED);
  chsc6x_get_xy(&touchX, &touchY);
  log_d("%dx%d", touchX, touchY);

  if (!isPressedLastState) {
    repaint = true;
    isPressedLastState = true;
  }

  if (lastTouchedX != touchX) {
    lastTouchedX = touchX;
    repaint = true;
  }

  if (lastTouchedY != touchY) {
    lastTouchedY = touchY;
    repaint = true;
  }

  if (!repaint) {
    delay(60);
    //yield();
    return result;
  }

  //tft.fillScreen(TFT_SKYBLUE);

  isNextButtonTouched = false;
  isPreviousButtonTouched = false;
  isModeButtonTouched = false;

  // debug touch events
  tft.fillCircle(touchX - 5, touchY - 5, 10, TFT_GREEN);

  if (isPointInCenteredRect(touchX, touchY, iModeButtonMarginLeft, iModeButtonMarginTop, sprButtonModeSelect.width(), sprButtonModeSelect.height())) {
    isModeButtonTouched = true;
  } else if (isPointInCenteredRect(touchX, touchY, iPreviousButtonMarginLeft, iPreviousButtonMarginTop, sprButtonNext.width(), sprButtonNext.height())) {
    isPreviousButtonTouched = true;
  } else if (isPointInCenteredRect(touchX, touchY, iNextButtonMarginLeft, iNextButtonMarginTop, sprButtonNext.width(), sprButtonNext.height())) {
    isNextButtonTouched = true;
  } else {
    showUIDemo(true);
    delay(1000);
  }

  if (isPreviousButtonTouched) {
    result = DO_PREVIOUS;
    //tft.fillCircle(touchX - 10, touchY - 10, 20, TFT_GREEN);
    // showUIDemo(false);
    drawBackButton(false);
    delay(1000);
  }

  if (isNextButtonTouched) {
    result = DO_NEXT;
    // tft.fillCircle(touchX - 10, touchY - 10, 20, TFT_RED);
    //showUIDemo(false);
    drawNextButton(false);
    delay(1000);
  }

  if (isModeButtonTouched) {
    switchMode();

    // Grab a copy of the area before button is drawn
    tft.readRect(
        iModeButtonMarginLeft - triangleSideLength / 2,
        iModeButtonMarginTop - triangleSideLength / 2,
        triangleSideLength,
        triangleSideLength,
        tft_buffer
        );

    // blink the button for 3 seconds to indicate the mode change
    for (int i = 0; i < 3; i++) {
      drawModeSwitchButton(true);
      delay(500);
      // Paste back the original image to area
      tft.pushRect(
        iModeButtonMarginLeft - triangleSideLength / 2,
        iModeButtonMarginTop - triangleSideLength / 2,
        triangleSideLength,
        triangleSideLength,
        tft_buffer
        );
    }
  }

  // tft.drawLine(0, xw, tft.width(), xw, TFT_RED);
  //tft.drawLine(xw, 0, xw, tft.width(), TFT_BLUE);

  delay(60);
  // yield();
  return result;
}
