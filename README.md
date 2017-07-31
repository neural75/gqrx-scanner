# gqrx-scan
A frequency scanner for GQRX
## Description
[work in progress]

A simple scanner written in C that uses gqrx remote protocol to perform a fast scan of the band. It can be used in conjunction with the gqrx bookmarks to look for already stored frequencies or, in a free sweep scan mode, to explore the band within a specified frequency range. 

The sweep scan uses an adaptive algorithm to remember active frequencies during the sweep that prioritizes active stations without stopping the sweep for new ones. 

Gqrx Squelch level is used as the threshold, when the signal is strong enough it stops the scanner that adjusts the frequency to  the nearest carrier.   

The sweep is performed fast (well, as fast as it can get), on signal detection a fine tuning is performed to pinpoint the exact carrier frequency (subdivision of upper and lower limits).  

## Features
* Support for gqrx bookmarks file format
* Frequency range constrained scan (also for bookmarks)
* Fast free sweep scan with memory of already seen stations


## TODOs
* tags matching filters (only bookmarked frequencies with specified tag) 
* automatic audio recording on signal detection
* configurable thresholds (delay time, and so on)
* better output (timestamps and other info)

## Build
<pre>
cmake .
make
sudo make install
</pre>

