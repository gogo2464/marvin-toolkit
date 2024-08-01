Test scripts for libgcrypt

Generic setup
=============

Install dependencies by running the `step0.sh` script.

Generate key with `step1.sh`.

Convert the private key to a format the harness can load:
```
PYTHONPATH=tlsfuzzer marvin-venv/bin/python3 pkcs#8_to_gcrypt_txt.py \
rsa2048/pkcs8.pem key.txt
```


libgcrypt *without* implicit rejection
====================================
Test harness for libgcrypt *without* implicit rejection a.k.a Marvin workaround.

Usage
-----

Instead of running `step2.sh` run the `step2-alt.sh` script.

Compile this reproducer:
```
gcc -o time_decrypt time_decrypt.c -lgcrypt
```

Execute it against one of the `ciphers.bin` files, for example the one
for 2048 bit key:
```
./time_decrypt -i rsa2048_repeat/ciphers.bin \
-o rsa2048_repeat/raw_times.bin -k key.txt -n 256
```

libgcrypt with OAEP padding
===========================

The OAEP encryption in libgcrypt is vulnerable to the Marvin attack. The fixes are available
in the same PR as fixes for the implicit rejection.

Usage
-----

Generate new inputs using `step2-oaep-alt.sh`.

Compile this reproducer:
```
gcc -o time_decrypt_oaep time_decrypt_oaep.c -lgcrypt
```

Execute it against one of the `ciphers.bin` files, for example the one
for 2048 bit key:
```
./time_decrypt_oaep -i rsa2048_oaep_repeat/ciphers.bin \
-o rsa2048_oaep_repeat/raw_times.bin -k key.txt -n 256
```

libgcrypt with implicit rejection
=================================
The patches to implement the implicit rejection (including test vectors are available here):

https://gitlab.com/redhat-crypto/libgcrypt/libgcrypt-mirror/-/merge_requests/19/

Generate new inputs using `step2-marvin.sh` (warning, this takes most of the time!).

Compile this reproducer:
```
gcc -o time_decrypt_ir time_decrypt_ir.c -lgcrypt
```

Execute it against one of the `ciphers.bin` files, for example the one
for 2048 bit key:
```
./time_decrypt_ir -i rsa2048_repeat/pms_values.bin \
-o rsa2048_repeat/raw_times.bin -k key.txt -n 256
```

Post-processing
===============

Convert the captured timing information to a format understandable by
the analysis script (change the `TEST_DIR` variable to match the directory
with your results):
```
TEST_DIR=rsa2048_repeat
PYTHONPATH=tlsfuzzer marvin-venv/bin/python3 tlsfuzzer/tlsfuzzer/extract.py \
-l ${TEST_DIR}/log.csv --raw-times ${TEST_DIR}/raw_times.bin \
-o ${TEST_DIR}/ \
--binary 8 --endian little --clock-frequency 2712.003
```
The `--clock-frequency` is the TSC frequency, as reported by the kernel on
my machine:
```
[    1.506811] tsc: Refined TSC clocksource calibration: 2712.003 MHz
```
Specifying it is optional, but then the analysis will interpret clock
ticks as seconds so interpretation of the results and graphs in terms of
CPU clock cycles will be more complex.

**Warning:** None of the clock sources used by the test drivers
actually run at the same frequency as the CPU frequency! Remember to specify
`--endian big` when running on s390x!

Finally, run the analysis:
```
TEST_DIR=rsa2048_repeat
PYTHONPATH=tlsfuzzer marvin-venv/bin/python3 tlsfuzzer/tlsfuzzer/analysis.py \
-o ${TEST_DIR}/ --verbose
```

Interpretation of results
=========================

Detailed information about produced output is available in
[tlsfuzzer documentation](https://tlsfuzzer.readthedocs.io/en/latest/timing-analysis.html)
but what's most important is in the summary:
```
Sign test mean p-value: 0.5538, median p-value: 0.562, min p-value: 0.06575
Friedman test (chisquare approximation) for all samples
p-value: 0.9617988327160067
Worst pair: 8(valid_246), 11(zero_byte_in_padding_48_4)
Mean of differences: -2.59050e-07s, 95% CI: -4.24877e-06s, 3.403219e-06s (±3.826e-06s)
Median of differences: -4.29572e-08s, 95% CI: -9.08922e-08s, 5.531000e-09s (±4.821e-08s)
Trimmed mean (5%) of differences: -3.89065e-07s, 95% CI: -1.18398e-06s, 3.069665e-07s (±7.455e-07s)
Trimmed mean (25%) of differences: -6.51617e-08s, 95% CI: -1.41947e-07s, 1.304047e-08s (±7.749e-08s)
Trimmed mean (45%) of differences: -4.19826e-08s, 95% CI: -9.36116e-08s, 8.553458e-09s (±5.108e-08s)
Trimean of differences: -4.15053e-08s, 95% CI: -9.20906e-08s, 9.241294e-09s (±5.067e-08s)
For detailed report see rsa2048_repeat/report.csv
Analysis return value: 0
```

The Friedman test p-value specifies how confident is the test in presence of
side channel (the smaller the p-value the more confidant it is, i.e. a
p-value of 1e-6 means 1 in a million chance that there isn't a side-channel).
The other important information are the 95% Confidence Intervals reported,
they specify how sensitive is the script (in this case it's unlikely that
it would be able to detect a side channel smaller than 4.821e-08s or 48ns).
