# Wireguard Implementation for esp-32 using esp-idf and Linux Simulator

This is an implementation of the [WireGuard®](https://www.wireguard.com/) for ESP-32 and Simulator for Linux, based on [WireGuard Implementation for lwIP](https://github.com/smartalock/wireguard-lwip) and [esp_wireguard](https://github.com/trombik/esp_wireguard).

# CONTENT OF THE DIRECTORY

- Esp32_tcpprompt_wg: it contains the project of the VPN client for FreeRTOS developed for the ESP32.
- Posix_GCC_lwip1: it contains a project developed for FreeRTOS simulator for Linux, that uses a TAP and sends simple messages over a TCP/IP connection.
- Posix_GCC_lwip2_wg: it contains the same project as the directory Posix_GCC_lwip1, but with a WireGuard module added to the communication.
- Report directory containing the LaTex version
- final Report in pdf version

# License

The code is copyrighted under BSD 3 clause Copyright (c) 2022 Francesco Spagnoletti. 

See [LICENSE](https://github.com/trombik/esp_wireguard/blob/main/LICENSE) for details.

# Authors

- [Francesco Spagnoletti](https://github.com/fspagnoletti)
- [Lucia Vencato](https://github.com/luciavencato)
- [Lorenzo Chiola](https://github.com/LorenzoChiola)
- [Simone Pistilli](https://github.com/il-pist)
