# Goal of Setup

From the user's perspective, the goal of setup is to describe what the set of instruments that should be supported by the system. For example, the user might want to give (in some form) the following list of instruments to support:

* First violin section
* Second violin section
* Viola section
* Cello section
* Double bass section
* Flute 1
* Flute 2
* Oboe 1
* Oboe 2
* Clarinet 1
* Clarinet 2
* Bassoon 1
* Bassoon 2
* Trumpet 1
* Trumpet 2
* Horn 1
* Horn 2
* Horn 3
* Horn 4d
* Trombone 1
* Trombone 2
* Bass Trombone
* Tuba
* Timpani

Note that there are both sections and individual instruments, and that the difference is important.

The result should be a configuration of Dorico that:

* Uses the Fiddle plugin for the specified instruments.
* Falls through to the next endpoint config for other instruments.
* Falls through to the next endpoint if too many instruments are requested. E.g., if the user requests a third flute, the system should fall through to the next endpoint config. (This is because the Fiddle plugin only supports two flutes.)
* Falls through to the next endpoint if the user requests an instrument that is not supported by the Fiddle plugin. (This is because the Fiddle plugin only supports a subset of the instruments in the Dorico plugin.)

Furthermore, I don't want the user to have to go to Dorico's play tab to set up the instruments. Adding instruments in the setup tab should automatically assign instruments to the Fiddle plugin up to the limit of the plugin (outlined above). 

