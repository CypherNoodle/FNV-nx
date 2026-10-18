"""Own synthetic MSVC-style registration ABI fixture, with relocatable addresses."""
import struct


def helpers(handler,cookie):
    # Install a cookie-encoded scope pointer and preserve nonvolatile registers.
    prolog=(b'\x68'+struct.pack('<I',handler)+bytes.fromhex(
        '64ff35000000008b442410896c24108d6c241029c4535657a1')+
        struct.pack('<I',cookie)+bytes.fromhex(
        '3145fc33c5508965e8ff75f88b45fcc745fcfeffffff8945f88d45f064a300000000c3'))
    epilog=bytes.fromhex('8b4df064890d00000000595f5f5e5b8be55d51c3')
    return prolog,epilog
