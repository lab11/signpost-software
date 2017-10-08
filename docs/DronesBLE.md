Drone + Signpost Collaboration
=====================

Make sure to `git pull`, `git submodule init`, and `git submodule update`.

### Relevent Files and Apps:

#### For the Radio Module:

Update radio kernel:
```
cd signpost-software/kernel/boards/radio_module/
make flash
```

Install radio app:
```
cd signpost-softare/apps/radio_module/queue_and_transmit/
make flash ID=XX
```
XX represents a uinique hexidecimal byte distinguishing this radio. It can be
anything.

#### For the Control Module:

Update control kernel:
```
cd signpost-software/kernel/boards/controller/
make flash
```

Install control app:
```
cd signpost-softare/apps/controller/signpost_controller_send_bytes_app/
make flash
```

#### For the Storage Master (On the Controller Module):

Make sure to install an SD card into the underside of the module.

Update storage kernel:
```
cd signpost-software/kernel/boards/storage_master/
make flash
```

Install storage app:
```
cd signpost-softare/apps/storage_master/signpost_storage_master_app/
make flash
```

#### For a module that generates test eventual data:

Update the module's associated kernel like the above modules.

Install the test app:
```
cd signpost-softare/apps/tests/send_eventual_client_test/
make flash
```

#### For Linux:

Example app can be found in `signpost-software/receiver/drone_ble/`. It
requires nodejs and noble.

They can be installed with these commands:
```
curl -sL https://deb.nodesource.com/setup_8.x | sudo -E bash -
sudo apt-get install -y nodejs
cd signpost-software/receiver/drone_ble/
npm install noble
```
The example app searches for and connects to advertising Signposts, and
collects sent packets in an array.


