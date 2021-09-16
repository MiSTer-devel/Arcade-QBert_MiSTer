![QBert Character](./qbert.png) MiSTer Q*Bert
=============================================

Q*Bert arcade core for MiSTer FPGA by Pierco. It's a reproduction of the original PCBs rather than a reinterpretation but with some dual port RAM exceptions.

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

Working:

- QBert: 100% compatible.
- QBert Qubes: 100% compatible.
- Mad Planets: 100% compatible.

Not working yet:

- Curve Ball: dip switch order is weird, no sound.
- Insector: 100% compatible, dip switch problems, controller inverted & no test mode(?).
- Tyls: 100% compatible - dip switches inverted.

Known Bugs
----------

- QBert Qubes: "Supreme Nosers" screen works only after a reset.
- Problem with vertical position register E1-2. When a new object is falling from the top of the screen (ball), it appears briefly at the bottom of the screen.
- High Scores screen: the big three letters of player's name are not displayed correctly. It works well after a few resets (is it a problem with bus sharing logic which sends zeros to simulate high impedance for ORing outputs?). I don't have the PCB so it's difficult to know the original behavior.
- Votrax chip is cruelly missing, QBert needs his @!#?@! voice!!!
