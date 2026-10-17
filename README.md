# WrplReplayParser
Warthunder .wrpl Replayer Parser written in cpp with python bindings

a feature filled wrpl replay parser that has python bindings (and will be publish on pypi soon)
this parser lets you access battle messages, chat messages, various global object data (like player information and team information), and entity construction information plus aircraft and tank positions (see FMSync and GMSync functions)
I have completed most of my inital goals, and will work to clean up the codebase as time goes on. this include reorganization / refactoring plus adding of probably sorely needed utility functions.

currently supported replay version: 18c0b

This attempts to be buildable for:

Windows: MSYS2 minGW, MSVC

Linux (WSL2) gcc

## Known aircraft sync limitation

Some valid replays contain an aircraft sync before the corresponding ECS unit is created. The parser currently requires the unit to exist when it reads the sync, so `LoadFromReader` raises `RuntimeError` and discards the aircraft updates from that packet. For example, server replay `127b004900163e25` has a sync for UID 400 at 84.514 seconds and creates that unit at 84.554 seconds. Handling this ordering remains open; the failure does not necessarily indicate a malformed packet.

# TODO

setup loading of char.vromfs.bin and proper translation code

overhaul directory structure

work on reducing certain memory usage, vromfs are rather expensive to load. maybe make a vromfs repackager?

ADD COMMENTS

ensure LICENCE is correctly setup

tests
