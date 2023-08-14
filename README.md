# A-RYTH-MATIK
6CH Eurorack trigger sequencer and euclidean Rhythm Generator with SSD1306 0.96 OLED

This is our take on a PCB version of the HAGIWO 6 Channel OLED sequencer.

![MODULOVE 6CH Eurorack trigger sequencer](https://modulove.de/arythmatik-b1/Modulove_A-RYTH-MATIK_Productshots_FrontPanel_PCB.jpg)

The module is an Arduino-based 6CH Eurorack trigger sequencer and euclidean Rhythm Generator with SSD1306 0.96 OLED.
Input and configuration of the parameters has changed with this firmware to be more performative and less 'clicky'.

It can be clocked via input and offers an additional reset input.

- "Manual mode" with control over parameter: Hits, Offset, Limit, Mute
- "Random mode" where each channel changes randomly over time and sticks to entered value

Easy to modify / reprogramm it easily with the Arduino IDE.

![HAGIWO 6CH Eurorack trigger sequencer](https://modulove.de/arythmatik-b1/Modulove_A-RYTH-MATIK_Productshots_Front.jpg)

It offers six outputs plus additional status LEDs per channel

- 3U 6HP size
- Panel Design by [bkrsmdesign](https://www.instagram.com/bkrsmdesign/ "Sasha Kruse")
- Rotary Encoder for Parameter Selection
- CLK in: Clock IN
- RESET in: Reset IN
- OUTPUT Voltage 5V
- Skiff friendly
- 50 mA draw

<h1>Changes we made to the original design</h1>
<ul>
	<li>All presoldered SMD Kits</li>
	<li>Inputs / Outputs protected by protection circuit</li>
	<li>Additional RESET input</li>
	<li>Status LEDs per output</li>
	<li>Clock Status LED</li>
	
</ul>


Currently there are a few ideas floating around for further expanding / alternating the FW.
Subscribe for updates and pitch your ideas!


DIY Info / Links:

Building the Module is simple and suited for beginners. All Kits come with SMD parts already soldered.

Assembly Instructions (Quickstart) is here: [A-RYTH-MATIK QuickStart](https://github.com/modulove/A-RYTH-MATIK/blob/main/A-Ryth-Matik_QuickStart.pdf "A-RYTH-MATIK QuickStart Guide").

[Buildvideo](https://www.youtube.com/watch?v=W5n_3bvGCUo)

To update / load the firmware on to your Arduino Nano or LGT8F328P you can just use the [A-RYTH-MATIK Firmware uploader in Chrome Browser here:](https://dl.modulove.de/module/arythmatik/ "A-RYTH-MATIK Firmware uploader").

Modulargrid: [A-RYTH-MATIK on modulargrid.net](https://www.modulargrid.net/e/modulove-a-ryth-matik "A-RYTH-MATIK on modulargrid.net").

More Info:

- [HAGIWO 6CH Euclidean rhythm sequencer Article](https://note.com/solder_state/n/n433b32ea6dbc "HAGIWO 6CH Euclidean rhythm sequencer module article").

- [HAGIWO 6CH gate seq Article](https://note.com/solder_state/n/n17c69afd484d "HAGIWO 6CH gate sequencer module article").

- [HAGIWO 6CH gate sequencer Video](https://www.youtube.com/watch?v=YszdC8YdFl0 "HAGIWO 6CH gate sequencer module Youtube Video").

- [HAGIWO Euclidean rhythm sequencer Video](https://www.youtube.com/watch?v=lkoBfiq6KPY "HAGIWO  Euclidean rhythm sequencer module Youtube Video").


Thanks for all the inspiration and work they have done go out especially to [HAGIWO](https://www.youtube.com/@HAGIWO "HAGIWO Youtube Channel"), [Testbild-Synth Github](https://github.com/Testbild-synth "Testbild-synth Github Page"), [mzuelch Github](https://github.com/mzuelch "Michael Zülch Github Page") and all other people envolved in this corner!

<a href="https://www.tindie.com/stores/modulove/?ref=offsite_badges&utm_source=sellers_modulove&utm_medium=badges&utm_campaign=badge_small"><img src="https://d2ss6ovg47m0r5.cloudfront.net/badges/tindie-smalls.png" alt="I sell on Tindie" width="200" height="55"></a>
