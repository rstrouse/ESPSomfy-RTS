# ESPSomfy-RTS
A controller for Somfy RTS blinds and shades that supports up to 32 individual shades over 433.42MHz RTS protocol.

Most of my home is automated and one of the more annoying aspects was that there were three very expensive patio roller shades that still didn't have any type of automation attached to them.  Since they were on the patio I had to run around on the patio looking for the Telis torpedo remote any time I wanted to move the outside shades.  I have a rather large patio and the shades do a really good job at keeping the hot late afternoon sun from baking the house.

So I went searching for libraries that could automate my shades and relieve me from damning the torpedo.  I didn't just want to move them I wanted to interact with them and manage their position.  And because that Telis torpedo has been with me for so long I still wanted to be able to use that as well.

The research led me to several projects that looked like they would do what I want.  Most of them however, could send commands using a CC1101 radio attached to an ESP32 but I really wanted to be able to capture information from any external remotes as well.  In the end I did not find what I wanted so this repository was born. ESPSomfy RTS is capable of not only controlling the shades but it can also manage the current position even when an old school remote is used to move the shades.

This software uses a couple of readily available hardware components.  These include an ESP32 module and a CC1101 Transceiver module.  The CC1101 is connected via SPI to the ESP32 and controlled using SmartRC-CC1101-Driver library.  All in at the start of 2023 the total cost for me was about $12us for the final components.

# Functionality
After you get this up and running you will have the ability to interact with your shades using the built-in web interface, socket interface, and MQTT.  There is also a full [Home Assistant integration](https://github.com/rstrouse/ESPSomfy-RTS-HA) that can be installed through HACS that can control your shades remotely and provide automations.

![image](https://user-images.githubusercontent.com/47839015/213935196-753e994c-7cd6-480f-8e6e-e5a61266fc3c.png)

# Getting Started
To get started you must create a radio device.  There wiki contains full instructions on how to get this up and running.  You don't need a soldering iron to make this project work. Dupont connections between the radio and the ESP32 will suffice.  However, I have also included some instructions on how to make an inconspicuous radio enclosure for a few bucks.  Here is the [Simple Hardware Guide](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Simple-ESPSomfy-RTS-device)

Next you need to get the initial firmware installed onto the ESP32.  Once the firmware built and installed for your ESP32.  The firmware installation process is a matter of compiling the sketch using the Arduino IDE.  You will find the firmware guide in the wiki [Firmware Guide](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Compiling-the-Firmware)

Once you have your hardware built it is simply a matter of connecting the ESP32 to your network and begin pariing your shades.  The software guide in the wiki will walk you through pairing your shades, linking remotes, and configuring your shades.  The wiki also includes a comprehensive software guide that you can use to configure your shades.  The good news is that this process is pretty easy after the firmware is installed on the ESP32.

[Configuring ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Configuring-the-Software)


## Integrations
While the interface that comes with the ESPSomfy RTS is a huge improvement, the whole idea of this project is to make the shades controllable from everywhere that I want to control them.  So for that I created a couple of interfaces that you can use to bolt on your own automation.  These options are for those people that have a propeller on their hat.  They do things make red nodes (using Node-Red) or have their own web interface.

You can find the documentation for the interfaces in the [Integrations](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Integrations) wiki.  Plenty of stuff there for you folks that make red nodes and stuff.
  
## Sources for this Project
I spent some time reading about a myriad of topics but in the end the primary source for this project comes from https://pushstack.wordpress.com/somfy-rts-protocol/.  The work done on pushstack regarding the protocol timing made this feasible without burning a bunch of time measuring pulses.  
  
Configuration of the Transceiver is done with the ELECHOUSE_CC1101 library which you will need to include in your project should you want to compile the code.  The one used for compiling this module can be found here. https://github.com/LSatan/SmartRC-CC1101-Driver-Lib

  
 






