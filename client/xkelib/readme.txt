Updated last using devkit symbols 21076.11-14719


========================================================================================================
How to add to another project that is in svn using tortoisesvn:

http://tortoisesvn.net/docs/nightly/TortoiseSVN_en/tsvn-dug-externals.html

- right click on a project folder, select properties
- select subversion tab, and click on properties button
- click on new->externals
- click on new
- fill in the form
	local path: xkelib (up to you, I used this to create a folder xkelib in the root of my project)
	url: http://teamfsd.com/svn/teamfsd/trunk/xkelib (use relative url if adding to a project already in this repo)

alternately, you could just nest it by making a new folder and use svn to checkout into it
either way, additions/commits need to be done separately unless using externals and on the same svn host

========================================================================================================

to use once you have it in your project folder
- import existing project, and import the xkelib.vcxproj
- add it's root folder to both your library search path and include search path
	- project settings -> c/c++ -> general -> additional include directories (add it as the last)
	- project settings -> linker -> general -> additional library directories (add it as the last)
- make sure to select the appropriate build ver for your sdk ver (or see below if a config doesn't already exist)
- #include "xkelib.h"
- DONE!

========================================================================================================

you may have to edit the .def files to match them to you're sdk version
 (this is not required, but may trigger some version mismatch issues and cause more import tables for the same module than is required)
ie:
dumpbin /headers xapilib.lib > xapilib.txt
will show you what to put in to make it match you're sdk
  DLL name     : xam.xex@20871.0+1861.0
you'd put:
LIBRARY xam.xex@20871.0+1861.0
into xamext.def

dumpbin /headers xboxkrnl.lib
will show you what to put in to make it match you're sdk
  DLL name     : xboxkrnl.exe@20871.0+1861.0
you'd put:
LIBRARY xboxkrnl.exe@20871.0+1861.0
into kernelext.def

