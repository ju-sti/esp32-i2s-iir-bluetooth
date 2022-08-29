# ESP32 Bluetooth IIR I2S

Build a "smart speaker" by adding a Bluetooth receiver and improving its sound using an ESP32 microcontroller.

The ESP32 µc receives audio from a device connected by Bluetooth (smartphone, FireTV, Apple TV, anything that has Bluetooth), improves the audio by applying IIR or biquad filters and then outputs by using the I2S interface to an integrated amplifier. The whole assembly is very small and can be easily put into a speaker box. The only connection of the speaker is a USB connector for power. The ESP32 and amplifier use almost no power, so it is possible to power the speaker directly from a smartphone (by using USB-C or a USB-on-the-go cable). 


Required hardware:
- ESP32 microcontroller
    - has builtin Bluetooth and DSP
    - has a good software support (ESP-IDF and ESP-ADF frameworks)
- I2S DAC
    - I used a cheap MAX98357A board. It's an I2S DAC with an integrated amplifier.
- The project could be extended by a USB to I2S board TI (e.g. PCM2707) for wired audio input
- An actual speaker 
- known frequency response of the speaker and its correction curve (can be recorded and generated with ARTA software)
    - in my case I used a finished speaker design and correction curve designed by [Michael Uibel](http://www.uibel.net/), the SAKPC EA speaker


Project structure:
- The code is based on [Espressif IoT Development Framework (ESP-IDF) version 4.3](https://github.com/espressif/esp-idf/tree/release/v4.3). It also uses the [Espressif Audio Development Framework](https://github.com/espressif/esp-adf)
- Important files:
    - bt_downsample_iir_i2s.c: The pipeline definition connecting the Bluetooth code to IIR to I2S. The file is based on a bluetooth sample from ESP-ADF.
    - my_board/my_codec: Definition of the I2S pins of the ESP32. Varies depending on the used board. Also, it defines an empty I2S interface. Most I2S chips have an additional I2C interface to control the ADC or amplifier but the used MAX98357A doesn't.
    - iir_pipeline_element.c: The actual IIR filtering. I converted the IIR filters from human-readable format or the format Equalizer APO uses to the required individual terms using the modified [biquad calculator](https://www.earlevel.com/main/2021/09/02/biquad-calculator-v3/) of Nigel Redmon.
