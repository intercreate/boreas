# uart_rs485_echo

Echo server over RS-485 using the Boreas async UART API. Any bytes
received on the bus are sent back with an `echo: ` prefix.

Exercises the full async TX + async RX double-buffer path with
software-driven DE/RE GPIO toggling around each TX burst.

**See [../uart_loopback/README.md](../uart_loopback/README.md)** for
shared build/flash instructions, transceiver wiring, the scope
verification procedure, and the two-node round-trip test setup.
