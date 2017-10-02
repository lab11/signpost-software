Deployment Software
====================

This is the deployment folder, where we store application 
binaries and info files for over the air update.

To do an OTA update run the deploy.py script:
```deploy.py /path/to/app.bin version_string output_folder```

Then commit your changes and pull them own the AWS instance that we are
fetching updates from. The app will eventually check and fetch the update.
