# demo example

## What the example does
A single freeRTOS task is created for the TCP connection.
The example connects to a WireGuard server. It simulate a login communication.
When the link is up, the device sends the first login messages and waits for an answer. 
When the answers is received it sends another packet.

The messages sent by the device are the following:

-> Hello from ESP32, please login Username:

   -- It is the first message sent by the ESP32 which waits for an answers with the user credential. 

-> Password:

   -- After receiving the username it asks for a password, and waits for the user to answer.
 
-> Authentication succeded
   Select commands:
   - Hello:  to get greetings
   - Usage:  to read cpu load
   - LogOut: Log Out
   
   -- If the combination of username and password are correct, the user can choose one of these commands and will have a different answer
 
-> Authentication failed, try again...  Username: 
    
   -- If the username or the password (or both) is wrong, the authentication failed and the esp32 asks again for the credentials.
    
## Requirements

* An ESP32 development board
* WiFi network
* [`wireguard-tools`](https://github.com/WireGuard/wireguard-tools)
* A WireGuard server

## Generating keys

```console
wg genkey | tee private.key | wg pubkey > public.key
```
