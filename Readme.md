# ![QBert Character](./qbert.png) MiSTer Q*Bert

Q*Bert arcade core for MiSTer FPGA. It's a reproduction of the original PCBs with the following exceptions:

#### Main logic board:
- I removed D2 to D4 multiplexers because FG POS/ID registers E1-2, E2-3 & E4 are now dual port RAM.
- I removed D5 to D7 multiplexers because BG character RAM E7 is dual port.
- I removed D8 to D10 multiplexers because E10-11 buffer RAM is dual port.
- I removed E8/E9-10 bus isolation because of dual port E7 & E10-11.
- I removed H13 mux because color RAM G13 to G14 is dual port.

#### Audio board MA216:
- All the necessary logic to hook up the SC01 is missing.

#### Ports:

- IP17-10: input port for test buttons, coins and player 1/2.
- IP47-40: input port for joystick.
- OP27-20: output port for sound interface
- OP33-37: output port for knocker and coin meter.

Bugs & WIP
----------

- Sound (WIP)
- MRA file (WIP)
- DIP switch order is not correct.
- ~~Screen rotation doesn't work~~
- Problem with horizontal position register E1-2. When a new object is falling from the top of the screen (ball), it appears briefly at the bottom of the screen.
- Load/reset: loading the ROM doesn't reset the core properly, another manual reset is needed.
- High Scores screen: the big three letters of player's name are not displayed correctly. It works well after a few resets (it could be a problem with bus sharing logic which sends zeros to simulate high impedance and allow ORing outputs).
- Votrax chip is cruelly missing, QBert needs his @!#?@! voice!!!
- The left side of the screen shows a series of zero characters.

