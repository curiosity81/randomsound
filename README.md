# randomsound
[Randomsound](http://archive.ubuntu.com/ubuntu/pool/universe/r/randomsound/randomsound_0.2.orig.tar.gz) fills the entropy pool using data derived from the sound card.
This version of randomsound which writes data from sound card to file can be used for verifying randomness of the sound card.
Data is saved in a file in the randomsound directory.

## Introduction
Randomness in a computer is in general deterministic.
Mixing the randomness from a deterministic algorithm with non-deterministic data from some "device" increases the security of cryptographic algorithms and methods.
Examples for such "devices" are files, keyboard and mouse input, internet traffic or specific hardware devices like a sound card.
However, if the device is correctly configured is not always clear, even though the software seems to work correctly.
This version of randomsound writes a file (rsound.out) in the randomsound directory which contains the data directly derived from the sound card for further analysis.
Make sure to delete rsound.out after each run or use the original randomsound program if you are sure that sound card is configured correctly.

Deterministic randomness is generated from /dev/urandom.
Non-deterministic randomness is kept in the entropy pool and provided by /dev/random.
See [this](https://stackoverflow.com/questions/23712581/differences-between-random-and-urandom) for the difference.
As a result:
(i) /dev/urandom always produces enough randomness but is vulnerable to cryptographic attacks;
(ii) /dev/random provides non-deterministic randomness but entropy pool might run empty, so that the device locks;

This implies, that a secure system should use /dev/urandom combined with a full entropy pool that is continously filled with high quality randomness.

## Methods
Nothing special.

## Examples:
### Start randomsound
Program must be run as root. Exit the program via Ctrl+C.

```
sudo ./randomsound -v
Random sound daemon. Copyright 2007 Daniel Silverstone.

Will keep random pool between 256 and 3840 bits of entropy.
Will retain a buffer of 1024 bytes, making entropy deposits of 64 bytes at a time.
Writing 512 bits of entropy to file
Writing 512 bits of entropy to file
Writing 512 bits of entropy to file
Did it fail?!?!
Writing 512 bits of entropy to file
Did it fail?!?!
Writing 512 bits of entropy to file
Writing 512 bits of entropy to file
...
Writing 512 bits of entropy to file
Writing 512 bits of entropy to file
Writing 512 bits of entropy to file
```

### Request random data
In this case, use /dev/random and NOT /dev/urandom (see [this](https://stackoverflow.com/questions/23712581/differences-between-random-and-urandom) for the difference)!
Might take some time.

```
head -c 500 /dev/random > /dev/null
```

### Analyze derived data
Needs the program [ent](https://packages.ubuntu.com/de/xenial/ent).
Can be installed via apt-get (ubunutu).
The closer the resulting entropy is to 8 bits per byte the better. 
```
cat rsound.out | ent
Entropy = 7.907658 bits per byte.

Optimum compression would reduce the size
of this 3136 byte file by 1 percent.

Chi square distribution for 3136 samples is 411.76, and randomly
would exceed this value less than 0.01 percent of the times.

Arithmetic mean value of data bytes is 126.2790 (127.5 = random).
Monte Carlo value for Pi is 2.973180077 (error 5.36 percent).
Serial correlation coefficient is 0.074282 (totally uncorrelated = 0.0).
```
