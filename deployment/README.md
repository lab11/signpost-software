Deployment Software
====================

This is the deployment folder, where we store application 
binaries and info files for over the air update.

Standard workflow is to push the binaries first to the "deployment-test" branch
which modules inside the office are set to fetch from.

If that works, then we can put those binaries on master for the rest
of the signposts to fetch.

Binaries are accessed directly using rawgit. An info.txt file in the
same directory as the binary stores version, length and CRC information.

Use the deployment python script to generate the info.txt file
