Control Module
==========

The control module manages the signpost. Its core management functionality
is abstracted into the signpost_controller library. The signpost_controller
applications calls that library and periodically sends GPS coordinates
and energy statistics to the cloud. The signpost_controller_without_logging
does not report these metrics, but still manages the signpost.

Other applications for debugging and testing can be found in their respective
folders.
