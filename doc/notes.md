# Notes on sensor log packet investigation results

<!-- TOC -->

- [Introduction](#introduction)
- [Known packets](#known-packets)
    - [`0x00` (Timestamp)](#0x00-timestamp)
    - [`0x0F` (Sequence ID)](#0x0f-sequence-id)
    - [`0x81` (Worn Period)](#0x81-worn-period)
- [Partially known packets](#partially-known-packets)
    - [`0x80` (Heartrate)](#0x80-heartrate)
- [Packets under investigation](#packets-under-investigation)
    - [`0x0B`](#0x0b)
    - [`0x42`](#0x42)

<!-- /TOC -->

## Introduction

All packets share the same layout: A header followed by the packet payload. The packet header has the following format:

| offset | type | value             |
|-------:|-----:|:------------------|
|   0x00 |  ui8 | Packet identifier |
|   0x01 |  ui8 | Payload size      |

The following tables only detail the payload and omit the packet header. The offsets are relative to the payload (i.e. starting at 0x00). The packets are sorted by packet ID in the following sections.

## Known packets

### `0x00` (Timestamp)

| offset | size | type      | comments                                                                                                                       |
|-------:|-----:|:----------|:-------------------------------------------------------------------------------------------------------------------------------|
|   0x00 |    8 | file_time | Current UTC time stamp as a [`FILETIME`](https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime) |

All chunks appear to start with a timestamp packet. Timestamp packets have been observed in locations other than the chunk start (e.g. during bike rides). Some timestamp packages have a value of `0xFFFFFFFF'FFFFFFFF` (-1 in decimal). They do not represent actual time stamps. It is unclear, whether those are the result of an error condition (*"could not gather time stamp"*), or whether they are used as special markers.

### `0x0F` (Sequence ID)

| offset | size | type | comments    |
|-------:|-----:|:-----|:------------|
|   0x00 |    4 | ui32 | Sequence ID |

The sequence ID represents the current chunk index. It is the final packet in any chunk. Consecutive chunks have consecutive sequence IDs.

### `0x81` (Worn Period)

| offset | size | type | comments                          |
|-------:|-----:|:-----|:----------------------------------|
|   0x00 |    4 | ui32 | Cumulative worn period in seconds |
|   0x04 |    2 | ui16 | Seconds since last cumulative     |

## Partially known packets

### `0x80` (Heartrate)

| offset | size | type | comments                             |
|-------:|-----:|:-----|:-------------------------------------|
|   0x00 |    1 | ui8  | Current heart rate reading           |
|   0x01 |    1 | ui8  | Unknown value in the range `[0..10]` |

The value at offset 0x01 has unknown meaning. It could be some sort of confidence measure (where 0 means *"inaccurate"* and 10 *"fully accurate"*), a time interval of sorts, or something else. During sleep tracking that value seems to be consistently recorded as 10.

## Packets under investigation

### `0x0B`

Appears to be 3 flags (either `0x00` or `0x01`). Probably related to device state (worn, not worn, charging, etc.).

### `0x42`

| offset | size | type | comments |
|-------:|-----:|:-----|:---------|
|   0x00 |    2 | ui16 | Unknown  |

The value appears to be somewhat constant when the Band isn't worn. This could be a measure of the galvanic skin resistance/conductance.
