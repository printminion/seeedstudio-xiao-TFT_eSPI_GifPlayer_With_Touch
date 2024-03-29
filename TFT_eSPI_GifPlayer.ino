#define USE_TFT_ESPI_LIBRARY

#include <vector>
/**
 * 1. install TFT_eSPI library
 * enable #include <User_Setups/Setup66_Seeed_XIAO_Round.h>
 * in .\Arduino\libraries\TFT_eSPI\User_Setup_Select.h
 * 2. install lvgl v8.* (it wont work with v9). Copy lv_conf.h
 * from https://github.com/Seeed-Studio/Seeed_Arduino_RoundDisplay/tree/main?tab=readme-ov-file#note
 * as described in notes
 * 3. Create "data" folder in SD-Card
 * 4. Put multiple gif images with size 240x240 (use https://ezgif.com/ to resize)
 * 
 * 5. Print nice 3d case https://cults3d.com/en/design-collections/printminion/seeed-studio-round-display-for-xiao-1-28-inch-round-touch-screen-240x240
 * 6. Support me: Buy Me A Coffee https://www.buymeacoffee.com/printminion
 * 7. Follow me: https://twitter.com/printminion
 */
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>

#include "AnimatedGIF.h"

#include "lv_xiao_round_screen.h"

AnimatedGIF gif;

// rule: loop GIF at least during 3s, maximum 5 times, and don't loop/animate longer than 30s per GIF
const int maxLoopIterations =     1; // stop after this amount of loops
const int maxLoopsDuration  =  3000; // ms, max cumulated time after the GIF will break loop
const int maxGifDuration    =240000; // ms, max GIF duration

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
#define DISPLAY_WIDTH 240

const int DO_BACK = 1;
const int DO_NEXT = 2;
const int DO_NOTHING = 3;

static void MyCustomDelay( unsigned long ms ) {
  delay( ms );
  // log_d("delay %d\n", ms);
}

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
  if (iWidth > DISPLAY_WIDTH)
      iWidth = DISPLAY_WIDTH;
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

int gifPlay( char* gifPath )
{ // 0=infinite
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

    // check if screen tapped
    //if(chsc6x_is_pressed()){
    int result = loopUI();

    if (result == DO_NOTHING) {
      then = 0;
    } else if (result == DO_NEXT) {
      log_d("LoopUI result: %d", result);
      then = maxGifDuration + DO_NEXT;
      gif.close();
      return then;
    } else if (result == DO_BACK) {
      log_d("LoopUI result: %d", result);
      then = maxGifDuration + DO_BACK;
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

  tft.drawString("GIF Files:", textPosX - 40, textPosY + 20);
  
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
  Serial.begin(115200);
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

  pinMode(D2, OUTPUT);
  while (!SD.begin(D2)) {
    Serial.print("SD Card mount failed! (attempt ");
    Serial.print(attempts);
    Serial.print(" of ");
    Serial.print(maxAttempts);
    Serial.println(")");
    // log_n("SD Card mount failed! (attempt %d of %d)", attempts, maxAttempts );
    isblinked = !isblinked;
    attempts++;
    if (isblinked) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    tft.drawString("INSERT SD", tft.width() / 2, tft.height() / 2);

    if (attempts > maxAttempts) {
      Serial.println("Giving up");
    }
    delay(delayBetweenAttempts);
  }

  // log_n("SD Card mounted!");
  Serial.print("SD Card mounted!");

  tft.begin();
  tft.fillScreen(TFT_BLACK);

  totalFiles = getGifInventory("/data");  // scan the SD card GIF folder

  showUIDemo();
  delay(5000);
}

void showUIDemo() {
    tft.fillScreen(TFT_BLACK);
    drawBackButton();
    drawNextButton();
    int marginTop = (tft.height() / 2) + 40;
    tft.drawString("controls", 80, 40);
    tft.drawString("back", 40, marginTop);
    tft.drawString("next", 165, marginTop);

    // dummy button 
    tft.drawRoundRect(95, 190, 60, 40, 5, TFT_WHITE);
    tft.drawString("ok", 115, 200);

}

bool isUiDemoSeen = false;
bool isUiDemoDisplayed = false;

void loop() {
  
  if (!isUiDemoSeen) {
    
    if (!isUiDemoDisplayed) {
      showUIDemo();
      isUiDemoDisplayed = true;
    }
      
    if (chsc6x_is_pressed()) {
      isUiDemoSeen = true;
    }
    
    delay(60);
    return;
  }

  tft.fillScreen(TFT_BLACK);

 
  const char *fileName = GifFiles[currentFile++ % totalFiles].c_str();
  const char *fileDir = "/data/";
  char *filePath = (char *)malloc(strlen(fileName) + strlen(fileDir) + 1);
  strcpy(filePath, fileDir);
  strcat(filePath, fileName);

  int loops = maxLoopIterations;           // max loops
  int durationControl = maxLoopsDuration;  // force break loop after xxx ms

  //while(loops-->0 && durationControl > 0 ) {
  while (durationControl > 0) {
    int result = gifPlay((char *)filePath);

    if (result == maxGifDuration + DO_BACK) {
      durationControl = 0;
      currentFile = currentFile - 2;
      if (currentFile < 0) {
        currentFile = totalFiles;
      }
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
TFT_eSprite spr = TFT_eSprite(&tft);       // Sprite object
TFT_eSprite sprRight = TFT_eSprite(&tft);  // Sprite object

int xw;
int yh;

lv_coord_t touchX, touchY;

bool isRightButton = false;
bool isLeftButton = false;

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

  sprRight.createSprite(triangleSideLength, triangleSideLength);

  // Create the Sprite
  //spr.setColorDepth(8);
  spr.createSprite(triangleSideLength, triangleSideLength);
  spr.fillSprite(TFT_TRANSPARENT);
  // spr.fillTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_GREEN);

  spr.fillTriangle(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, TFT_WHITE);
  spr.drawTriangle(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, TFT_BLACK);
  
  spr.drawWideLine(triangleSideLength - 1, 0, triangleSideLength - 1, triangleSideLength, 5, TFT_BLACK);
  spr.drawWideLine(triangleSideLength - 1, triangleSideLength, 0, triangleSideLengthHalf, 5, TFT_BLACK);
  spr.drawWideLine(triangleSideLength - 1, 0, 0, triangleSideLengthHalf, 5, TFT_BLACK);

  //sprRight.setColorDepth(8);
  sprRight.createSprite(triangleSideLength, triangleSideLength);
  sprRight.fillSprite(TFT_TRANSPARENT);
  sprRight.fillTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_WHITE);
  sprRight.drawTriangle(0, 0, 0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, TFT_BLACK);
  
  sprRight.drawWideLine(0, 0, 0, triangleSideLength, 5, TFT_BLACK);
  sprRight.drawWideLine(0, triangleSideLength, triangleSideLength, triangleSideLengthHalf, 5, TFT_BLACK);
  sprRight.drawWideLine(0, 0, triangleSideLength, triangleSideLengthHalf, 5, TFT_BLACK);
}

void drawBackButton() {
    int x1 = 60;
    int y1 = 120;

    // drawX(x1, y1);

    tft.setPivot(x1, y1);
    //spr.pushRotated(180);
    spr.pushSprite(x1 - triangleSideLengthHalf, y1 - triangleSideLengthHalf, TFT_TRANSPARENT);
}

void drawNextButton() {
    int x1 = 190;
    int y1 = 120;
    // drawX(x1, y1);

    tft.setPivot(x1, y1);
    // sprRight.pushRotated(0);
    sprRight.pushSprite(x1 - triangleSideLengthHalf, y1 - triangleSideLengthHalf, TFT_TRANSPARENT);
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

  isRightButton = false;
  isLeftButton = false;

  if (touchX < 100) {
    isLeftButton = true;
  } else if (touchX > 140) {
    isRightButton = true;
  }

  if (isLeftButton) {
    result = DO_BACK;
    //tft.fillCircle(touchX - 10, touchY - 10, 20, TFT_GREEN);
    drawBackButton();
  }

  if (isRightButton) {
    result = DO_NEXT;
    // tft.fillCircle(touchX - 10, touchY - 10, 20, TFT_RED);
    drawNextButton();
  }

  // tft.drawLine(0, xw, tft.width(), xw, TFT_RED);
  //tft.drawLine(xw, 0, xw, tft.width(), TFT_BLUE);

  delay(60);
  // yield();
  return result;
}
