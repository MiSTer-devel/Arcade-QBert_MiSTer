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

- SC01 is not implemented. Waiting for brave people to do it.

**NVRAM:**

- Not implemented - NVRAM should be C5/C6 on schematics.

**Ports:**

- IP17-10: input port for test buttons, coins and player 1/2.
- IP47-40: input port for joystick.
- OP27-20: output port for sound interface
- OP33-37: output port for knocker and coin meter.

Game Compatibility List
-----------------------

- QBert: 100% compatible.
- QBert Qubes: 100% compatible, bug on rolling cube screen and dip switches inverted.
- Curve Ball: dip switch order is weird, no sound.
- Insector: 100% compatible, dip switch problems, controller inverted & no test mode(?).
- Mad Planets: 100% compatible but blinking letters.
- Tyls: 100% compatible - dip switches inverted.

...so to summarize, with the exception of QBert, don't expect another game to work properly.

Known Bugs
----------

- MRA files.
- Problem with vertical position register E1-2. When a new object is falling from the top of the screen (ball), it appears briefly at the bottom of the screen.
- High Scores screen: the big three letters of player's name are not displayed correctly. It works well after a few resets (is it a problem with bus sharing logic which sends zeros to simulate high impedance for ORing outputs?). I don't have the PCB so it's difficult to know the original behavior.
- Votrax chip is cruelly missing, QBert needs his @!#?@! voice!!!
- QBert Qubes: the rolling cube screen has a bug and Mad Planets has blinking letters (timing?).

