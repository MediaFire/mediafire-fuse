Oct 06, 2015
============

The CMake system will now build debian packages using the CPack system.


Sep 10, 2015
============

Fixed a really annoying bug that made using multiple accounts difficult.
Now, when a username is specified on the commandline, the corresponding
password must be supplied as a parameter or otherwise the it will prompt.
Previously, the tool would read a password from the configfile that didn't
even when a different username was supplied.

Jun 04, 2015
============

The 'master' branch recieved some big changes that significantly improve
responsiveness and drastically reduces the number of API calls that
are made.

These new commits also improve compatibility with LibreOffice, Abiword,
Gnumeric, Nano, Mousepad, and others.
