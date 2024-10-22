#include "TFTs.h"
#include "WiFi_WPS.h"

TFTs::TFTs() : TFT_eSPI(), chip_select(), enabled(false), UnpackedImageBuffer(nullptr) {
    for (uint8_t digit = 0; digit < NUM_DIGITS; digit++) {
        digits[digit] = 0;
    }
}

TFTs::~TFTs() {
    freeImageBuffer();
}

// Then modify your existing begin() to work with the constructor:
void TFTs::begin() {
    // Start with all displays selected.
    chip_select.begin();
    chip_select.setAll();

    // Turn power on to displays.
    pinMode(TFT_ENABLE_PIN, OUTPUT);
    enableAllDisplays();
    InvalidateImageInBuffer();

    // Initialize the super class.
    init();

    // Set SPIFFS ready
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS initialization failed!");
        NumberOfClockFaces = 0;
        return;
    }

    // Allocate the image buffer
    if (!allocateImageBuffer()) {
        Serial.println(F("Warning: Failed to allocate image buffer"));
    }

    NumberOfClockFaces = CountNumberOfClockFaces();
    loadClockFacesNames();
}

void TFTs::reinit() {
  // Start with all displays selected.
  chip_select.begin();
  chip_select.setAll();

  // Turn power on to displays.
  pinMode(TFT_ENABLE_PIN, OUTPUT);
  enableAllDisplays();

  // Initialize the super class.
  init();
}

void TFTs::clear() {
  // Start with all displays selected.
  chip_select.setAll();
  enableAllDisplays();
}

void TFTs::loadClockFacesNames() {
  int8_t i = 0;
  const char* filename = "/clockfaces.txt";
  Serial.println("Load clock face's names");
  fs::File f = SPIFFS.open(filename);
  if(!f) {
    Serial.println("SPIFFS clockfaces.txt not found.");
    return;
  }
  while(f.available() && i<9) {
      patterns_str[i] = f.readStringUntil('\n');
      patterns_str[i].replace("\r", "");
      Serial.println(patterns_str[i]);
      i++;
    }
  f.close();
}



void TFTs::showNoMqttStatus() {
  chip_select.setSecondsTens();
  setTextColor(TFT_RED, TFT_BLACK);
  fillRect(0, TFT_HEIGHT - 27, TFT_WIDTH, 27, TFT_BLACK);
  setCursor(5, TFT_HEIGHT - 27, 4);
  print("NO MQTT !");
  }

void TFTs::showTemperature() { 
  #ifdef ONE_WIRE_BUS_PIN
   if (fTemperature > -30) { // only show if temperature is valid
      chip_select.setHoursOnes();
      setTextColor(TFT_CYAN, TFT_BLACK);
      fillRect(0, TFT_HEIGHT - 17, TFT_WIDTH, 17, TFT_BLACK);
      setCursor(5, TFT_HEIGHT - 17, 2);  // Font 2. 16 pixel high
      print("T: ");
      print(sTemperatureTxt);
      print(" C");
   }
#ifdef DEBUG_OUTPUT
    Serial.println("Temperature to LCD");
#endif    
  #endif
}

void TFTs::setDigit(uint8_t digit, uint8_t value, show_t show) {
  uint8_t old_value = digits[digit];
  digits[digit] = value;
  
  if (show != no && (old_value != value || show == force)) {
    showDigit(digit);
  

    if (digit == HOURS_ONES) {
        showTemperature();
      }
  }
}

/* 
 * Displays the bitmap for the value to the given digit. 
 */
 
void TFTs::showDigit(uint8_t digit) {
  chip_select.setDigit(digit);

  if (digits[digit] == blanked) {
    fillScreen(TFT_BLACK);
  }
  else {
    uint8_t file_index = current_graphic * 10 + digits[digit];
    DrawImage(file_index);
    
    uint8_t NextNumber = digits[SECONDS_ONES] + 1;
    if (NextNumber > 9) NextNumber = 0; // pre-load only seconds, because they are drawn first
    NextFileRequired = current_graphic * 10 + NextNumber;
  }
}

void TFTs::LoadNextImage() {
  if (NextFileRequired != FileInBuffer) {
#ifdef DEBUG_OUTPUT
    Serial.println("Preload next img");
#endif
    LoadImageIntoBuffer(NextFileRequired);
  }
}

void TFTs::InvalidateImageInBuffer() { // force reload from Flash with new dimming settings
  FileInBuffer=255; // invalid, always load first image
}

bool TFTs::FileExists(const char* path) {
    fs::File f = SPIFFS.open(path, "r");
    bool Exists = ((f == true) && !f.isDirectory());
    f.close();
    return Exists;
}

// These BMP functions are stolen directly from the TFT_SPIFFS_BMP example in the TFT_eSPI library.
// Unfortunately, they aren't part of the library itself, so I had to copy them.
// I've modified DrawImage to buffer the whole image at once instead of doing it line-by-line.


bool TFTs::allocateImageBuffer() {
    freeImageBuffer();  // Free any existing buffer
    
    size_t required_size = TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t);
    
    UnpackedImageBuffer = (uint16_t*)malloc(required_size);
    if (UnpackedImageBuffer == nullptr) {
        Serial.println(F("Failed to allocate image buffer"));
        return false;
    }
    
    return true;
}

void TFTs::freeImageBuffer() {
    if (UnpackedImageBuffer != nullptr) {
        free(UnpackedImageBuffer);
        UnpackedImageBuffer = nullptr;
    }
}



int8_t TFTs::CountNumberOfClockFaces() {
  int8_t i, found;
  char filename[10];

  Serial.print("Searching for BMP clock files... ");
  found = 0;
  for (i=1; i < 10; i++) {
    sprintf(filename, "/%d.bmp", i*10); // search for files 10.bmp, 20.bmp,...
    if (!FileExists(filename)) {
      found = i-1;
      break;
    }
  }
  Serial.print(found);
  Serial.println(" fonts found.");
  return found;
}

bool TFTs::LoadImageIntoBuffer(uint8_t file_index) {
    if (!isBufferAllocated() && !allocateImageBuffer()) {
        return false;
    }

    uint32_t StartTime = millis();
    fs::File bmpFS;
    char filename[10];
    
    #ifdef USE_CLK_FILES
    sprintf(filename, "/%d.clk", file_index);
    #else
    sprintf(filename, "/%d.bmp", file_index);
    #endif

    #ifdef DEBUG_OUTPUT
    Serial.print("Loading: ");
    Serial.println(filename);
    #endif
    
    bmpFS = SPIFFS.open(filename, "r");
    if (!bmpFS) {
        Serial.print("File not found: ");
        Serial.println(filename);
        return false;
    }

    // Clear buffer
    memset(UnpackedImageBuffer, 0, TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t));

    #ifdef USE_CLK_FILES
    // CLK file handling
    uint16_t magic = read16(bmpFS);
    if (magic != 0x4B43) { // "CK" header
        Serial.println("Invalid CLK file");
        bmpFS.close();
        return false;
    }

    int16_t w = read16(bmpFS);
    int16_t h = read16(bmpFS);
    int16_t x = (TFT_WIDTH - w) / 2;
    int16_t y = (TFT_HEIGHT - h) / 2;

    uint8_t lineBuffer[w * 2];
    
    for (int16_t row = 0; row < h; row++) {
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        
        for (int16_t col = 0; col < w; col++) {
            uint16_t color;
            if (dimming == 255) {
                color = (lineBuffer[col*2+1] << 8) | (lineBuffer[col*2]);
            } else {
                uint8_t PixM = lineBuffer[col*2+1];
                uint8_t PixL = lineBuffer[col*2];
                uint16_t r = ((PixM & 0xF8) * dimming) >> 8;
                uint16_t g = ((((PixM << 5) | (PixL >> 3)) & 0xFC) * dimming) >> 8;
                uint16_t b = (((PixL << 3) & 0xF8) * dimming) >> 8;
                color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            }
            // Convert 2D coordinates to 1D array index
            int32_t bufferIndex = (row + y) * TFT_WIDTH + (col + x);
            if (bufferIndex >= 0 && bufferIndex < TFT_WIDTH * TFT_HEIGHT) {
                UnpackedImageBuffer[bufferIndex] = color;
            }
        }
    }

    #else
    // BMP file handling
    uint16_t magic = read16(bmpFS);
    if (magic == 0xFFFF) {
        Serial.print("Can't open file. Make sure you upload the SPIFFs image with BMPs: ");
        Serial.println(filename);
        bmpFS.close();
        return false;
    }
    
    if (magic != 0x4D42) {
        Serial.print("File not a BMP. Magic: ");
        Serial.println(magic);
        bmpFS.close();
        return false;
    }

    read32(bmpFS); // filesize
    read32(bmpFS); // reserved
    uint32_t seekOffset = read32(bmpFS); // start of bitmap
    uint32_t headerSize = read32(bmpFS);
    int16_t w = read32(bmpFS);
    int16_t h = read32(bmpFS);
    read16(bmpFS); // color planes
    uint16_t bitDepth = read16(bmpFS);

    // Center image on display
    int16_t x = (TFT_WIDTH - w) / 2;
    int16_t y = (TFT_HEIGHT - h) / 2;

    #ifdef DEBUG_OUTPUT
    Serial.print(" image W, H, BPP: ");
    Serial.print(w); Serial.print(", "); 
    Serial.print(h); Serial.print(", "); 
    Serial.println(bitDepth);
    Serial.print(" dimming: ");
    Serial.println(dimming);
    Serial.print(" offset x, y: ");
    Serial.print(x); Serial.print(", "); 
    Serial.println(y);
    #endif

    if (read32(bmpFS) != 0 || (bitDepth != 24 && bitDepth != 1 && bitDepth != 4 && bitDepth != 8)) {
        Serial.println("BMP format not recognized.");
        bmpFS.close();
        return false;
    }

    // Handle color palette for low bit depth images
    uint32_t palette[256];
    uint32_t paletteSize = 0;
    if (bitDepth <= 8) {
        read32(bmpFS); read32(bmpFS); read32(bmpFS); // skip size, w/h resolution
        paletteSize = read32(bmpFS);
        if (paletteSize == 0) paletteSize = pow(2, bitDepth);
        bmpFS.seek(14 + headerSize);
        for (uint16_t i = 0; i < paletteSize; i++) {
            palette[i] = read32(bmpFS);
        }
    }

    bmpFS.seek(seekOffset);

    uint32_t lineSize = ((bitDepth * w + 31) >> 5) * 4;
    uint8_t lineBuffer[lineSize];

    // Read bottom-up BMP
    for (int16_t row = h - 1; row >= 0; row--) {
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t* bptr = lineBuffer;

        for (int16_t col = 0; col < w; col++) {
            uint8_t r, g, b;
            
            if (bitDepth == 24) {
                b = *bptr++;
                g = *bptr++;
                r = *bptr++;
            } else {
                uint32_t c = 0;
                if (bitDepth == 8) {
                    c = palette[*bptr++];
                }
                else if (bitDepth == 4) {
                    c = palette[(*bptr >> ((col & 0x01) ? 0 : 4)) & 0x0F];
                    if (col & 0x01) bptr++;
                }
                else { // bitDepth == 1
                    c = palette[(*bptr >> (7 - (col & 0x07))) & 0x01];
                    if ((col & 0x07) == 0x07) bptr++;
                }
                b = c;
                g = c >> 8;
                r = c >> 16;
            }

            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            if (dimming < 255) {
                // Apply dimming
                r = (((color >> 11) & 0x1F) * dimming) >> 8;
                g = (((color >> 5) & 0x3F) * dimming) >> 8;
                b = ((color & 0x1F) * dimming) >> 8;
                color = (r << 11) | (g << 5) | b;
            }

            // Convert 2D coordinates to 1D array index
            int32_t bufferIndex = (row + y) * TFT_WIDTH + (col + x);
            if (bufferIndex >= 0 && bufferIndex < TFT_WIDTH * TFT_HEIGHT) {
                UnpackedImageBuffer[bufferIndex] = color;
            }
        }
    }
    #endif

    FileInBuffer = file_index;
    bmpFS.close();

    #ifdef DEBUG_OUTPUT
    Serial.print("img load time: ");
    Serial.println(millis() - StartTime);
    #endif

    return true;
}

// Modify DrawImage to use 1D array
void TFTs::DrawImage(uint8_t file_index) {
    if (!isBufferAllocated()) {
        if (!allocateImageBuffer()) {
            return;
        }
    }

    uint32_t StartTime = millis();
    #ifdef DEBUG_OUTPUT
    Serial.println("");  
    Serial.print("Drawing image: ");  
    Serial.println(file_index);  
    #endif  

    if (file_index != FileInBuffer) {
        #ifdef DEBUG_OUTPUT
        Serial.println("Not preloaded; loading now...");  
        #endif  
        LoadImageIntoBuffer(file_index);
    }
    
    bool oldSwapBytes = getSwapBytes();
    setSwapBytes(true);
    pushImage(0, 0, TFT_WIDTH, TFT_HEIGHT, UnpackedImageBuffer);
    setSwapBytes(oldSwapBytes);

    #ifdef DEBUG_OUTPUT
    Serial.print("img transfer time: ");  
    Serial.println(millis() - StartTime);  
    #endif
}


// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t TFTs::read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t TFTs::read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

String TFTs::clockFaceToName(uint8_t clockFace) {
  return patterns_str[clockFace -1];
}

uint8_t TFTs::nameToClockFace(String name) {
  for(int i=0; i<9; i++) {
    if(patterns_str[i] == name) {
      return i+1;
    }
  }
  return 1;
}
//// END STOLEN CODE
