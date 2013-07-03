slock
=====

This is my own version of [slock](http://tools.suckless.org/slock), the
*Simple X display locker*:

* it shows random pastel colors each time you lock instead of having fixed ones;
* it validates your password everytime you press a key so the Return key is not needed.


Requirements
------------
In order to build slock, you need the Xlib header files.


Installation
------------
Edit `config.mk` to match your local setup (slock is installed into
the `/usr/local` namespace by default).

Afterwards, enter the following command to build and install slock
(if necessary as `root`):

    # make clean install


Running
-------
Simply invoke the `slock` command. To get out of it,
enter your password and press Enter. You can press Escape to erase
what you typed if you don't want to hit Backspace like crazy.
