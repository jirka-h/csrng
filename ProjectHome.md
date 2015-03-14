# NEWS #
  * 2013 Jan 08 - a package for the Arch Linux is now available https://aur.archlinux.org/packages/csprng/
# Description of csprng project #
The _csprng_ project provides cryptographically secure pseudorandom number generator. It consists of
  * C library
  * _csprng-generate_ utility to generate stream of random numbers written to file or to STDOUT
  * Linux daemon _csprngd_ to fill entropy of Linux kernel random device _/dev/random_

It combines these three components to provide a high quality cascade random number generator:
  * [HAVEGE](http://www.irisa.fr/caps/projects/hipsor/) hardware random number generator. HAVEGE combines on-the-fly hardware volatile entropy gathering with pseudo-random number generation. The internal state of HAVEGE includes thousands of internal volatile hardware states of the CPU and is merely unmonitorable. The CPU intern states include caches, branch predictors, TLBs, long pipelines, instruction level parallelism, ... The state of these components is not architectural (i.e. the result of an ordinary application does not depend on it), it is also volatile and cannot be directly monitored by the user. Every invocation of the operating system modifies thousands of these binary volatile states.
  * Cryptographically secure pseudo-random number generator (CSPRNGD): block cipher AES-128 working in the counter mode based Deterministic Random Bit Generator as defined by [NIST SP800-90 document](http://csrc.nist.gov/publications/nistpubs/800-90A/SP800-90A.pdf)
  * Run-time random number statistical testing and verification as defined by FIPS PUB 140-2
    * Monobit test
    * Poker test
    * Runs test
    * Long run test
    * Continuous run test

## Similar tools and their limitations ##
The project has been inspired by the following open source tools
  * [rngd daemon](http://linux.die.net/man/8/rngd): Check and feed random data from hardware device to kernel
  * [haveged](http://www.issihosts.com/haveged/): HAVEGE random number generator which feeds data to the linux random device
  * new Intel's [RdRand](http://en.wikipedia.org/wiki/RdRand) instruction. This project has been very much inspired by the cascade RNG concept as used by Intel

Each of these tools has some drawbacks:
  * rngd relies completely on the hardware random number generator. Without such generator or some other high quality entropy source it cannot be used.
  * the speed of rngd is limited by the speed of underlying entropy source. It lacks the way to expand the provided entropy.
  * rngd depends completely on the good quality of the hardware random generator. It lacks [randomness extractor/whitener](http://en.wikipedia.org/wiki/Randomness_extractor).
  * haveged generator offers very good speed of random number generation (200MB/s) on the mainstream desktop (CPU Athlon II X3 440 @3.0GHz with DDR3 1600MHz PC12800). However, it lacks any runtime verification of the generated data. It's considered dangerous to send unverified data with unknown entropy to the kernel's random device
  * Intel's RdRand is currently limited only on [Ivy Bridge](http://en.wikipedia.org/wiki/Ivy_Bridge_(microarchitecture)) processors scheduled for the release by the end of April 2012.
  * RdRand is hard-wired in the processors and users have no possibility to influence it's operation.
  * While general operation of RdRand is well known the details of the operation are [unknown:](http://software.intel.com/en-us/articles/download-the-latest-bull-mountain-software-implementation-guide/?wapkw=bull+mountain) Is derivation function used? Is additional input used? How Online Health Tests work and what criteria are used to reject certain sequences?
  * There is some discussion going on that RdRand can provide some [backdoor](http://us.generation-nt.com/answer/re-patch-1-2-random-add-support-architectural-random-hooks-help-204273641.html). Since it's hardwired we don't really have any means to check it.

## Features ##
_csprngd_ project enables a full control of the mode of the operation. User can do trade-offs between the speed and the quality of the generated random data. Main features include
  * control of the input entropy data. User can choose between the built-in _HAVEGE_ RNG or any input from file, named pipe or standard input.
  * control if additional input data shall be used
  * control of the source of the additional input data processed by _Cryptographically secure pseudo-random number generator (CSPRNGD)_ for prediction resistance.
  * control whether _Derivation Function_ is used. _Derivation Function_ is used when the entropy of the input data is unknown or cannot be trusted. It will process entropy and additional input data through the block cipher function before using them to reseed the internal state of _CSPRNGD_ and generate random numbers. _Derivation Function_ acts as the randomness whitener.
  * control how many 128-bits random data blocks are generated before internal state of the _CSPRNGD_ is reseeded. Based on this setting the RNG can generate less output bytes than consumed or it can act as the randomness expander, generating more output bytes than consumed.
  * control whether run-time randomness statistical testing is performed. Run-time testing  acts on blocks of length 20000 bites. Those blocks which are failing the tests are excluded from the output. This testing is very CPU intensive and will reduce the speed of the generator typically by the factor of 10x. While it will avoid certain output (like long runs of zeros) it will turn output sequence to be non-uniform. Such testing is desirable for cryptographic applications (like feeding entropy to the Linux's kernel random device) but it's not suitable other applications, like Monte Carlo simulation.
  * possibility to manual control _HAVEGE_ parameters like CPU instruction and data cache sizes

## Overview how csprng cascade RNG works ##
![http://csrng.googlecode.com/files/flow_overview.png](http://csrng.googlecode.com/files/flow_overview.png)