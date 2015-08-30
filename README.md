# RP-103
Here are Arduino sketch of controller module of RP-103.
RP-103 is a MIDI controlled pipe organ using 13 recorders as sound pipes.

RP-103 is a project of R-MONO Lab. Information is as follows.

<http://r-mono-lab-en.tumblr.com/products/rp-103>

Note:

This code is beta, including some testing code. And some comments are in Japanese. Sorry.

If you have any ideas or suggestions, please leave me a message. Thanks.


## Libraries
This sketch uses three liblaries.
- MIDI    
- TLC5940 
- Metro

You should copy these libraries into your library folder and 
you should make small change in tlc_config.h of TLC5940 library
as follows.


line 94:

    #define TLC_PWM_PERIOD   256  // original is 8192 

This change makes PWM frequency higher in order to prevent 
audible hum noise from the solenoids. This change makes;
- PWM frequency -> around 31kHz, (originally around 1kHz)
- PWM duty range -> from 0 to 128, (originally 0 to 1024)


