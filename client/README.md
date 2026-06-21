# Orbitfight Web Client

This is intended to be the primary web client for orbitfight.

## Usage

For testing this locally, drag-and-drop index.html into your browser. The client can work independently. (TODO: WASM module shared between client and server)

To connect to a C++ orbitfight server, you run the C++ headless server, then connect to its hosted address via the web client.
For connecting to a remote server, you will also have to supply your TLS key and certificate files to it (--tls-key \<key\>, --tls-cert \<cert\>).
