# ESPSomfy-RTS <image src="https://user-images.githubusercontent.com/47839015/218898940-3541b360-5c49-4e38-a918-392cd0408b76.png" style="width:177px;display:inline-block;float:right"></image>

A controller for Somfy RTS blinds and shades that supports up to 32 individual shades over 433.42MHz RTS protocol.

Most of my home is automated and one of the more annoying aspects was that there were three very expensive patio roller shades that still didn't have any type of automation attached to them.  Since they were on the patio I had to run around on the patio looking for the Telis torpedo remote any time I wanted to move the outside shades.  I have a rather large patio and the shades do a really good job at keeping the hot late afternoon sun from baking the house.

So I went searching for libraries that could automate my shades and relieve me from damning the torpedo.  I didn't just want to move them I wanted to interact with them and manage their position.  And because that Telis torpedo has been with me for so long I still wanted to be able to use that as well.

The research led me to several projects that looked like they would do what I want.  Most of them however, could send commands using a CC1101 radio attached to an ESP32 but I really wanted to be able to capture information from any external remotes as well.  In the end I did not find what I wanted so this repository was born. ESPSomfy RTS is capable of not only controlling the shades but it can also manage the current position even when an old school remote is used to move the shades.

This software uses a couple of readily available hardware components.  These include an ESP32 module and a CC1101 Transceiver module.  The CC1101 is connected via SPI to the ESP32 and controlled using SmartRC-CC1101-Driver library.  All in at the start of 2023 the total cost for me was about $12us for the final components.

# Functionality
After you get this up and running you will have the ability to interact with your shades using the built-in web interface, socket interface, and MQTT.  There is also a full [Home Assistant integration](https://github.com/rstrouse/ESPSomfy-RTS-HA) that can be installed through HACS that can control your shades remotely and provide automations.

![image](https://user-images.githubusercontent.com/47839015/219909983-275b4e8b-ab89-44d0-8e3e-ae9713ae34c0.png)

* Move the shade using up, down, and my buttons
* Set and remove my button favorite
* Interactive shade positioning

## Moving a Shade
You can move the shade to the full up position by clicking the up button.  To stop the shade during travel, press the my button and the shade will stop.  To move the shade to the full down position press the down button.  At any point during the movement you can press the my button to stop the shade.

If you would like to move the shade to a favorite position.  Press the my button while the shade is at rest and the shade will move to the position set for the my button.  These are displayed under the shade name so you do not have to guess what the current favorite is set to.

To move a shade to a target percentage of closed, click or tap on the shade icon.  An interface will open that will allow you to select the position using a slider control.  Move the slider to the desired position.  When you release the slider the shade will begin to open or close to reach the desired position.  You can change this position even when the shade is moving.

![image](https://user-images.githubusercontent.com/47839015/219912949-ab90d33e-4ef7-4294-b137-e8c2925a6fde.png)


## Setting a Favorite
To set your favorite my position you can either use the ESPSomfy RTS interface or your Somfy remote.  ESPSomfy RTS listens for the long press of the my button so if you use the remote it will pick up this favorite.  However, if you previously had a favorite set before installing ESPSomfy RTS you should reset it using ESPSomfy RTS.  Somfy uses the same command to set and unset its favorites.

To set or unset a favorite long press the my button.  After a few seconds a screen like below will appear.  ESPSomfy RTS allows you to set a favorite by position, so you can drag the slider to 37% and press the `SET MY POSITION` button.  After pressing the button the shade will move to that position and jog briefly indicating that the favorite has been saved.

![image](https://user-images.githubusercontent.com/47839015/219910353-d256a3a3-8a76-45cb-9dcc-e98fd4fd0b43.png)

To unset a favorite perform the long press on the my button to open the favorites interface.  Then move the slider to the current my position and the button will turn red and the text will change to `CLEAR MY POSITION`.  Once you press the button the shade will jog indicating that the favorite has been cleared.

![image](https://user-images.githubusercontent.com/47839015/219911658-b0ea72c8-d571-4405-9d27-20cad3d6549f.png)


# Getting Started
To get started you must create a radio device.  The wiki contains full instructions on how to get this up and running.  You don't need a soldering iron to make this project work. Dupont connections between the radio and the ESP32 will suffice.  However, I have also included some instructions on how to make an inconspicuous radio enclosure for a few bucks.  Here is the [Simple Hardware Guide](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Simple-ESPSomfy-RTS-device)

Next you need to get the initial firmware installed onto the ESP32.  Once the firmware built and installed for your ESP32.  The firmware installation process is a matter of compiling the sketch using the Arduino IDE.  You will find the firmware guide in the wiki [Firmware Guide](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Compiling-the-Firmware)

Once you have your hardware built it is simply a matter of connecting the ESP32 to your network and begin pariing your shades.  The software guide in the wiki will walk you through pairing your shades, linking remotes, and configuring your shades.  The wiki also includes a comprehensive software guide that you can use to configure your shades.  The good news is that this process is pretty easy after the firmware is installed on the ESP32.

[Configuring ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Configuring-the-Software)


## Integrations
While the interface that comes with the ESPSomfy RTS is a huge improvement, the whole idea of this project is to make the shades controllable from everywhere that I want to control them.  So for that I created a couple of interfaces that you can use to bolt on your own automation.  These options are for those people that have a propeller on their hat.  They do things make red nodes (using Node-Red) or have their own web interface.

You can find the documentation for the interfaces in the [Integrations](https://github.com/rstrouse/ESPSomfy-RTS/wiki/Integrations) wiki.  Plenty of stuff there for you folks that make red nodes and stuff.
  
## Sources for this Project
I spent some time reading about a myriad of topics but in the end the primary source for this project comes from https://pushstack.wordpress.com/somfy-rts-protocol/.  The work done on pushstack regarding the protocol timing made this feasible without burning a bunch of time measuring pulses.  
  
Configuration of the Transceiver is done with the ELECHOUSE_CC1101 library which you will need to include in your project should you want to compile the code.  The one used for compiling this module can be found here. https://github.com/LSatan/SmartRC-CC1101-Driver-Lib

  
 






