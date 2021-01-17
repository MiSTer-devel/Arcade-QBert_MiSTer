![QBert Character](./qbert.png) MiSTer Q*Bert
=============================================

Q*Bert arcade core for MiSTer FPGA. It's a reproduction of the original PCBs with the following exceptions:

- I removed D2 to D4 multiplexers because FG POS/ID registers E1-2, E2-3 & E4 are now dual port RAM.
- I removed D5 to D7 multiplexers because BG character RAM E7 is dual port.
- I removed D8 to D10 multiplexers because E10-11 buffer RAM is dual port.
- I removed E8/E9-10 bus isolation because of dual port E7 & E10-11.
- I removed H13 mux because color RAM G13 to G14 is dual port.

Additional details
------------------

**Audio board MA216:**
- SC01 is fake (samples) - WIP.

**Ports:**

- IP17-10: input port for test buttons, coins and player 1/2.
- IP47-40: input port for joystick.
- OP27-20: output port for sound interface
- OP33-37: output port for knocker and coin meter.

Game Compatibility List
-----------------------

- QBert: 100% compatible but rotation doesn't work yet.
- QBert Qubes: 100% compatible, bug on rolling cube screen and dip switches inverted
- Curve Ball: dip switch order is weird, no sound
- Insector: 100% compatible, dip switch, controller inverted, problem with test mode
- Mad Planets: 100% compatible but blinking letters
- Tyls: 100% compatible - dip switches inverted
- Three stooges: ?

Bugs & WIP
----------

- NVRAM (C5/C6) WIP
- MRA files. WIP
- Problem with horizontal position register E1-2. When a new object is falling from the top of the screen (ball), it appears briefly at the bottom of the screen. WIP
- High Scores screen: the big three letters of player's name are not displayed correctly. It works well after a few resets (is it a problem with bus sharing logic which sends zeros to simulate high impedance for ORing outputs?).
- Votrax chip is cruelly missing, QBert needs his @!#?@! voice!!! WIP (fake Votrax)
- The left side of the screen shows a series of zero characters. It seems to be an original bug, todo: implement a screen position tool like the one on Minimig core.
- QBert Qubes: the rolling cube screen has a bug (timing?). WIP

