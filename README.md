# gqrx-scanner
A frequency scanner for Gqrx Software Defined Radio receiver
## Description
[work in progress]

*Warning*

This repository is not ready yet. I have to complete parsing options and so forth, so please ignore it for now.


gqrx-scanner is a frequency scanner written in C that uses gqrx remote protocol to perform a fast scan of the band. It can be used in conjunction with the gqrx bookmarks to look for the already stored frequencies or, in a free sweep scan mode, to explore the band within a specified frequency range. 

The sweep scan uses an adaptive algorithm to remember active frequencies during the sweep that prioritizes active stations without stop searching for the new ones. 

Gqrx Squelch level is used as the threshold, when the signal is strong enough it stops the scanner that in turn adjusts the frequency to the nearest carrier seen.   

The sweep is performed fast (well, as fast as it can), self asjusting the scanning speed. On signal detection a fine tuning is performed to pinpoint the nearest carrier frequency (subdivision of upper and lower limits with increasing precision) and, for the already seen carriers, a previous value is used to avoid fine tuning at every hit. This value is also averaged out with the new one in order to converge eventually to the exact frequency in less time (after 4 hits of the same frequency).

## Features
* Support for Gqrx bookmarks file format
* Frequency range constrained scan (also for bookmarks)
* Fast sweep scan with adaptive monitor of the most active stations 
* Automatic Frequency Locking in sweep mode
* Interactive monitor to skip, ban or pause a frequency manually

## Notes on Gqrx settings
Gqrx Remote Protocol must be enabled: Tools->Remote Control.

It is advisable to disable AGC during the scan: adjust the fixed gain lowering the noise floor to at least -60/-70 dBFS and set the squelch level to -50/-40 dBFS, depending on the band activities and noise levels.

The best results are obtained in relative quiet frequencies with sporadic transmissions. If the band is cluttered with armonics and other types of persistent noise, avoid the sweep scan and use the bookmarks mode with a higher squelch level. 

In sweep mode use a limited bandwidth of about 2 MHz in order to avoid VFO and noise floor levels chainging during the sweep.   

You may also consider to adjust FFT options: FFT size and rate (on FFT Settings) to improve performances (and cpu usages).
I have found better result with high fft size (64536) and 17 fps refresh rate, but this depends on your hardware.

## Command line Options
TODO

## Interactive Commands 
These keyboard shortcuts are available during scan:
<pre>
[space] OR [enter]  :   Skips a locked frequency (listening to the next).
'b'                 :   Bans a locked frequency, the bandwidth banned is about 10 Khz from the locked freq. 
'c'                 :   Clears all banned frequencies.
'p'                 :   Pauses scan on locked frequency, 'p' again to unpause. 
</pre>


## TODOs
* tags matching filters (only bookmarked frequencies with specified tag) 
* automatic audio recording on signal detection
* configurable thresholds (delay time, and so on)
* parsable output in csv?


## Build and Install
<pre>
cmake .
make
sudo make install
</pre>

