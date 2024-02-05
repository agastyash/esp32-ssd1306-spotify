// Base Libraries
#include <WiFi.h>
#include <WiFiClientSecure.h>

// Spotify-Arduino Libraries
#include <SpotifyArduino.h>
#include <SpotifyArduinoCert.h>
#include <ArduinoJson.h>

// SSD1306 Libraries
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Ignore power issues (brownout) and make do with what the board can pull; sometimes useful.
// Also uncomment the first line of code in the setup()
// #include "soc/soc.h"
// #include "soc/rtc_cntl_reg.h"

// secret.h template:
/*  
    // Wireless Setup
    char ssid[] =         "SSID";
    char password[] = "PASSWORD";

    // Spotify Client Setup
    char clientId[] = "CLIENT_ID_XXXXXXXXXXXXXXXXXXXXX";
    char clientSecret[] = "CLIENT_SECRET_XXXXXXXXXXXXX";
    #define SPOTIFY_MARKET "US"   // Optional but useful
    #define SPOTIFY_REFRESH_TOKEN        "REFRESH_TOKEN"
*/
// Get refresh token from the official spotify-api-arduino library example
// https://github.com/witnessmenow/spotify-api-arduino/tree/main#setup-instructions

// Credentials (Wireless and Spotify incl. refresh token)
#include <secret.h>

// Wifi and Spotify API clients
WiFiClientSecure client;
SpotifyArduino spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);

// SSD1306 OLED Setup
#define SCREEN_WIDTH 128 // OLED display width
#define SCREEN_HEIGHT 64 // OLED display height
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define MAX_CHARS     15 // Max characters to render before running a scrolling text function on displayed text
#define SDA           15 // SDA Pin
#define SCL           14 // SCL Pin

// I2C connection with SSD1306 (pins are normally defined by the Wire library, this project uses custom pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Placeholder text for initialization
String trackUri,
       trackName = "Loading...",
       artistName = "[agshr]",
       progressText = "00:00 / 00:00";

bool trackWrapCheck = true, artistWrapCheck = true, // Wrap booleans to avoid repeatedly rendering text that fits on screen
     playingState, vacationTime, playPauseIconState = false;                 // play/pause, idle and play/pause icon states
     
// Time between requests and screen refreshes
unsigned long APIRequestDueTime,
              textRefreshDueTime,
              progressBarDueTime,
              delayBetweenRequests = 1000,
              delayBetweenTextRefresh = 250,
              delayBetweenProgressBarRequests = 1000;

// Numerical variables for calculating and predicting track progress
long progress, duration, pseudoProgress; int display_clampedPercentage, funVar; float percentage;

// Multicore task definitions
TaskHandle_t spotifyAPIFetch; // Runs on same core as WiFi and system tasks
TaskHandle_t screenUpdates;   // Runs on separate core

// Spotify logo bitmap 32x32
#define LOGO_HEIGHT   32
#define LOGO_WIDTH    32
const unsigned char epd_bitmap_spotify [] PROGMEM = {
	0x00, 0x0f, 0xf0, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x01, 0xff, 0xff, 0x80, 0x03, 0xff, 0xff, 0xc0, 
	0x07, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0xff, 0xf0, 0x1f, 0xff, 0xff, 0xf8, 0x3f, 0xff, 0xff, 0xfc, 
	0x3f, 0xff, 0xff, 0xfc, 0x7e, 0x00, 0x0f, 0xfe, 0x7c, 0x00, 0x01, 0xfe, 0x7c, 0x3e, 0x80, 0x3e, 
	0xff, 0xff, 0xf8, 0x1f, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x7f, 0xff, 
	0xfe, 0x00, 0x07, 0xff, 0xfc, 0x09, 0x00, 0xff, 0xff, 0xff, 0xf0, 0x7f, 0xff, 0xff, 0xfe, 0x7f, 
	0x7f, 0xff, 0xff, 0xfe, 0x7f, 0x80, 0x7f, 0xfe, 0x7e, 0x00, 0x07, 0xfe, 0x3e, 0x3f, 0x01, 0xfc, 
	0x3f, 0xff, 0xf1, 0xfc, 0x1f, 0xff, 0xff, 0xf8, 0x0f, 0xff, 0xff, 0xf0, 0x07, 0xff, 0xff, 0xe0, 
	0x03, 0xff, 0xff, 0xc0, 0x01, 0xff, 0xff, 0x80, 0x00, 0x7f, 0xfe, 0x00, 0x00, 0x0f, 0xf0, 0x00
};



//  FUNCTION DEFINITIONS  //

void wirelessSetup(); void spotifyBoot(); void execute1306();
void idleMode(bool idleStatus); 
void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying);
void performAPIFetch( void * pvParameters );
void performScreenUpdate( void * pvParameters );
void printTrack(); void printArtist(); void updateProgress(); void emptyRegion(int x1, int y1, int w, int h);
String formatMilliseconds(int milliseconds1, int milliseconds2); 



void setup()
{
    // Disable the brownout detector
    // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);

    // SSD1306 Initialization
    execute1306();

    // WiFi setup
    wirelessSetup();

    // Spotify logo animation and placeholder rendering
    spotifyBoot();

    // Handle HTTPS Verification
    client.setCACert(spotify_server_cert); // These expire every few months
    // ... or don't! (don't don't)
    // client.setInsecure(); (bad idea i think)

    // If you want to enable some extra debugging
    // uncomment the "#define SPOTIFY_DEBUG" in SpotifyArduino.h (spams a lot of text)

    // Serial.println("Refreshing Access Tokens");
    if (!spotify.refreshAccessToken())
    {
        // Serial.println("Failed to get access tokens");
    }

    // Pin API fetches to core 0
    xTaskCreatePinnedToCore(
                        performAPIFetch,
                        "spotifyAPIFetch",
                        10000,
                        NULL,
                        1,
                        &spotifyAPIFetch,
                        0);

    // Pin screen updates to core 1
    xTaskCreatePinnedToCore(
                        performScreenUpdate,
                        "screenUpdates",
                        10000,
                        NULL,
                        1,
                        &screenUpdates,
                        1);
}



void loop()
{
    // Miau
}



//  Function Definitions

//  Setup functions  //

void wirelessSetup()
{
    // Start WiFi connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Start wifi indicator animation
    display.drawPixel(61, 34, WHITE);
    display.drawLine(61, 32, 62, 32, WHITE);
    display.drawLine(63, 33, 63, 34, WHITE);
    display.drawLine(61, 30, 63, 30, WHITE);
    display.drawPixel(64, 31, WHITE);
    display.drawLine(65, 32, 65, 34, WHITE);
    display.display();

    // Wait for connection and blink the last wifi bar until done (usually not more than once)
    for (int i = 0; WiFi.status() != WL_CONNECTED; i++)
    {
        if (i%2)
        {
            display.drawLine(61, 28, 64, 28, WHITE);
            display.drawPixel(65, 29, WHITE); display.drawPixel(66, 30, WHITE);
            display.drawLine(67, 31, 67, 34, WHITE);
            display.display();
        }
        else
        {
            display.drawLine(61, 28, 64, 28, BLACK);
            display.drawPixel(65, 29, BLACK); display.drawPixel(66, 30, BLACK);
            display.drawLine(67, 31, 67, 34, BLACK);
            display.display();
        }
        delay(500);
    }

    // Empty wifi icon area
    emptyRegion(61, 28, 20, 20);
    
    // Serial.println("");
    // Serial.print("Connected to ");
    // Serial.println(ssid);
    
    // Centers the wifi confirmation text on screen and displays it for 1 second
    // This is unnecessarily long but it's only done once so it doesn't matter much and it looks cleaner, if you know your SSID you can just plug cursor values directly into the setCursor call
    int16_t centercursorx, centercursory; uint16_t centerwidth, centerheight;
    display.getTextBounds(String("Connected to:"), 0, 0, &centercursorx, &centercursory, &centerwidth, &centerheight);
    display.setCursor((SCREEN_WIDTH-centerwidth)/2, ((SCREEN_HEIGHT-centerheight)/2));
    display.println("Connected to:");
    display.getTextBounds(String(ssid), 0, 0, &centercursorx, &centercursory, &centerwidth, &centerheight);
    display.setCursor((SCREEN_WIDTH-centerwidth)/2, ((SCREEN_HEIGHT-centerheight)/2) + 6);
    display.println(ssid);
    display.display();
    delay(1000);
    display.clearDisplay();
}

void execute1306()
{
    // Ensure Wire starts on user defined pins (my ESP32-CAM doesn't have the default ESP32 GPIO pins for SDA and SCL)
    Wire.begin(SDA, SCL);

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        // Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }

    // Initialize with adafruit logo and clear
    display.display();
    delay(700);
    display.clearDisplay();
    display.display();

    // Initial text settings
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setTextWrap(false);
}

void spotifyBoot()
{
    // Spotify logo animation with a small ease in effect
    for (int i = -32; i < -5; i += 3)
    {
        display.clearDisplay();
        display.drawBitmap(i, 0, epd_bitmap_spotify, LOGO_WIDTH, LOGO_HEIGHT, 1);
        display.display();
    }
    for (int i = -5; i < 1; i += 1)
    {
        display.clearDisplay();
        display.drawBitmap(i, 0, epd_bitmap_spotify, LOGO_WIDTH, LOGO_HEIGHT, 1);
        display.display();
    }
    display.clearDisplay();

    // Draw Spotify logo, play icon, progress text and placeholder track info text
    display.drawBitmap(0, 0, epd_bitmap_spotify, LOGO_WIDTH, LOGO_HEIGHT, 1);

    display.setCursor(40, 6);
    display.println(trackName);
    display.setCursor(40, 18);
    display.println(artistName);
    display.fillTriangle(120, 41, 120, 49, 124, 45, WHITE);
    display.setCursor(4, 42);
    display.println(progressText);

    display.display();
}



//  Formatting helper functions  //

String formatMilliseconds(int milliseconds1, int milliseconds2)
{
    // Ensure the input is not greater than 99 minutes and 59 seconds (99:59)
    if (milliseconds1 > 5999000 || milliseconds2 > 5999000) {
        // milliseconds1 = milliseconds2 = 5999000; // Limit to 99:59
        // OR
        return ""; // Simply stop displaying the progress text (better idea)
    }

    int minutes1 = milliseconds1 / 60000;           int minutes2 = milliseconds2 / 60000;
    int seconds1 = (milliseconds1 % 60000) / 1000;  int seconds2 = (milliseconds2 % 60000) / 1000;

    // Format the result as "00:00"
    String formattedTime1 = (minutes1 < 10 ? "0" : "") + String(minutes1) + ":" + (seconds1 < 10 ? "0" : "") + String(seconds1);
    String formattedTime2 = (minutes2 < 10 ? "0" : "") + String(minutes2) + ":" + (seconds2 < 10 ? "0" : "") + String(seconds2);

    return formattedTime1 + " / " + formattedTime2;
}



//  State change functions  //

void idleMode(bool idleStatus)
{
    vacationTime = idleStatus;
    if (idleStatus)
    {
        playPauseIconState = false;
        playingState = false;
        emptyRegion(120, 42, 5, 8);
        delayBetweenRequests += 13000;
        delayBetweenProgressBarRequests = 200;
        trackName = "Nothing is playing.   ", trackWrapCheck = true;
        artistName = "[agshr]", artistWrapCheck = true;
        progressText = "";
        playPauseIconState = false;
        emptyRegion(120, 41, 5, 9);
        progress = 0;
        percentage = 0;
        duration = 16000;
    }
    else
    {
        delayBetweenRequests -= 13000;
        delayBetweenProgressBarRequests = 1000;
    }
}



//  Multicore task functions  //

void performAPIFetch( void * pvParameters ){
    // delay(500);
    // Serial.print("API fetch running on core ");
    // Serial.println(xPortGetCoreID());

    while(true)
    {
        int y = millis();

        // Run the API fetch
        int status = spotify.getCurrentlyPlaying(printCurrentlyPlayingToSerial, SPOTIFY_MARKET);

        if (status == 200)
        {
            if (vacationTime)
            {
                idleMode(false);
                // Serial.println("No longer idle.");
                spotify.getCurrentlyPlaying(printCurrentlyPlayingToSerial, SPOTIFY_MARKET);
            }
            // Serial.println("Successful refresh.");
            
        }
        else
        if (status == 204)
        {
            if (!vacationTime)
            {
                idleMode(true);
                // Serial.println("Doesn't seem to be anything playing");
                spotify.getCurrentlyPlaying(printCurrentlyPlayingToSerial, SPOTIFY_MARKET);
            }
        }
        else
        {
            // Serial.print("Error: ");
            // Serial.println(status);
        }
        // Dynamic delay (adjusted by the delayBetweenRequests variable)
        delay(((delayBetweenRequests-(millis()-y))*((millis()-y)<delayBetweenRequests)));
    }
}

void performScreenUpdate( void * pvParameters ){
    // Serial.print("Screen updates running on core ");
    // Serial.println(xPortGetCoreID());
    
    // delayBetween* variables control how often the screen is updated for that particular element
    while(true)
    {
        if (millis() - progressBarDueTime >= delayBetweenProgressBarRequests)
        {
            updateProgress();

            progressBarDueTime = millis();
        }

        if (millis() - textRefreshDueTime > delayBetweenTextRefresh)
        {
            if (trackWrapCheck)
            {
                printTrack();
            }
            if (artistWrapCheck)
            {
                printArtist();
            }
            display.display();

            textRefreshDueTime = millis();
        }
    }
}



//  API linked functions  //

void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
    // If vacation mode is on just ensure trackUri is set to something asinine and skip this
    if (vacationTime)
    {
        trackUri = "nahimjustchillingimcoolin" + String(millis());
        return;
    }
    // Only update main data if the track has changed
    if (trackUri != currentlyPlaying.trackUri)
    {
        // Update trackuri value
        trackUri = currentlyPlaying.trackUri;

        // Update track variables
        trackName = String(currentlyPlaying.trackName) + "   ";
        trackWrapCheck = true;
        
        // Update artist variables
        artistName = "";
        for (int i = 0; i < currentlyPlaying.numArtists; i++)
        {
            if (i > 0)
            {
                artistName+=", ";
            }
            artistName += currentlyPlaying.artists[i].artistName;
        }
        artistName += "   ";
        artistWrapCheck = true;

        // Store duration
        duration = currentlyPlaying.durationMs;
    }
    
    // Calculate progress
    progress = currentlyPlaying.progressMs;
    // Pseudo progress is based on the esp's internal clock instead of actual update requests to the spotify API
    pseudoProgress = millis();
    // Update pause state
    if (currentlyPlaying.isPlaying)
    {
        playingState = true;
    }
    else
    {
        playingState = false;
    }
    updateProgress();
}

//  Screen refresh functions  //

void emptyRegion(int x1, int y1, int w, int h)
{
    display.fillRect(x1, y1, w, h, BLACK);
}

void printTrack()
{
    // Empty track text area
    emptyRegion(40, 6, 88, 8);

    if (trackName.length() - 3 > MAX_CHARS)
    {
        // Display the currently loaded track name order
        display.setCursor(40, 6);
        display.println(trackName);

        // Left shift the whole string by 2
        String ogTrackName = trackName; // Store original name
        for (unsigned int j = 0; j < trackName.length() - 1; j++) // Shift all characters but the last two
        {
            trackName[j] = ogTrackName[j+1];
        }
        for (unsigned int k = 0; k < 1; k++)
        {
            // Set last 2 characters to the first ones from the original string
            trackName[trackName.length() - 1 + k] = ogTrackName[k];
        }
    }
    else // Just display the name once and stop running the print cycle entirely
    {
        display.setCursor(40, 6);
        display.println(trackName);
        trackWrapCheck = false;
    }
}

void printArtist()
{
    // Empty artist text area
    emptyRegion(40, 18, 88, 8);

    // artistName gives the full string of the track name
    if (artistName.length() - 3 > MAX_CHARS)
    {
        // Display the currently loaded string
        display.setCursor(40, 18);
        display.println(artistName);

        // Left shift the whole string by 2
        String ogArtistName = artistName; // Store original name
        for (unsigned int j = 0; j < artistName.length() - 1; j++) // Shift all characters but the last two
        {
            artistName[j] = ogArtistName[j+1];
        }
        for (unsigned int k = 0; k < 1; k++)
        {
            // Set last 2 characters to the first ones from the original string
            artistName[artistName.length() - 1 + k] = ogArtistName[k];
        }
    }
    else // Just display the name once and stop running the print cycle entirely
    {
        display.setCursor(40, 18);
        display.println(artistName);
        artistWrapCheck = false;
    }
}

void updateProgress()
{
    // Calculate progress and duration depending on pause status
    if (playingState)
    {
        // Progress bar percentage update
        percentage = ((float)(progress+millis()-pseudoProgress) / (float)duration) * 128; // Looney tunes type idea just to get this to look like it's not slow lol
        
        // Progress text update
        progressText = formatMilliseconds(progress+millis()-pseudoProgress, duration);

        if (!playPauseIconState) // Set the play icon
        {
            playPauseIconState = true;
            emptyRegion(120, 41, 5, 9);
            display.fillTriangle(120, 41, 120, 49, 124, 45, WHITE);
        }
    }
    else if (vacationTime)
    {
        if (percentage > 119)
        {
            funVar = -1000;
        }
        if (percentage < 13)
        {
            funVar = 1000;
        }
        percentage = (int)(((float)(progress) / (float)duration) * 128);
        progress += funVar;
    }
    else // if (!playingState) i.e if paused
    {
        if (playPauseIconState) // Set the pause icon
        {
            playPauseIconState = false;
            emptyRegion(120, 41, 5, 9);
            display.fillRect(120, 41, 2, 9, WHITE);
            display.fillRect(123, 41, 2, 9, WHITE);
        }
    }

    // Empty progress text area and refresh
    emptyRegion(4, 42, 80, 8);
    display.setCursor(4, 42);
    display.println(progressText);

    // Empty progress bar area
    emptyRegion(0, 53, 128, 10);
    // Update progress bar
    // safeX denotes the cleanest possible value towards the edges of the screen for rendering the circle without it getting cut off
    int safeX = (percentage*(percentage > 4 && percentage < 123) + 3*(percentage <= 4) + 124*(percentage >= 123));
    display.fillRect(0, 56, safeX, 3, WHITE);
    display.drawLine(0, 57, 127, 57, WHITE);
    display.fillCircle(safeX, 57, 3, WHITE);

    // Push all updates
    display.display();
}
