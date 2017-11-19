# net-poc - various network/linux tests

It includes the following tests

## splice

It has the follwoing components
* *client* : Acts as a source to transfer payload to the *server* / *broker*. It uses `sendfile()`.
* *server* : Receives the payload from *client* and sends it to the `destination`. It can `splice()` or do a normal copy().
* *destination* : It receives the payload from the *server* / *broker*

### Running the splice test

Build the binaries first
```sh
$ cd splice
$ make
```
Invoke the *destination* `nc` server on port 3490 (for example)
```sh
$ nc -l -l 3490 > /dev/null
```
Start the *server*
```sh
$ ./server
Usage: ./server [server port] [destination host] [destination port] [(s)pliced copy (default) / (n)ormal copy] [buffer size in bytes, (default 65536)]
$ ./server 8000 localhost 3490 s
```
Create a sample payload of 4MB (for example)
```sh
$ dd if=/dev/urandom of=4MB.txt bs=4096 count=1024
```
Use the *client* to transfer the payload to the *server*
```sh
$ ./client
Usage: ./client [server address] [server port] [file]
$ ./client localhost 8000 4MB.txt
Total bytes sent = 4194304
```
On *server*, following stats are seen
```sh
$ ./server 8000 localhost 3490 s
--------
Accepted connection on descriptor 5 (host=127.0.0.1, port=39021)
SPLICE Copy
total time Taken to copy : 7298 micro seconds
usr=0, sys=4, real=7
Closed connection on descriptor 5 and 6
```
