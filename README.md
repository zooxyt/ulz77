ulz77 -- A LZ77 Compression Library
===================================
A one source one header LZ77 Compression Library.


Theory
------
The LZ series of algorithm are basically some kind of methods to represent contents with a shorter sequence of bytes (such as a reference of an index and length of the recent buffer). 

More details could be found at:
http://en.wikipedia.org/wiki/LZ77_and_LZ78


Specification
-------------
Buffer size: 4096 Bytes

A matched will be encoded into at least 3 bytes

```
8 bits     4 bits           12 bits            7 bits          1 bit
sentinel + matched length + matched position + (extra length + extra sig) 
```

SENTINEL + 3 + 0 means source data is a SENTINEL

```
Matched length  Encoded value
3               reserved for sentinel
4               1
17              14
18              15, 1 | 0(no extra)
```


Features
--------
LZ77 encoding and decoding
Stream support
File compression/decompression support


Usage
-----
Help info will be displayed by executing the following command:
```
$ ulz77 --help
```


```
usage : ulz77 [-options]

  --method   <method>       Specify interface of compression library
    [stream|file]
  -c         <sourcefile>   Input file
  -o         <destfile>     Output file
  -bs        <blocksize>    Specify block size of stream

  --help                    Show help info
  --version                 Show version info
```

License
-------
GPLv3

