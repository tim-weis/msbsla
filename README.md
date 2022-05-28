# The MS Band Sensor Log Analyzer

A tool to aid in reverse engineering the MS Band Sensor Log data format.

Sensor log data is the data that used to be uploaded by the official MS Health app to the MS Health Dashboard for analysis. The processed data was subsequently used by the Mobile/Desktop and Web application to render diagrams and statistical overviews.

The primary goal of this software is to reverse the data format, allowing clients to perform the analysis previously done inside the online portal, bringing back much of the value that made MS Band devices unique.

## UI

The main UI consists of the following parts:

![Main UI](/assets/images/screenshot_ui.png).

The top part shows the folder contents with the raw binary sensor log files, alongside buttons to set a different folder and load a sensor log file.

Below that is the list of the binary information, converted to a human-readable representation where available. The list can be sorted by index, type, or size. The conversion is controlled through a client authored *packet_descriptions.json* file. At this time there is no UI that allows users to add/update/remove packet description entries.

The bottom is reserved for a diagram area. The graphs currently are taken from a hard-coded list of packet types. It is intended to provide a UI to add/remove/update graphs in the diagram area, allowing users to conveniently display a visual rendition of any given packet under investigation.

## Documentation

The results of reverse engineering the format is documented [here](/doc/notes.md). The JSON schema of the *packet_descriptions.json* has not yet been documented.
