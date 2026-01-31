# atp

atp - 📡 📡 

# notes

Spawn thread lazily for main work. Keep interface similar to *nix sockets interface (so that at one point can do LD_PRELOAD trick). 

Libs: (1) atp (2) stun

Apps: (1) stun client (2) full-duplex echo server to test atp

TODO:

1. Check RFC8445 - ICE.
2. Get LAN working. Test setup: one interface thru lan (NKN), wifi through mobile data.
3. Do further survey of from where to where udp hole punching works.
4. libs/stun code review, keepalive testing, actual unit tests(?)
5. Try using claude/cursor to generate full atp as an experiment? :skull:

