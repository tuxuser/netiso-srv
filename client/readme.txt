NetIso 1.0
----------

================================================================================
    Overview - what it does
================================================================================
- plugs in... it is a plugin after all
- allows disks burned for flashed drive to work in unflashed drives
- allows disks burned for flashed drives to work in unpaired drives (dvd key lost etc)
- allows a directory of iso(s) to be served to the console as if a disk was in the drive
- allows NXE install of normally installable disk images

================================================================================
    Overview - what doesn't do
================================================================================
- will not allow simultaneous use of of connectx and this
- will not allow retail/real disks to work in unpaired drives
- does not emulate the hv/drive security layer
- does not interfere with real disks when a server iso is not mounted
- does not, on it's own, provide a way to choose server isos

================================================================================
    Installation
================================================================================
- make sure your Aurora plugin is up to date (to be able to select isos from the server)
- put the server IP in a txt file 'netiso.xex.txt' (sample provided) either beside
    netiso.xex (if starting from dash launch ini with a recent version of dash launch)
	or on the root of the content partition on the internal hard drive
- once the plugin is running, it defaults to disk mode (and will assist burned disks in unflashed drives)
- once Aurora plugin is running, you will be able to use the Guide File Manager menu
    to mount (or dismount current) disks from the server via the Smb: drive item

================================================================================
    Caveats
================================================================================
The work herein is presented as-is, any risk is solely the end users
    responsibility. While all of us are sorry when unforeseen things happen, not
    every situation or mistake can be accounted for before they have been
    spotted. Please use responsibly.

================================================================================
    Known Bugs
================================================================================
- we don't personally know any bugs, feel free to introduce us and see if we hit it off

================================================================================
    To Do
================================================================================
- the server isn't terribly portable
- release the bugger

================================================================================
    Support (report bugs/request features)
================================================================================
    english:        http://www.realmodscene.com/index.php?/forum/14-dashlaunch/
    french/english: http://homebrew-connection.org/forum/index.php?board=7.0

================================================================================
    Thanks
================================================================================
- thanks to SpkLeader and Swizzy
- thanks to everyone else who helped with early testing
- thanks to Phoenix for letting me putter around with the plugin stuff
- thanks to vgcrepairs for providing the cygnos
- thanks to Team Xecuter for the demons, glitch chips and all the support
- thanks to RF1911 and tydye

~brought to you by cOz~
//2015

================================================================================
    ChangeLog
================================================================================
V1.0
- first release
