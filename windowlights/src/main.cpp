#include <Arduino.h>
#include <NeoPixelBus.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

#define HEIGHT 78
#define WIDTH 46
#define OUPUTPIN 2
#define SOCKETTIMOUT 100
#define FRAMECOUNT 30

//Bottom Left always 0
//sequential anti-clockwise
#define PIXELCOUNT (2 * HEIGHT + 2 * (WIDTH - 2))

Ticker memLog;

//Setup LED "strip"
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(PIXELCOUNT, OUPUTPIN);

/*
Modes:
    0   Sleep
    1   Single Frame
    2   Frame Buffer
    3   Frame Buffer Loop
*/
byte currentMode = 0x00;
bool isAnimating = false;

//Config Options, store in flash
byte colourOffsets[3] = {0x00, 0x00, 0x00};
#define BRIGHTNESS 1

class Frame {
    public:
        byte pixels[PIXELCOUNT][3];
        unsigned long delay;
        byte isUsed = false;
};

Frame frameBuffer[FRAMECOUNT];

//Setup Socket
WiFiServer wifiServer(42069);

//Function defs
void writeField();
void readPixel(WiFiClient);
void readPixels(WiFiClient);
void readFrame(WiFiClient);
void readFrames(WiFiClient);
void changeMode(WiFiClient);
bool waitForPackets(WiFiClient, int);
void clearFrameBuffer();
void blankOutput();
void updateDisplay();
void writeFrameToDisplay(Frame);
void readAnimation(WiFiClient);
RgbColor parseColour(byte, byte, byte);
RgbColor parseColour(WiFiClient);
unsigned long parseDelay(WiFiClient);

//Animation config
byte activeAnimation;
RgbColor animationColour1;
RgbColor animationColour2;
unsigned long animationDelay;

void logMem() {
    Serial.printf("%d bytes free memory\n", ESP.getFreeHeap());
    if(isAnimating)
        Serial.println("Animating");
    else
        Serial.println("Not Animating");

    Serial.printf("Current Mode: %d\n", currentMode);
}

void setup() {
    //Setup serial output
    Serial.begin(115200);

    //Setup and connect to wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin("Muninn", "8586197600");

    while(WiFi.status() != WL_CONNECTED)
        delay(100);

    Serial.println("Finished waiting for wifi");
    
    clearFrameBuffer();

    //Start strip
    strip.Begin();
    
    blankOutput();

    Serial.println("Setup Finished");

    Serial.printf("%d Pixels\n", PIXELCOUNT);

    //Begin listening on port.
    wifiServer.begin();
    Serial.println(WiFi.localIP());

    memLog.attach(30, logMem);

    logMem();
}

void loop() {
    delay(1);

    updateDisplay();

    //Serial.println(ESP.getFreeHeap());
    WiFiClient client = wifiServer.available();
    if(!client) return;

    Serial.println("Client Connected");
    uint8_t buffer;
    while(client.connected()) {
        delay(1);
        //connection recieved, wait for first byte
        // if(!waitForPackets(client, 1)){
        //     Serial.println("did not get byte\n");
        //     break;
        // }

        //First byte has been received, read it.
        client.read(&buffer, 1);

        switch (buffer) {
            case 0x01:
                //Change mode
                //Serial.println("changing mode");
                changeMode(client);
                break;
            case 0x02:
                //Set Frame
                Serial.println("receiving frame");
                readFrame(client);
                break;
            case 0x03:
                //Set Frames
                Serial.println("receiving frames");
                readFrames(client);
                break;
            case 0x04:
                //Set Pixel
                Serial.println("receiving pixel");
                readPixel(client);
                break;
            case 0x05:
                //Pixels
                Serial.println("receiving pixels");
                readPixels(client);
                break;
            case 0x06:
                //Clear Frame Buffer
                Serial.println("clearing frame buffer");
                clearFrameBuffer();
                break;
            default:
                Serial.printf("invalid instruction %d\n", buffer);
                break;
        }
        break;
    }
    Serial.println("Client Disconnected");
    client.stop();
}

int frameCounter = 0;
unsigned long lastFrameTime = 0;

void updateDisplay() {
    if(!isAnimating) return;
    
    if(frameCounter > FRAMECOUNT - 1 ){
        if(currentMode == 2)
            isAnimating = false;
        frameCounter = 0;
    }

    if(!frameBuffer[frameCounter].isUsed){
        frameCounter++;
        return;
    }

    Serial.println("Starting display update");

    isAnimating = false;

    if(millis() - lastFrameTime >= frameBuffer[frameCounter].delay){
        writeFrameToDisplay(frameBuffer[frameCounter++]);
    }

    isAnimating = true;
}

void writeFrameToDisplay(Frame frame) {
    Serial.println("Writing frame to display");

    for(int i = 0; i < PIXELCOUNT; i++) {
        strip.SetPixelColor(i, RgbColor(frame.pixels[i][0],frame.pixels[i][1],frame.pixels[i][2]));
    }

    strip.Show();
    lastFrameTime = millis();
}

void readPixel(WiFiClient client) {
    Serial.println("read pixel");
    if(!waitForPackets(client, 4)) return;
    byte buffer[4];

    client.read(buffer, 4);

    Serial.printf("setting pixel %d to %d %d %d\n", buffer[0], buffer[1], buffer[2], buffer[3]);

    strip.SetPixelColor(buffer[0], RgbColor(buffer[1], buffer[2], buffer[3]));
    strip.Show();
}

void readPixels(WiFiClient client) {
    Serial.println("read pixel");

    if(!waitForPackets(client, 1)) return;

    //Read first byte, pixel count
    byte numPixels;
    client.read(&numPixels, 1);

    byte buffer[4];

    for(;numPixels > 0; numPixels--) {
        if(!waitForPackets(client, 4)) return;

        client.read(buffer, 4);

        Serial.printf("setting pixel %d to %d %d %d\n", buffer[0], buffer[1], buffer[2], buffer[3]);

        strip.SetPixelColor(buffer[0], RgbColor(buffer[1], buffer[2], buffer[3]));
    }

    strip.Show();
}

void readFrame(WiFiClient client) {
    //FRAME NUMBER + FRAME DELAY MSB + FRAME DELAY LSB + (PIXELCOUNT * 3 )
    byte buffer[1 + 2 + 2 + (PIXELCOUNT * 3)];
    if(!waitForPackets(client, sizeof(buffer))) return;

    client.read(buffer, sizeof(buffer));

    Frame frame;
    frame.delay = (buffer[1] << 24) | (buffer[2] << 16) | (buffer[3] << 8) | buffer[4];
    frame.isUsed = true;

    //j is buffer position index
    for(int i = 0, j = 4; i< PIXELCOUNT; i++) {
        frame.pixels[i][0] = buffer[++j];
        frame.pixels[i][1] = buffer[++j];
        frame.pixels[i][2] = buffer[++j];
    }

    frameBuffer[buffer[0]] = frame;

    //if reading frame 0 and in single frame mode, output frame
    if(((int)buffer[0]) == 0 && currentMode == 1) {
        writeFrameToDisplay(frameBuffer[0]);
    }
}

void readFrames(WiFiClient client) {

}

void readAnimation(WiFiClient client) {
    // if(!waitForPackets(client, 2)) return;
    // byte buffer[2];

    // client.read(buffer, 2);

    // activeAnimation = buffer[1];

    // switch (buffer[0])
    // {
    //     case 0x00: //snake
    //     case 0x01: //fade
    //         animationColour1 = parseColour(client);
    //         animationColour2 = parseColour(client);
    //         break;
    //     case 0x02: //breath
    //         animationColour1 = parseColour(client);
    //         break;
    // }

    // animationDelay = parseDelay(client);
}

void changeMode(WiFiClient client) {
    //if(!waitForPackets(client, 1)) return;
    byte buffer;

    client.read(&buffer, 1);
    Serial.printf("Setting mode from %d to %d\n", currentMode, buffer);
    currentMode = buffer;

    switch (buffer)
    {
        case 0x01:  //Single frame
            isAnimating = false;
            Serial.println("mode set to single frame");
            break;
        case 0x02:  //Frame Buffer
            isAnimating = true;
            Serial.println("mode set to frame buffer");
            break;
        case 0x03:  //Frame Buffer Loop
            isAnimating = true;
            Serial.println("mode set to frame buffer loop");
            break;
        case 0x04:
            isAnimating = true;
            Serial.println("mode set to animation");
            readAnimation(client);
            break;
        case 0x00:  //sleep
            //Serial.println("mode set to sleep");
        default:
            isAnimating = false;
            clearFrameBuffer();
            blankOutput();
    }
}

void blankOutput() {
    for(int i = 0; i < PIXELCOUNT; i++)
        strip.SetPixelColor(i, RgbColor(0,0,0));
    strip.Show();
}

void clearFrameBuffer() {

    for(byte i = 0; i < FRAMECOUNT; i++) {
        frameBuffer[0] = Frame();
    }
}

// RgbColor parseColour(byte red, byte green, byte blue) {
//     return RgbColor(red, green, blue);
// }

// RgbColor parseColour(WiFiClient client) {
//     if(!waitForPackets(client, 3)) return;
//     byte buffer[3];
//     client.read(buffer, 3);

//     return RgbColor(buffer[0], buffer[1], buffer[2]);
// }

// unsigned long parseDelay(WiFiClient client) {
    // if(!waitForPackets(client, 4)) return;
    // byte buffer[4];
    // client.read(buffer, 4);

    // return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
//}

/**
 * @brief Wait for specificed number of packets to be recieved or timeout.
 * 
 * @param client 
 * @param packetCount 
 * @return true - packets were recieved
 * @return false - connection timed out before packets were recieved
 */
bool waitForPackets(WiFiClient client, int packetCount) {
    // Serial.printf("waiting for %d packets\n", packetCount);
    bool timedout = false;
    unsigned long startedWaiting = millis();

    while(client.available() < packetCount) {
        if(millis() - startedWaiting > SOCKETTIMOUT) {
            timedout = true;
            Serial.println("Connection Timed Out");
            Serial.printf("Started waiting at: %d\nCurrent time: %d\n", startedWaiting, millis());
            break;
        }
        delay(1);
    }

    return !timedout;
}
