# Host test

Runs the encoder and receiver parser on a PC (ESP-IDF calls are stubbed):

```
cd tests
gcc -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-variable \
    -I host_stub -I ../components/toslink/include \
    host_test.c ../components/toslink/toslink_proto.c -o host_test && ./host_test
```
