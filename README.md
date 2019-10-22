# PiscolaMaker
Code to implement Piscola maker

## Materials required

* 1x ESP32
* 1 MOSFET per dispenser
* 1 water pump per dispenser
* 1 button per dispenser
* 1 button to dispense all
* 1 button to confirm order


## TO-DO

* Test every function and route
* Document API
* Test API
* Create presets

## Changelog

#### v0.1.1

* corrected bad use of struct in query route
* removed some not used stuff
#### v0.1

* Implemented queue of drinks
* Queue of drinks with live status
* Implemented basic serving route
* Implemented basic query route
* Implemented pots support
* Implemented multiple drink dispensers
* Defined some useful variables
* Eliminated overhead on data structures
* Extensibility for drink types, names and dispensers
* Support for 10 drink instructions, async
