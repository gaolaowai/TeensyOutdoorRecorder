# TeensyOutdoorRecorder
Low-power, low-cost recorder for wildlife/aquatic research.

## Materials
project uses a teensy3.6, along with an audio recording board from PJRC. For initial programming and setup, it uses an LCD screen with a 16x2 matrix, via an I2C backpack attached to the LCD screen, and three pushbuttons connected to a 2.2k pullup resistor each. A 3v coin cell battery is attached to the Teensy's onboard RTC, which then allows for creating timestamp named WAV files.

After removing the LCD screen and restarting the device, it will record for the selected duration, save to SD, then go into a very-low power mode for its sleep duration. When the sleep duration is reached, the device resets itself and begins the cycle again. The onboard LED (connected to pin 13) flashes while recording is in progress.

Though the Teensy 3.6 has a default speed of 180mhz, this code has been proven to run without issue at 24mhz (while still allowing a serial connection for testing/debugging).

## Thanks
Many thanks for the PJRC forumn for its help, advice, and examples, along with dev time being sponsored by the Mote Marine Lab - Fisheries Habitat Ecology and Acoustics Program
