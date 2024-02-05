#### esp32-ssd1306-spotify
# Low footprint OLED Spotify Display [and Controls]
Use an ESP32 devboard and an SSD1306 OLED display (128x64 minimum) to show the Spotify player status of a linked account.
![Demo](https://github.com/agastyash/esp32-ssd1306-spotify/assets/45848089/5bcacd38-23f1-4af4-a787-d450f22a6117)



## Features
- Uses both cores to keep the display updated while handling API fetch tasks separately and at a precise tickrate.
- Display update functions built from scratch to make region-specific updates as efficient as possible.
- Scrolling text and progress bar updates at separate time intervals based on the ESP32's own timer.
- Predictive progress bar display to make spikes in API or CPU response time less noticeable.

## Foundation
- Built on the [spotify-api-arduino](https://github.com/witnessmenow/spotify-api-arduino/) library, and Adafruit's GFX and SSD1306 libraries.
- Created using PlatformIO and Visual Studio Code.

## To-do
- Add support for controls via buttons rigged to other GPIO pins.
- Add a safe software reset cycle when nothing is playing for a long time.

## Instructions
1. Get a Spotify app refresh token from [here](https://github.com/witnessmenow/spotify-api-arduino/blob/main/examples/getRefreshToken/getRefreshToken.ino)
2. Clone this repository, open it with PlatformIO in VS Code.
3. Download and place the Adafruit SSD1306, Adafruit GFX and SpotifyArduino libraries under lib/.
4. Create a 'secret.h' file under include/.
5. Copy the format from the comment in main.cpp and fill in the details including the refresh token obtained from step 1.
6. Change the SCL and SDA pin definitions in main.cpp to the pins you want to use.
7. Compile and upload the code. If something goes wrong, set the ess-debug flag to ON in main.cpp and use the serial output to debug.
