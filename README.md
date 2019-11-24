# adhoc_addiction

1. Create msg struct - Donee
    - nodeID
    - pathID
    - hopCount
    - powerLvl

2. Global info for nodes
    - nodeID
    - degree
    - array of childrenIDs
    - parentID
    - powerLvl
    - RSSI and minRSSI

New node needs to:
    - initialize - Done
    - turn on green LED - Done
    - Broadcast info
    - if no response:
        - increment power

## New Node
init - Done

green LED on - Done

broadcast

while no response
- increment power

if single response
- if RSSI < min_RSSI - Done
    - ignore - Done
- else
    - set responding node as parent and broadcast info to parent

if multiple responses
- turn on red led
- ignore responses with RSSI < min_RSSI
- find node with shortest path
- set responding node as parent and broadcast info to parent

if no response at max power
- turn off LED
- give up


## Generic Node
Send info every 2 seconds to parent

If receive info from child send to parent

If receive info from parent (control command to turn LEDs yellow) send to children

## Sink Node
receive info from children

send info through UART to user - Sulley -> in progress.

if receive info through UART for control command

- send control command to all children
